//
// Created by lukew on 12/07/2021.
//

#include <Kernel/Bus/USB/USBController.h>

namespace Kernel::USB {

u8 USBController::allocate_address()
{
    return m_next_device_index++;
}

}
