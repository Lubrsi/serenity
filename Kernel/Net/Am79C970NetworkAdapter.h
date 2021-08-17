/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtrVector.h>
#include <Kernel/Net/NetworkAdapter.h>
#include <Kernel/Bus/PCI/Access.h>
#include <Kernel/Bus/PCI/Device.h>
#include <Kernel/IO.h>
#include <Kernel/Random.h>

namespace Kernel {

class Am79C970NetworkAdapter final
    : public NetworkAdapter
    , public PCI::Device {
public:
    static RefPtr<Am79C970NetworkAdapter> try_to_initialize(PCI::Address);

    KResult initialize();

    Am79C970NetworkAdapter(PCI::Address, u8 irq);
    virtual ~Am79C970NetworkAdapter() override;

    virtual void send_raw(ReadonlyBytes) override;
    virtual bool link_up() override;
    virtual i32 link_speed() override;
    virtual bool link_full_duplex() override;

    virtual StringView purpose() const override { return class_name(); }

private:
    virtual bool handle_irq(const RegisterState&) override;
    virtual StringView class_name() const override { return "Am79C970NetworkAdapter"; }

    struct [[gnu::packed]] rx_32bit_desc {
        volatile u32 buffer_physical_address;

        volatile u16 buffer_byte_count : 12; // Buffer byte count in twos complement
        volatile u8 ones : 4; // Must be 4 ones.

        volatile u8 reserved1;

        volatile bool end_of_packet : 1; // This descriptor contains the end of the packet.
        volatile bool start_of_packet : 1; // This descriptor contains the start of the packet.
        volatile bool buffer_error : 1; // The NIC didn't own the next buffer while chaining multiple buffers together.
        volatile bool crc_error : 1; // CRC error - only valid if end_of_packet is set and overflow is not set.
        volatile bool overflow : 1; // Lost data due to FIFO overflow
        volatile bool framing_error : 1;
        volatile bool error : 1; // An error occurred (OR of framing_error, overflow, crc_error and buffer_error)
        volatile bool owned_by_nic : 1; // false = owned by us, true = owned by the NIC

        volatile u16 message_byte_count : 12; // Message byte count - size of the received packet, only valid when err is not set
        volatile u8 zeros : 4; // Must be 4 zeroes.

        volatile u8 runt_packet_count;
        volatile u8 receive_collision_count;

        volatile u32 reserved2;
    };
    static_assert(sizeof(rx_32bit_desc) == 16);

    struct [[gnu::packed]] tx_32bit_desc {
        volatile u32 buffer_physical_address;

        volatile u16 buffer_byte_count : 12; // Buffer byte count in twos complement
        volatile u8 ones : 4; // Must be 4 ones.

        volatile u8 reserved1;

        volatile bool end_of_packet : 1; // This descriptor contains the end of the packet.
        volatile bool start_of_packet : 1; // This descriptor contains the start of the packet.
        volatile bool deferred : 1; // The NIC had to defer this frame because it was busy
        volatile bool one : 1; // Only one retry was required to transmit the frame
        volatile bool more : 1; // More than one retry was required to transmit the frame
        volatile bool add_fcs_or_no_fcs : 1;
        volatile bool error : 1; // An error occurred (OR of underflow, late_collision, loss_of_carrier and retry_error)
        volatile bool owned_by_nic : 1; // false = owned by us, true = owned by the NIC

        volatile u8 transmit_retry_count : 4;
        volatile u16 reserved2 : 12;

        volatile u16 time_domain_reflectometry : 10;
        volatile bool retry_error : 1; // The NIC has failed 16 times in a row to transmit the message
        volatile bool loss_of_carrier : 1;
        volatile bool late_collision : 1;
        volatile bool excessive_deferral : 1;
        volatile bool underflow : 1; // The NIC truncated the message because the FIFO became empty before the end of the frame.
        volatile bool buffer_error : 1; // The buffer doesn't contain the end of the packet and the NIC doesn't own the next buffer.

        volatile u32 reserved3;
    };
    static_assert(sizeof(tx_32bit_desc) == 16);

    struct alignas(u32) [[gnu::packed]] InitializationBlock32bit {
        u16 mode; // Initializes CSR15 to this value.

        u8 reserved1 : 4;
        u8 num_rx_descriptors_log2 : 4;

        u8 reserved2 : 4;
        u8 num_tx_descriptors_log2 : 4;

        u8 mac_part_0;
        u8 mac_part_1;
        u8 mac_part_2;
        u8 mac_part_3;
        u8 mac_part_4;
        u8 mac_part_5;

        u16 reserved3;

        u64 logical_address_filter;

        u32 physical_address_of_first_rx_descriptor;
        u32 physical_address_of_first_tx_descriptor;
    };
    static_assert(sizeof(InitializationBlock32bit) == 28);

    void reset();
    void read_mac_address();

    KResult initialize_rx_descriptors();
    KResult initialize_tx_descriptors();

    KResult initialize_32bit();

    u32 in32(u16 address);

    void out_rap_32(u32 value);

    u32 in_csr_32(u32 csr_number);
    void out_csr_32(u32 csr_number, u32 value);

    u32 in_bcr_32(u32 bcr_number);
    void out_bcr_32(u32 bcr_number, u32 value);

    void receive();
    void check_for_transfer_errors();

    void dump_registers();

    Atomic<bool> m_initialization_done { false };

    IOAddress m_io_base;
    u16 m_chip_version { 0 };

    // The maximum this NIC supports is 512 for both in 32-bit mode and 128 for both in 16-bit mode.
    // Note that these can only be in powers of two, as the NIC takes the number of descriptors in integer log2 form.
    static constexpr size_t number_of_rx_descriptors = 32;
    static constexpr size_t number_of_tx_descriptors = 8;

    // FIXME: Actually calculate these
    static constexpr size_t number_of_rx_descriptors_log2 = 5;
    static constexpr size_t number_of_tx_descriptors_log2 = 3;

    static constexpr size_t rx_buffer_size = PAGE_SIZE;
    static constexpr size_t rx_buffer_page_count = rx_buffer_size / PAGE_SIZE;

    static constexpr size_t tx_buffer_size = PAGE_SIZE;
    static constexpr size_t tx_buffer_page_count = tx_buffer_size / PAGE_SIZE;

    OwnPtr<Memory::Region> m_rx_descriptors_region;
    OwnPtr<Memory::Region> m_tx_descriptors_region;
    OwnPtr<Memory::Region> m_rx_buffer_region;
    OwnPtr<Memory::Region> m_tx_buffer_region;
    Array<void*, number_of_rx_descriptors> m_rx_buffers;
    Array<void*, number_of_tx_descriptors> m_tx_buffers;

    size_t m_next_rx_descriptor { 0 };
    size_t m_next_tx_descriptor { 0 };

    EntropySource m_entropy_source;
    WaitQueue m_wait_queue;

    Mutex m_transfer_lock { "Am79C970" };
};

}
