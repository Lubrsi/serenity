/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/Bus/USB/OHCIRootHub.h>
#include <Kernel/Bus/USB/OHCIController.h>
#include <Kernel/Bus/USB/USBClasses.h>
#include <Kernel/Bus/USB/USBEndpoint.h>
#include <Kernel/Bus/USB/USBHub.h>
#include <Kernel/Bus/USB/USBRequest.h>
#include <Kernel/StdLib.h>

namespace Kernel::USB {

static USBDeviceDescriptor ohci_root_hub_device_descriptor = {
    sizeof(USBDeviceDescriptor), // 18 bytes long
    DESCRIPTOR_TYPE_DEVICE,
    0x0110, // USB 1.1
    USB_CLASS_HUB,
    0, // Hubs use subclass 0
    0, // Full Speed Hub
    64, // Max packet size
    0x0, // Vendor ID
    0x0, // Product ID
    0x0110, // Product version (can be anything, currently matching usb_spec_compliance_bcd)
    0, // Index of manufacturer string. FIXME: There is currently no support for string descriptors.
    0, // Index of product string. FIXME: There is currently no support for string descriptors.
    0, // Index of serial string. FIXME: There is currently no support for string descriptors.
    1, // One configuration descriptor
};

static USBConfigurationDescriptor ohci_root_hub_configuration_descriptor = {
    sizeof(USBConfigurationDescriptor), // 9 bytes long
    DESCRIPTOR_TYPE_CONFIGURATION,
    sizeof(USBConfigurationDescriptor) + sizeof(USBInterfaceDescriptor) + sizeof(USBEndpointDescriptor) + sizeof(USBHubDescriptor), // Combined length of configuration, interface, endpoint and hub descriptors.
    1, // One interface descriptor
    1, // Configuration #1
    0, // Index of configuration string. FIXME: There is currently no support for string descriptors.
    (1 << 7) | (1 << 6), // Bit 6 is set to indicate that the root hub is self powered. Bit 7 must always be 1.
    0, // 0 mA required from the bus (self-powered)
};

static USBInterfaceDescriptor ohci_root_hub_interface_descriptor = {
    sizeof(USBInterfaceDescriptor), // 9 bytes long
    DESCRIPTOR_TYPE_INTERFACE,
    0, // Interface #0
    0, // Alternate setting
    1, // One endpoint
    USB_CLASS_HUB,
    0, // Hubs use subclass 0
    0, // Full Speed Hub
    0, // Index of interface string. FIXME: There is currently no support for string descriptors
};

static USBEndpointDescriptor ohci_root_hub_endpoint_descriptor = {
    sizeof(USBEndpointDescriptor), // 7 bytes long
    DESCRIPTOR_TYPE_ENDPOINT,
    USBEndpoint::ENDPOINT_ADDRESS_DIRECTION_IN | 1, // IN Endpoint #1
    USBEndpoint::ENDPOINT_ATTRIBUTES_TRANSFER_TYPE_INTERRUPT, // Interrupt endpoint
    2, // Max Packet Size FIXME: I'm not sure what this is supposed to be as it is implementation defined. 2 is the number of bytes Get Port Status returns.
    0xFF, // Max possible interval
};

KResultOr<NonnullOwnPtr<OHCIRootHub>> OHCIRootHub::try_create(NonnullRefPtr<OHCIController> ohci_controller)
{
    auto root_hub = adopt_own_if_nonnull(new (nothrow) OHCIRootHub(ohci_controller));
    if (!root_hub)
        return ENOMEM;

    return root_hub.release_nonnull();
}

OHCIRootHub::OHCIRootHub(NonnullRefPtr<OHCIController> ohci_controller)
: m_ohci_controller(ohci_controller)
{
}

KResult OHCIRootHub::setup(Badge<OHCIController>)
{
    auto hub_or_error = Hub::try_create_root_hub(m_ohci_controller, Device::DeviceSpeed::FullSpeed);
    if (hub_or_error.is_error())
        return hub_or_error.error();

    m_hub = hub_or_error.release_value();

    // NOTE: The root hub will be on the default address at this point.
    // The root hub must be the first device to be created, otherwise the HCD will intercept all default address transfers as though they're targeted at the root hub.
    auto result = m_hub->enumerate_device();
    if (result.is_error())
        return result;

    // NOTE: The root hub is no longer on the default address.
    result = m_hub->enumerate_and_power_on_hub();
    if (result.is_error())
        return result;

    return KSuccess;
}

KResultOr<size_t> OHCIRootHub::handle_control_transfer(Transfer& transfer)
{
    auto& request = transfer.request();
    auto* request_data = transfer.buffer().as_ptr() + sizeof(USBRequestData);

    //if constexpr (OHCI_DEBUG) {
    dbgln("OHCIRootHub: Received control transfer.");
    dbgln("OHCIRootHub: Request Type: 0x{:02x}", request.request_type);
    dbgln("OHCIRootHub: Request: 0x{:02x}", request.request);
    dbgln("OHCIRootHub: Value: 0x{:04x}", request.value);
    dbgln("OHCIRootHub: Index: 0x{:04x}", request.index);
    dbgln("OHCIRootHub: Length: 0x{:04x}", request.length);
    //}

    size_t length = 0;

    switch (request.request) {
    case HubRequest::GET_STATUS: {
        if (request.index > m_ohci_controller->root_hub_descriptor().number_of_downstream_ports)
            return EINVAL;

        length = min(transfer.transfer_data_size(), sizeof(HubStatus));
        VERIFY(length <= sizeof(HubStatus));
        HubStatus hub_status {};

        if (request.index == 0) {
            // If index == 0, the actual request is Get Hub Status
            m_ohci_controller->get_hub_status({}, hub_status);
            memcpy(request_data, (void*)&hub_status, length);
            break;
        }

        // If index != 0, the actual request is Get Port Status
        m_ohci_controller->get_port_status({}, request.index - 1, hub_status);
        memcpy(request_data, (void*)&hub_status, length);
        break;
    }
    case HubRequest::GET_DESCRIPTOR: {
        u8 descriptor_type = request.value >> 8;
        switch (descriptor_type) {
        case DESCRIPTOR_TYPE_DEVICE:
            length = min(transfer.transfer_data_size(), sizeof(USBDeviceDescriptor));
            VERIFY(length <= sizeof(USBDeviceDescriptor));
            memcpy(request_data, (void*)&ohci_root_hub_device_descriptor, length);
            break;
            case DESCRIPTOR_TYPE_CONFIGURATION:
                length = min(transfer.transfer_data_size(), sizeof(USBConfigurationDescriptor));
                VERIFY(length <= sizeof(USBConfigurationDescriptor));
                memcpy(request_data, (void*)&ohci_root_hub_configuration_descriptor, length);
                break;
                case DESCRIPTOR_TYPE_INTERFACE:
                    length = min(transfer.transfer_data_size(), sizeof(USBInterfaceDescriptor));
                    VERIFY(length <= sizeof(USBInterfaceDescriptor));
                    memcpy(request_data, (void*)&ohci_root_hub_interface_descriptor, length);
                    break;
                    case DESCRIPTOR_TYPE_ENDPOINT:
                        length = min(transfer.transfer_data_size(), sizeof(USBEndpointDescriptor));
                        VERIFY(length <= sizeof(USBEndpointDescriptor));
                        memcpy(request_data, (void*)&ohci_root_hub_endpoint_descriptor, length);
                        break;
                        case DESCRIPTOR_TYPE_HUB: {
                            length = min(transfer.transfer_data_size(), sizeof(USBHubDescriptor));
                            VERIFY(length <= sizeof(USBHubDescriptor));
                            auto* ohci_root_hub_hub_descriptor = &m_ohci_controller->root_hub_descriptor();
                            memcpy(request_data, (void const*)ohci_root_hub_hub_descriptor, length);
                            break;
                        }
                        default:
                            return EINVAL;
        }
        break;
    }
    case USB_REQUEST_SET_ADDRESS:
        dbgln_if(OHCI_DEBUG, "OHCIRootHub: Attempt to set address to {}, ignoring.", request.value);
        // FIXME: No magic values pls. (This is the max amount of addresses allowed on USB)
        if (request.value >= 128)
            return EINVAL;
        // Ignore SET_ADDRESS requests. USBDevice sets its internal address to the new allocated address that it just sent to us.
        // The internal address is used to check if the request is directed at the root hub or not.
        break;
        case HubRequest::SET_FEATURE: {
            if (request.index == 0) {
                // If index == 0, the actual request is Set Hub Feature.
                // OHCI does not support Set Hub Feature.
                // Therefore, ignore the request, but return an error if the value is not "Local Power Source" or "Over-current"
                switch (request.value) {
                case HubFeatureSelector::C_HUB_LOCAL_POWER:
                    case HubFeatureSelector::C_HUB_OVER_CURRENT:
                        break;
                default:
                    return EINVAL;
                }
                break;
            }

            // If index != 0, the actual request is Set Port Feature.
            u8 port = request.index & 0xFF;
            if (port > m_ohci_controller->root_hub_descriptor().number_of_downstream_ports)
                return EINVAL;

            auto feature_selector = (HubFeatureSelector)request.value;
            auto result = m_ohci_controller->set_port_feature({}, port - 1, feature_selector);
            if (result.is_error())
                return result;
            break;
        }
        case HubRequest::CLEAR_FEATURE: {
            if (request.index == 0) {
                // If index == 0, the actual request is Clear Hub Feature.
                auto feature_selector = (HubFeatureSelector)request.value;
                auto result = m_ohci_controller->clear_hub_feature({}, feature_selector);
                if (result.is_error())
                    return result;
                break;
            }

            // If index != 0, the actual request is Clear Port Feature.
            u8 port = request.index & 0xFF;
            if (port > m_ohci_controller->root_hub_descriptor().number_of_downstream_ports)
                return EINVAL;

            auto feature_selector = (HubFeatureSelector)request.value;
            auto result = m_ohci_controller->clear_port_feature({}, port - 1, feature_selector);
            if (result.is_error())
                return result;
            break;
        }
        default:
            return EINVAL;
    }

    transfer.set_complete();
    return length;
}

}
