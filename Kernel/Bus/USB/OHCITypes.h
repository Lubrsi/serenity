//
// Created by lukew on 10/07/2021.
//

#pragma once

#include <AK/Types.h>

namespace Kernel::USB {

/* == Revision register == */
static constexpr u32 HC_REVISION_REVISION = 0xFF;

/* == Control register == */
// This determines the ratio of control endpoint descriptors over bulk endpoint descriptors.
static constexpr u32 HC_CONTROL_CONTROL_BULK_SERVICE_RATIO_ONE_TO_ONE = 0b00;
static constexpr u32 HC_CONTROL_CONTROL_BULK_SERVICE_RATIO_TWO_TO_ONE = 0b01;
static constexpr u32 HC_CONTROL_CONTROL_BULK_SERVICE_RATIO_THREE_TO_ONE = 0b10;
static constexpr u32 HC_CONTROL_CONTROL_BULK_SERVICE_RATIO_FOUR_TO_ONE = 0b11;

static constexpr u32 HC_CONTROL_PERIODIC_LIST_ENABLE = (1 << 2);
static constexpr u32 HC_CONTROL_ISOCHRONOUS_ENABLE = (1 << 3);
static constexpr u32 HC_CONTROL_CONTROL_LIST_ENABLE = (1 << 4);
static constexpr u32 HC_CONTROL_BULK_LIST_ENABLE = (1 << 5);

static constexpr u32 HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_USB_RESET = (0b00 << 6);
static constexpr u32 HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_USB_RESUME = (0b01 << 6);
static constexpr u32 HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_USB_OPERATIONAL = (0b10 << 6);
static constexpr u32 HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_USB_SUSPEND = (0b11 << 6);
static constexpr u32 HC_CONTROL_HOST_CONTROLLER_FUNCTIONAL_STATE_MASK = (0b11 << 6);

// This bit is used to determine if the controller is owned by SMM or not.
static constexpr u32 HC_CONTROL_INTERRUPT_ROUTING = (1 << 8);

static constexpr u32 HC_CONTROL_REMOTE_WAKEUP_CONNECTED = (1 << 9);
static constexpr u32 HC_CONTROL_REMOTE_WAKEUP_ENABLE = (1 << 10);

/* == Command Status register == */
static constexpr u32 HC_COMMAND_STATUS_HOST_CONTROLLER_RESET = (1 << 0);
static constexpr u32 HC_COMMAND_STATUS_CONTROL_LIST_FILLED = (1 << 1);
static constexpr u32 HC_COMMAND_STATUS_BULK_LIST_FILLED = (1 << 2);
static constexpr u32 HC_COMMAND_STATUS_OWNERSHIP_CHANGE_REQUEST = (1 << 3);

// NOTE: Cannot be written to.
static constexpr u32 HC_COMMAND_STATUS_SCHEDULING_OVERRUN_COUNT = (0b11 << 16);

/* == Interrupts. These are the same for Interrupt Enable, Disable and Status. == */
// Writing 1 to a bit in interrupt enable will enable the corresponding interrupt.
// Writing 1 to a bit in interrupt disable will disable the corresponding interrupt.
// Writing 1 to a bit in interrupt status will acknowledge the corresponding interrupt.
// Writing 0 to any of the interrupt bits in Interrupt Enable and Disable does nothing.

static constexpr u32 INTERRUPT_SCHEDULING_OVERRUN = (1 << 0);
static constexpr u32 INTERRUPT_WRITEBACK_DONE_HEAD = (1 << 1);
static constexpr u32 INTERRUPT_START_OF_FRAME = (1 << 2);
static constexpr u32 INTERRUPT_RESUME_DETECTED = (1 << 3);
static constexpr u32 INTERRUPT_UNRECOVERABLE_ERROR = (1 << 4);
static constexpr u32 INTERRUPT_FRAME_NUMBER_OVERFLOW = (1 << 5);
static constexpr u32 INTERRUPT_ROOT_HUB_STATUS_CHANGE = (1 << 6);
static constexpr u32 INTERRUPT_OWNERSHIP_CHANGE = (1 << 30);
static constexpr u32 INTERRUPT_ALL = INTERRUPT_SCHEDULING_OVERRUN | INTERRUPT_WRITEBACK_DONE_HEAD | INTERRUPT_START_OF_FRAME | INTERRUPT_RESUME_DETECTED | INTERRUPT_UNRECOVERABLE_ERROR | INTERRUPT_FRAME_NUMBER_OVERFLOW | INTERRUPT_ROOT_HUB_STATUS_CHANGE | INTERRUPT_OWNERSHIP_CHANGE;

// NOTE: This is only valid for Interrupt Enable and Disable.
// Writing 1 to this bit in Interrupt Enable will allow any enabled interrupts to fire.
// Writing 1 to this bit in Interrupt Enable will disallow any enabled interrupts from firing.
static constexpr u32 INTERRUPT_MASTER_INTERRUPT_ENABLE = (1 << 31);

/* == Frame Interval register == */
static constexpr u32 HC_FRAME_INTERVAL_FRAME_INTERVAL = (0x3FFF << 0);
static constexpr u32 HC_FRAME_INTERVAL_FS_LARGEST_DATA_PACKET = (0x7FFF << 16);
static constexpr u32 HC_FRAME_INTERVAL_FRAME_INTERVAL_TOGGLE = (1UL << 31);

/* == Frame Remaining register == */
// NOTE: This register cannot be written to.

static constexpr u32 HC_FRAME_REMAINING_FRAME_REMAINING = (0x3FFF << 0);
static constexpr u32 HC_FRAME_REMAINING_FRAME_REMAINING_TOGGLE = (1UL << 31);

/* == Frame Number register == */
// NOTE: This register cannot be written to.
static constexpr u32 HC_FRAME_NUMBER_FRAME_NUMBER = (0xFFFF << 0);

/* == Periodic Start register == */
static constexpr u32 HC_PERIODIC_START_PERIODIC_START = (0x3FFF << 0);

/* == Low Speed Threshold register == */
static constexpr u32 HC_LOW_SPEED_THRESHOLD_LOW_SPEED_THRESHOLD = (0xFFF << 0);

/* == Root Hub Descriptor A register == */

// NOTE: Cannot be written to.
static constexpr u32 HC_ROOT_DESCRIPTOR_A_NUMBER_OF_DOWNSTREAM_PORTS = (0xFF << 0);

// NOTE: This field is only valid when NO_POWER_SWITCHING is cleared.
// When this field is 1, each port is powered individually and thus can be controlled by global power via
// HC_ROOT_HUB_STATUS_WRITE_{CLEAR,SET}_GLOBAL_POWER and per-port power via HC_ROOT_HUB_PORT_STATUS_WRITE_{CLEAR,SET}_PORT_POWER.
// See also HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK.
// When this field is 0, all ports are powered at the same time.
static constexpr u32 HC_ROOT_DESCRIPTOR_A_POWER_SWITCHING_MODE = (1 << 8);

// When this field is 1, all ports are always powered on as long as the controller is powered.
// When this field is 0, ports are power switched. See HC_ROOT_DESCRIPTOR_A_POWER_SWITCHING_MODE above.
static constexpr u32 HC_ROOT_DESCRIPTOR_A_NO_POWER_SWITCHING = (1 << 9);

// NOTE: This field will always read as 0. It indicates that the root hub is not a compound device.
static constexpr u32 HC_ROOT_DESCRIPTOR_A_DEVICE_TYPE = (1 << 10);

// NOTE: This field is only valid if HC_ROOT_DESCRIPTOR_A_NO_OVER_CURRENT_PROTECTION is cleared.
static constexpr u32 HC_ROOT_DESCRIPTOR_A_OVER_CURRENT_PROTECTION_TYPE = (1 << 11);

// Returns the amount of time to wait before accessing a port after powering it on in units of 2 milliseconds.
// NOTE: Yes, this can be written to, but doesn't make sense to do. Be careful not to write into this.
static constexpr u32 HC_ROOT_DESCRIPTOR_A_POWER_ON_TO_POWER_GOOD_TIME = (1 << 24);

/* Root Hub Descriptor B register */

// NOTE: For all the device removable fields:
//       Cleared = Attached device is removable.
//       Set = Attached device is not removable.
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_1 = (1 << 1);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_2 = (1 << 2);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_3 = (1 << 3);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_4 = (1 << 4);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_5 = (1 << 5);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_6 = (1 << 6);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_7 = (1 << 7);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_8 = (1 << 8);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_9 = (1 << 9);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_10 = (1 << 10);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_11 = (1 << 11);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_12 = (1 << 12);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_13 = (1 << 13);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_14 = (1 << 14);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_PORT_15 = (1 << 15);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_DEVICE_REMOVABLE_ALL_PORTS = (0xFE << 0);

// NOTE: For all the port power control mask fields:
//       Cleared = Port power is controlled by HC_ROOT_HUB_PORT_STATUS_WRITE_{CLEAR,SET}_PORT_POWER.
//       Set = Port power is controlled by HC_ROOT_HUB_STATUS_WRITE_{CLEAR,SET}_GLOBAL_POWER.
//       These fields are only valid if HC_ROOT_DESCRIPTOR_A_POWER_SWITCHING_MODE is cleared.
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_1 = (1 << 17);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_2 = (1 << 18);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_3 = (1 << 19);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_4 = (1 << 20);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_5 = (1 << 21);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_6 = (1 << 22);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_7 = (1 << 23);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_8 = (1 << 24);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_9 = (1 << 25);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_10 = (1 << 26);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_11 = (1 << 27);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_12 = (1 << 28);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_13 = (1 << 29);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_14 = (1 << 30);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_PORT_15 = (1UL << 31);
static constexpr u32 HC_ROOT_DESCRIPTOR_B_PORT_POWER_CONTROL_MASK_ALL_PORTS = (0xFEUL << 16);

/* Root Hub Status register */

// NOTE: Always read as 0.
static constexpr u32 HC_ROOT_HUB_STATUS_READ_LOCAL_POWER_STATUS = (1 << 0);

// When written with 1, it turns off global power mode. Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_STATUS_WRITE_CLEAR_GLOBAL_POWER = (1 << 0);

// NOTE: Cannot be written to.
// NOTE: Always read as 0 if per-port overcurrent protection is implemented.
static constexpr u32 HC_ROOT_HUB_STATUS_OVER_CURRENT_INDICATOR = (1 << 1);

// When read, returns 1 if Connect Status Change is a remote wakeup event. 0 otherwise.
static constexpr u32 HC_ROOT_HUB_STATUS_READ_DEVICE_REMOTE_WAKEUP_ENABLE = (1 << 15);

// When written with a 1, enables device remote wakeup. Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_STATUS_WRITE_SET_REMOTE_WAKEUP_ENABLE = (1 << 15);

// NOTE: Always read as 0.
static constexpr u32 HC_ROOT_HUB_STATUS_READ_LOCAL_POWER_STATUS_CHANGE = (1 << 16);

// When written with 1, it turns on global power mode. Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_STATUS_WRITE_SET_GLOBAL_POWER = (1 << 16);

// When written with 1, the field is cleared. Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_STATUS_OVER_CURRENT_INDICATOR_CHANGE = (1 << 17);

// When written with a 1, disables device remote wakeup. Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_STATUS_WRITE_CLEAR_REMOTE_WAKEUP_ENABLE = (1UL << 31);

/* Root Hub Port Status register */

// On read, returns 0 if no device is connected, 1 otherwise.
// NOTE: Always reads as a 1 if the device is non-removable, as indicated in hc_root_descriptor_b.device_removable[port_number]
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_READ_CURRENT_CONNECT_STATUS = (1 << 0);

// When written with a 1, clears HC_ROOT_HUB_PORT_STATUS_READ_PORT_ENABLE_STATUS. Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_WRITE_CLEAR_PORT_ENABLE = (1 << 0);

// On read, returns 0 if the port is disabled, 1 otherwise.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_READ_PORT_ENABLE_STATUS = (1 << 1);

// When written with 1, sets HC_ROOT_HUB_PORT_STATUS_READ_PORT_ENABLE_STATUS if HC_ROOT_HUB_PORT_STATUS_READ_CURRENT_CONNECT_STATUS is 1.
// If HC_ROOT_HUB_PORT_STATUS_READ_CURRENT_CONNECT_STATUS is 0, it instead sets HC_ROOT_HUB_PORT_STATUS_CONNECT_STATUS_CHANGE to 1.
// This is to indicate to us that we tried to enable a disconnected port.
// Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_WRITE_SET_PORT_ENABLE = (1 << 1);

// On read, returns 0 if the port is not suspended, 1 otherwise.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_READ_PORT_SUSPEND_STATUS = (1 << 2);

// When written with 1, sets HC_ROOT_HUB_PORT_STATUS_READ_PORT_SUSPEND_STATUS if HC_ROOT_HUB_PORT_STATUS_READ_CURRENT_CONNECT_STATUS is 1.
// If HC_ROOT_HUB_PORT_STATUS_READ_CURRENT_CONNECT_STATUS is 0, it instead sets HC_ROOT_HUB_PORT_STATUS_CONNECT_STATUS_CHANGE to 1.
// This is to indicate to us that we tried to enable a suspended port.
// Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_WRITE_SET_PORT_SUSPEND = (1 << 2);

// On read, returns 0 if there is no overcurrent condition, 1 otherwise.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_READ_PORT_OVER_CURRENT_INDICATOR = (1 << 3);

// When written with 1, initiates a resume if and only if HC_ROOT_HUB_PORT_STATUS_READ_PORT_SUSPEND_STATUS is 1.
// Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_WRITE_CLEAR_SUSPEND_STATUS = (1 << 3);

// On read, returns 0 if the port reset signal is not active, 1 otherwise.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_READ_PORT_RESET_STATUS = (1 << 4);

// When written with 1, sets HC_ROOT_HUB_PORT_STATUS_READ_PORT_RESET_STATUS if HC_ROOT_HUB_PORT_STATUS_READ_CURRENT_CONNECT_STATUS is 1.
// If HC_ROOT_HUB_PORT_STATUS_READ_CURRENT_CONNECT_STATUS is 0, it instead sets HC_ROOT_HUB_PORT_STATUS_CONNECT_STATUS_CHANGE to 1.
// This is to indicate to us that we tried to reset a disconnected port.
// Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_WRITE_SET_PORT_RESET = (1 << 4);

// On read, returns 0 if port is off, 1 otherwise.
// NOTE: Always reads as a 1 if power switching is not supported.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_READ_PORT_POWER_STATUS = (1 << 8);

// When written with 1, sets HC_ROOT_HUB_PORT_STATUS_READ_PORT_POWER_STATUS. Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_WRITE_SET_PORT_POWER = (1 << 8);

// On read, returns 0 if full speed device, 1 if low speed device.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_READ_LOW_SPEED_DEVICE_ATTACHED = (1 << 9);

// When written with 1, clears HC_ROOT_HUB_PORT_STATUS_READ_PORT_POWER_STATUS. Writing 0 does nothing.
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_WRITE_CLEAR_PORT_POWER = (1 << 9);

static constexpr u32 HC_ROOT_HUB_PORT_STATUS_CONNECT_STATUS_CHANGE = (1 << 16);
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_PORT_ENABLE_STATUS_CHANGE = (1 << 17);
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_PORT_SUSPEND_STATUS_CHANGE = (1 << 18);
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_PORT_OVER_CURRENT_INDICATOR_CHANGE = (1 << 19);
static constexpr u32 HC_ROOT_HUB_PORT_STATUS_PORT_RESET_STATUS_CHANGE = (1 << 20);

struct [[gnu::packed]] OperationalRegisters {
    volatile u32 hc_revision;
    volatile u32 hc_control;
    volatile u32 hc_command_status;
    volatile u32 hc_interrupt_status;
    volatile u32 hc_interrupt_enable;
    volatile u32 hc_interrupt_disable;
    volatile u32 host_controller_communication_area_physical_address;
    volatile u32 current_periodic_interrupt_list_endpoint_descriptor_physical_address; // NOTE: Cannot be written to.
    volatile u32 first_endpoint_descriptor_of_control_list_physical_address;
    volatile u32 current_endpoint_descriptor_of_control_list_physical_address;
    volatile u32 first_endpoint_descriptor_of_bulk_list_physical_address;
    volatile u32 current_endpoint_descriptor_of_bulk_list_physical_address;
    volatile u32 last_completed_transfer_descriptor_physical_address; // NOTE: Cannot be written to.
    volatile u32 hc_frame_interval;
    volatile u32 hc_frame_remaining; // NOTE: Cannot be written to.
    volatile u32 hc_frame_number; // NOTE: Cannot be written to.
    volatile u32 hc_periodic_start;
    volatile u32 hc_low_speed_threshold;
    volatile u32 hc_root_descriptor_a;
    volatile u32 hc_root_descriptor_b;
    volatile u32 hc_root_hub_status;

    // NOTE: This is bounded by HC_ROOT_DESCRIPTOR_A_NUMBER_OF_DOWNSTREAM_PORTS.
    volatile u32 hc_root_hub_port_status[];
};
static_assert(sizeof(OperationalRegisters) == 0x54);

struct [[gnu::packed]] HostControllerCommunicationArea {
    volatile u32 endpoint_descriptor_interrupt_table[32];
    volatile u16 frame_number;
    volatile u16 pad_1;
    volatile u32 done_head;

    // NOTE: The spec says this is 116 bytes, but that wouldn't fill the 256 byte block (it would only be 252 bytes)
    //       Thus, this contains 4 extra bytes.
    volatile u8 reserved[120];
};
static_assert(sizeof(HostControllerCommunicationArea) == 256);


}
