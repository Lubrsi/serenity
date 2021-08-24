/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/Bus/USB/OHCI/OHCIController.h>
#include <Kernel/Bus/USB/USBRequest.h>
#include <Kernel/IO.h>
#include <Kernel/Process.h>
#include <Kernel/StdLib.h>

namespace Kernel::USB {

KResultOr<NonnullRefPtr<OHCIController>> OHCIController::try_to_initialize(PCI::Address address)
{
    // NOTE: This assumes that address is pointing to a valid OHCI controller.
    auto controller = adopt_ref_if_nonnull(new (nothrow) OHCIController(address));
    if (!controller)
        return ENOMEM;

    auto init_result = controller->initialize();
    if (init_result.is_error())
        return init_result;

    return controller.release_nonnull();
}

UNMAP_AFTER_INIT OHCIController::OHCIController(PCI::Address address)
    : PCI::Device(address)
    , IRQHandler(PCI::get_interrupt_line(address))
{
}

UNMAP_AFTER_INIT OHCIController::~OHCIController()
{
}

KResult OHCIController::initialize()
{
    dmesgln("OHCI: Controller found {} @ {}", PCI::get_id(pci_address()), pci_address());
    dmesgln("OHCI: Interrupt line: {}", PCI::get_interrupt_line(pci_address()));

    PCI::enable_bus_mastering(pci_address());
    PCI::disable_io_space(pci_address());
    PCI::enable_memory_space(pci_address());

    size_t operational_registers_region_size = PCI::get_BAR_space_size(pci_address(), 0);
    auto operational_registers_address = PCI::get_BAR0(pci_address()) & ~0xf;
    m_operational_registers_region = MM.allocate_kernel_region(PhysicalAddress(page_base_of(operational_registers_address)), Memory::page_round_up(operational_registers_region_size), "OHCI Operational Registers", Memory::Region::Access::Read | Memory::Region::Access::Write, Memory::Region::Cacheable::No);
    if (!m_operational_registers_region)
        return ENOMEM;

    dmesgln("OHCI: Operational registers region @ {} {}", m_operational_registers_region->vaddr(), m_operational_registers_region->physical_page(0)->paddr());

    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    auto revision = operational_registers->hc_revision & HC_REVISION_REVISION;

    if (revision != 0x10) {
        dmesgln("OHCI: Unknown revision 0x{:02x}, expected 0x10", revision);
        return ENOTSUP;
    }

    // The HCCA needs to be at a particular alignment. The controller tells us this alignment.
    // We need to write 0xFFFF'FFFF to host_controller_communication_area_physical_address and then read it back.
    // When reading it back, the controller will have removed the bottom bits to suit the required alignment.
    operational_registers->host_controller_communication_area_physical_address = 0xFFFFFFFF;
    u32 returned_address = operational_registers->host_controller_communication_area_physical_address;
    u32 alignment = ~returned_address + 1;
    dmesgln("OHCI: Required HCCA alignment: 0x{:08x}", alignment);

    // FIXME: allocate_contiguous_kernel_region only takes multiples of the PAGE_SIZE for the alignment.
    m_host_controller_communication_area_region = MM.allocate_contiguous_kernel_region(Memory::page_round_up(sizeof(HostControllerCommunicationArea)), "OHCI Host Controller Communication Area", Memory::Region::Access::Read | Memory::Region::Access::Write, Memory::Region::Cacheable::No);
    if (!m_host_controller_communication_area_region)
        return ENOMEM;

    dmesgln("OHCI: HCCA @ {} {}", m_host_controller_communication_area_region->vaddr(), m_host_controller_communication_area_region->physical_page(0)->paddr());

    m_endpoint_descriptor_pool = OHCIDescriptorPool<OHCIEndpointDescriptor>::try_create("OHCI Endpoint Descriptor Pool");
    if (!m_endpoint_descriptor_pool) {
        dmesgln("OHCI: Failed to create endpoint descriptor pool!");
        return ENOMEM;
    }

    m_general_transfer_descriptor_pool = OHCIDescriptorPool<OHCIGeneralTransferDescriptor>::try_create("OHCI General Transfer Descriptor Pool");
    if (!m_general_transfer_descriptor_pool) {
        dmesgln("OHCI: Failed to create general transfer descriptor pool!");
        return ENOMEM;
    }

    m_control_endpoints_head = m_endpoint_descriptor_pool->try_take_free_descriptor();
    if (!m_control_endpoints_head) {
        dmesgln("OHCI: No free endpoint descriptors to make control endpoint head!");
        return ENOMEM;
    }

    spawn_port_proc();

    auto reset_result = reset();
    if (reset_result.is_error())
        return reset_result;

    return start();
}

KResult OHCIController::reset()
{
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();

    enable_irq();

    // Disable all interrupts except the ownership change interrupt. This is because we may currently be sharing the registers with the firmware.
    // If we don't disable all interrupts during SMM takeover, the firmware may get interrupted stormed, causing a system freeze.
    // This can happen when the system is started up with a device already attached to the controller, causing continuous root hub status change interrupts to the firmware instead of us.
    operational_registers->hc_interrupt_disable = INTERRUPT_ALL & ~INTERRUPT_OWNERSHIP_CHANGE;

    // Make sure the ownership change interrupt is enabled so SMM gets notified of the ownership change request.
    operational_registers->hc_interrupt_enable = INTERRUPT_OWNERSHIP_CHANGE;

    // FIXME: This doesn't work on real hardware when I tested it...
    if (operational_registers->hc_control & HC_CONTROL_INTERRUPT_ROUTING) {
        dbgln("OHCI: Taking control from SMM...");

        // 5.1.1.3.3 OS Driver, SMM Active
        // Take control of the controller from SMM.
        auto command_status = operational_registers->hc_command_status;
        command_status |= HC_COMMAND_STATUS_OWNERSHIP_CHANGE_REQUEST;
        operational_registers->hc_command_status = command_status;

        u16 attempt = 0;
        while (((operational_registers->hc_control & HC_CONTROL_INTERRUPT_ROUTING) == HC_CONTROL_INTERRUPT_ROUTING) && attempt < 100) {
            IO::delay(1000);
            attempt++;
        }

        if (operational_registers->hc_control & HC_CONTROL_INTERRUPT_ROUTING) {
            dbgln("OHCI: SMM failed to respond, reset.");

            // Clear HcControl to 0. This will disable all the lists, disable remote wakeup, set the ControlBulkServiceRatio to OneToOne and the functional state to USBReset.
            operational_registers->hc_control = 0;

            // Wait at least 100 ms for the bus to reset.
            IO::delay(100000);
        } else {
            dbgln("OHCI: Took control from SMM!");
        }
    } else {
        dbgln("OHCI: Cold boot, must wait for bus reset.");
        // Wait at least 100 ms for the bus to reset.
        IO::delay(100000);
    }

    // Clear HcControl to 0. This will disable all the lists, disable remote wakeup, set the ControlBulkServiceRatio to OneToOne and the functional state to USBReset.
    operational_registers->hc_control = 0;

    // Wait at least 100 ms for the bus to reset.
    IO::delay(100000);

    // We must save the frame interval because controller reset will change it back to the nominal value (11999) and we should restore it to the value it was at before reset.
    auto original_frame_interval = operational_registers->hc_frame_interval & HC_FRAME_INTERVAL_FRAME_INTERVAL;

    auto command_status = operational_registers->hc_command_status;
    command_status |= HC_COMMAND_STATUS_HOST_CONTROLLER_RESET;
    operational_registers->hc_command_status = command_status;

    // FIXME: Timeout
    while (operational_registers->hc_command_status & HC_COMMAND_STATUS_HOST_CONTROLLER_RESET) {
        // Usual reset time is 10 us.
        IO::delay(10);
    }

    // Now that the controller has been reset, the functional state has been set to USBSuspend.
    // Because of this, we now must setup and go operational within 2 ms, otherwise the controller will go into USBResume.
    operational_registers->host_controller_communication_area_physical_address = m_host_controller_communication_area_region->physical_page(0)->paddr().get();
    operational_registers->hc_interrupt_disable = INTERRUPT_ALL;
    operational_registers->hc_interrupt_enable = INTERRUPT_SCHEDULING_OVERRUN | INTERRUPT_WRITEBACK_DONE_HEAD | INTERRUPT_RESUME_DETECTED | INTERRUPT_UNRECOVERABLE_ERROR | INTERRUPT_ROOT_HUB_STATUS_CHANGE;

    operational_registers->first_endpoint_descriptor_of_control_list_physical_address = m_control_endpoints_head->physical_address;

    auto control = operational_registers->hc_control;
    control &= ~(HC_CONTROL_INTERRUPT_ROUTING | HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_MASK | HC_CONTROL_ENABLE_MASK | HC_CONTROL_CONTROL_BULK_SERVICE_RATIO_MASK);
    // FIXME: Enable the periodic, isochronous and bulk lists when supported.
    control |= HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_USB_OPERATIONAL | HC_CONTROL_CONTROL_LIST_ENABLE | HC_CONTROL_REMOTE_WAKEUP_ENABLE | HC_CONTROL_CONTROL_BULK_SERVICE_RATIO_FOUR_TO_ONE;

    // Go operational.
    operational_registers->hc_control = control;

    // Work out FSLargestDataPacket. This is done by taking out the maximum number of bit times for transaction overhead (which is 210 according to spec), followed by multiplying by 6/7 to account for worst case bit stuffing overhead.
    u16 fs_largest_data_packet = (original_frame_interval - 210) * 6 / 7;
    // FIXME: Some bit stuff around FIT.
    operational_registers->hc_frame_interval = HC_FRAME_INTERVAL_FRAME_INTERVAL_TOGGLE | (fs_largest_data_packet << 16) | original_frame_interval;

    // 90% of the frame interval will be spent processing the control and bulk lists.
    // 10% of the frame interval will be spent processing the periodic list.
    // The control and bulk lists come first, then the periodic list.
    u16 periodic_start = original_frame_interval * 9 / 10;
    operational_registers->hc_periodic_start = periodic_start;

    dbgln("OHCI: Reset complete.");

    return KSuccess;
}

void OHCIController::create_root_hub_descriptor()
{
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    auto root_hub_descriptor_a = operational_registers->hc_root_hub_descriptor_a;
    // FIXME: Use HcRhDescriptorB to set up DeviceRemovable and PortPowerControl once USBHubDescriptor supports them.

    // FIXME: Should we use initializers in USBHubDescriptor instead?
    memset(&m_root_hub_descriptor, 0, sizeof(USBHubDescriptor));
    m_root_hub_descriptor.descriptor_header.length = sizeof(USBHubDescriptor);
    m_root_hub_descriptor.descriptor_header.descriptor_type = DESCRIPTOR_TYPE_HUB;
    m_root_hub_descriptor.number_of_downstream_ports = root_hub_descriptor_a & HC_ROOT_DESCRIPTOR_A_NUMBER_OF_DOWNSTREAM_PORTS;

    if (root_hub_descriptor_a & HC_ROOT_DESCRIPTOR_A_NO_POWER_SWITCHING || !(root_hub_descriptor_a & HC_ROOT_DESCRIPTOR_A_POWER_SWITCHING_MODE))
        m_root_hub_descriptor.hub_characteristics |= HUB_CHARACTERISTIC_GANGED_POWER_SWITCHING;
    else
        m_root_hub_descriptor.hub_characteristics |= HUB_CHARACTERISTIC_INDIVIDUAL_PORT_POWER_SWITCHING;

    // The OHCI root hub is not allowed to be a compound device.
    m_root_hub_descriptor.hub_characteristics &= ~HUB_CHARACTERISTIC_COMPOUND_DEVICE;

    if (root_hub_descriptor_a & HC_ROOT_DESCRIPTOR_A_NO_OVER_CURRENT_PROTECTION)
        m_root_hub_descriptor.hub_characteristics |= HUB_CHARACTERISTIC_NO_OVER_CURRENT_PROTECTION;
    else if (!(root_hub_descriptor_a & HC_ROOT_DESCRIPTOR_A_POWER_SWITCHING_MODE))
        m_root_hub_descriptor.hub_characteristics |= HUB_CHARACTERISTIC_GLOBAL_OVER_CURRENT_PROTECTION;
    else
        m_root_hub_descriptor.hub_characteristics |= HUB_CHARACTERISTIC_INDIVIDUAL_PORT_OVER_CURRENT_PROTECTION;

    m_root_hub_descriptor.power_on_to_power_good_time = ((root_hub_descriptor_a & HC_ROOT_DESCRIPTOR_A_POWER_ON_TO_POWER_GOOD_TIME) >> 24) & 0xFF;
}

KResult OHCIController::start()
{
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    auto control = operational_registers->hc_control;

    // FIXME: Don't crash on this.
    VERIFY(control & HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_USB_OPERATIONAL);

    create_root_hub_descriptor();

    auto root_hub_or_error = OHCIRootHub::try_create(*this);
    if (root_hub_or_error.is_error())
        return root_hub_or_error.error();

    m_root_hub = root_hub_or_error.release_value();
    return m_root_hub->setup({});
}

KResult OHCIController::stop()
{
    TODO();
}

void OHCIController::spawn_port_proc()
{
    RefPtr<Thread> usb_hotplug_thread;

    Process::create_kernel_process(usb_hotplug_thread, "OHCIHotplug", [&] {
        auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();

        for (;;) {
            m_root_hub_status_wait_queue.wait_forever("OHCIHotplug");

            dbgln_if(OHCI_DEBUG, "OHCI: Hotplug thread wakeup. Checking for port updates...");

            if (m_root_hub)
                m_root_hub->check_for_port_updates();

            dbgln_if(OHCI_DEBUG, "OHCI: Hotplug check finished. Re-enabling status change interrupt and going back to sleep...");

            // Re-enable the status change interrupt.
            operational_registers->hc_interrupt_enable = INTERRUPT_ROOT_HUB_STATUS_CHANGE;
        }
    });
}

void OHCIController::get_hub_status(Badge<OHCIRootHub>, HubStatus& hub_status)
{
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    auto status = operational_registers->hc_root_hub_status;

    if (status & HC_ROOT_HUB_STATUS_READ_LOCAL_POWER_STATUS)
        hub_status.status |= HUB_STATUS_LOCAL_POWER_SOURCE;

    if (status & HC_ROOT_HUB_STATUS_READ_LOCAL_POWER_STATUS_CHANGE)
        hub_status.change |= HUB_STATUS_LOCAL_POWER_SOURCE_CHANGED;

    if (status & HC_ROOT_HUB_STATUS_OVER_CURRENT_INDICATOR)
        hub_status.status |= HUB_STATUS_OVER_CURRENT;

    if (status & HC_ROOT_HUB_STATUS_OVER_CURRENT_INDICATOR_CHANGE)
        hub_status.change |= HUB_STATUS_OVER_CURRENT_CHANGED;
}

void OHCIController::get_port_status(Badge<OHCIRootHub>, u8 port, HubStatus& port_status)
{
    // The check is done by OHCIRootHub.
    VERIFY(port < m_root_hub_descriptor.number_of_downstream_ports);
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    auto status = operational_registers->hc_root_hub_port_status[port];

    if (status & HC_ROOT_HUB_PORT_STATUS_READ_CURRENT_CONNECT_STATUS)
        port_status.status |= PORT_STATUS_CURRENT_CONNECT_STATUS;

    if (status & HC_ROOT_HUB_PORT_STATUS_CONNECT_STATUS_CHANGE)
        port_status.change |= PORT_STATUS_CONNECT_STATUS_CHANGED;

    if (status & HC_ROOT_HUB_PORT_STATUS_READ_PORT_ENABLE_STATUS)
        port_status.status |= PORT_STATUS_PORT_ENABLED;

    if (status & HC_ROOT_HUB_PORT_STATUS_PORT_ENABLE_STATUS_CHANGE)
        port_status.change |= PORT_STATUS_PORT_ENABLED_CHANGED;

    if (status & HC_ROOT_HUB_PORT_STATUS_READ_PORT_SUSPEND_STATUS)
        port_status.status |= PORT_STATUS_SUSPEND;

    if (status & HC_ROOT_HUB_PORT_STATUS_PORT_SUSPEND_STATUS_CHANGE)
        port_status.change |= PORT_STATUS_SUSPEND_CHANGED;

    if (status & HC_ROOT_HUB_PORT_STATUS_READ_PORT_OVER_CURRENT_INDICATOR)
        port_status.status |= PORT_STATUS_OVER_CURRENT;

    if (status & HC_ROOT_HUB_PORT_STATUS_PORT_OVER_CURRENT_INDICATOR_CHANGE)
        port_status.change |= PORT_STATUS_OVER_CURRENT_INDICATOR_CHANGED;

    if (status & HC_ROOT_HUB_PORT_STATUS_READ_PORT_RESET_STATUS)
        port_status.status |= PORT_STATUS_RESET;

    if (status & HC_ROOT_HUB_PORT_STATUS_PORT_RESET_STATUS_CHANGE)
        port_status.change |= PORT_STATUS_RESET_CHANGED;

    if (status & HC_ROOT_HUB_PORT_STATUS_READ_LOW_SPEED_DEVICE_ATTACHED)
        port_status.status |= PORT_STATUS_LOW_SPEED_DEVICE_ATTACHED;

    dbgln_if(OHCI_DEBUG, "OHCI: get_port_status status=0x{:04x} change=0x{:04x}", port_status.status, port_status.change);
}

KResult OHCIController::clear_hub_feature(Badge<OHCIRootHub>, HubFeatureSelector feature_selector)
{
    dbgln_if(OHCI_DEBUG, "OHCI: clear_hub_feature: feature_selector={}", (u8)feature_selector);

    // Writing 0 to any of the bits in HcRhStatus has no effect as they are all Write Clear, so we can start at 0 and build up.
    u32 new_status = 0;

    switch (feature_selector) {
    case HubFeatureSelector::C_HUB_LOCAL_POWER:
        // OHCI does not support the local power status feature, ignore the request.
        return KSuccess;
    case HubFeatureSelector::C_HUB_OVER_CURRENT:
        new_status |= HC_ROOT_HUB_STATUS_OVER_CURRENT_INDICATOR_CHANGE;
        break;
    default:
        return EINVAL;
    }

    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    operational_registers->hc_root_hub_status = new_status;

    return KSuccess;
}

KResult OHCIController::set_port_feature(Badge<OHCIRootHub>, u8 port, HubFeatureSelector feature_selector)
{
    // The check is done by OHCIRootHub.
    VERIFY(port < m_root_hub_descriptor.number_of_downstream_ports);

    dbgln_if(OHCI_DEBUG, "OHCI: set_port_feature: port={} feature_selector={}", port, (u8)feature_selector);

    // Writing 0 to any of the bits in HcRhPortStatus has no effect as they are all Write Clear, so we can start at 0 and build up.
    u32 new_status = 0;

    switch (feature_selector) {
    case HubFeatureSelector::PORT_ENABLE:
        new_status |= HC_ROOT_HUB_PORT_STATUS_WRITE_SET_PORT_ENABLE;
        break;
    case HubFeatureSelector::PORT_POWER:
        new_status |= HC_ROOT_HUB_PORT_STATUS_WRITE_SET_PORT_POWER;
        break;
    case HubFeatureSelector::PORT_RESET:
        new_status |= HC_ROOT_HUB_PORT_STATUS_WRITE_SET_PORT_RESET;
        break;
    case HubFeatureSelector::PORT_SUSPEND:
        new_status |= HC_ROOT_HUB_PORT_STATUS_WRITE_SET_PORT_SUSPEND;
        break;
    default:
        return EINVAL;
    }

    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    operational_registers->hc_root_hub_port_status[port] = new_status;

    return KSuccess;
}

KResult OHCIController::clear_port_feature(Badge<OHCIRootHub>, u8 port, HubFeatureSelector feature_selector)
{
    // The check is done by OHCIRootHub.
    VERIFY(port < m_root_hub_descriptor.number_of_downstream_ports);

    dbgln_if(OHCI_DEBUG, "OHCI: clear_port_feature: port={} feature_selector={}", port, (u8)feature_selector);

    // Writing 0 to any of the bits in HcRhPortStatus has no effect as they are all Write Clear, so we can start at 0 and build up.
    u32 new_status = 0;

    switch (feature_selector) {
    case HubFeatureSelector::PORT_ENABLE:
        new_status |= HC_ROOT_HUB_PORT_STATUS_WRITE_CLEAR_PORT_ENABLE;
        break;
    case HubFeatureSelector::PORT_POWER:
        new_status |= HC_ROOT_HUB_PORT_STATUS_WRITE_CLEAR_PORT_POWER;
        break;
    case HubFeatureSelector::PORT_SUSPEND:
        new_status |= HC_ROOT_HUB_PORT_STATUS_WRITE_CLEAR_SUSPEND_STATUS;
        break;
    case HubFeatureSelector::C_PORT_CONNECTION:
        new_status |= HC_ROOT_HUB_PORT_STATUS_CONNECT_STATUS_CHANGE;
        break;
    case HubFeatureSelector::C_PORT_RESET:
        new_status |= HC_ROOT_HUB_PORT_STATUS_PORT_RESET_STATUS_CHANGE;
        break;
    case HubFeatureSelector::C_PORT_ENABLE:
        new_status |= HC_ROOT_HUB_PORT_STATUS_PORT_ENABLE_STATUS_CHANGE;
        break;
    case HubFeatureSelector::C_PORT_SUSPEND:
        new_status |= HC_ROOT_HUB_PORT_STATUS_PORT_SUSPEND_STATUS_CHANGE;
        break;
    case HubFeatureSelector::C_PORT_OVER_CURRENT:
        new_status |= HC_ROOT_HUB_PORT_STATUS_PORT_OVER_CURRENT_INDICATOR_CHANGE;
        break;
    default:
        return EINVAL;
    }

    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    operational_registers->hc_root_hub_port_status[port] = new_status;

    return KSuccess;
}

bool OHCIController::handle_irq(RegisterState const&)
{
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();

    // The controller will still set status bits for disabled interrupts, so mask out the disabled interrupts.
    auto interrupt_status = operational_registers->hc_interrupt_status & operational_registers->hc_interrupt_enable;
    dbgln_if(OHCI_DEBUG, "OHCI: handle_irq, IS: 0x{:08x}", interrupt_status);

    if (interrupt_status == 0) {
        dbgln_if(OHCI_DEBUG, "OHCI: Interrupt status is empty. Interrupt not for us or spurious.");
        return false;
    }

    // Acknowledge the interrupts.
    operational_registers->hc_interrupt_status = interrupt_status;

    if (interrupt_status & INTERRUPT_ROOT_HUB_STATUS_CHANGE) {
        dbgln_if(OHCI_DEBUG, "OHCI: Root hub status change interrupt!");
        // Disable the status change interrupt to prevent further status change interrupts while handling port resets.
        // This is because we get interrupts for any status change, even ones we caused ourselves.
        // The hot plug thread is responsible for re-enabling this interrupt.
        operational_registers->hc_interrupt_disable = INTERRUPT_ROOT_HUB_STATUS_CHANGE;
        m_root_hub_status_wait_queue.wake_all();
    }

    if (interrupt_status & INTERRUPT_WRITEBACK_DONE_HEAD) {
        dbgln_if(OHCI_DEBUG, "OHCI: Writeback done head interrupt!");
        auto* host_controller_communication_area = (HostControllerCommunicationArea*)m_host_controller_communication_area_region->vaddr().as_ptr();
        host_controller_communication_area->done_head = 0;
    }

    if (interrupt_status & INTERRUPT_START_OF_FRAME) {
        dbgln_if(OHCI_DEBUG, "OHCI: Start of frame interrupt!");
        m_received_start_of_frame_interrupt = true;
        // This interrupt is enabled in pipe_removed. In there, we only want this to trigger once, so disable it straight away.
        operational_registers->hc_interrupt_disable = INTERRUPT_START_OF_FRAME;
    }

    return true;
}

OHCIGeneralTransferDescriptor* OHCIController::create_general_transfer_descriptor(Pipe& pipe, GTDDirection direction)
{
    auto* transfer_descriptor = m_general_transfer_descriptor_pool->try_take_free_descriptor();
    if (!transfer_descriptor)
        return nullptr;

    u32 control = 0;
    control |= static_cast<u32>(direction);
    control |= GTD_DATA_TOGGLE_GET_FROM_TRANSFER_DESCRIPTOR;
    control |= pipe.data_toggle() ? GTD_DATA_TOGGLE_TOGGLE_VALUE : 0;
    transfer_descriptor->control = control;

    pipe.set_toggle(!pipe.data_toggle());

    return transfer_descriptor;
}

KResult OHCIController::create_general_chain(Pipe& pipe, GTDDirection direction, Ptr32<u8>& buffer_address, size_t max_packet_size, size_t transfer_size, OHCIGeneralTransferDescriptor** td_chain, OHCIGeneralTransferDescriptor** last_td)
{
    // We need to create `n` transfer descriptors based on the max
    // size of each transfer (which we've learned from the device already by reading
    // its device descriptor, or 8 bytes). Each TD then has its buffer pointer
    // set to the initial buffer address + (max_size * index), where index is
    // the ID of the TD in the chain.
    size_t byte_count = 0;
    OHCIGeneralTransferDescriptor* current_td = nullptr;
    OHCIGeneralTransferDescriptor* prev_td = nullptr;
    OHCIGeneralTransferDescriptor* first_td = nullptr;

    // Keep creating transfer descriptors while we still have some data
    while (byte_count < transfer_size) {
        size_t packet_size = transfer_size - byte_count;
        if (packet_size > max_packet_size) {
            packet_size = max_packet_size;
        }

        current_td = create_general_transfer_descriptor(pipe, direction);
        if (current_td == nullptr) {
            free_general_transfer_descriptor_chain(first_td);
            return ENOMEM;
        }

        if (Checked<FlatPtr>::addition_would_overflow(reinterpret_cast<FlatPtr>(&*buffer_address), byte_count)) {
            free_general_transfer_descriptor_chain(first_td);
            return EOVERFLOW;
        }

        auto start_of_buffer_pointer = Ptr32<u8>(buffer_address + byte_count);

        if (auto result = current_td->set_buffer_addresses(start_of_buffer_pointer, max_packet_size); result.is_error()) {
            free_general_transfer_descriptor_chain(first_td);
            return result;
        }

        if (direction == GTDDirection::In) {
            // Allow short packets.
            u32 current_td_control = current_td->control;
            current_td_control |= GTD_BUFFER_ROUNDING;
            current_td->control = current_td_control;
        }

        byte_count += packet_size;

        if (prev_td != nullptr)
            prev_td->insert_next_transfer_descriptor(current_td);
        else
            first_td = current_td;

        prev_td = current_td;
    }

    *last_td = current_td;
    *td_chain = first_td;
    return KSuccess;
}

void OHCIController::free_general_transfer_descriptor_chain(OHCIGeneralTransferDescriptor* first_descriptor)
{
    auto* descriptor = first_descriptor;

    while (descriptor) {
        auto* next = descriptor->next;

        descriptor->free();
        m_general_transfer_descriptor_pool->release_to_pool(descriptor);
        descriptor = next;
    }
}

OHCIEndpointDescriptor* OHCIController::get_endpoint_descriptor_for_pipe(Pipe const& pipe)
{
    OHCIEndpointDescriptor* endpoint = nullptr;

    switch (pipe.type()) {
    case Pipe::Type::Control: {
        auto* current_endpoint = m_control_endpoints_head;

        while (current_endpoint) {
            if (current_endpoint->associated_pipe == &pipe) {
                endpoint = current_endpoint;
                break;
            }

            current_endpoint = current_endpoint->next;
        }
        break;
    }
    default:
        TODO();
    }

    return endpoint;
}

size_t OHCIController::poll_transfer_queue(Transfer& transfer, OHCIEndpointDescriptor* endpoint, OHCIGeneralTransferDescriptor* data_descriptor_chain)
{
    // When the head and tail addresses are the same, the transfer is complete.
    // If the endpoint has halted, the transfer is complete.
    if ((endpoint->transfer_descriptor_head_physical_address & ~ED_TD_HEAD_FLAGS_MASK) != endpoint->transfer_descriptor_tail_physical_address
        && (endpoint->transfer_descriptor_head_physical_address & ED_TD_HEAD_FLAG_HALTED) == 0) {
        dbgln_if(OHCI_DEBUG, "OHCI: Transfer on endpoint {:p} is still in progress.", endpoint);
        return 0;
    }

    size_t transfer_size = 0;

    OHCIGeneralTransferDescriptor* descriptor = data_descriptor_chain;
    bool error_occurred = false;

    while (descriptor) {
        if constexpr (OHCI_DEBUG) {
            dbgln("OHCI: Checking TD in poll_transfer_queue:");
            descriptor->print();
        }

        if (!error_occurred) {
            u32 condition_code = descriptor->control & GTD_CONDITION_CODE_MASK;

            if (condition_code != GTD_CONDITION_CODE_NO_ERROR) {
                // Remove this transfer from the endpoint. FIXME: Is this correct?
                endpoint->transfer_descriptor_head_physical_address = endpoint->transfer_descriptor_tail_physical_address;

                // NOTE: Completion is set after the while loop.
                transfer.set_error_occurred();
                dbgln("OHCI: Transfer failed! Condition Code: 0b{:04b}", (condition_code >> 28) & 0b1111);

                if constexpr (OHCI_DEBUG) {
                    dbgln("OHCI: ED of errored GTD:");
                    endpoint->print();
                }

                // We still want to loop through to the last TD to unlink the terminator TD.
                // Zero out the transfer size to indicate no valid data.
                transfer_size = 0;
                error_occurred = true;
            }

            size_t length = descriptor->buffer_size;
            if (descriptor->current_buffer_pointer_physical_address != 0) {
                // FIXME: Check for overflow?
                length -= descriptor->end_of_buffer_physical_address - descriptor->current_buffer_pointer_physical_address + 1;
            }

            transfer_size += length;
        }

        // Don't include the terminator TD.
        if (descriptor->next == endpoint->tail_transfer_descriptor)
            break;

        descriptor = descriptor->next;
    }

    transfer.set_complete();

    // This is the TD just before the terminator TD. Break off the link to the terminator TD so we don't end up freeing it.
    descriptor->next = nullptr;

    return transfer_size;
}

KResultOr<size_t> OHCIController::submit_control_transfer(Transfer& transfer)
{
    Pipe& pipe = transfer.pipe(); // Short circuit the pipe related to this transfer

    dbgln_if(OHCI_DEBUG, "OHCI: Received control transfer for address {}. Root Hub is at address {}.", pipe.device_address(), m_root_hub->device_address());

    // Short-circuit the root hub.
    if (pipe.device_address() == m_root_hub->device_address())
        return m_root_hub->handle_control_transfer(transfer);

    auto* endpoint = get_endpoint_descriptor_for_pipe(pipe);
    if (!endpoint) {
        dbgln("OHCI: Received control transfer for pipe at {:p} but there's no endpoint descriptor associated with it?", &pipe);
        return ENOENT;
    }

    auto* setup_td = create_general_transfer_descriptor(pipe, GTDDirection::Setup);
    if (!setup_td)
        return ENOMEM;

    ArmedScopeGuard free_setup_td_on_error_guard = [&]() {
        setup_td->free();
        m_general_transfer_descriptor_pool->release_to_pool(setup_td);
    };

    auto transfer_buffer_pointer = Ptr32<u8>(transfer.buffer_physical().as_ptr());
    if (auto result = setup_td->set_buffer_addresses(transfer_buffer_pointer, sizeof(USBRequestData)); result.is_error())
        return result;

    // NOTE: The overflow check in set_buffer_addresses checked for overflow with sizeof(USBRequestData) - 1, so we need to check it again but with the full size this time.
    if (Checked<FlatPtr>::addition_would_overflow(reinterpret_cast<FlatPtr>(&*transfer_buffer_pointer), sizeof(USBRequestData)))
        return EOVERFLOW;

    auto direction = (transfer.request().request_type & USB_REQUEST_TRANSFER_DIRECTION_DEVICE_TO_HOST) == USB_REQUEST_TRANSFER_DIRECTION_DEVICE_TO_HOST ? GTDDirection::In : GTDDirection::Out;
    auto data_buffer_pointer = Ptr32<u8>(transfer_buffer_pointer + sizeof(USBRequestData));

    OHCIGeneralTransferDescriptor* data_descriptor_chain = nullptr;
    OHCIGeneralTransferDescriptor* last_data_descriptor = nullptr;
    if (auto result = create_general_chain(pipe, direction, data_buffer_pointer, pipe.max_packet_size(), transfer.transfer_data_size(), &data_descriptor_chain, &last_data_descriptor); result.is_error())
        return result;

    ArmedScopeGuard free_data_chain_on_error_guard = [&]() {
        if (data_descriptor_chain)
            free_general_transfer_descriptor_chain(data_descriptor_chain);
    };

    // Status TD always has toggle set to 1
    pipe.set_toggle(true);

    auto* status_td = create_general_transfer_descriptor(pipe, direction);
    if (!status_td)
        return ENOMEM;

    // No errors can occur from now on, disarm the scope guards.
    free_setup_td_on_error_guard.disarm();
    free_data_chain_on_error_guard.disarm();

    // Link transfers together
    if (data_descriptor_chain) {
        setup_td->insert_next_transfer_descriptor(data_descriptor_chain);
        last_data_descriptor->insert_next_transfer_descriptor(status_td);
    } else {
        setup_td->insert_next_transfer_descriptor(status_td);
    }

    // Link the last TD for this transfer to the dummy TD created for this endpoint.
    // FIXME: Remove this cast!
    status_td->insert_next_transfer_descriptor((OHCIGeneralTransferDescriptor*)endpoint->tail_transfer_descriptor);

    if constexpr (OHCI_DEBUG) {
        dbgln("OHCI: TD chain:");

        auto* descriptor = setup_td;
        while (descriptor && descriptor != endpoint->tail_transfer_descriptor) {
            descriptor->print();
            descriptor = descriptor->next;
        }
    }

    // Attach the descriptor chain to the endpoint.
    endpoint->transfer_descriptor_head_physical_address = setup_td->physical_address;

    // Enable the endpoint.
    u32 control = endpoint->control;
    control &= ~ED_CONTROL_SKIP;
    endpoint->control = control;

    // Tell the controller that the control list is ready.
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    operational_registers->hc_command_status = HC_COMMAND_STATUS_CONTROL_LIST_FILLED;

    // FIXME: Use interrupts instead of polling for completion.
    size_t transfer_size = 0;
    while (!transfer.complete()) {
        // NOTE: The data chain and the status TD are accessible from the setup TD here.
        transfer_size = poll_transfer_queue(transfer, endpoint, setup_td);
    }

    free_general_transfer_descriptor_chain(setup_td);

    return transfer_size;
}

KResult OHCIController::pipe_created(Pipe const& pipe)
{
    // FIXME: This is a bit weird...
    if (!m_root_hub || !m_root_hub->initialized() || pipe.device_address() == m_root_hub->device_address()) {
        // Endpoint descriptors are not needed for the root hub.
        return KSuccess;
    }

    auto* new_endpoint_descriptor = m_endpoint_descriptor_pool->try_take_free_descriptor();
    if (!new_endpoint_descriptor) {
        dbgln("OHCI: No free endpoint descriptors left!");
        return ENOMEM;
    }

    ArmedScopeGuard free_endpoint_descriptor_on_error_guard = [&]() {
        new_endpoint_descriptor->free();
        m_endpoint_descriptor_pool->release_to_pool(new_endpoint_descriptor);
    };

    // Start out with skipping this ED until a transfer is submitted for this ED.
    u32 control = ED_CONTROL_SKIP;

    control |= (pipe.device_address() & 0x7F) << ED_CONTROL_FUNCTION_ADDRESS;
    control |= (pipe.endpoint_address() & 0xF) << ED_CONTROL_ENDPOINT_NUMBER;
    control |= (pipe.max_packet_size() & 0x7FF) << ED_CONTROL_MAX_PACKET_SIZE;

    switch (pipe.device_speed()) {
    case Pipe::DeviceSpeed::LowSpeed:
        control |= ED_CONTROL_LOW_SPEED;
        break;
    case Pipe::DeviceSpeed::FullSpeed:
        // Endpoints are full speed by default.
        break;
    default:
        dbgln("OHCI: Cannot create endpoint for pipe with speed: {}", (u8)pipe.device_speed());
        return ENOTSUP;
    }

    switch (pipe.direction()) {
    case Pipe::Direction::In:
        control |= ED_CONTROL_DIRECTION_IN;
        break;
    case Pipe::Direction::Out:
        control |= ED_CONTROL_DIRECTION_OUT;
        break;
    case Pipe::Direction::Bidirectional:
        // The particular direction depends on the transfer instead of the overall endpoint.
        control |= ED_CONTROL_DIRECTION_GET_FROM_TRANSFER_DESCRIPTOR;
        break;
    default:
        TODO();
    }

    OHCIEndpointDescriptor* head = nullptr;

    switch (pipe.type()) {
    case Pipe::Type::Control:
        head = m_control_endpoints_head;
        break;
    default:
        TODO();
    }

    VERIFY(head);

    // Endpoints require a dummy transfer descriptor at the end to figure out when it's reached the end of the TD list.
    // Endpoints can tell they are at the end of the TD list when the head TD pointer is the same as the tail TD pointer.
    // That means the last TD always gets skipped and we don't want to skip any actual TDs.
    if (pipe.type() != Pipe::Type::Isochronous) {
        auto* tail_transfer_descriptor = m_general_transfer_descriptor_pool->try_take_free_descriptor();
        if (!tail_transfer_descriptor) {
            dbgln("OHCI: No free transfer descriptors to create tail transfer descriptor!");
            return ENOMEM;
        }

        // Start with an empty TD list (head and tail are the same)
        new_endpoint_descriptor->transfer_descriptor_head_physical_address = tail_transfer_descriptor->physical_address;
        new_endpoint_descriptor->transfer_descriptor_tail_physical_address = tail_transfer_descriptor->physical_address;
        new_endpoint_descriptor->tail_transfer_descriptor = tail_transfer_descriptor;
    } else {
        TODO();
    }

    free_endpoint_descriptor_on_error_guard.disarm();

    // Set the flags we have been building up.
    new_endpoint_descriptor->control = control;

    // Add the new endpoint descriptor to the endpoint linked list.
    auto* next_ed_after_head = head->next;

    new_endpoint_descriptor->next_endpoint_descriptor_physical_address = head->next_endpoint_descriptor_physical_address;
    new_endpoint_descriptor->next = next_ed_after_head;
    new_endpoint_descriptor->previous = head;

    if (next_ed_after_head)
        next_ed_after_head->previous = new_endpoint_descriptor;

    head->next_endpoint_descriptor_physical_address = new_endpoint_descriptor->physical_address;
    head->next = new_endpoint_descriptor;

    // Associate the pipe with the endpoint.
    new_endpoint_descriptor->associated_pipe = &pipe;

    if constexpr (OHCI_DEBUG) {
        dbgln("OHCI: New ED:");
        new_endpoint_descriptor->print();
    }

    return KSuccess;
}

KResult OHCIController::pipe_destroyed(Pipe const& pipe)
{
    // FIXME: This is a bit weird...
    if (!m_root_hub || !m_root_hub->initialized() || pipe.device_address() == m_root_hub->device_address()) {
        // Endpoint descriptors are not needed for the root hub.
        return KSuccess;
    }

    auto* endpoint = get_endpoint_descriptor_for_pipe(pipe);
    if (!endpoint) {
        dbgln("OHCI: Received pipe destroyed notification for pipe at {:p} but there's no endpoint descriptor associated with it?", &pipe);
        return ENOENT;
    }

    // Prevent controller from processing this endpoint.
    u32 endpoint_control = endpoint->control;
    endpoint_control |= ED_CONTROL_SKIP;
    endpoint->control = endpoint_control;

    // Update the links.
    if (endpoint->previous) {
        endpoint->previous->next = endpoint->next;
        endpoint->previous->next_endpoint_descriptor_physical_address = endpoint->next ? endpoint->next->next_endpoint_descriptor_physical_address : 0;
    }

    if (endpoint->next)
        endpoint->next->previous = endpoint->previous;

    // Before we can free the memory, we must ensure that the controller is not interacting with this endpoint.
    // For control and bulk pipes, we do this by disabling the relevant endpoint list for the pipe and then wait for a start of frame (SOF) interrupt.
    // When we receive the SOF interrupt, we know that the controller is no longer interacting with the list and won't interact with the list again until it's re-enabled.
    // FIXME: Handle interrupt and isochronous pipes.

    u32 list_to_disable = 0;
    switch (pipe.type()) {
    case Pipe::Type::Control:
        list_to_disable = HC_CONTROL_CONTROL_LIST_ENABLE;
        break;
    case Pipe::Type::Bulk:
        list_to_disable = HC_CONTROL_BULK_LIST_ENABLE;
        break;
    default:
        TODO();
    }

    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();

    if (list_to_disable != 0) {
        // Disable the list that we determined above.
        u32 hc_control = operational_registers->hc_control;
        hc_control &= ~list_to_disable;
        operational_registers->hc_control = hc_control;

        // Wait for SOF interrupt.
        m_received_start_of_frame_interrupt = false;

        // NOTE: This will be disabled for us in handle_irq.
        operational_registers->hc_interrupt_enable = INTERRUPT_START_OF_FRAME;

        // FIXME: Timeout?
        while (!m_received_start_of_frame_interrupt)
            ;

        // The controller is no longer interacting with the endpoint. Free it and re-enable the relevant list.
        endpoint->free();
        m_endpoint_descriptor_pool->release_to_pool(endpoint);

        hc_control = operational_registers->hc_control;
        hc_control |= list_to_disable;
        operational_registers->hc_control = hc_control;
    } else {
        TODO();
    }

    return KSuccess;
}

KResult OHCIController::pipe_changed(Pipe const& pipe)
{
    // FIXME: This is a bit weird...
    if (!m_root_hub || !m_root_hub->initialized() || pipe.device_address() == m_root_hub->device_address()) {
        // Endpoint descriptors are not needed for the root hub.
        return KSuccess;
    }

    auto* endpoint = get_endpoint_descriptor_for_pipe(pipe);
    if (!endpoint) {
        dbgln("OHCI: Received pipe changed notification for pipe at {:p} but there's no endpoint descriptor associated with it?", &pipe);
        return ENOENT;
    }

    u32 control = endpoint->control;
    control &= ~(ED_CONTROL_FUNCTION_ADDRESS_MASK | ED_CONTROL_ENDPOINT_NUMBER_MASK | ED_CONTROL_MAX_PACKET_SIZE_MASK);
    control |= (pipe.device_address() & 0x7F) << ED_CONTROL_FUNCTION_ADDRESS;
    control |= (pipe.endpoint_address() & 0xF) << ED_CONTROL_ENDPOINT_NUMBER;
    control |= (pipe.max_packet_size() & 0x7FF) << ED_CONTROL_MAX_PACKET_SIZE;

    return KSuccess;
}

}
