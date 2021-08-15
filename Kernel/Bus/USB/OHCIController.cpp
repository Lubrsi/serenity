/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/IO.h>
#include <Kernel/Bus/USB/OHCIController.h>
#include <Kernel/Bus/USB/OHCITypes.h>
#include <Kernel/StdLib.h>
#include <Kernel/Process.h>

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

    // Clear out the HCCA
    memset(m_host_controller_communication_area_region->vaddr().as_ptr(), 0, sizeof(HostControllerCommunicationArea));

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

    if (operational_registers->hc_control & HC_CONTROL_INTERRUPT_ROUTING) {
        dbgln("OHCI: Taking control from SMM...");

        // 5.1.1.3.3 OS Driver, SMM Active
        // Take control of the controller from SMM.
        auto command_status = operational_registers->hc_command_status;
        command_status |= HC_COMMAND_STATUS_OWNERSHIP_CHANGE_REQUEST;
        operational_registers->hc_command_status = command_status;

        u8 attempt = 0;
        while (operational_registers->hc_control & HC_CONTROL_INTERRUPT_ROUTING && attempt < 100) {
            IO::delay(1000);
            dbgln("attempt {} failed", attempt);
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

    while (operational_registers->hc_command_status & HC_COMMAND_STATUS_HOST_CONTROLLER_RESET) {
        // FIXME: Timeout
        // Usual reset time is 10 us.
        IO::delay(10);
    }

    // Now that the controller has been reset, the functional state has been set to USBSuspend.
    // Because of this, we now must setup and go operational within 2 ms, otherwise the controller will go into USBResume.
    operational_registers->host_controller_communication_area_physical_address = m_host_controller_communication_area_region->physical_page(0)->paddr().get();
    operational_registers->hc_interrupt_disable = INTERRUPT_ALL;
    operational_registers->hc_interrupt_enable = INTERRUPT_SCHEDULING_OVERRUN | INTERRUPT_WRITEBACK_DONE_HEAD | INTERRUPT_RESUME_DETECTED | INTERRUPT_UNRECOVERABLE_ERROR | INTERRUPT_ROOT_HUB_STATUS_CHANGE;

    auto control = operational_registers->hc_control;
    control &= ~(HC_CONTROL_INTERRUPT_ROUTING | HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_MASK);
    control |= HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_USB_OPERATIONAL | HC_CONTROL_PERIODIC_LIST_ENABLE | HC_CONTROL_ISOCHRONOUS_ENABLE | HC_CONTROL_CONTROL_LIST_ENABLE | HC_CONTROL_BULK_LIST_ENABLE | HC_CONTROL_REMOTE_WAKEUP_ENABLE;

    // Go operational.
    operational_registers->hc_control = control;

    dbgln("We just wrote out 0x{:08x} to control", control);
    control = operational_registers->hc_control;
    dbgln("Control is: 0x{:08x}", control);

    // Work out FSLargestDataPacket. This is done by taking out the maximum number of bit times for transaction overhead (which is 210 according to spec), followed by multiplying by 6/7 to account for worst case bit stuffing overhead.
    u16 fs_largest_data_packet = (original_frame_interval - 210) * 6/7;
    // FIXME: Some bit stuff around FIT.
    operational_registers->hc_frame_interval = HC_FRAME_INTERVAL_FRAME_INTERVAL_TOGGLE | (fs_largest_data_packet << 16) | original_frame_interval;

    // 90% of the frame interval will be spent processing the control and bulk lists.
    // 10% of the frame interval will be spent processing the periodic list.
    // The control and bulk lists come first, then the periodic list.
    u16 periodic_start = original_frame_interval * 9/10;
    operational_registers->hc_periodic_start = periodic_start;

    dbgln("OHCI: Reset complete.");

    return KSuccess;
}

void OHCIController::create_root_hub_descriptor()
{
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    auto root_hub_descriptor_a = operational_registers->hc_root_hub_descriptor_a;
    // FIXME: Use HcRhDescriptorB to set up DeviceRemovable and PortPowerControl once USBHubDescriptor supports them.

    dbgln("create_root_hub_descriptor");

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

    m_root_hub_descriptor.power_on_to_power_good_time = (root_hub_descriptor_a & HC_ROOT_DESCRIPTOR_A_POWER_ON_TO_POWER_GOOD_TIME) >> 24;
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

            dbgln("OHCI: Hotplug thread wakeup. Checking for port updates...");

            if (m_root_hub)
                m_root_hub->check_for_port_updates();

            dbgln("OHCI: Hotplug check finished. Re-enabling status change interrupt and going back to sleep...");

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

    dbgln("OHCI: get_port_status status=0x{:04x} change=0x{:04x}", port_status.status, port_status.change);
}

KResult OHCIController::clear_hub_feature(Badge<OHCIRootHub>, HubFeatureSelector feature_selector)
{
    dbgln("OHCI: clear_hub_feature: feature_selector={}", (u8)feature_selector);

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

    dbgln("OHCI: set_port_feature: port={} feature_selector={}", port, (u8)feature_selector);

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

    dbgln("OHCI: clear_port_feature: port={} feature_selector={}", port, (u8)feature_selector);

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
    auto interrupt_status = operational_registers->hc_interrupt_status;
    dbgln("OHCI: handle_irq, IS: 0x{:08x}", interrupt_status);

    if (interrupt_status == 0) {
        dbgln("OHCI: Interrupt status is empty. Interrupt not for us or spurious.");
        return false;
    }

    // Acknowledge the interrupts.
    operational_registers->hc_interrupt_status = interrupt_status;

    if (interrupt_status & INTERRUPT_ROOT_HUB_STATUS_CHANGE) {
        dbgln("OHCI: Root hub status change!");
        // Disable the status change interrupt to prevent further status change interrupts while handling port resets.
        // This is because we get interrupts for any status change, even ones we caused ourselves.
        // The hot plug thread is responsible for re-enabling this interrupt.
        operational_registers->hc_interrupt_disable = INTERRUPT_ROOT_HUB_STATUS_CHANGE;
        m_root_hub_status_wait_queue.wake_all();
    }

    return true;
}

KResultOr<size_t> OHCIController::submit_control_transfer(Transfer& transfer)
{
    Pipe& pipe = transfer.pipe(); // Short circuit the pipe related to this transfer

    dbgln("OHCI: Received control transfer for address {}. Root Hub is at address {}.", pipe.device_address(), m_root_hub->device_address());

    // Short-circuit the root hub.
    if (pipe.device_address() == m_root_hub->device_address())
        return m_root_hub->handle_control_transfer(transfer);

    TODO();
}

}
