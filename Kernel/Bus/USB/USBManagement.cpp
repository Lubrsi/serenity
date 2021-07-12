//
// Created by lukew on 10/07/2021.
//

#include <Kernel/Bus/PCI/Access.h>
#include <Kernel/Bus/USB/USBManagement.h>
#include <Kernel/Sections.h>
#include <Kernel/CommandLine.h>
#include <Kernel/Bus/USB/UHCIController.h>
#include <Kernel/Bus/USB/OHCIController.h>

namespace Kernel::USB {

static USBManagement* s_the;

UNMAP_AFTER_INIT USBManagement::USBManagement()
    : m_controllers(enumerate_controllers())
{
}

UNMAP_AFTER_INIT NonnullRefPtrVector<USBController> USBManagement::enumerate_controllers() const
{
    NonnullRefPtrVector<USBController> controllers;

    if (!kernel_command_line().disable_usb()) {
        PCI::enumerate([&controllers](const PCI::Address& address, PCI::ID) {
            if (PCI::get_class(address) == 0xc && PCI::get_subclass(address) == 0x3) {
                if (PCI::get_programming_interface(address) == 0x0) {
                    if (kernel_command_line().disable_uhci_controller())
                        return;

                    if (auto uhci_controller = UHCIController::try_to_initialize(address); !uhci_controller.is_null())
                        controllers.append(uhci_controller.release_nonnull());

                    return;
                }

                if (PCI::get_programming_interface(address) == 0x10) {
                    if (kernel_command_line().disable_ohci_controller())
                        return;

                    if (auto ohci_controller = OHCIController::try_to_initialize(address); !ohci_controller.is_null())
                        controllers.append(ohci_controller.release_nonnull());

                    return;
                }

                if (PCI::get_programming_interface(address) == 0x20) {
                    dmesgln("USBManagement: EHCI controller found at {} is not currently supported.", address);
                    return;
                }

                if (PCI::get_programming_interface(address) == 0x30) {
                    dmesgln("USBManagement: xHCI controller found at {} is not currently supported.", address);
                    return;
                }

                dmesgln("USBManagement: Unknown/unsupported controller at {} with programming interface 0x{:02x}", address, PCI::get_programming_interface(address));
            }
        });
    }

    return controllers;
}

bool USBManagement::initialized()
{
    return (s_the != nullptr);
}

UNMAP_AFTER_INIT void USBManagement::initialize()
{
    VERIFY(!USBManagement::initialized());
    s_the = new USBManagement();
}

USBManagement& USBManagement::the()
{
    return *s_the;
}

}
