/*
 * Copyright (c) 2022, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/FileStream.h>
#include <LibGfx/Font/WOFF2/Font.h>
#include <LibCompress/Brotli.h>
#include <LibGfx/Font/TrueType/Font.h>
#include <LibCore/MemoryStream.h>
#include <LibCore/InputBitStream.h>

namespace WOFF2 {

static constexpr size_t BUFFER_SIZE = 8 * KiB;
static constexpr size_t WOFF2_HEADER_SIZE_IN_BYTES = 48;
static constexpr u32 WOFF2_SIGNATURE = 0x774F4632;
static constexpr u32 TTCF_SIGNAURE = 0x74746366;
static constexpr size_t SFNT_HEADER_SIZE = 12;
static constexpr size_t SFNT_TABLE_SIZE = 16;

[[maybe_unused]] static ErrorOr<u16> read_255_u_short(Core::Stream::ReadonlyMemoryStream& stream, ByteBuffer& buffer)
{
    auto one_byte_buffer = buffer.span().slice(0, 1);

    u8 code = 0;
    u16 final_value = 0;

    constexpr u8 one_more_byte_code_1 = 255;
    constexpr u8 one_more_byte_code_2 = 254;
    constexpr u8 word_code = 253;
    constexpr u8 lowest_u_code = 253;
    constexpr u16 lowest_u_code_multiplied_by_2 = lowest_u_code * 2;

    auto next_byte = TRY(stream.read(one_byte_buffer));
    if (next_byte.size() != 1)
        return Error::from_string_literal("Not enough data to read code of 255ushort"sv);

    code = next_byte[0];

    if (code == word_code) {
        dbgln("3 bytes");

        auto two_byte_buffer = buffer.span().slice(0, 2);
        auto two_next_bytes = TRY(stream.read(two_byte_buffer));
        if (two_next_bytes.size() != 2)
            return Error::from_string_literal("Not enough data to read word value of 255ushort"sv);

        final_value = (two_next_bytes[0] << 8) & 0xff00;
        final_value |= two_next_bytes[1] & 0xff;
        return final_value;
    }

    if (code == one_more_byte_code_1) {
        dbgln("2 bytes");

        next_byte = TRY(stream.read(one_byte_buffer));
        if (next_byte.size() != 1)
            return Error::from_string_literal("Not enough data to read one more byte of 255ushort"sv);

        final_value = next_byte[0];
        final_value += lowest_u_code;
        return final_value;
    }

    if (code == one_more_byte_code_2) {
        dbgln("2 bytes");

        next_byte = TRY(stream.read(one_byte_buffer));
        if (next_byte.size() != 1)
            return Error::from_string_literal("Not enough data to read one more byte of 255ushort"sv);

        final_value = next_byte[0];
        final_value += lowest_u_code_multiplied_by_2;
        return final_value;
    }

    dbgln("1 byte");
    return code;
}

static ErrorOr<u32> read_uint_base_128(Core::Stream::Stream& stream, ByteBuffer& buffer)
{
    auto one_byte_buffer = buffer.span().slice(0, 1);
    u32 accumulator = 0;

    for (u8 i = 0; i < 5; ++i) {
        auto next_byte_buffer = TRY(stream.read(one_byte_buffer));
        if (next_byte_buffer.size() != 1)
            return Error::from_string_literal("Not enough data to read UIntBase128 type"sv);

        u8 next_byte = next_byte_buffer[0];

        if (i == 0 && next_byte == 0x80)
            return Error::from_string_literal("UIntBase128 type contains a leading zero"sv);

        if (accumulator & 0xFE000000)
            return Error::from_string_literal("UIntBase128 type exceeds the length of a u32"sv);

        accumulator = (accumulator << 7) | (next_byte & 0x7F);

        if ((next_byte & 0x80) == 0)
            return accumulator;
    }

    return Error::from_string_literal("UIntBase128 type is larger than 5 bytes"sv);
}

static u16 be_u16(u8 const* ptr)
{
    return (((u16)ptr[0]) << 8) | ((u16)ptr[1]);
}

static void be_u16(u8* ptr, u16 value)
{
    ptr[0] = (value >> 8) & 0xff;
    ptr[1] = value & 0xff;
}

static u32 be_u32(u8 const* ptr)
{
    return (((u32)ptr[0]) << 24) | (((u32)ptr[1]) << 16) | (((u32)ptr[2]) << 8) | ((u32)ptr[3]);
}

static void be_u32(u8* ptr, u32 value)
{
    ptr[0] = (value >> 24) & 0xff;
    ptr[1] = (value >> 16) & 0xff;
    ptr[2] = (value >> 8) & 0xff;
    ptr[3] = value & 0xff;
}

static i16 be_i16(u8 const* ptr)
{
    return (((i16)ptr[0]) << 8) | ((i16)ptr[1]);
}

static void be_i16(u8* ptr, i16 value)
{
    ptr[0] = (value >> 8) & 0xff;
    ptr[1] = value & 0xff;
}

static u16 pow_2_less_than_or_equal(u16 x)
{
    u16 result = 1;
    while (result < x)
        result <<= 1;
    return result;
}

enum class TransformationVersion {
    Version0,
    Version1,
    Version2,
    Version3,
};

struct TableDirectoryEntry {
    TransformationVersion transformation_version { TransformationVersion::Version0 };
    String tag;
    u32 original_length { 0 };
    Optional<u32> transform_length;

    u32 tag_to_u32() const
    {
        VERIFY(tag.length() == 4);
        return (static_cast<u8>(tag[0]) << 24)
             | (static_cast<u8>(tag[1]) << 16)
             | (static_cast<u8>(tag[2]) << 8)
             | static_cast<u8>(tag[3]);
    }

    bool has_transformation() const
    {
        return transform_length.has_value();
    }
};

// NOTE: Any tags less than 4 characters long are padded with spaces at the end.
static constexpr Array<StringView, 63> known_tag_names = {
    "cmap"sv,
    "head"sv,
    "hhea"sv,
    "hmtx"sv,
    "maxp"sv,
    "name"sv,
    "OS/2"sv,
    "post"sv,
    "cvt "sv,
    "fpgm"sv,
    "glyf"sv,
    "loca"sv,
    "prep"sv,
    "CFF "sv,
    "VORG"sv,
    "EBDT"sv,
    "EBLC"sv,
    "gasp"sv,
    "hdmx"sv,
    "kern"sv,
    "LTSH"sv,
    "PCLT"sv,
    "VDMX"sv,
    "vhea"sv,
    "vmtx"sv,
    "BASE"sv,
    "GDEF"sv,
    "GPOS"sv,
    "GSUB"sv,
    "EBSC"sv,
    "JSTF"sv,
    "MATH"sv,
    "CBDT"sv,
    "CBLC"sv,
    "COLR"sv,
    "CPAL"sv,
    "SVG "sv,
    "sbix"sv,
    "acnt"sv,
    "avar"sv,
    "bdat"sv,
    "bloc"sv,
    "bsln"sv,
    "cvar"sv,
    "fdsc"sv,
    "feat"sv,
    "fmtx"sv,
    "fvar"sv,
    "gvar"sv,
    "hsty"sv,
    "just"sv,
    "lcar"sv,
    "mort"sv,
    "morx"sv,
    "opbd"sv,
    "prop"sv,
    "trak"sv,
    "Zapf"sv,
    "Silf"sv,
    "Glat"sv,
    "Gloc"sv,
    "Feat"sv,
    "Sill"sv,
};

struct CoordinateTripletEncoding {
    u8 byte_count { 0 };
    u8 x_bits { 0 };
    u8 y_bits { 0 };
    Optional<u16> delta_x;
    Optional<u16> delta_y;
    Optional<bool> negative_x;
    Optional<bool> negative_y;
};

// https://www.w3.org/TR/WOFF2/#triplet_decoding
// 5.2. Decoding of variable-length X and Y coordinates
static Array<CoordinateTripletEncoding, 128> coordinate_triplet_encodings = {
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 0,    .negative_x = {}, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 0,    .negative_x = {}, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 256,  .negative_x = {}, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 256,  .negative_x = {}, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 512,  .negative_x = {}, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 512,  .negative_x = {}, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 768,  .negative_x = {}, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 768,  .negative_x = {}, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 1024, .negative_x = {}, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 0, .y_bits = 8, .delta_x = {}, .delta_y = 1024, .negative_x = {}, .negative_y = false },

    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 0,    .delta_y = {}, .negative_x = true,  .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 0,    .delta_y = {}, .negative_x = false, .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 256,  .delta_y = {}, .negative_x = true,  .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 256,  .delta_y = {}, .negative_x = false, .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 512,  .delta_y = {}, .negative_x = true,  .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 512,  .delta_y = {}, .negative_x = false, .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 768,  .delta_y = {}, .negative_x = true,  .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 768,  .delta_y = {}, .negative_x = false, .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 1024, .delta_y = {}, .negative_x = true,  .negative_y = {} },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 8, .y_bits = 0, .delta_x = 1024, .delta_y = {}, .negative_x = false, .negative_y = {} },

    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 1,  .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 1,  .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 1,  .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 1,  .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 17, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 17, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 17, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 17, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 33, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 33, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 33, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 33, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 49, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 49, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 49, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 1, .delta_y = 49, .negative_x = false, .negative_y = false },

    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 1,  .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 1,  .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 1,  .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 1,  .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 17, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 17, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 17, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 17, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 33, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 33, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 33, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 33, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 49, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 49, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 49, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 33, .delta_y = 49, .negative_x = false, .negative_y = false },

    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 1,  .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 1,  .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 1,  .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 1,  .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 17, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 17, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 17, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 17, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 33, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 33, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 33, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 33, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 49, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 49, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 49, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 2, .x_bits = 4, .y_bits = 4, .delta_x = 49, .delta_y = 49, .negative_x = false, .negative_y = false },

    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 1,   .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 1,   .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 1,   .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 1,   .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 257, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 257, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 257, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 257, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 513, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 513, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 513, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 1, .delta_y = 513, .negative_x = false, .negative_y = false },

    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 1,   .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 1,   .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 1,   .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 1,   .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 257, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 257, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 257, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 257, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 513, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 513, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 513, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 257, .delta_y = 513, .negative_x = false, .negative_y = false },

    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 1,   .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 1,   .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 1,   .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 1,   .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 257, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 257, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 257, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 257, .negative_x = false, .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 513, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 513, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 513, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 3, .x_bits = 8, .y_bits = 8, .delta_x = 513, .delta_y = 513, .negative_x = false, .negative_y = false },

    CoordinateTripletEncoding { .byte_count = 4, .x_bits = 12, .y_bits = 12, .delta_x = 0, .delta_y = 0, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 4, .x_bits = 12, .y_bits = 12, .delta_x = 0, .delta_y = 0, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 4, .x_bits = 12, .y_bits = 12, .delta_x = 0, .delta_y = 0, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 4, .x_bits = 12, .y_bits = 12, .delta_x = 0, .delta_y = 0, .negative_x = false, .negative_y = false },

    CoordinateTripletEncoding { .byte_count = 5, .x_bits = 16, .y_bits = 16, .delta_x = 0, .delta_y = 0, .negative_x = true,  .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 5, .x_bits = 16, .y_bits = 16, .delta_x = 0, .delta_y = 0, .negative_x = false, .negative_y = true  },
    CoordinateTripletEncoding { .byte_count = 5, .x_bits = 16, .y_bits = 16, .delta_x = 0, .delta_y = 0, .negative_x = true,  .negative_y = false },
    CoordinateTripletEncoding { .byte_count = 5, .x_bits = 16, .y_bits = 16, .delta_x = 0, .delta_y = 0, .negative_x = false, .negative_y = false },
};

struct FontPoint {
    i16 x { 0 };
    i16 y { 0 };
    bool on_curve { false };
};

static ErrorOr<Vector<FontPoint>> retrieve_points_of_simple_glyph(Core::Stream::ReadonlyMemoryStream& flags_stream, Core::Stream::ReadonlyMemoryStream& glyph_stream, u16 number_of_points, ByteBuffer& read_buffer)
{
    Vector<FontPoint> points;
    TRY(points.try_ensure_capacity(number_of_points));

    auto one_byte_buffer = read_buffer.span().slice(0, 1);

    i16 x = 0;
    i16 y = 0;

    for (u32 point = 0; point < number_of_points; ++point) {
        auto flag_byte_buffer = TRY(flags_stream.read(one_byte_buffer));
        if (flag_byte_buffer.size() != 1)
            return Error::from_string_literal("Not enough data to read flags for simple glyph"sv);

        u8 flags = flag_byte_buffer[0];
        bool on_curve = (flags & 0x80) == 0;
        u8 coordinate_triplet_index = flags & 0x7F;

        dbgln("point {}: flags: 0x{:02x}, on_curve: {}, coordinate_triplet_index: {}", point, flags, on_curve, coordinate_triplet_index);

        auto& coordinate_triplet_encoding = coordinate_triplet_encodings[coordinate_triplet_index];

        // The byte_count in the array accounts for the flags, but we already read them in from a different stream.
        u8 byte_count_not_including_flags = coordinate_triplet_encoding.byte_count - 1;

        auto point_coordinate_buffer = read_buffer.span().slice(0, byte_count_not_including_flags);
        auto point_coordinates = TRY(glyph_stream.read(point_coordinate_buffer));
        dbgln("pcs: {}, bcnif: {}", point_coordinates.size(), byte_count_not_including_flags);
        if (point_coordinates.size() != byte_count_not_including_flags) {
            points.unchecked_append(FontPoint { .x = x, .y = y, .on_curve = on_curve });
            continue;
            //return Error::from_string_literal("Not enough data to read point coordinates for simple glyph"sv);
        }

        i16 delta_x = 0;
        i16 delta_y = 0;

        dbgln("xbits: {}, ybits: {}", coordinate_triplet_encoding.x_bits, coordinate_triplet_encoding.y_bits);

        switch (coordinate_triplet_encoding.x_bits) {
        case 0:
            break;
        case 4:
            delta_x = static_cast<i16>(point_coordinates[0] & 0xF0) >> 4;
            break;
        case 8:
            delta_x = static_cast<i16>(point_coordinates[0]);
            break;
        case 12:
            delta_x = (static_cast<i16>(point_coordinates[0]) << 4) | (static_cast<i16>(point_coordinates[1] & 0xF0) >> 4);
            break;
        case 16:
            delta_x = be_i16(point_coordinates.data());
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        switch (coordinate_triplet_encoding.y_bits) {
        case 0:
            break;
        case 4:
            delta_y = static_cast<i16>(point_coordinates[0] & 0x0F);
            break;
        case 8:
            delta_y = byte_count_not_including_flags == 2 ? static_cast<i16>(point_coordinates[1]) : static_cast<i16>(point_coordinates[0]);
            break;
        case 12:
            delta_y = (static_cast<i16>(point_coordinates[1] & 0x0F) << 4) | static_cast<i16>(point_coordinates[2]);
            break;
        case 16:
            delta_y = be_i16(point_coordinates.offset(2));
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        if (coordinate_triplet_encoding.delta_x.has_value()) {
            if (Checked<i16>::addition_would_overflow(delta_x, coordinate_triplet_encoding.delta_x.value()))
                return Error::from_errno(EOVERFLOW);

            delta_x += coordinate_triplet_encoding.delta_x.value();
        }

        if (coordinate_triplet_encoding.delta_y.has_value()) {
            if (Checked<i16>::addition_would_overflow(delta_y, coordinate_triplet_encoding.delta_y.value()))
                return Error::from_errno(EOVERFLOW);

            delta_y += coordinate_triplet_encoding.delta_y.value();
        }

        if (coordinate_triplet_encoding.negative_x.has_value() && coordinate_triplet_encoding.negative_x.value())
            delta_x = -delta_x;

        if (coordinate_triplet_encoding.negative_y.has_value() && coordinate_triplet_encoding.negative_y.value())
            delta_y = -delta_y;

        if (Checked<i16>::addition_would_overflow(x, delta_x))
            return Error::from_errno(EOVERFLOW);

        if (Checked<i16>::addition_would_overflow(y, delta_y))
            return Error::from_errno(EOVERFLOW);

        x += delta_x;
        y += delta_y;

        points.unchecked_append(FontPoint { .x = x, .y = y, .on_curve = on_curve });
    }

    return points;
}

static constexpr size_t TRANSFORMED_GLYF_TABLE_HEADER_SIZE_IN_BYTES = 36;

enum class LocaElementSize {
    TwoBytes,
    FourBytes,
};

struct GlyfAndLocaTableBuffers {
    ByteBuffer glyf_table;
    ByteBuffer loca_table;
};

enum SimpleGlyphFlags : u8 {
    OnCurve = 0x01,
    XShortVector = 0x02,
    YShortVector = 0x04,
    RepeatFlag = 0x08,
    XIsSameOrPositiveXShortVector = 0x10,
    YIsSameOrPositiveYShortVector = 0x20,
};

enum CompositeGlyphFlags : u16 {
    Arg1AndArg2AreWords = 0x0001,
    ArgsAreXYValues = 0x0002,
    RoundXYToGrid = 0x0004,
    WeHaveAScale = 0x0008,
    MoreComponents = 0x0020,
    WeHaveAnXAndYScale = 0x0040,
    WeHaveATwoByTwo = 0x0080,
    WeHaveInstructions = 0x0100,
    UseMyMetrics = 0x0200,
    OverlapCompound = 0x0400,
    ScaledComponentOffset = 0x0800,
    UnscaledComponentOffset = 0x1000,
};

static ErrorOr<GlyfAndLocaTableBuffers> create_glyf_and_loca_tables_from_transformed_glyf_table(Core::Stream::ReadonlyMemoryStream& table_stream, ByteBuffer& read_buffer)
{
    auto header_read_buffer = read_buffer.span().slice(0, TRANSFORMED_GLYF_TABLE_HEADER_SIZE_IN_BYTES);
    auto header_buffer = TRY(table_stream.read(header_read_buffer));
    dbgln("1");
    if (header_buffer.size() != TRANSFORMED_GLYF_TABLE_HEADER_SIZE_IN_BYTES)
        return Error::from_string_literal("Not enough data to read header of transformed glyf table"sv);

    for (size_t i = 0; i < header_buffer.size(); ++i)
        dbgln("head byte {}: 0x{:02x}", i, header_buffer[i]);

    // Skip: reserved, optionFlags
    u16 num_glyphs = be_u16(header_buffer.offset(4));
    u16 index_format = be_u16(header_buffer.offset(6));
    auto loca_element_size = index_format == 0 ? LocaElementSize::TwoBytes : LocaElementSize::FourBytes;

    u32 number_of_contours_stream_size = be_u32(header_buffer.offset(8));
    u32 number_of_points_stream_size = be_u32(header_buffer.offset(12));
    u32 flag_stream_size = be_u32(header_buffer.offset(16));
    u32 glyph_stream_size = be_u32(header_buffer.offset(20));
    u32 composite_stream_size = be_u32(header_buffer.offset(24));
    u32 bounding_box_stream_size = be_u32(header_buffer.offset(28));
    u32 instruction_stream_size = be_u32(header_buffer.offset(32));

    size_t table_size = TRY(table_stream.size());
    u64 total_size_of_streams = number_of_contours_stream_size;
    total_size_of_streams += number_of_points_stream_size;
    total_size_of_streams += flag_stream_size;
    total_size_of_streams += glyph_stream_size;
    total_size_of_streams += composite_stream_size;
    total_size_of_streams += bounding_box_stream_size;
    total_size_of_streams += instruction_stream_size;

    dbgln("table_size: {}, tsos: {}", table_size, total_size_of_streams);
    if (table_size < total_size_of_streams)
        return Error::from_string_literal("Not enough data to read in streams of transformed glyf table"sv);

    auto all_tables_buffer = TRY(ByteBuffer::create_zeroed(total_size_of_streams));
    u64 all_tables_buffer_offset = 0;

    dbgln("nocss: {}", number_of_contours_stream_size);
    auto number_of_contours_stream_buffer = TRY(table_stream.read(all_tables_buffer.span().slice(all_tables_buffer_offset, number_of_contours_stream_size)));
    auto number_of_contours_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(number_of_contours_stream_buffer));
    all_tables_buffer_offset += number_of_contours_stream_size;

    dbgln("nopss: {}", number_of_points_stream_size);
    auto number_of_points_stream_buffer = TRY(table_stream.read(all_tables_buffer.span().slice(all_tables_buffer_offset, number_of_points_stream_size)));
    auto number_of_points_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(number_of_points_stream_buffer));
    all_tables_buffer_offset += number_of_points_stream_size;

    dbgln("fss: {}", flag_stream_size);
    auto flag_stream_buffer = TRY(table_stream.read(all_tables_buffer.span().slice(all_tables_buffer_offset, flag_stream_size)));
    auto flag_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(flag_stream_buffer));
    all_tables_buffer_offset += flag_stream_size;

    dbgln("gss: {}", glyph_stream_size);
    auto glyph_stream_buffer = TRY(table_stream.read(all_tables_buffer.span().slice(all_tables_buffer_offset, glyph_stream_size)));
    dbgln("gsbs: {}", glyph_stream_buffer.size());
    auto glyph_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(glyph_stream_buffer));
    all_tables_buffer_offset += glyph_stream_size;

    auto composite_stream_buffer = TRY(table_stream.read(all_tables_buffer.span().slice(all_tables_buffer_offset, composite_stream_size)));
    auto composite_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(composite_stream_buffer));
    all_tables_buffer_offset += composite_stream_size;

    auto bounding_box_bitmap_stream_buffer = TRY(table_stream.read(all_tables_buffer.span().slice(all_tables_buffer_offset, num_glyphs)));
    auto bounding_box_bitmap_memory_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(bounding_box_bitmap_stream_buffer));
    auto bounding_box_bitmap_bit_stream = TRY(Core::Stream::BigEndianInputBitStream::construct(*bounding_box_bitmap_memory_stream));
    all_tables_buffer_offset += num_glyphs;

    // FIXME: Potential subtraction underflow
    auto bounding_box_stream_buffer = TRY(table_stream.read(all_tables_buffer.span().slice(all_tables_buffer_offset, bounding_box_stream_size - num_glyphs)));
    auto bounding_box_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(bounding_box_stream_buffer));
    all_tables_buffer_offset += bounding_box_stream_size - num_glyphs;

    auto instruction_buffer = TRY(table_stream.read(all_tables_buffer.span().slice(all_tables_buffer_offset, instruction_stream_size)));
    dbgln("iss: {}, ibs: {}", instruction_stream_size, instruction_buffer.size());
    auto instruction_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(instruction_buffer));

    auto two_byte_buffer = read_buffer.span().slice(0, 2);
    auto bounding_box_read_buffer = read_buffer.span().slice(0, 4 * sizeof(i16));


    ByteBuffer reconstructed_glyf_table_buffer;
    Vector<u32> loca_indexes;

    for (size_t glyph_index = 0; glyph_index < num_glyphs; ++glyph_index) {
        dbgln("glyph_index: {}", glyph_index);
        bool has_bounding_box = TRY(bounding_box_bitmap_bit_stream->read_bit());

        auto number_of_contours_buffer = TRY(number_of_contours_stream->read(two_byte_buffer));
        if (number_of_contours_buffer.size() != 2)
            return Error::from_string_literal("Not enough data in number of contours buffer"sv);

        i16 number_of_contours = be_i16(number_of_contours_buffer.data());
        dbgln("noc: {}", number_of_contours);

        size_t starting_glyf_table_size = reconstructed_glyf_table_buffer.size();

        if (number_of_contours == 0) {
            // Empty glyph
            if (has_bounding_box)
                return Error::from_string_literal("Empty glyphs cannot have an explicit bounding box"sv);
        } else if (number_of_contours > 0) {
            // Simple glyph
            Vector<u16> end_points_of_contours;

            u16 number_of_points = 0;

            for (i32 contour_index = 0; contour_index < number_of_contours; ++contour_index) {
                dbgln("= read nopftc");
                u32 number_of_points_for_this_contour = TRY(read_255_u_short(*number_of_points_stream, read_buffer));
                dbgln("ci: {}, nopftc: {}", contour_index, number_of_points_for_this_contour);

                if (Checked<u16>::addition_would_overflow(number_of_points, number_of_points_for_this_contour))
                    return Error::from_errno(EOVERFLOW);

                number_of_points += number_of_points_for_this_contour;

                if (Checked<u32>::addition_would_overflow(number_of_points, -1))
                    return Error::from_errno(EOVERFLOW);

                end_points_of_contours.append(number_of_points - 1);
            }

            auto points = TRY(retrieve_points_of_simple_glyph(*flag_stream, *glyph_stream, number_of_points, read_buffer));

            for (size_t i = 0; i < points.size(); ++i)
                dbgln("point {}: on_curve: {}, x: {}, y: {}", i, points[i].on_curve, points[i].x, points[i].y);

            dbgln("== read is");
            u16 instruction_size = 0;
            auto maybe_is = read_255_u_short(*glyph_stream, read_buffer);
            if (!maybe_is.is_error())
                instruction_size = maybe_is.release_value();
            auto instructions_read_buffer = TRY(ByteBuffer::create_zeroed(instruction_size));
            auto instructions_buffer = TRY(instruction_stream->read(instructions_read_buffer));
            dbgln("is: {}", instruction_size);
            if (instructions_buffer.size() != instruction_size)
                return Error::from_string_literal("Not enough data to read in instructions"sv);

            i16 bounding_box_x_min = 0;
            i16 bounding_box_y_min = 0;
            i16 bounding_box_x_max = 0;
            i16 bounding_box_y_max = 0;

            if (has_bounding_box) {
                auto bounding_box_buffer = TRY(bounding_box_stream->read(bounding_box_read_buffer));
                if (bounding_box_buffer.size() != 4 * sizeof(i16))
                    return Error::from_string_literal("Not enough data to read in bounding box"sv);

                bounding_box_x_min = be_i16(bounding_box_buffer.data());
                bounding_box_y_min = be_i16(bounding_box_buffer.offset(2));
                bounding_box_x_max = be_i16(bounding_box_buffer.offset(4));
                bounding_box_y_max = be_i16(bounding_box_buffer.offset(6));
            } else {
                for (size_t point_index = 0; point_index < points.size(); ++point_index) {
                    auto& point = points.at(point_index);

                    if (point_index == 0) {
                        bounding_box_x_min = bounding_box_x_max = point.x;
                        bounding_box_y_min = bounding_box_y_max = point.y;
                        continue;
                    }

                    bounding_box_x_min = min(bounding_box_x_min, point.x);
                    bounding_box_x_max = max(bounding_box_x_max, point.x);
                    bounding_box_y_min = min(bounding_box_y_min, point.y);
                    bounding_box_y_max = min(bounding_box_y_max, point.y);
                }
            }

            constexpr size_t GLYF_HEADER_SIZE_IN_BYTES = 10;
            u64 known_definite_size = reconstructed_glyf_table_buffer.size();
            known_definite_size += GLYF_HEADER_SIZE_IN_BYTES;
            known_definite_size += end_points_of_contours.size() * sizeof(u16);
            known_definite_size += sizeof(u16); // Instruction Length
            known_definite_size += instruction_size;

            dbgln("before: {} after: {}", reconstructed_glyf_table_buffer.size(), known_definite_size);
            TRY(reconstructed_glyf_table_buffer.try_resize(known_definite_size));
            size_t offset_into_known_definite_size_buffer = starting_glyf_table_size;

            be_i16(reconstructed_glyf_table_buffer.offset_pointer(offset_into_known_definite_size_buffer), number_of_contours);
            offset_into_known_definite_size_buffer += sizeof(i16);

            be_i16(reconstructed_glyf_table_buffer.offset_pointer(offset_into_known_definite_size_buffer), bounding_box_x_min);
            offset_into_known_definite_size_buffer += sizeof(i16);

            be_i16(reconstructed_glyf_table_buffer.offset_pointer(offset_into_known_definite_size_buffer), bounding_box_y_min);
            offset_into_known_definite_size_buffer += sizeof(i16);

            be_i16(reconstructed_glyf_table_buffer.offset_pointer(offset_into_known_definite_size_buffer), bounding_box_x_max);
            offset_into_known_definite_size_buffer += sizeof(i16);

            be_i16(reconstructed_glyf_table_buffer.offset_pointer(offset_into_known_definite_size_buffer), bounding_box_y_max);
            offset_into_known_definite_size_buffer += sizeof(i16);

            for (u16 end_point_of_contour : end_points_of_contours) {
                be_u16(reconstructed_glyf_table_buffer.offset_pointer(offset_into_known_definite_size_buffer), end_point_of_contour);
                offset_into_known_definite_size_buffer += sizeof(u16);
            }

            be_u16(reconstructed_glyf_table_buffer.offset_pointer(offset_into_known_definite_size_buffer), instruction_size);
            offset_into_known_definite_size_buffer += sizeof(u16);

            reconstructed_glyf_table_buffer.overwrite(offset_into_known_definite_size_buffer, instructions_buffer.data(), instruction_size);
            // NOTE: No need to add to offset_into_known_definite_size_buffer, as we don't know the size of the glpyh for definite from now on.

            // Point coordinates start off relative to (0, 0)
            i16 previous_point_x = 0;
            i16 previous_point_y = 0;

            for (auto& point : points) {
                u8 flags = 0;

                // FIXME: Implement flag deduplication (setting the REPEAT_FLAG for a string of same flags)

                if (point.on_curve)
                    flags |= SimpleGlyphFlags::OnCurve;

                if (previous_point_x == point.x) {
                    flags |= SimpleGlyphFlags::XIsSameOrPositiveXShortVector;
                } else if (point.x >= -255 && point.x <= 255) {
                    flags |= SimpleGlyphFlags::XShortVector;

                    if (point.x >= 0)
                        flags |= SimpleGlyphFlags::XIsSameOrPositiveXShortVector;
                }

                if (previous_point_y == point.y) {
                    flags |= SimpleGlyphFlags::YIsSameOrPositiveYShortVector;
                } else if (point.y >= -255 && point.y <= 255) {
                    flags |= SimpleGlyphFlags::YShortVector;

                    if (point.y >= 0)
                        flags |= SimpleGlyphFlags::YIsSameOrPositiveYShortVector;
                }

                previous_point_x = point.x;
                previous_point_y = point.y;
            }

            // Point coordinates start off relative to (0, 0)
            previous_point_x = 0;
            previous_point_y = 0;

            for (auto& point : points) {
                if (previous_point_x == point.x) {
                    // No need to write to the table.
                } else if (point.x >= -255 && point.x <= 255) {
                    TRY(reconstructed_glyf_table_buffer.try_append(abs(point.x)));
                } else {
                    TRY(reconstructed_glyf_table_buffer.try_resize(reconstructed_glyf_table_buffer.size() + 2));

                    // FIXME: This is kinda nasty.
                    be_i16(reconstructed_glyf_table_buffer.offset_pointer(-2), point.x);
                }
            }

            for (auto& point : points) {
                if (previous_point_y == point.y) {
                    // No need to write to the table.
                } else if (point.y >= -255 && point.y <= 255) {
                    TRY(reconstructed_glyf_table_buffer.try_append(abs(point.y)));
                } else {
                    TRY(reconstructed_glyf_table_buffer.try_resize(reconstructed_glyf_table_buffer.size() + 2));

                    // FIXME: This is kinda nasty.
                    be_i16(reconstructed_glyf_table_buffer.offset_pointer(-2), point.y);
                }
            }
        } else {
            // Composite glyph
            if (!has_bounding_box)
                return Error::from_string_literal("Composite glyphs must have an explicit bounding box"sv);

            return Error::from_string_literal("Composite glyphs not supported yet"sv);
        }

        dbgln("nli: {}", starting_glyf_table_size);
        loca_indexes.append(starting_glyf_table_size);
    }

    loca_indexes.append(reconstructed_glyf_table_buffer.size());

    u8 loca_element_size_in_bytes = loca_element_size == LocaElementSize::TwoBytes ? sizeof(u16) : sizeof(u32);
    size_t loca_table_buffer_size = loca_indexes.size() * loca_element_size_in_bytes;
    ByteBuffer loca_table_buffer = TRY(ByteBuffer::create_zeroed(loca_table_buffer_size));
    for (size_t loca_indexes_index = 0; loca_indexes_index < loca_indexes.size(); ++loca_indexes_index) {
        u32 loca_index = loca_indexes.at(loca_indexes_index);
        size_t loca_offset = loca_indexes_index * loca_element_size_in_bytes;

        if (loca_element_size == LocaElementSize::TwoBytes) {
            dbgln("loca_index: 0x{:08x}", loca_index >> 1);
            be_u16(loca_table_buffer.offset_pointer(loca_offset), loca_index >> 1);
        } else {
            dbgln("loca_index: 0x{:08x}", loca_index);

            be_u32(loca_table_buffer.offset_pointer(loca_offset), loca_index);
        }
    }

    dbgln("gts: {}, lts: {}", reconstructed_glyf_table_buffer.size(), loca_table_buffer.size());
    for (size_t i = 0; i < reconstructed_glyf_table_buffer.size(); ++i)
        dbgln("rgtb[{}] = 0x{:02x}", i, reconstructed_glyf_table_buffer[i]);
    return GlyfAndLocaTableBuffers { .glyf_table = move(reconstructed_glyf_table_buffer), .loca_table = move(loca_table_buffer) };
}

ErrorOr<NonnullRefPtr<Font>> Font::try_load_from_file(StringView path)
{
    auto woff2_file_stream = TRY(Core::Stream::File::open(path, Core::Stream::OpenMode::Read));
    return try_load_from_externally_owned_memory(*woff2_file_stream);
}

ErrorOr<NonnullRefPtr<Font>> Font::try_load_from_externally_owned_memory(Core::Stream::SeekableStream& stream)
{
    auto stream_size = TRY(stream.size());
    auto read_buffer = TRY(ByteBuffer::create_zeroed(BUFFER_SIZE));

    auto header_buffer = read_buffer.span().slice(0, WOFF2_HEADER_SIZE_IN_BYTES);
    auto header_bytes = TRY(stream.read(header_buffer));
    if (header_bytes.size() != WOFF2_HEADER_SIZE_IN_BYTES)
        return Error::from_string_literal("WOFF2 file too small"sv);

    // The signature field in the WOFF2 header MUST contain the value of 0x774F4632 ('wOF2'), which distinguishes it from WOFF 1.0 files.
    // If the field does not contain this value, user agents MUST reject the file as invalid.
    u32 signature = be_u32(header_bytes.data());
    dbgln("woff2 signature: 0x{:08x}", signature);
    if (signature != WOFF2_SIGNATURE)
        return Error::from_string_literal("Invalid WOFF2 signature"sv);

    // The interpretation of the WOFF2 Header is the same as the WOFF Header in [WOFF1], with the addition of one new totalCompressedSize field.
    // NOTE: See WOFF/Font.cpp for more comments about this.
    u32 flavor = be_u32(header_bytes.offset(4));           // The "sfnt version" of the input font.
    u32 length = be_u32(header_bytes.offset(8));           // Total size of the WOFF file.
    u16 num_tables = be_u16(header_bytes.offset(12));      // Number of entries in directory of font tables.
    // Skip: reserved
    u32 total_sfnt_size = be_u32(header_bytes.offset(16)); // Total size needed for the uncompressed font data, including the sfnt header, directory, and font tables (including padding).
    u32 total_compressed_size = be_u32(header_bytes.offset(20)); // Total length of the compressed data block.
    // Skip: major_version, minor_version
    u32 meta_offset = be_u32(header_bytes.offset(28)); // Offset to metadata block, from beginning of WOFF file.
    u32 meta_length = be_u32(header_bytes.offset(32)); // Length of compressed metadata block.
    // Skip: meta_orig_length
    u32 priv_offset = be_u32(header_bytes.offset(40)); // Offset to private data block, from beginning of WOFF file.
    u32 priv_length = be_u32(header_bytes.offset(44)); // Length of private data block.

    if (length > stream_size)
        return Error::from_string_literal("Invalid WOFF length"sv);
    if (meta_length == 0 && meta_offset != 0)
        return Error::from_string_literal("Invalid WOFF meta block offset"sv);
    if (priv_length == 0 && priv_offset != 0)
        return Error::from_string_literal("Invalid WOFF private block offset"sv);
    if (flavor == TTCF_SIGNAURE)
        return Error::from_string_literal("Font collections not yet supported"sv);

    // NOTE: "The "totalSfntSize" value in the WOFF2 Header is intended to be used for reference purposes only. It may represent the size of the uncompressed input font file,
    //        but if the transformed 'glyf' and 'loca' tables are present, the uncompressed size of the reconstructed tables and the total decompressed font size may differ
    //        substantially from the original total size specified in the WOFF2 Header."
    //        We use it as an initial size of the font buffer and extend it as necessary.
    auto font_buffer = TRY(ByteBuffer::create_zeroed(total_sfnt_size));

    // ISO-IEC 14496-22:2019 4.5.1 Offset table
    constexpr size_t OFFSET_TABLE_SIZE_IN_BYTES = 12;
    TRY(font_buffer.try_ensure_capacity(OFFSET_TABLE_SIZE_IN_BYTES));
    u16 search_range = pow_2_less_than_or_equal(num_tables);
    be_u32(font_buffer.data() + 0, flavor);
    be_u16(font_buffer.data() + 4, num_tables);
    be_u16(font_buffer.data() + 6, search_range * 16);
    be_u16(font_buffer.data() + 8, AK::log2(search_range));
    be_u16(font_buffer.data() + 10, num_tables * 16 - search_range * 16);

    Vector<TableDirectoryEntry> table_entries;
    TRY(table_entries.try_ensure_capacity(num_tables));

    auto one_byte_buffer = read_buffer.span().slice(0, 1);
    u64 total_length_of_all_tables = 0;

    for (size_t table_entry_index = 0; table_entry_index < num_tables; ++table_entry_index) {
        TableDirectoryEntry table_directory_entry;
        auto flags_byte_buffer = TRY(stream.read(one_byte_buffer));
        if (flags_byte_buffer.size() != 1)
            return Error::from_string_literal("Not enough data to read flags entry of table directory entry"sv);

        u8 flags_byte = flags_byte_buffer[0];

        switch ((flags_byte & 0xC0) >> 6) {
        case 0:
            table_directory_entry.transformation_version = TransformationVersion::Version0;
            break;
        case 1:
            table_directory_entry.transformation_version = TransformationVersion::Version1;
            break;
        case 2:
            table_directory_entry.transformation_version = TransformationVersion::Version2;
            break;
        case 3:
            table_directory_entry.transformation_version = TransformationVersion::Version3;
            break;
        default:
            VERIFY_NOT_REACHED();
        }

        u8 tag_number = flags_byte & 0x3F;

        if (tag_number != 0x3F) {
            table_directory_entry.tag = known_tag_names[tag_number];
        } else {
            auto four_byte_buffer = read_buffer.span().slice(0, 4);
            auto tag_name_buffer = TRY(stream.read(four_byte_buffer));
            if (tag_name_buffer.size() != 4)
                return Error::from_string_literal("Not enough data to read tag name entry of table directory entry"sv);

            table_directory_entry.tag = tag_name_buffer;
        }

        VERIFY(table_directory_entry.tag.length() == 4);
        table_directory_entry.original_length = TRY(read_uint_base_128(stream, read_buffer));

        bool needs_to_read_transform_length = false;
        if (table_directory_entry.tag.is_one_of("glyf"sv, "loca"sv))
            needs_to_read_transform_length = table_directory_entry.transformation_version == TransformationVersion::Version0;
        else
            needs_to_read_transform_length = table_directory_entry.transformation_version != TransformationVersion::Version0;

        if (needs_to_read_transform_length) {
            dbgln("table {} has transform", table_directory_entry.tag);
            u32 transform_length = TRY(read_uint_base_128(stream, read_buffer));
            table_directory_entry.transform_length = transform_length;
            total_length_of_all_tables += transform_length;
        } else {
            total_length_of_all_tables += table_directory_entry.original_length;
        }

        table_entries.unchecked_append(move(table_directory_entry));
    }

    // FIXME: Read in collection header and entries.

    auto glyf_table = table_entries.find_if([](TableDirectoryEntry const& entry) {
        return entry.tag == "glyf"sv;
    });

    auto loca_table = table_entries.find_if([](TableDirectoryEntry const& entry) {
        return entry.tag == "loca"sv;
    });

    // "In other words, both glyf and loca tables must either be present in their transformed format or with null transform applied to both tables."
    if (glyf_table.is_end() != loca_table.is_end())
        return Error::from_string_literal("Must have both 'loca' and 'glyf' tables if one of them is present"sv);

    if (!glyf_table.is_end() && !loca_table.is_end()) {
        if (glyf_table->transformation_version != loca_table->transformation_version)
            return Error::from_string_literal("The 'loca' and 'glyf' tables must have the same transformation version"sv);
    }

    if (!loca_table.is_end()) {
        if (loca_table->has_transformation() && loca_table->transform_length.value() != 0)
            return Error::from_string_literal("Transformed 'loca' table must have a transform length of 0"sv);
    }

    auto compressed_bytes_read_buffer = TRY(ByteBuffer::create_zeroed(total_compressed_size));
    auto compressed_bytes = TRY(stream.read(compressed_bytes_read_buffer));
    if (compressed_bytes.size() != total_compressed_size)
        return Error::from_string_literal("Not enough data to read in the reported size of the compressed data"sv);

    auto compressed_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(compressed_bytes));
    auto brotli_stream = Compress::BrotliDecompressionStream { *compressed_stream };
    auto decompressed_table_data = TRY(brotli_stream.read_all());
    if (decompressed_table_data.size() != total_length_of_all_tables)
        return Error::from_string_literal("Size of the decompressed data is not equal to the total of the reported lengths of each table"sv);

    auto decompressed_data_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(decompressed_table_data));
    size_t font_buffer_offset = SFNT_HEADER_SIZE + num_tables * SFNT_TABLE_SIZE;
    for (size_t table_entry_index = 0; table_entry_index < num_tables; ++table_entry_index) {
        auto& table_entry = table_entries.at(table_entry_index);
        u32 length_to_read = table_entry.has_transformation() ? table_entry.transform_length.value() : table_entry.original_length;

        auto table_buffer = TRY(ByteBuffer::create_zeroed(length_to_read));
        auto table_bytes = TRY(decompressed_data_stream->read(table_buffer));
        if (table_bytes.size() != length_to_read)
            return Error::from_string_literal("Not enough data to read decompressed table"sv);

        size_t table_directory_offset = SFNT_HEADER_SIZE + table_entry_index * SFNT_TABLE_SIZE;

        if (table_entry.has_transformation()) {
            if (table_entry.tag == "glyf"sv) {
                auto table_stream = TRY(Core::Stream::ReadonlyMemoryStream::construct(table_bytes));
                auto glyf_and_loca_buffer = TRY(create_glyf_and_loca_tables_from_transformed_glyf_table(*table_stream, read_buffer));

                constexpr u32 GLYF_TAG = 0x676C7966;
                constexpr u32 LOCA_TAG = 0x6C6F6361;

                TRY(font_buffer.try_ensure_capacity(font_buffer_offset + glyf_and_loca_buffer.glyf_table.size() + glyf_and_loca_buffer.loca_table.size()));

                // ISO-IEC 14496-22:2019 4.5.2 Table directory
                be_u32(font_buffer.data() + table_directory_offset, GLYF_TAG);
                // FIXME: WOFF2 does not give us the original checksum.
                be_u32(font_buffer.data() + table_directory_offset + 4, 0);
                be_u32(font_buffer.data() + table_directory_offset + 8, font_buffer_offset);
                be_u32(font_buffer.data() + table_directory_offset + 12, glyf_and_loca_buffer.glyf_table.size());

                font_buffer.overwrite(font_buffer_offset, glyf_and_loca_buffer.glyf_table.data(), glyf_and_loca_buffer.glyf_table.size());
                font_buffer_offset += glyf_and_loca_buffer.glyf_table.size();

                be_u32(font_buffer.data() + table_directory_offset + 16, LOCA_TAG);
                // FIXME: WOFF2 does not give us the original checksum.
                be_u32(font_buffer.data() + table_directory_offset + 20, 0);
                be_u32(font_buffer.data() + table_directory_offset + 24, font_buffer_offset);
                be_u32(font_buffer.data() + table_directory_offset + 28, glyf_and_loca_buffer.loca_table.size());

                font_buffer.overwrite(font_buffer_offset, glyf_and_loca_buffer.loca_table.data(), glyf_and_loca_buffer.loca_table.size());
                font_buffer_offset += glyf_and_loca_buffer.loca_table.size();
            } else if (table_entry.tag == "loca"sv) {
                // Do nothing. Transformed loca tables don't have any length and are constructed from the transformed glyf table.
            } else if (table_entry.tag == "hmtx"sv) {
                return Error::from_string_literal("Decoding transformed hmtx table not yet supported"sv);
            } else {
                return Error::from_string_literal("Unknown transformation"sv);
            }
        } else {
            // ISO-IEC 14496-22:2019 4.5.2 Table directory
            be_u32(font_buffer.data() + table_directory_offset, table_entry.tag_to_u32());
            // FIXME: WOFF2 does not give us the original checksum.
            be_u32(font_buffer.data() + table_directory_offset + 4, 0);
            be_u32(font_buffer.data() + table_directory_offset + 8, font_buffer_offset);
            be_u32(font_buffer.data() + table_directory_offset + 12, length_to_read);

            TRY(font_buffer.try_ensure_capacity(font_buffer_offset + length_to_read));
            font_buffer.overwrite(font_buffer_offset, table_buffer.data(), length_to_read);

            font_buffer_offset += length_to_read;
        }
    }

//    for (size_t i = 0; i < font_buffer.size(); ++i) {
//        dbgln("font_buffer[{}] = 0x{:02x} ('{}')", i, font_buffer[i], (char)font_buffer[i]);
//    }

//    auto remaining = TRY(stream.read_all());
//    for (size_t i = 0; i < remaining.size(); ++i) {
//        dbgln("{}: 0x{:02x}", i, remaining[i]);
//    }

    auto input_font = TRY(TTF::Font::try_load_from_externally_owned_memory(font_buffer.bytes()));
    return adopt_ref(*new Font(input_font, move(font_buffer)));;
}

}
