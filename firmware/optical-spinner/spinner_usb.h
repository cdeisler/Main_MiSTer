#pragma once

#include <HID.h>
#include "spinner_config.h"

struct __attribute__((packed)) ControllerReport {
    int8_t x;
    int8_t y;
    int8_t dial;
    uint8_t buttons;
};

#if SPINNER_USB_MODE == SPINNER_USB_DIAL
#define HID_REPORT_ID 4

static const uint8_t controllerHidDescriptor[] PROGMEM = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x04,        // Usage (Joystick)
    0xA1, 0x01,        // Collection (Application)
    0x85, HID_REPORT_ID,

    0x09, 0x01,        // Collection usage (Pointer)
    0xA1, 0x00,        // Collection (Physical)
    0x09, 0x30,        // Usage (X)
    0x09, 0x31,        // Usage (Y)
    0x15, 0x81,        // Logical Minimum (-127)
    0x25, 0x7F,        // Logical Maximum (127)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x02,        // Report Count (2)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0xC0,              // End Collection

    0x09, 0x37,        // Usage (Dial)
    0x15, 0x81,        // Logical Minimum (-127)
    0x25, 0x7F,        // Logical Maximum (127)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x06,        // Input (Data, Variable, Relative)

    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01,        // Usage Minimum (Button 1)
    0x29, 0x03,        // Usage Maximum (Button 3)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x03,        // Report Count (3)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0x75, 0x05,        // Report Size (5)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x03,        // Input (Constant)
    0xC0               // End Collection
};

static HIDSubDescriptor controllerHidSubDescriptor(
    controllerHidDescriptor, sizeof(controllerHidDescriptor)
);

class ControllerHid {
public:
    ControllerHid() {
        HID().AppendDescriptor(&controllerHidSubDescriptor);
    }
};

static ControllerHid controllerHid;

inline void beginSpinnerUsb() {}

inline bool sendSpinnerReport(const ControllerReport &report) {
    return HID().SendReport(HID_REPORT_ID, &report, sizeof(report))
        == (int)(sizeof(report) + 1);
}
#else
#include <Mouse.h>

inline void beginSpinnerUsb() {
    Mouse.begin();
}

inline bool sendSpinnerReport(const ControllerReport &report) {
    const uint8_t payload[4] = {
        report.buttons,
        (uint8_t)(SPINNER_MOUSE_AXIS == 0 ? report.dial : 0),
        (uint8_t)(SPINNER_MOUSE_AXIS == 1 ? report.dial : 0),
        0
    };
    return HID().SendReport(1, payload, sizeof(payload))
        == (int)(sizeof(payload) + 1);
}
#endif