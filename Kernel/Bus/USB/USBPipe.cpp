/*
 * Copyright (c) 2021, Jesse Buhagiar <jooster669@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/Bus/USB/PacketTypes.h>
#include <Kernel/Bus/USB/UHCI/UHCIController.h>
#include <Kernel/Bus/USB/USBPipe.h>
#include <Kernel/Bus/USB/USBTransfer.h>

namespace Kernel::USB {

KResultOr<NonnullOwnPtr<Pipe>> Pipe::try_create_pipe(USBController const& controller, Type type, Direction direction, DeviceSpeed speed, u8 endpoint_address, u16 max_packet_size, i8 device_address, u8 poll_interval)
{
    auto pipe = adopt_own_if_nonnull(new (nothrow) Pipe(controller, type, direction, speed, endpoint_address, max_packet_size, poll_interval, device_address));
    if (!pipe)
        return ENOMEM;

    return pipe.release_nonnull();
}

Pipe::Pipe(USBController const& controller, Type type, Pipe::Direction direction, DeviceSpeed speed, u16 max_packet_size)
    : m_controller(controller)
    , m_type(type)
    , m_direction(direction)
    , m_speed(speed)
    , m_endpoint_address(0)
    , m_max_packet_size(max_packet_size)
    , m_poll_interval(0)
    , m_data_toggle(false)
{
    auto result = m_controller->pipe_created(*this);
    // FIXME: For now.
    VERIFY(!result.is_error());
}

Pipe::Pipe(USBController const& controller, Type type, Direction direction, DeviceSpeed speed, USBEndpointDescriptor& endpoint [[maybe_unused]])
    : m_controller(controller)
    , m_type(type)
    , m_direction(direction)
    , m_speed(speed)
{
    // TODO: decode endpoint structure

    auto result = m_controller->pipe_created(*this);
    // FIXME: For now.
    VERIFY(!result.is_error());
}

Pipe::Pipe(USBController const& controller, Type type, Direction direction, DeviceSpeed speed, u8 endpoint_address, u16 max_packet_size, u8 poll_interval, i8 device_address)
    : m_controller(controller)
    , m_type(type)
    , m_direction(direction)
    , m_speed(speed)
    , m_device_address(device_address)
    , m_endpoint_address(endpoint_address)
    , m_max_packet_size(max_packet_size)
    , m_poll_interval(poll_interval)
    , m_data_toggle(false)
{
    auto result = m_controller->pipe_created(*this);
    // FIXME: For now.
    VERIFY(!result.is_error());
}

Pipe::~Pipe()
{
    auto result = m_controller->pipe_destroyed(*this);
    // FIXME: For now.
    VERIFY(!result.is_error());
}

KResultOr<size_t> Pipe::control_transfer(u8 request_type, u8 request, u16 value, u16 index, u16 length, void* data)
{
    USBRequestData usb_request;

    usb_request.request_type = request_type;
    usb_request.request = request;
    usb_request.value = value;
    usb_request.index = index;
    usb_request.length = length;

    auto transfer = Transfer::try_create(*this, length);

    if (!transfer)
        return ENOMEM;

    transfer->set_setup_packet(usb_request);

    dbgln_if(USB_DEBUG, "Pipe: Transfer allocated @ {}", transfer->buffer_physical());
    auto transfer_len_or_error = m_controller->submit_control_transfer(*transfer);

    if (transfer_len_or_error.is_error())
        return transfer_len_or_error.error();

    auto transfer_length = transfer_len_or_error.release_value();

    // TODO: Check transfer for completion and copy data from transfer buffer into data
    if (length > 0)
        memcpy(reinterpret_cast<u8*>(data), transfer->buffer().as_ptr() + sizeof(USBRequestData), length);

    dbgln_if(USB_DEBUG, "Pipe: Control Transfer complete!");
    return transfer_length;
}

void Pipe::set_max_packet_size(u16 max_size)
{
    m_max_packet_size = max_size;
    auto result = m_controller->pipe_changed(*this);
    // FIXME: For now.
    VERIFY(!result.is_error());
}

void Pipe::set_device_address(i8 addr)
{
    m_device_address = addr;
    auto result = m_controller->pipe_changed(*this);
    // FIXME: For now.
    VERIFY(!result.is_error());
}

}
