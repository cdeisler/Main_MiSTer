#include <Arduino.h>
#include <OpticalEncoder.h>

#ifndef SPINNER_PIN_PROFILE
#define SPINNER_PIN_PROFILE "kraken_micro_pins.h"
#endif
#include SPINNER_PIN_PROFILE

#define SAMPLE_INTERVAL_US 20UL
#define REPORT_INTERVAL_US 1000000UL
#define SEQUENCE_CAPACITY 64

static volatile uint8_t *encoderInputRegister;
static uint8_t encoderMaskA;
static uint8_t encoderMaskB;
static uint8_t encoderState;
static int32_t encoderCount;
static uint32_t validTransitions;
static uint32_t invalidTransitions;

static uint32_t stateSamples[4];
static uint32_t transitions[16];
static uint8_t sequence[SEQUENCE_CAPACITY];
static uint8_t sequenceLength;
static uint32_t sequenceOmitted;
static uint32_t samples;
static uint32_t changes;
static uint32_t skippedPeriods;

static uint8_t readEncoderState() {
    const uint8_t levels = *encoderInputRegister;
    const uint8_t channelA = (levels & encoderMaskA) != 0;
    const uint8_t channelB = (levels & encoderMaskB) != 0;
    return (channelA << 1) | channelB;
}

static void updateDecoder(uint8_t state) {
    const int8_t transition = decodeOpticalTransition(encoderState, state);
    encoderState = state;

    if (transition == 2) {
        invalidTransitions++;
    } else if (transition != 0) {
        encoderCount += transition;
        validTransitions++;
    }
}

static void printReport(uint32_t previousValid, uint32_t previousInvalid) {
    Serial.print(F("# sample_window,rate_hz="));
    Serial.print(1000000UL / SAMPLE_INTERVAL_US);
    Serial.print(F(",samples="));
    Serial.print(samples);
    Serial.print(F(",changes="));
    Serial.print(changes);
    Serial.print(F(",count="));
    Serial.print(encoderCount);
    Serial.print(F(",valid="));
    Serial.print(validTransitions - previousValid);
    Serial.print(F(",invalid="));
    Serial.print(invalidTransitions - previousInvalid);
    Serial.print(F(",skipped_periods="));
    Serial.println(skippedPeriods);

    Serial.print(F("# state_samples,00="));
    Serial.print(stateSamples[0]);
    Serial.print(F(",01="));
    Serial.print(stateSamples[1]);
    Serial.print(F(",10="));
    Serial.print(stateSamples[2]);
    Serial.print(F(",11="));
    Serial.println(stateSamples[3]);

    static const uint8_t fromStates[12] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3};
    static const uint8_t toStates[12] =   {1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 2};
    Serial.print(F("# transitions"));
    for (uint8_t index = 0; index < 12; index++) {
        Serial.print(',');
        Serial.print(fromStates[index], BIN);
        Serial.print('>');
        Serial.print(toStates[index], BIN);
        Serial.print('=');
        Serial.print(transitions[(fromStates[index] << 2) | toStates[index]]);
    }
    Serial.println();

    Serial.print(F("# sequence"));
    for (uint8_t index = 0; index < sequenceLength; index++) {
        Serial.print(',');
        Serial.print(sequence[index]);
    }
    Serial.print(F(",omitted="));
    Serial.println(sequenceOmitted);
}

static void clearWindow() {
    memset(stateSamples, 0, sizeof(stateSamples));
    memset(transitions, 0, sizeof(transitions));
    sequenceLength = 0;
    sequenceOmitted = 0;
    samples = 0;
    changes = 0;
    skippedPeriods = 0;
}

void setup() {
    const uint8_t portA = digitalPinToPort(ENCODER_PIN_A);
    const uint8_t portB = digitalPinToPort(ENCODER_PIN_B);
    if (portA == NOT_A_PIN || portA != portB) {
        while (true) {
        }
    }

    pinMode(ENCODER_PIN_A, INPUT);
    pinMode(ENCODER_PIN_B, INPUT);
    encoderInputRegister = portInputRegister(portA);
    encoderMaskA = digitalPinToBitMask(ENCODER_PIN_A);
    encoderMaskB = digitalPinToBitMask(ENCODER_PIN_B);
    encoderState = readEncoderState();

    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
    }

    Serial.println(F("# Optical encoder Arduino Micro fixed-rate diagnostic"));
    Serial.print(F("# pin_a="));
    Serial.print(ENCODER_PIN_A);
    Serial.print(F(",pin_b="));
    Serial.print(ENCODER_PIN_B);
    Serial.print(F(",sample_interval_us="));
    Serial.print(SAMPLE_INTERVAL_US);
    Serial.print(F(",rate_hz="));
    Serial.println(1000000UL / SAMPLE_INTERVAL_US);
    Serial.println(F("# state_encoding,0=00,1=01,2=10,3=11"));
}

void loop() {
    static uint32_t nextSample = micros();
    static uint32_t reportStart = nextSample;
    static uint32_t previousValid;
    static uint32_t previousInvalid;

    const uint32_t now = micros();
    if ((int32_t)(now - nextSample) < 0) {
        return;
    }

    if ((uint32_t)(now - nextSample) >= SAMPLE_INTERVAL_US) {
        skippedPeriods += (now - nextSample) / SAMPLE_INTERVAL_US;
        nextSample = now;
    }
    nextSample += SAMPLE_INTERVAL_US;

    const uint8_t state = readEncoderState();
    stateSamples[state]++;
    samples++;

    if (state != encoderState) {
        const uint8_t previousState = encoderState;
        transitions[(previousState << 2) | state]++;
        updateDecoder(state);
        changes++;

        if (sequenceLength < SEQUENCE_CAPACITY) {
            sequence[sequenceLength++] = state;
        } else {
            sequenceOmitted++;
        }
    }

    if ((uint32_t)(now - reportStart) >= REPORT_INTERVAL_US) {
        printReport(previousValid, previousInvalid);
        previousValid = validTransitions;
        previousInvalid = invalidTransitions;
        clearWindow();
        reportStart = micros();
        nextSample = reportStart + SAMPLE_INTERVAL_US;
    }
}