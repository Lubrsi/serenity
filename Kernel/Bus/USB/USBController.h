//
// Created by lukew on 10/07/2021.
//

#pragma once

#include <AK/RefCounted.h>
#include <Kernel/KResult.h>
#include <Kernel/Bus/USB/USBDevice.h>
#include <Kernel/Bus/USB/USBTransfer.h>

namespace Kernel::USB {

class USBController : public RefCounted<USBController> {
public:
    virtual ~USBController() = default;

    virtual bool initialize() = 0;

    virtual bool reset() = 0;
    virtual void stop() = 0;
    virtual void start() = 0;

    virtual KResultOr<size_t> submit_control_transfer(Transfer&) = 0;

    virtual RefPtr<USB::Device> const get_device_at_port(USB::Device::PortNumber) = 0;
    virtual RefPtr<USB::Device> const get_device_from_address(u8) = 0;
};

}
