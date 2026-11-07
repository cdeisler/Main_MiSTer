#pragma once

#ifndef SPINNER_PIN_PROFILE
#define SPINNER_PIN_PROFILE "kraken_micro_pins.h"
#endif
#include SPINNER_PIN_PROFILE

#define SPINNER_USB_DIAL 0
#define SPINNER_USB_MOUSE 1
#ifndef SPINNER_USB_MODE
#define SPINNER_USB_MODE SPINNER_USB_MOUSE
#endif

#ifndef SPINNER_MOUSE_AXIS
#define SPINNER_MOUSE_AXIS 0
#endif

#define HID_REPORT_INTERVAL_US 1000UL
#define CONTROL_DEBOUNCE_MS 5UL
#define SPINNER_INVERT 0
#define SPINNER_SCALE_NUMERATOR 1L
#define SPINNER_SCALE_DENOMINATOR 1L
#define SPINNER_ACCEL_THRESHOLD_1 0L
#define SPINNER_ACCEL_MULTIPLIER_1 1L
#define SPINNER_ACCEL_THRESHOLD_2 0L
#define SPINNER_ACCEL_MULTIPLIER_2 1L

static_assert(SPINNER_USB_MODE == SPINNER_USB_DIAL || SPINNER_USB_MODE == SPINNER_USB_MOUSE,
              "Choose Dial (0) or mouse (1)");
static_assert(SPINNER_MOUSE_AXIS == 0 || SPINNER_MOUSE_AXIS == 1,
              "Choose mouse X (0) or Y (1)");
static_assert(SPINNER_SCALE_NUMERATOR > 0, "Spinner scale numerator must be positive");
static_assert(SPINNER_SCALE_DENOMINATOR > 0, "Spinner scale denominator must be positive");