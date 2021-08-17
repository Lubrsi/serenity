/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define AM79C970_DEBUG 1

#include <AK/MACAddress.h>
#include <Kernel/Debug.h>
#include <Kernel/Net/Am79C970NetworkAdapter.h>
#include <Kernel/Bus/PCI/IDs.h>
#include <Kernel/Sections.h>

namespace Kernel {

// CSR is "Control and Access Registers". These registers allow us to control the NIC and see its state.
static constexpr u16 CSR_CONTROLLER_STATUS = 0;
static constexpr u16 CSR_INITIALIZATION_BLOCK_ADDRESS_LOWER_16_BITS = 1;
static constexpr u16 CSR_INITIALIZATION_BLOCK_ADDRESS_HIGHER_16_BITS = 2;
static constexpr u16 CSR_INTERRUPT_MASKS_AND_DEFERRAL_CONTROL = 3;
static constexpr u16 CSR_TEST_AND_FEATURES_CONTROL = 4;
static constexpr u16 CSR_MODE = 15;
static constexpr u16 CSR_SOFTWARE_STYLE = 58;
static constexpr u16 CSR_CHIP_ID_REGISTER_LOWER = 88;

// BCR is "Bus Configuration Registers". These registers are used to configure the bus interface and other features.
static constexpr u16 BCR_MISC_CONFIG = 2; // Miscellaneous Configuration
static constexpr u16 BCR_LINK_STATUS = 4;

// This NIC can operate in either 16-bit or 32-bit mode. 16-bit is called Word I/O (WIO), 32-bit is called Double Word I/O (DWIO)
// WIO and DWIO have the same registers but at different offsets, therefore we must know both, especially because we can't be sure
// which mode the NIC is in before reset. However, we reset the NIC into 32-bit mode, so we only need to know the offsets for the
// reset register.

// RDP is "Register Data Port", this is the data port just for CSR. It is at the same offset for both WIO and DWIO.
static constexpr u16 RDP_REG = 0x10;

// RAP is "Register Address Port", this is the index port for both CSR and BCR
static constexpr u16 DWIO_RAP_REG = 0x14;

// Reset register. Reading from it resets the NIC.
static constexpr u16 WIO_RESET_REG = 0x14;
static constexpr u16 DWIO_RESET_REG = 0x18;

// BDP is "BCR Data Port", this is the data port just for BCR.
static constexpr u16 DWIO_BDP_REG = 0x1C;

static constexpr u16 CSR0_INIT_FLAG = (1 << 0);
static constexpr u16 CSR0_START_FLAG = (1 << 1);
static constexpr u16 CSR0_STOP_FLAG = (1 << 2);
static constexpr u16 CSR0_TRANSMIT_DEMAND_FLAG = (1 << 3);
static constexpr u16 CSR0_INTERRUPT_ENABLE_FLAG = (1 << 6);
static constexpr u16 CSR0_MEMORY_ERROR_FLAG = (1 << 11);
static constexpr u16 CSR0_MISSED_FRAME_FLAG = (1 << 12);
static constexpr u16 CSR0_COLLISION_ERROR_FLAG = (1 << 13);
static constexpr u16 CSR0_BABBLE_ERROR_FLAG = (1 << 14);
static constexpr u16 CSR0_ERROR_OCCURRED = (1 << 15); // This bit is an OR of all the error flags, allowing us to easily tell if an error occurred.

static constexpr u16 CSR3_BIG_ENDIAN_FLAG = (1 << 2);

static constexpr u16 CSR4_AUTO_PAD_TRANSMIT_FLAG = (1 << 11);

static constexpr u16 CSR15_PORT_SELECT_AUI = (0 << 7);
static constexpr u16 CSR15_PORT_SELECT_10_BASE_T = (1 << 7);

static constexpr u16 BCR2_AUTO_SELECT_FLAG = (1 << 1);

static constexpr u16 BCR4_LINK_STATUS_ENABLE = (1 << 6);

// These are all a part of CSR0, but are separated to indicate they're specifically interrupt flags.
static constexpr u16 INTERRUPT_FLAG_INTERRUPT_OCCURRED = (1 << 7); // This bit is an OR of all the interrupt flags, allowing us to easily tell if an interrupt occurred.
static constexpr u16 INTERRUPT_FLAG_INIT_DONE = (1 << 8);
static constexpr u16 INTERRUPT_FLAG_TRANSMIT_COMPLETE = (1 << 9);
static constexpr u16 INTERRUPT_FLAG_RECEIVE = (1 << 10);

static constexpr u16 CHIP_VERSION_AM79C970A = 0x2621;

UNMAP_AFTER_INIT RefPtr<Am79C970NetworkAdapter> Am79C970NetworkAdapter::try_to_initialize(PCI::Address address)
{
    auto id = PCI::get_id(address);
    if (id.vendor_id != (u16)PCI::VendorID::AMD)
        return {};
    if (id.device_id != 0x2000)
        return {};

    u8 irq = PCI::get_interrupt_line(address);
    auto adapter = adopt_ref_if_nonnull(new (nothrow) Am79C970NetworkAdapter(address, irq));
    if (!adapter)
        return {};

    auto initialization_result = adapter->initialize();
    if (initialization_result.is_error())
        return {};

    return adapter;
}

Am79C970NetworkAdapter::Am79C970NetworkAdapter(PCI::Address address, u8 irq)
    : PCI::Device(address, irq)
{
    set_interface_name(address);
}

KResult Am79C970NetworkAdapter::initialize()
{
    m_io_base = IOAddress(PCI::get_BAR0(pci_address()) & ~1);

    dmesgln("Am79C970: Found @ {}", pci_address());
    dmesgln("Am79C970: IO Base: {}", m_io_base);
    dmesgln("Am79C970: Interrupt line: {}", PCI::get_interrupt_line(pci_address()));

    PCI::enable_bus_mastering(pci_address());

    reset();
    read_mac_address();
    const auto& mac = mac_address();
    dmesgln("Am79C970: MAC Address: {}", mac.to_string());

    auto result = initialize_rx_descriptors();
    if (result.is_error())
        return result;

    result = initialize_tx_descriptors();
    if (result.is_error())
        return result;

    // The NIC will send an interrupt when initialization is done, so lets enable it now.
    enable_irq();

    return initialize_32bit();
}

Am79C970NetworkAdapter::~Am79C970NetworkAdapter()
{
}

u32 Am79C970NetworkAdapter::in32(u16 address)
{
    dbgln_if(AM79C970_DEBUG, "Am79C970: in32 @ 0x{:04x}", address);
    return m_io_base.offset(address).in<u32>();
}

void Am79C970NetworkAdapter::out_rap_32(u32 value)
{
    // This sets the index for CSR and BCR in 32-bit mode.
    dbgln_if(AM79C970_DEBUG, "Am79C970: out_rap_32 value=0x{:08x}", value);
    m_io_base.offset(DWIO_RAP_REG).out(value);
    //full_memory_barrier();
}

u32 Am79C970NetworkAdapter::in_csr_32(u32 csr_number)
{
    dbgln_if(AM79C970_DEBUG, "Am79C970: in_csr_32 csr_number=0x{:08x}", csr_number);
    out_rap_32(csr_number);
    return m_io_base.offset(RDP_REG).in<u32>();
}

void Am79C970NetworkAdapter::out_csr_32(u32 csr_number, u32 value)
{
    dbgln_if(AM79C970_DEBUG, "Am79C970: out_csr_32 csr_number=0x{:08x} value=0x{:08x}", csr_number, value);
    out_rap_32(csr_number);
    m_io_base.offset(RDP_REG).out(value);
}

u32 Am79C970NetworkAdapter::in_bcr_32(u32 bcr_number)
{
    dbgln_if(AM79C970_DEBUG, "Am79C970: in_bcr_32 bcr_number=0x{:08x}", bcr_number);
    out_rap_32(bcr_number);
    return m_io_base.offset(DWIO_BDP_REG).in<u32>();
}

void Am79C970NetworkAdapter::out_bcr_32(u32 bcr_number, u32 value)
{
    dbgln_if(AM79C970_DEBUG, "Am79C970: out_bcr_32 bcr_number=0x{:08x} value=0x{:08x}", bcr_number, value);
    out_rap_32(bcr_number);
    m_io_base.offset(DWIO_BDP_REG).out(value);
}

void Am79C970NetworkAdapter::reset()
{
    // Reading from the reset register resets the NIC but it is at a different offset depending on the mode.
    // The other issue is that we cannot be sure which mode it is in before reset.
    // We can get around this by reading the 32-bit reset register and then the 16-bit register.

    // If the NIC is in 32-bit mode, this will reset the NIC.
    // If the NIC is in 16-bit mode, this will read garbage but will not do any harm.
    m_io_base.offset(DWIO_RESET_REG).in<u32>();

    // If the NIC is in 32-bit mode, this will reset the NIC again.
    // If the NIC is in 16-bit mode, this will reset the NIC for the first time.
    m_io_base.offset(WIO_RESET_REG).in<u16>();

    // Now wait for the NIC to reset.
    IO::delay(1);

    // Set the NIC to 32-bit mode. This is done by writing 0 to the status register (CSR0).
    // After reset, RAP is cleared to 0, meaning it is pointing to CSR0.
    // Therefore, we can just write to RDP straight away without having to set RAP.
    m_io_base.offset(RDP_REG).out<u32>(0);

    u32 software_style = in_csr_32(CSR_SOFTWARE_STYLE);

    // Clear the software style. (The top 16 bits are undefined, so no need to keep them)
    software_style &= 0xFF00;

    // We want to use the PCnet-PCI software style. This mode is also 32-bit.
    software_style |= 0x2;

    out_csr_32(CSR_SOFTWARE_STYLE, software_style);

    u32 misc_config = in_bcr_32(BCR_MISC_CONFIG);

    if (m_chip_version != CHIP_VERSION_AM79C970A) {
        // Enable auto select. This will use 10BASE-T when the link is up and working, and AUI when it is failing.
        misc_config |= BCR2_AUTO_SELECT_FLAG;

        out_bcr_32(BCR_MISC_CONFIG, misc_config);
    } else {
        misc_config &= ~BCR2_AUTO_SELECT_FLAG;
        out_bcr_32(BCR_MISC_CONFIG, misc_config);

        u32 mode = in_csr_32(CSR_MODE);
        mode |= CSR15_PORT_SELECT_10_BASE_T;
        out_csr_32(CSR_MODE, mode);
    }

    dbgln_if(AM79C970_DEBUG, "Am79C970: Reset complete.");
}

void Am79C970NetworkAdapter::read_mac_address()
{
    MACAddress mac{};

    // The MAC address is stored in the first 6 bytes of the I/O space.
    u32 tmp = in32(0);

    mac[0] = tmp & 0xFF;
    mac[1] = (tmp >> 8) & 0xFF;
    mac[2] = (tmp >> 16) & 0xFF;
    mac[3] = (tmp >> 24) & 0xFF;

    // The top 2 bytes go to waste here, but it can't be helped as we're in 32-bit mode here and 16-bit reads return all ones.
    tmp = in32(4);

    mac[4] = tmp & 0xFF;
    mac[5] = (tmp >> 8) & 0xFF;

    set_mac_address(mac);
}

KResult Am79C970NetworkAdapter::initialize_rx_descriptors()
{
    m_rx_descriptors_region = MM.allocate_contiguous_kernel_region(Memory::page_round_up(sizeof(rx_32bit_desc) * number_of_rx_descriptors + 16), "Am79C970 RX Descriptors", Memory::Region::Access::Read | Memory::Region::Access::Write);
    if (!m_rx_descriptors_region)
        return ENOMEM;

    dbgln_if(AM79C970_DEBUG, "Am79C970: First RX descriptor @ Virtual {} Physical {}", m_rx_descriptors_region->vaddr(), m_rx_descriptors_region->physical_page(0)->paddr());

    auto* rx_descriptors = (rx_32bit_desc*)m_rx_descriptors_region->vaddr().as_ptr();

    m_rx_buffer_region = MM.allocate_contiguous_kernel_region(rx_buffer_size * number_of_rx_descriptors, "Am79C970 RX Buffers", Memory::Region::Access::Read | Memory::Region::Access::Write);
    if (!m_rx_buffer_region)
        return ENOMEM;

    for (size_t i = 0; i < number_of_rx_descriptors; ++i) {
        auto& descriptor = rx_descriptors[i];
        m_rx_buffers[i] = m_rx_buffer_region->vaddr().as_ptr() + rx_buffer_size * i;

        // FIXME: This NIC is restricted to 32-bit physical addresses and PhysicalAddress is 64-bit.
        auto buffer_physical_address = m_rx_buffer_region->physical_page(rx_buffer_page_count * i)->paddr();
        descriptor.buffer_physical_address = buffer_physical_address.get();
        dbgln_if(AM79C970_DEBUG, "Am79C970: RX Buffer Region {} @ {}", i, buffer_physical_address);

        // The NIC wants us to specify the size in 2s complement.
        descriptor.buffer_byte_count = (u16)(-rx_buffer_size) & 0xFFF;
        descriptor.ones = 0b1111;
        descriptor.zeros = 0b0000;

        // Give all the receive descriptors to the NIC so we can receive data straight away.
        descriptor.owned_by_nic = true;
    }

    return KSuccess;
}

KResult Am79C970NetworkAdapter::initialize_tx_descriptors()
{
    m_tx_descriptors_region = MM.allocate_contiguous_kernel_region(Memory::page_round_up(sizeof(tx_32bit_desc) * number_of_tx_descriptors + 16), "Am79C970 TX Descriptors", Memory::Region::Access::Read | Memory::Region::Access::Write);
    if (!m_tx_descriptors_region)
        return ENOMEM;

    dbgln_if(AM79C970_DEBUG, "Am79C970: First TX descriptor @ Virtual {} Physical {}", m_tx_descriptors_region->vaddr().as_ptr(), m_tx_descriptors_region->physical_page(0)->paddr());

    auto* tx_descriptors = (tx_32bit_desc*)m_tx_descriptors_region->vaddr().as_ptr();

    m_tx_buffer_region = MM.allocate_contiguous_kernel_region(tx_buffer_size * number_of_tx_descriptors, "Am79C970 TX Buffers", Memory::Region::Access::Read | Memory::Region::Access::Write);
    if (!m_tx_buffer_region)
        return ENOMEM;

    for (size_t i = 0; i < number_of_tx_descriptors; ++i) {
        auto& descriptor = tx_descriptors[i];

        m_tx_buffers[i] = m_tx_buffer_region->vaddr().as_ptr() + tx_buffer_size * i;

        // FIXME: This NIC is restricted to 32-bit physical addresses and PhysicalAddress is 64-bit.
        auto buffer_physical_address = m_tx_buffer_region->physical_page(tx_buffer_page_count * i)->paddr();
        descriptor.buffer_physical_address = buffer_physical_address.get();
        dbgln_if(AM79C970_DEBUG, "Am79C970: TX Buffer Region {} @ {}", i, buffer_physical_address);

        // The NIC wants us to specify the size in 2s complement.
        descriptor.buffer_byte_count = (u16)(-tx_buffer_size) & 0xFFF;
        descriptor.ones = 0b1111;

        // Take ownership of all the transfer descriptors so we can transfer data straight away.
        descriptor.owned_by_nic = false;
    }

    return KSuccess;
}

KResult Am79C970NetworkAdapter::initialize_32bit()
{
    // NOTE: This should only use a single page, so it will be physically contiguous.
    auto initialization_block_page = MM.allocate_kernel_region(Memory::page_round_up(sizeof(InitializationBlock32bit)), "Am79C970 Init Block", Memory::Region::Access::Read | Memory::Region::Access::Write);
    if (!initialization_block_page)
        return ENOMEM;

    auto* initialization_block = (InitializationBlock32bit*)initialization_block_page->vaddr().as_ptr();
    dbgln_if(AM79C970_DEBUG, "Am79C970: Initialization block @ Virtual {} Physical {}", initialization_block_page->vaddr(), initialization_block_page->physical_page(0)->paddr());

    // Init block must be on a double-word boundary.
    VERIFY(initialization_block_page->physical_page(0)->paddr().get() % 4 == 0);

    // Initialize CSR15 to 0. The most important thing about this is that writing 0 makes sure we don't disable anything.
    initialization_block->mode = 0;

    initialization_block->reserved1 = 0;
    initialization_block->num_rx_descriptors_log2 = number_of_rx_descriptors_log2;

    initialization_block->reserved2 = 0;
    initialization_block->num_tx_descriptors_log2 = number_of_tx_descriptors_log2;

    const auto& mac = mac_address();
    initialization_block->mac_part_0 = mac[0];
    initialization_block->mac_part_1 = mac[1];
    initialization_block->mac_part_2 = mac[2];
    initialization_block->mac_part_3 = mac[3];
    initialization_block->mac_part_4 = mac[4];
    initialization_block->mac_part_5 = mac[5];
    initialization_block->reserved3 = 0;
    initialization_block->logical_address_filter = 0;

    // FIXME: This NIC is restricted to 32-bit physical addresses and PhysicalAddress is 64-bit.
    dbgln_if(AM79C970_DEBUG, "Am79C970: First RX descriptor @ {}", m_rx_descriptors_region->physical_page(0)->paddr());
    dbgln_if(AM79C970_DEBUG, "Am79C970: First TX descriptor @ {}", m_tx_descriptors_region->physical_page(0)->paddr());
    initialization_block->physical_address_of_first_rx_descriptor = m_rx_descriptors_region->physical_page(0)->paddr().get();
    initialization_block->physical_address_of_first_tx_descriptor = m_tx_descriptors_region->physical_page(0)->paddr().get();

    out_csr_32(CSR_INITIALIZATION_BLOCK_ADDRESS_LOWER_16_BITS, initialization_block_page->physical_page(0)->paddr().get() & 0xFFFF);
    out_csr_32(CSR_INITIALIZATION_BLOCK_ADDRESS_HIGHER_16_BITS, initialization_block_page->physical_page(0)->paddr().get() >> 16);

    u32 interrupt_masks = in_csr_32(CSR_INTERRUPT_MASKS_AND_DEFERRAL_CONTROL);

    // Use little endian mode.
    interrupt_masks &= ~CSR3_BIG_ENDIAN_FLAG;

    out_csr_32(CSR_INTERRUPT_MASKS_AND_DEFERRAL_CONTROL, interrupt_masks);

    u32 features = in_csr_32(CSR_TEST_AND_FEATURES_CONTROL);

    // Enable auto-padding sent packets to be at least 64 bytes.
    features |= CSR4_AUTO_PAD_TRANSMIT_FLAG;

    out_csr_32(CSR_TEST_AND_FEATURES_CONTROL, features);

    u32 chip_id_lower = in_csr_32(CSR_CHIP_ID_REGISTER_LOWER);
    m_chip_version = ((chip_id_lower >> 12) & 0xFFFF);
    dbgln("Am79C970: Chip Version: 0x{:08x}", m_chip_version);

    // Clear STOP and set INIT and enable interrupts.
    // This will initialize the NIC. The NIC will send an interrupt when initialization is done.
    out_csr_32(CSR_CONTROLLER_STATUS, CSR0_INIT_FLAG | CSR0_INTERRUPT_ENABLE_FLAG);

    dbgln_if(AM79C970_DEBUG, "Am79C970: Waiting for initialization interrupt...");

    // Wait for the initialization interrupt to happen.
    while (!m_initialization_done.load())
        m_wait_queue.wait_forever("Am79C970NetworkAdapter Initialization");

    // Start the NIC. This is done by clearing INIT (bit 0) and STOP (bit 2), then enabling STRT (bit 1) all at the same time.
    // Note that the interrupt enable flag is also passed again. This is because writing 0 to this flag has an effect (disables interrupts) unlike the rest of the flags in this register.
    out_csr_32(CSR_CONTROLLER_STATUS, CSR0_START_FLAG | CSR0_INTERRUPT_ENABLE_FLAG);

    dbgln_if(AM79C970_DEBUG, "Am79C970: Initialization complete");

    return KSuccess;
}

void Am79C970NetworkAdapter::send_raw(ReadonlyBytes payload)
{
    // This prevents multiple threads from using the same transfer descriptor.
    MutexLocker locker(m_transfer_lock);

    dbgln_if(AM79C970_DEBUG, "Am79C970: Sending packet ({} bytes)", payload.size());

    VERIFY(payload.size() <= tx_buffer_size);

    auto* tx_descriptors = (tx_32bit_desc*)m_tx_descriptors_region->vaddr().as_ptr();
    auto& descriptor = tx_descriptors[m_next_tx_descriptor];

    while (descriptor.owned_by_nic) {
        dbgln_if(AM79C970_DEBUG, "Am79C970: Waiting to get a free TX descriptor...");
        m_wait_queue.wait_forever("Am79C970NetworkAdapter");
    }

    auto* tx_buffer = m_tx_buffers[m_next_tx_descriptor];
    memcpy(tx_buffer, payload.data(), payload.size());

    // Since we don't split packets, this buffer is both the start and end of the packet.
    descriptor.start_of_packet = true;
    descriptor.end_of_packet = true;

    // Clear out the rest of TMD1
    descriptor.deferred = false;
    descriptor.one = false;
    descriptor.more = false;
    descriptor.add_fcs_or_no_fcs = false;
    descriptor.error = false;

    // Clear out TMD2
    descriptor.transmit_retry_count = 0;
    descriptor.time_domain_reflectometry = 0;
    descriptor.retry_error = false;
    descriptor.loss_of_carrier = false;
    descriptor.late_collision = false;
    descriptor.excessive_deferral = false;
    descriptor.underflow = false;
    descriptor.buffer_error = false;

    // The NIC wants us to use 2s complement for the length.
    descriptor.buffer_byte_count = (u16)(-payload.size()) & 0xFFF;
    descriptor.ones = 0b1111;

    // Give ownership to the NIC.
    descriptor.owned_by_nic = true;

    // Tell the NIC to transmit immediately.
    // Note that the interrupt enable flag is also set. This is because writing 0 to this flag has an effect (disables interrupts) unlike the rest of the flags in this register.
    out_csr_32(CSR_CONTROLLER_STATUS, CSR0_INTERRUPT_ENABLE_FLAG | CSR0_TRANSMIT_DEMAND_FLAG);

    dbgln_if(AM79C970_DEBUG, "Am79C970: Packet sent! (TX descriptor {})", m_next_tx_descriptor);

    ++m_next_tx_descriptor;
    m_next_tx_descriptor %= number_of_tx_descriptors;
}

void Am79C970NetworkAdapter::receive()
{
    auto* rx_descriptors = (rx_32bit_desc*)m_rx_descriptors_region->vaddr().as_ptr();
    for (;;) {
        auto& descriptor = rx_descriptors[m_next_rx_descriptor];

        // Once we get a descriptor that's owned by the NIC, there is no more data to be read in.
        if (descriptor.owned_by_nic)
            break;

        // FIXME: Support packets that span multiple buffers.
        VERIFY(descriptor.start_of_packet && descriptor.end_of_packet);

        auto* buffer = m_rx_buffers[m_next_rx_descriptor];
        u16 length = descriptor.message_byte_count;
        VERIFY(length <= rx_buffer_size);
        dbgln_if(AM79C970_DEBUG, "Am79C970: Received 1 packet @ {:p} (RX descriptor {}) ({} bytes)", buffer, m_next_rx_descriptor, length);

        did_receive({ buffer, length });

        // Give ownership back to the NIC.
        descriptor.owned_by_nic = true;

        ++m_next_rx_descriptor;
        m_next_rx_descriptor %= number_of_rx_descriptors;
    }
}

void Am79C970NetworkAdapter::check_for_transfer_errors()
{
    auto* tx_descriptors = (tx_32bit_desc*)m_tx_descriptors_region->vaddr().as_ptr();
    auto* tx_descriptors_bytes = (u32*)m_tx_descriptors_region->vaddr().as_ptr();

    for (size_t tx_index = 0; tx_index < number_of_tx_descriptors; ++tx_index) {
        auto& descriptor = tx_descriptors[tx_index];
        u32* descriptor_bytes = tx_descriptors_bytes + (4 * tx_index);

        if (descriptor.owned_by_nic)
            continue;

        for (size_t i = 0; i < 4; ++i)
            dbgln("TD descriptor {} TMD{}: 0x{:08x}", tx_index, i, descriptor_bytes[i]);

        if (descriptor.error) {
            dbgln("Am79C970: Transfer error occurred on TX descriptor {}:", tx_index);

            if (descriptor.buffer_error) {
                dbgln("Am79C970: - Buffer error (The buffer doesn't contain the end of the packet and the NIC doesn't own the next buffer)");
                // FIXME: Reset the NIC and return.
            }

            if (descriptor.underflow) {
                dbgln("Am79C970: - Underflow (The NIC truncated the message because the FIFO became empty before the end of the frame)");
                // FIXME: Reset the NIC and return.
            }

            if (descriptor.loss_of_carrier)
                dbgln("Am79C970: - Loss of carrier (link down)");

            if (descriptor.late_collision)
                dbgln("Am79C970: - Late collision");

            if (descriptor.retry_error)
                dbgln("Am79C970: - Excessive collisions (The NIC has failed 16 times in a row to transmit the message)");
        }
    }
}

bool Am79C970NetworkAdapter::handle_irq(const RegisterState&)
{
    u32 controller_status = in_csr_32(CSR_CONTROLLER_STATUS);

    m_entropy_source.add_random_event(controller_status);

    if ((controller_status & INTERRUPT_FLAG_INTERRUPT_OCCURRED) == 0) {
        dbgln_if(AM79C970_DEBUG, "Am79C970: Received IRQ but NIC says no interrupt occurred. Spurious or not for us.");
        return false;
    }

    // Acknowledge all interrupts and disable interrupts without requesting an immediate transfer and without affecting the state of the NIC.
    out_csr_32(CSR_CONTROLLER_STATUS, controller_status & ~(CSR0_INTERRUPT_ENABLE_FLAG | CSR0_TRANSMIT_DEMAND_FLAG | CSR0_STOP_FLAG | CSR0_START_FLAG | CSR0_INIT_FLAG));

    dbgln_if(AM79C970_DEBUG, "Am79C970: IRQ received! status=0x{:08x}", controller_status);

    if (controller_status & CSR0_ERROR_OCCURRED) {
        dbgln("Am79C970: Errors occurred:");

        if (controller_status & CSR0_BABBLE_ERROR_FLAG)
            dbgln("Am79C970: - Babble (transmitter was on the channel for too long)");

        if (controller_status & CSR0_COLLISION_ERROR_FLAG)
            dbgln("Am79C970: - Collision");

        if (controller_status & CSR0_MISSED_FRAME_FLAG)
            dbgln("Am79C970: - Missed frame (lost an incoming packet as there were no free receive descriptors)");

        if (controller_status & CSR0_MEMORY_ERROR_FLAG) {
            dbgln("Am79C970: - Memory error");
            // FIXME: Reset the NIC and return.
        }
    }

    if (controller_status & INTERRUPT_FLAG_INIT_DONE) {
        dbgln_if(AM79C970_DEBUG, "Am79C970: Received initialization IRQ!");
        m_initialization_done = true;

        // Wake up the thread blocked waiting for the initialization interrupt.
        m_wait_queue.wake_all();
    }

    if (controller_status & INTERRUPT_FLAG_RECEIVE) {
        // We don't block on receives, so no need to wake up the threads in the wait queue.
        receive();
    }

    if (controller_status & INTERRUPT_FLAG_TRANSMIT_COMPLETE) {
        dbgln_if(AM79C970_DEBUG, "Am79C970: Received transmit IRQ!");
        check_for_transfer_errors();
        // Wake up the thread(s) blocked waiting for a free TX descriptor.
        m_wait_queue.wake_all();
    }

    // Enable interrupts.
    out_csr_32(CSR_CONTROLLER_STATUS, CSR0_INTERRUPT_ENABLE_FLAG);

    return true;
}

bool Am79C970NetworkAdapter::link_up()
{
    dbgln("Am79C970: 0x{:08x}", in_bcr_32(BCR_LINK_STATUS));
    return in_bcr_32(BCR_LINK_STATUS) & BCR4_LINK_STATUS_ENABLE;
}

i32 Am79C970NetworkAdapter::link_speed()
{
    // Only supports 10BASE-T and AUI, both 10Mbit/s.
    return 10;
}

bool Am79C970NetworkAdapter::link_full_duplex()
{
    // FIXME: Account for the different chip versions that support full duplex and MII.
    return m_chip_version == CHIP_VERSION_AM79C970A;
}

}
