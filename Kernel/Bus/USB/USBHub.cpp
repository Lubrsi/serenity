//
// Created by lukew on 03/08/2021.
//

#include <Kernel/StdLib.h>
#include <Kernel/Bus/USB/USBController.h>
#include <Kernel/Bus/USB/USBHub.h>
#include <Kernel/Bus/USB/USBClasses.h>
#include <Kernel/Bus/USB/USBRequest.h>

namespace Kernel::USB {

KResultOr<NonnullRefPtr<Hub>> Hub::try_create_root_hub(NonnullRefPtr<USBController> controller, DeviceSpeed device_speed)
{
    auto pipe_or_error = Pipe::try_create_pipe(controller, Pipe::Type::Control, Pipe::Direction::Bidirectional, 0, 8, 0);
    if (pipe_or_error.is_error())
        return pipe_or_error.error();

    auto hub = AK::try_create<Hub>(controller, device_speed, pipe_or_error.release_value());
    if (!hub)
        return ENOMEM;

    // NOTE: Enumeration does not happen here, as the controller must know what the device address is at all times during enumeration to intercept requests.

    return hub.release_nonnull();
}

KResultOr<NonnullRefPtr<Hub>> Hub::try_create_from_device(Device& device)
{
    auto pipe_or_error = Pipe::try_create_pipe(device.controller(), Pipe::Type::Control, Pipe::Direction::Bidirectional, 0, device.device_descriptor().max_packet_size, device.address());
    if (pipe_or_error.is_error())
        return pipe_or_error.error();

    auto hub = AK::try_create<Hub>(device, pipe_or_error.release_value());
    if (!hub)
        return ENOMEM;

    // FIXME: HACK
    memcpy(&hub->m_device_descriptor, &device.device_descriptor(), sizeof(USBDeviceDescriptor));

    auto result = hub->enumerate_and_power_on_hub();
    if (result.is_error())
        return result;



    return hub.release_nonnull();
}

Hub::Hub(NonnullRefPtr<USBController> controller, DeviceSpeed device_speed, NonnullOwnPtr<Pipe> default_pipe)
    : Device(move(controller), PortNumber::Port1, device_speed, move(default_pipe))
{
}

Hub::Hub(Device const& device, NonnullOwnPtr<Pipe> default_pipe)
    : Device(device.controller(), device.address(), device.port(), device.speed(), move(default_pipe))
{
}

KResult Hub::enumerate_and_power_on_hub()
{
    // USBDevice::enumerate_device must be called before this.
    VERIFY(m_address > 0);

    dbgln("USB Hub: Enumerating and powering on for address {}", m_address);

    // FIXME
//    if (m_device_descriptor.device_class != (u8)Class::Hub) {
//        dbgln("Not a hub :^(");
//        return EINVAL;
//    }

    USBHubDescriptor descriptor {};

    m_default_pipe->set_device_address(m_address);

    // Get the first hub descriptor. All hubs are required to have a hub descriptor at index 0. USB 2.0 Specification Section 11.24.2.5.
    auto transfer_length_or_error = m_default_pipe->control_transfer(USB_REQUEST_TRANSFER_DIRECTION_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS, HubRequest::GET_DESCRIPTOR, (DESCRIPTOR_TYPE_HUB << 8), 0, sizeof(USBHubDescriptor), &descriptor);
    if (transfer_length_or_error.is_error())
        return transfer_length_or_error.error();

    if (transfer_length_or_error.value() < sizeof(USBHubDescriptor)) {
        dbgln("not enough bytes :^( {} < {}", transfer_length_or_error.value(), sizeof(USBHubDescriptor));
        return EIO;
    }

    if constexpr (USB_DEBUG) {
        dbgln("USB Hub Descriptor for {:04x}:{:04x}", m_vendor_id, m_product_id);
        dbgln("Number of Downstream Ports: {}", descriptor.number_of_downstream_ports);
        dbgln("Hub Characteristics: 0x{:04x}", descriptor.hub_characteristics);
        dbgln("Power On to Power Good Time: {} ms ({} * 2ms)", descriptor.power_on_to_power_good_time * 2, descriptor.power_on_to_power_good_time);
        dbgln("Hub Controller Current: {} mA", descriptor.hub_controller_current);
    }

    // FIXME: Queue the status change interrupt

    // Enable all the ports
    for (u8 port_index = 0; port_index < descriptor.number_of_downstream_ports; ++port_index) {
        auto result = m_default_pipe->control_transfer(USB_REQUEST_TRANSFER_DIRECTION_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_OTHER, HubRequest::SET_FEATURE, HubFeatureSelector::PORT_POWER, port_index + 1, 0, nullptr);
        if (result.is_error())
            dbgln("USB: Failed to power on port {} on hub at address {}.", port_index + 1, m_address);
    }

    // Wait for the ports to power up. power_on_to_power_good_time is in units of 2 ms and we want in us, so multiply by 2000.
    IO::delay(descriptor.power_on_to_power_good_time * 2000);

    memcpy(&m_hub_descriptor, &descriptor, sizeof(USBHubDescriptor));

    return KSuccess;
}

// USB 2.0 Specification Section 11.24.2.7
KResult Hub::get_port_status(u8 port, HubStatus& hub_status)
{
    // Ports are 1-based.
    if (port == 0 || port > m_hub_descriptor.number_of_downstream_ports)
        return EINVAL;

    auto transfer_length_or_error = m_default_pipe->control_transfer(USB_REQUEST_TRANSFER_DIRECTION_DEVICE_TO_HOST | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_OTHER, HubRequest::GET_STATUS, 0, port, sizeof(HubStatus), &hub_status);
    if (transfer_length_or_error.is_error())
        return transfer_length_or_error.error();

    // FIXME
//    if (transfer_length_or_error.value() != sizeof(HubStatus)) {
//        dbgln("USB Hub: Unexpected hub status size. Expected {}, got {}.", sizeof(HubStatus), transfer_length_or_error.value());
//        return EIO;
//    }

    return KSuccess;
}

// USB 2.0 Specification Section 11.24.2.2
KResult Hub::clear_port_feature(u8 port, HubFeatureSelector feature_selector)
{
    // Ports are 1-based.
    if (port == 0 || port > m_hub_descriptor.number_of_downstream_ports)
        return EINVAL;

    auto result = m_default_pipe->control_transfer(USB_REQUEST_TRANSFER_DIRECTION_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_OTHER, HubRequest::CLEAR_FEATURE, feature_selector, port, 0, nullptr);
    if (result.is_error())
        return result.error();

    return KSuccess;
}

// USB 2.0 Specification Section 11.24.2.13
KResult Hub::set_port_feature(u8 port, HubFeatureSelector feature_selector)
{
    // Ports are 1-based.
    if (port == 0 || port > m_hub_descriptor.number_of_downstream_ports)
        return EINVAL;

    auto result = m_default_pipe->control_transfer(USB_REQUEST_TRANSFER_DIRECTION_HOST_TO_DEVICE | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_OTHER, HubRequest::SET_FEATURE, feature_selector, port, 0, nullptr);
    if (result.is_error())
        return result.error();

    return KSuccess;
}

void Hub::check_for_port_updates()
{
    for (u8 port_number = 1; port_number < m_hub_descriptor.number_of_downstream_ports + 1; ++port_number) {
        dbgln("USB Hub: Checking for port updates on port {}...", port_number);

        HubStatus port_status {};
        auto result = get_port_status(port_number, port_status);
        if (result.is_error()) {
            dbgln("USB Hub: Error occurred when getting status for port {}: {}. Checking next port instead.", port_number, result.error());
            continue;
        }

        if (port_status.change & PORT_STATUS_CONNECT_STATUS_CHANGED) {
            if (port_status.status & PORT_STATUS_CURRENT_CONNECT_STATUS) {
                dbgln("USB Hub: New device attached to port {}!", port_number);

                result = clear_port_feature(port_number, HubFeatureSelector::C_PORT_CONNECTION);
                if (result.is_error()) {
                    dbgln("USB Hub: Error occurred when clearing port connection change for port {}: {}.", port_number, result.error());
                    return;
                }

                // Debounce the port. USB 2.0 Specification Page 150
                // Debounce interval is 100 ms (100000 us). USB 2.0 Specification Page 188 Table 7-14.
                constexpr u32 debounce_interval = 100 * 1000;

                // We must check if the device disconnected every so often. If it disconnects, we must reset the debounce timer.
                // This doesn't seem to be specified. Let's check every 10ms (10000 us).
                constexpr u32 debounce_disconnect_check_interval = 10 * 1000;

                u32 debounce_timer = 0;

                // FIXME: Timeout
                dbgln("USB Hub: Debouncing...");
                while (debounce_timer < debounce_interval) {
                    IO::delay(debounce_disconnect_check_interval);
                    debounce_timer += debounce_disconnect_check_interval;

                    result = get_port_status(port_number, port_status);
                    if (result.is_error()) {
                        dbgln("USB Hub: Error occurred when getting status while debouncing port {}: {}.", port_number, result.error());
                        return;
                    }

                    if (!(port_status.change & PORT_STATUS_CONNECT_STATUS_CHANGED))
                        continue;

                    dbgln("USB Hub: Connect status changed while debouncing, resetting debounce timer.");
                    debounce_timer = 0;
                    result = clear_port_feature(port_number, HubFeatureSelector::C_PORT_CONNECTION);
                    if (result.is_error()) {
                        dbgln("USB Hub: Error occurred when clearing port connection change while debouncing port {}: {}.", port_number, result.error());
                        return;
                    }
                }

                // Reset the port
                dbgln("USB Hub: Driving reset...");
                result = set_port_feature(port_number, HubFeatureSelector::PORT_RESET);
                if (result.is_error()) {
                    dbgln("USB Hub: Error occurred when resetting port {}: {}.", port_number, result.error());
                    return;
                }

                // FIXME: Timeout
                for (;;) {
                    // Wait at least 50 ms for the port to reset.
                    IO::delay(50000);

                    result = get_port_status(port_number, port_status);
                    if (result.is_error()) {
                        dbgln("USB Hub: Error occurred when getting status while resetting port {}: {}.", port_number, result.error());
                        return;
                    }

                    if (port_status.change & PORT_STATUS_RESET_CHANGED)
                        break;
                }

                // Stop asserting reset. This also causes the port to become enabled.
                result = clear_port_feature(port_number, HubFeatureSelector::C_PORT_RESET);
                if (result.is_error()) {
                    dbgln("USB Hub: Error occurred when resetting port {}: {}.", port_number, result.error());
                    return;
                }

                // Wait at least 10 ms for the port to recover.
                IO::delay(10000);

                dbgln("USB Hub: Reset complete!");

                result = get_port_status(port_number, port_status);
                if (result.is_error()) {
                    dbgln("USB Hub: Error occurred when getting status for port {} after reset: {}.", port_number, result.error());
                    return;
                }

                // FIXME: Check for high speed.
                auto speed = port_status.status & PORT_STATUS_LOW_SPEED_DEVICE_ATTACHED ? USB::Device::DeviceSpeed::LowSpeed : USB::Device::DeviceSpeed::FullSpeed;

                // FIXME: This only assumes two ports.
                auto device_or_error = USB::Device::try_create(m_controller, port_number == 1 ? PortNumber::Port1 : PortNumber::Port2, speed);
                if (device_or_error.is_error()) {
                    dbgln("USB Hub: Failed to create device for port {}: {}", port_number, device_or_error.error());
                    return;
                }

                dbgln("USB Hub: Created device!");

                auto device = device_or_error.release_value();
                if (device->device_descriptor().device_class == (u8)Class::Hub) {
                    auto hub_or_error = Hub::try_create_from_device(*device);
                    if (hub_or_error.is_error()) {
                        dbgln("USB Hub: Failed to upgrade device to hub for port {}: {}", port_number, device_or_error.error());
                        return;
                    }

                    dbgln("USB Hub: Upgraded device to hub!");


                    m_children.append(hub_or_error.release_value());
                } else {
                    m_children.append(device);
                }

            } else {
                dbgln("USB Hub: Device detached on port {}!", port_number);
            }
        }
    }

    for (auto& child : m_children) {
        dbgln("USB Hub: Looking at child with class {}", child.device_descriptor().device_class);
        if (child.device_descriptor().device_class == (u8)Class::Hub) {
            auto& hub_child = static_cast<Hub&>(child);
            dbgln("USB Hub: Checking for porting updates on child hub...");
            hub_child.check_for_port_updates();
        }
    }
}

}
