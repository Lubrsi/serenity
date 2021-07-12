//
// Created by lukew on 10/07/2021.
//

#include <Kernel/IO.h>
#include <Kernel/Bus/USB/OHCIController.h>
#include <Kernel/Bus/USB/OHCITypes.h>
#include <Kernel/StdLib.h>

namespace Kernel::USB {

RefPtr<OHCIController> OHCIController::try_to_initialize(PCI::Address address)
{
    // NOTE: This assumes that address is pointing to a valid OHCI controller.
    auto controller = adopt_ref_if_nonnull(new (nothrow) OHCIController(address));
    if (!controller)
        return {};

    if (controller->initialize())
        return controller;

    return nullptr;
}

bool OHCIController::initialize()
{
    dmesgln("OHCI: Controller found {} @ {}", PCI::get_id(pci_address()), pci_address());
    dmesgln("OHCI: Interrupt line: {}", PCI::get_interrupt_line(pci_address()));

    PCI::enable_bus_mastering(pci_address());
    PCI::disable_io_space(pci_address());
    PCI::enable_memory_space(pci_address());

    size_t operational_registers_region_size = PCI::get_BAR_space_size(pci_address(), 0);
    auto operational_registers_address = PCI::get_BAR0(pci_address()) & ~0xf;
    m_operational_registers_region = MM.allocate_kernel_region(PhysicalAddress(page_base_of(operational_registers_address)), page_round_up(operational_registers_region_size), "OHCI Operational Registers", Region::Access::Read | Region::Access::Write, Region::Cacheable::No);
    if (!m_operational_registers_region)
        return false;

    dmesgln("OHCI: Operational registers region @ {} {}", m_operational_registers_region->vaddr(), m_operational_registers_region->physical_page(0)->paddr());

    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    auto revision = operational_registers->hc_revision & HC_REVISION_REVISION;

    if (revision != 0x10) {
        dmesgln("OHCI: Unknown revision 0x{:02x}, expected 0x10", revision);
        return false;
    }

    // The HCCA needs to be at a particular alignment. The controller tells us this alignment.
    // We need to write 0xFFFF'FFFF to host_controller_communication_area_physical_address and then read it back.
    // When reading it back, the controller will have removed the bottom bits to suit the required alignment.
    operational_registers->host_controller_communication_area_physical_address = 0xFFFFFFFF;
    u32 returned_address = operational_registers->host_controller_communication_area_physical_address;
    u32 alignment = ~returned_address + 1;
    dmesgln("OHCI: Required HCCA alignment: 0x{:08x}", alignment);

    // FIXME: allocate_contiguous_kernel_region only takes multiples of the PAGE_SIZE for the alignment.
    m_host_controller_communication_area_region = MM.allocate_contiguous_kernel_region(page_round_up(sizeof(HostControllerCommunicationArea)), "OHCI Host Controller Communication Area", Region::Access::Read | Region::Access::Write, page_round_up(alignment), Region::Cacheable::No);
    if (!m_host_controller_communication_area_region)
        return false;

    dmesgln("OHCI: HCCA @ {} {}", m_host_controller_communication_area_region->vaddr(), m_host_controller_communication_area_region->physical_page(0)->paddr());

    // Clear out the HCCA
    memset(m_host_controller_communication_area_region->vaddr().as_ptr(), 0, sizeof(HostControllerCommunicationArea));

    bool reset_success = reset();
    if (!reset_success)
        return false;

    auto root_descriptor_a = operational_registers->hc_root_descriptor_a;
    root_descriptor_a |= HC_ROOT_DESCRIPTOR_A_NO_POWER_SWITCHING;
    operational_registers->hc_root_descriptor_a = root_descriptor_a;

    return true;
}

bool OHCIController::reset()
{
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();

    enable_irq();

    if (operational_registers->hc_control & HC_CONTROL_INTERRUPT_ROUTING) {
        // 5.1.1.3.3 OS Driver, SMM Active
        // Take control of the controller from SMM.
        auto command_status = operational_registers->hc_command_status;
        command_status |= HC_COMMAND_STATUS_OWNERSHIP_CHANGE_REQUEST;
        operational_registers->hc_command_status = command_status;

        while (operational_registers->hc_control & HC_CONTROL_INTERRUPT_ROUTING) {
            // FIXME: Timeout
        }

        dbgln("OHCI: Took control from SMM!");
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
    control &= ~(HC_CONTROL_INTERRUPT_ROUTING);
    control |= HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_USB_OPERATIONAL | HC_CONTROL_PERIODIC_LIST_ENABLE | HC_CONTROL_ISOCHRONOUS_ENABLE | HC_CONTROL_CONTROL_LIST_ENABLE | HC_CONTROL_BULK_LIST_ENABLE | HC_CONTROL_REMOTE_WAKEUP_ENABLE;

    // Go operational.
    operational_registers->hc_control = control;

    dbgln("We just wrote out 0x{:08x} to control", control);

    // Work out FSLargestDataPacket. This is done by taking out the maximum number of bit times for transaction overhead (which is 210 according to spec), followed by multiplying by 6/7 to account for worst case bit stuffing overhead.
    u16 fs_largest_data_packet = (original_frame_interval - 210) * 6/7;
    // FIXME: some bit stuff around FIT
    operational_registers->hc_frame_interval = HC_FRAME_INTERVAL_FRAME_INTERVAL_TOGGLE | (fs_largest_data_packet << 16) | original_frame_interval;

    // We want to give priority to the interrupt list for 90% of the frame interval.
    u16 periodic_start = original_frame_interval * 9/10;
    operational_registers->hc_periodic_start = periodic_start;

    dbgln("OHCI: Reset complete.");

    return true;
}

void OHCIController::start()
{
    TODO();
}

void OHCIController::stop()
{
    TODO();
}

UNMAP_AFTER_INIT OHCIController::OHCIController(PCI::Address address)
    : PCI::Device(address)
{
}

UNMAP_AFTER_INIT OHCIController::~OHCIController()
{
}

bool OHCIController::handle_irq(RegisterState const&)
{
    auto* operational_registers = (OperationalRegisters*)m_operational_registers_region->vaddr().as_ptr();
    auto interrupt_status = operational_registers->hc_interrupt_status;
    dbgln("IS: 0x{:08x}", interrupt_status);

    if (interrupt_status == 0) {
        dbgln("OHCI: Interrupt status is empty. Interrupt not for us or spurious.");
        return false;
    }



    return true;
}

KResultOr<size_t> OHCIController::submit_control_transfer(Transfer&)
{
    TODO();
}

const RefPtr<USB::Device> OHCIController::get_device_at_port(USB::Device::PortNumber)
{
    TODO();
}

const RefPtr<USB::Device> OHCIController::get_device_from_address(u8)
{
    TODO();
}

}
