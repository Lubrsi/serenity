//
// Created by lukew on 10/07/2021.
//

#pragma once

#include <Kernel/Bus/PCI/Device.h>
#include <Kernel/Bus/USB/USBController.h>

namespace Kernel::USB {

class OHCIController final
    : public USBController
    , public PCI::Device {
public:
    static RefPtr<OHCIController> try_to_initialize(PCI::Address address);
    virtual ~OHCIController() override;

    virtual bool initialize() override;
    virtual bool reset() override;
    virtual void stop() override;
    virtual void start() override;

    virtual StringView purpose() const override { return "OHCI"; }

    virtual KResultOr<size_t> submit_control_transfer(Transfer& transfer) override;

    virtual RefPtr<USB::Device> const get_device_at_port(USB::Device::PortNumber) override;
    virtual RefPtr<USB::Device> const get_device_from_address(u8 device_address) override;

private:
    explicit OHCIController(PCI::Address);

    virtual bool handle_irq(RegisterState const&) override;

    OwnPtr<Region> m_operational_registers_region;
    OwnPtr<Region> m_host_controller_communication_area_region;
};

}
