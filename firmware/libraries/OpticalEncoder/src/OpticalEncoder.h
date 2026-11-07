#pragma once

#include <stdint.h>

inline int8_t decodeOpticalTransition(uint8_t previous, uint8_t current) {
    static const int8_t transitions[16] = {
         0, -1,  1,  2,
         1,  0,  2, -1,
        -1,  2,  0,  1,
         2,  1, -1,  0
    };
    return transitions[(previous << 2) | current];
}

class OpticalMovement {
public:
    void add(int32_t transitions, int32_t numerator, int32_t denominator,
             int32_t multiplier = 1) {
        const int64_t scaled = remainder
            + (int64_t)transitions * numerator * multiplier;
        pending += scaled / denominator;
        remainder = scaled % denominator;
    }

    int8_t nextReport() const {
        if (pending < -127) return -127;
        if (pending > 127) return 127;
        return (int8_t)pending;
    }

    void acknowledge(int8_t sent) {
        pending -= sent;
    }

private:
    int64_t pending = 0;
    int64_t remainder = 0;
};