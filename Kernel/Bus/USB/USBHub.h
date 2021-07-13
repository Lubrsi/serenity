//
// Created by lukew on 11/07/2021.
//

#include <AK/Types.h>

namespace Kernel::USB {

// Hub Feature Selectors
// USB 2.0 Spec Pages 421-422 Table 11-17
enum class HubFeature : u16 {
    C_HUB_LOCAL_POWER = 0,
    C_HUB_OVER_CURRENT = 1,
    PORT_CONNECTION = 0,
    PORT_ENABLE = 1,
    PORT_SUSPEND = 2,
    PORT_OVER_CURRENT = 3,
    PORT_RESET = 4,
    PORT_POWER = 8,
    PORT_LOW_SPEED = 9,
    C_PORT_CONNECTION = 16,
    C_PORT_ENABLE = 17,
    C_PORT_SUSPEND = 18,
    C_PORT_OVER_CURRENT = 19,
    C_PORT_RESET = 20,
    PORT_TEST = 21,
    PORT_INDICATOR = 22,
};

}
