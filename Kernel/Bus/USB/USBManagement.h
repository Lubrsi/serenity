//
// Created by lukew on 10/07/2021.
//

#include <AK/NonnullRefPtr.h>
#include <AK/NonnullRefPtrVector.h>
#include <Kernel/Bus/USB/USBController.h>

namespace Kernel::USB {

class USBManagement {
    AK_MAKE_ETERNAL;

public:
    USBManagement();
    static bool initialized();
    static void initialize();
    static USBManagement& the();

private:
    NonnullRefPtrVector<USBController> enumerate_controllers() const;

    NonnullRefPtrVector<USBController> m_controllers;
};

}
