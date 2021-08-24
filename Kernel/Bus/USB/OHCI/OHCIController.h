/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Ptr32.h>
#include <Kernel/Bus/PCI/Device.h>
#include <Kernel/Bus/USB/OHCI/OHCIDescriptorPool.h>
#include <Kernel/Bus/USB/OHCI/OHCIRootHub.h>
#include <Kernel/Bus/USB/OHCI/OHCITypes.h>
#include <Kernel/Bus/USB/USBController.h>
#include <Kernel/Interrupts/IRQHandler.h>

namespace Kernel::USB {

class OHCIController final
    : public USBController
    , public PCI::Device
    , public IRQHandler {
public:
    static KResultOr<NonnullRefPtr<OHCIController>> try_to_initialize(PCI::Address address);
    virtual ~OHCIController() override;

    virtual KResult initialize() override;
    virtual KResult reset() override;
    virtual KResult stop() override;
    virtual KResult start() override;

    virtual StringView purpose() const override { return "OHCI"; }

    virtual KResultOr<size_t> submit_control_transfer(Transfer& transfer) override;

    USBHubDescriptor const& root_hub_descriptor() const { return m_root_hub_descriptor; }

    void get_hub_status(Badge<OHCIRootHub>, HubStatus&);
    void get_port_status(Badge<OHCIRootHub>, u8, HubStatus&);

    KResult clear_hub_feature(Badge<OHCIRootHub>, HubFeatureSelector);

    KResult set_port_feature(Badge<OHCIRootHub>, u8, HubFeatureSelector);
    KResult clear_port_feature(Badge<OHCIRootHub>, u8, HubFeatureSelector);

    virtual KResult pipe_created(Pipe const&) override;
    virtual KResult pipe_destroyed(Pipe const&) override;
    virtual KResult pipe_changed(Pipe const&) override;

private:
    explicit OHCIController(PCI::Address);

    virtual bool handle_irq(RegisterState const&) override;

    void create_root_hub_descriptor();
    void spawn_port_proc();

    OHCIGeneralTransferDescriptor* create_general_transfer_descriptor(Pipe&, GTDDirection);
    KResult create_general_chain(Pipe&, GTDDirection, Ptr32<u8>&, size_t, size_t, OHCIGeneralTransferDescriptor**, OHCIGeneralTransferDescriptor**);
    void free_general_transfer_descriptor_chain(OHCIGeneralTransferDescriptor*);

    OHCIEndpointDescriptor* get_endpoint_descriptor_for_pipe(Pipe const&);
    size_t poll_transfer_queue(Transfer&, OHCIEndpointDescriptor*, OHCIGeneralTransferDescriptor*);

    OwnPtr<Memory::Region> m_operational_registers_region;
    OwnPtr<Memory::Region> m_host_controller_communication_area_region;
    OwnPtr<OHCIDescriptorPool<OHCIEndpointDescriptor>> m_endpoint_descriptor_pool;
    OwnPtr<OHCIDescriptorPool<OHCIGeneralTransferDescriptor>> m_general_transfer_descriptor_pool;

    OHCIEndpointDescriptor* m_control_endpoints_head { nullptr };

    OwnPtr<OHCIRootHub> m_root_hub;
    USBHubDescriptor m_root_hub_descriptor;
    WaitQueue m_root_hub_status_wait_queue;

    Atomic<bool> m_received_start_of_frame_interrupt { false };
};

}
