/*
 * Copyright (c) 2022, Stephan Unverwerth <s.unverwerth@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibVirtGPU/Image.h>
#include <LibVirtGPU/CommandBufferBuilder.h>

namespace VirtGPU {

static Protocol::TextureFormat lib_gpu_pixel_format_to_virt_gpu_texture_format(GPU::PixelFormat const& pixel_format)
{
    switch (pixel_format) {
    case GPU::PixelFormat::Alpha:
        return Protocol::TextureFormat::VIRTIO_GPU_FORMAT_A8_UNORM;
    case GPU::PixelFormat::BGRA:
        return Protocol::TextureFormat::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    case GPU::PixelFormat::Luminance:
        return Protocol::TextureFormat::VIRTIO_GPU_FORMAT_L8_UNORM;
    case GPU::PixelFormat::LuminanceAlpha:
        return Protocol::TextureFormat::VIRTIO_GPU_FORMAT_R8G8_UNORM;
    case GPU::PixelFormat::Red:
        return Protocol::TextureFormat::VIRTIO_GPU_FORMAT_R8_UNORM;
    case GPU::PixelFormat::RGB:
        return Protocol::TextureFormat::VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM;
    case GPU::PixelFormat::RGBA:
        return Protocol::TextureFormat::VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM;
    default:
        dbgln("Don't know how to convert LibGPU pixel format '{}' to a VirtGPU texture format", to_underlying(pixel_format));
        TODO();
    }
}

Image::Image(Device& device, void const* ownership_token, GPU::PixelFormat const& pixel_format, u32 width, u32 height, u32 depth, u32 max_levels)
    : GPU::Image(ownership_token, pixel_format, width, height, depth, max_levels)
    , m_device(device)
{
    VirGL3DResourceSpec image_spec {
        .target = to_underlying(Gallium::PipeTextureTarget::TEXTURE_2D), // FIXME: We currently only support Texture2D.
        .format = to_underlying(lib_gpu_pixel_format_to_virt_gpu_texture_format(pixel_format)),
        .bind = to_underlying(Protocol::BindTarget::VIRGL_BIND_SAMPLER_VIEW), // FIXME: ???
        .width = width,
        .height = height,
        .depth = depth,
        .array_size = 1, // FIXME: We currently only support Texture2D.
        .last_level = 0, // FIXME: Is this 0 based?
        .nr_samples = 0, // FIXME: ???
        .flags = 0, // FIXME: ???
        .created_resource_id = 0,
    };

    m_resource_id = MUST(device.create_virgl_resource(image_spec));
}

void Image::regenerate_mipmaps()
{
//    dbgln("VirtGPU::Image::regenerate_mipmaps(): unimplemented");
}

void Image::write_texels(u32 level, Vector3<i32> const&, void const* input_data, GPU::ImageDataLayout const& input_layout)
{
    if (level != 0)
        return;
//    dbgln("VirtGPU::Image::write_texels(): unimplemented");
    auto width = width_at_level(level);
    auto height = height_at_level(level);
    auto depth = depth_at_level(level);

    // FIXME: Overflow
    auto size_of_input_data = width * height * depth * GPU::pixel_size_in_bytes(input_layout.pixel_type);

    dbgln("write_texels resource_id={} level={} width={} height={} depth={} size={} input_data=", m_resource_id, level, width, height, depth, size_of_input_data);
//    for (auto byte : ReadonlyBytes { input_data, size_of_input_data }) {
//        dbgln("0x{:02x}", byte);
//    }

    // Directly copy to the Image's resource at the given level...
    auto target_resource = m_resource_id;
    auto target_level = level;

    // ... unless the input layout pixel format doesn't match the Image's pixel format.
    // FIXME: pixel_type has way more info to consider than just the pixel format.
//    bool matching_pixel_formats = pixel_format() == input_layout.pixel_type.format;
//    if (!matching_pixel_formats) {
//        dbgln("using staging as {} != {}", to_underlying(pixel_format()), to_underlying(input_layout.pixel_type.format));
//        // Create a staging buffer to send the input data to. The staging buffer will then be blitted to the image, where VirGL will do the pixel conversion for us.
//        VirGL3DResourceSpec staging_buffer_spec = {
//            .target = to_underlying(Gallium::PipeTextureTarget::TEXTURE_2D), // FIXME: We currently only support Texture2D. Also, is this correct?
//            .format = to_underlying(lib_gpu_pixel_format_to_virt_gpu_texture_format(input_layout.pixel_type.format)), // FIXME: pixel_type has way more info to consider than just the pixel format.
//            .bind = to_underlying(Protocol::BindTarget::VIRGL_BIND_SAMPLER_VIEW), // FIXME: ???
//            .width = width,
//            .height = height,
//            .depth = depth,
//            .array_size = 1, // FIXME: We currently only support Texture2D.
//            .last_level = 0, // The staging buffer doesn't have mipmaps.
//            .nr_samples = 0, // FIXME: ???
//            .flags = 0, // FIXME: ???
//            .created_resource_id = 0,
//        };
//
//        // FIXME: Free this after we have finished using it.
//        target_resource = MUST(m_device.create_virgl_resource(staging_buffer_spec));
//
//        // The staging buffer doesn't have mipmaps.
//        target_level = 0;
//    }

    // Upload the texture data into the target resource via the kernel virgl transfer region.
    VirGLTransferDescriptor descriptor {
        .data = const_cast<void*>(input_data), // FIXME: Remove this const_cast
        .offset_in_region = 0,
        .num_bytes = size_of_input_data,
        .direction = VIRGL_DATA_DIR_GUEST_TO_HOST,
    };
    auto maybe_error = m_device.copy_data_to_transfer_region(descriptor);
    if (maybe_error.is_error()) {
        dbgln("error uploading texture data: {}", maybe_error.error());
        VERIFY_NOT_REACHED();
    }

    CommandBufferBuilder builder;
    (void)target_level;
    builder.append_transfer3d(target_resource, 0, width, height, depth, VIRGL_DATA_DIR_GUEST_TO_HOST);
    builder.append_end_transfers_3d();

//    if (!matching_pixel_formats) {
//        // Perform the blit mentioned above.
//
//        // FIXME: pixel_type has way more info to consider than just the pixel format.
//        auto input_data_format = lib_gpu_pixel_format_to_virt_gpu_texture_format(input_layout.pixel_type.format);
//        auto image_format = lib_gpu_pixel_format_to_virt_gpu_texture_format(pixel_format());
//
//        // source_level is 0 as the staging buffer only has 1 level.
//        builder.append_blit_to_resource_of_same_dimensions(target_resource, input_data_format, /* source_level= */ 0, m_resource_id, image_format, 0, width, height, depth);
//    }

    MUST(m_device.upload_command_buffer(builder.build()));
}

void Image::read_texels(u32, Vector3<i32> const&, void*, GPU::ImageDataLayout const&) const
{
//    dbgln("VirtGPU::Image::read_texels(): unimplemented");
}

void Image::copy_texels(GPU::Image const&, u32, Vector3<u32> const&, Vector3<u32> const&, u32, Vector3<u32> const&)
{
//    dbgln("VirtGPU::Image::copy_texels(): unimplemented");
}

Protocol::TextureFormat Image::texture_format() const
{
    return lib_gpu_pixel_format_to_virt_gpu_texture_format(pixel_format());
}

}
