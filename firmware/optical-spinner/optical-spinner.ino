#include <Arduino.h>
#include <OpticalEncoder.h>
#include <util/atomic.h>
#include "spinner_config.h"
#include "spinner_usb.h"

static const uint8_t controlPins[] = {
    JOYSTICK_UP_PIN,
    JOYSTICK_DOWN_PIN,
    JOYSTICK_LEFT_PIN,
    JOYSTICK_RIGHT_PIN,
    BUTTON_1_PIN,
    BUTTON_2_PIN,
    BUTTON_3_PIN,
};

enum ControlIndex {
    CONTROL_UP,
    CONTROL_DOWN,
    CONTROL_LEFT,
    CONTROL_RIGHT,
    CONTROL_BUTTON_1,
    CONTROL_BUTTON_2,
    CONTROL_BUTTON_3,
    CONTROL_COUNT
};

static volatile uint8_t *encoderInputRegister;
static uint8_t encoderMaskA;
static uint8_t encoderMaskB;
static volatile uint8_t encoderState;
static volatile int32_t rawTransitions;
static volatile uint32_t validTransitions;
static volatile uint32_t invalidTransitions;

static bool stableControls[CONTROL_COUNT];
static bool candidateControls[CONTROL_COUNT];
static uint32_t candidateSinceMs[CONTROL_COUNT];

static OpticalMovement movement;
static ControllerReport lastSentReport;
static bool reportSent;

static uint8_t readEncoderState() {
    const uint8_t levels = *encoderInputRegister;
    const uint8_t channelA = (levels & encoderMaskA) != 0;
    const uint8_t channelB = (levels & encoderMaskB) != 0;
    return (channelA << 1) | channelB;
}

static void captureEncoderTransition() {
    const uint8_t state = readEncoderState();
    const int8_t transition = decodeOpticalTransition(encoderState, state);
    encoderState = state;

    if (transition == 2) {
        invalidTransitions++;
    } else if (transition != 0) {
        rawTransitions += SPINNER_INVERT ? -transition : transition;
        validTransitions++;
    }
}

static void encoderInterruptA() {
    captureEncoderTransition();
}

static void encoderInterruptB() {
    captureEncoderTransition();
}

static bool updateControls(uint32_t nowMs) {
    bool changed = false;

    for (uint8_t index = 0; index < CONTROL_COUNT; index++) {
        const bool pressed = digitalRead(controlPins[index]) == LOW;
        if (pressed != candidateControls[index]) {
            candidateControls[index] = pressed;
            candidateSinceMs[index] = nowMs;
        } else if (
            pressed != stableControls[index]
            && (uint32_t)(nowMs - candidateSinceMs[index]) >= CONTROL_DEBOUNCE_MS
        ) {
            stableControls[index] = pressed;
            changed = true;
        }
    }

    return changed;
}

static int32_t accelerationMultiplier(int32_t transitions) {
    const uint32_t speed = transitions < 0
        ? (uint32_t)(-(int64_t)transitions)
        : (uint32_t)transitions;

    if (SPINNER_ACCEL_THRESHOLD_2 > 0 && speed >= SPINNER_ACCEL_THRESHOLD_2) {
        return SPINNER_ACCEL_MULTIPLIER_2;
    }
    if (SPINNER_ACCEL_THRESHOLD_1 > 0 && speed >= SPINNER_ACCEL_THRESHOLD_1) {
        return SPINNER_ACCEL_MULTIPLIER_1;
    }
    return 1;
}

static void processEncoderMovement() {
    int32_t transitions;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        transitions = rawTransitions;
        rawTransitions = 0;
    }

    if (transitions == 0) {
        return;
    }

    movement.add(transitions, SPINNER_SCALE_NUMERATOR,
                 SPINNER_SCALE_DENOMINATOR, accelerationMultiplier(transitions));
}

static ControllerReport makeReport() {
    ControllerReport report = {};

#if SPINNER_USB_MODE == SPINNER_USB_DIAL
    if (stableControls[CONTROL_LEFT] != stableControls[CONTROL_RIGHT]) {
        report.x = stableControls[CONTROL_LEFT] ? -127 : 127;
    }
    if (stableControls[CONTROL_UP] != stableControls[CONTROL_DOWN]) {
        report.y = stableControls[CONTROL_UP] ? -127 : 127;
    }
#endif

    report.dial = movement.nextReport();

    for (uint8_t index = 0; index < 3; index++) {
        if (stableControls[CONTROL_BUTTON_1 + index]) {
            report.buttons |= (uint8_t)(1u << index);
        }
    }

    return report;
}

static bool controlsDiffer(
    const ControllerReport &left,
    const ControllerReport &right
) {
    return left.x != right.x
        || left.y != right.y
        || left.buttons != right.buttons;
}

void setup() {
    beginSpinnerUsb();
    const uint8_t portA = digitalPinToPort(ENCODER_PIN_A);
    const uint8_t portB = digitalPinToPort(ENCODER_PIN_B);
    if (portA == NOT_A_PIN || portA != portB
        || digitalPinToInterrupt(ENCODER_PIN_A) == NOT_AN_INTERRUPT
        || digitalPinToInterrupt(ENCODER_PIN_B) == NOT_AN_INTERRUPT) {
        while (true) {
        }
    }

    pinMode(ENCODER_PIN_A, INPUT);
    pinMode(ENCODER_PIN_B, INPUT);
    encoderInputRegister = portInputRegister(portA);
    encoderMaskA = digitalPinToBitMask(ENCODER_PIN_A);
    encoderMaskB = digitalPinToBitMask(ENCODER_PIN_B);
    encoderState = readEncoderState();

    const uint32_t nowMs = millis();
    for (uint8_t index = 0; index < CONTROL_COUNT; index++) {
        pinMode(controlPins[index], INPUT_PULLUP);
        const bool pressed = digitalRead(controlPins[index]) == LOW;
        stableControls[index] = pressed;
        candidateControls[index] = pressed;
        candidateSinceMs[index] = nowMs;
    }

    attachInterrupt(
        digitalPinToInterrupt(ENCODER_PIN_A),
        encoderInterruptA,
        CHANGE
    );
    attachInterrupt(
        digitalPinToInterrupt(ENCODER_PIN_B),
        encoderInterruptB,
        CHANGE
    );
}

void loop() {
    static uint32_t nextReportUs = micros();

    updateControls(millis());
    const uint32_t nowUs = micros();
    if ((int32_t)(nowUs - nextReportUs) < 0) {
        return;
    }
    nextReportUs = nowUs + HID_REPORT_INTERVAL_US;

    processEncoderMovement();
    const ControllerReport report = makeReport();
    if (
        reportSent
        && report.dial == 0
        && !controlsDiffer(report, lastSentReport)
    ) {
        return;
    }

    if (sendSpinnerReport(report)) {
        movement.acknowledge(report.dial);
        lastSentReport = report;
        lastSentReport.dial = 0;
        reportSent = true;
    }
}