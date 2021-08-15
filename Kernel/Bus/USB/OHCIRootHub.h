/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullOwnPtr.h>
#include <AK/NonnullRefPtr.h>
#include <Kernel/KResult.h>
#include <Kernel/Bus/USB/USBHub.h>
#include <Kernel/Bus/USB/USBTransfer.h>

namespace Kernel::USB {

class OHCIController;

class OHCIRootHub {
public:
    static KResultOr<NonnullOwnPtr<OHCIRootHub>> try_create(NonnullRefPtr<OHCIController>);

    OHCIRootHub(NonnullRefPtr<OHCIController>);
    ~OHCIRootHub() = default;

    KResult setup(Badge<OHCIController>);

    u8 device_address() const { return m_hub->address(); }

    KResultOr<size_t> handle_control_transfer(Transfer& transfer);

    void check_for_port_updates() { m_hub->check_for_port_updates(); }

private:
    NonnullRefPtr<OHCIController> m_ohci_controller;
    RefPtr<Hub> m_hub;
};

}
