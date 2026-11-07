#include "../libraries/OpticalEncoder/src/OpticalEncoder.h"
#include <assert.h>
#include <stdio.h>

int main() {
    const int8_t expected[16] = {
         0, -1,  1,  2,
         1,  0,  2, -1,
        -1,  2,  0,  1,
         2,  1, -1,  0
    };
    for (uint8_t previous = 0; previous < 4; previous++) {
        for (uint8_t current = 0; current < 4; current++) {
            assert(decodeOpticalTransition(previous, current)
                   == expected[(previous << 2) | current]);
        }
    }

    const uint8_t clockwise[] = {0, 2, 3, 1, 0};
    int32_t count = 0;
    for (uint8_t index = 1; index < sizeof(clockwise); index++) {
        count += decodeOpticalTransition(clockwise[index - 1], clockwise[index]);
    }
    assert(count == 4);
    for (uint8_t index = sizeof(clockwise) - 1; index > 0; index--) {
        count += decodeOpticalTransition(clockwise[index], clockwise[index - 1]);
    }
    assert(count == 0);
    assert(decodeOpticalTransition(0, 3) == 2);
    assert(decodeOpticalTransition(2, 2) == 0);

    OpticalMovement burst;
    burst.add(300, 1, 1);
    assert(burst.nextReport() == 127);
    assert(burst.nextReport() == 127);
    burst.acknowledge(127);
    assert(burst.nextReport() == 127);
    burst.acknowledge(127);
    assert(burst.nextReport() == 46);
    burst.acknowledge(46);
    assert(burst.nextReport() == 0);
    burst.add(-300, 1, 1);
    burst.acknowledge(-127);
    burst.acknowledge(-127);
    assert(burst.nextReport() == -46);
    burst.acknowledge(-46);
    assert(burst.nextReport() == 0);

    OpticalMovement fraction;
    fraction.add(1, 1, 3);
    fraction.add(1, 1, 3);
    assert(fraction.nextReport() == 0);
    fraction.add(1, 1, 3);
    assert(fraction.nextReport() == 1);
    fraction.acknowledge(1);
    fraction.add(-1, 1, 3);
    fraction.add(-1, 1, 3);
    assert(fraction.nextReport() == 0);
    fraction.add(-1, 1, 3);
    assert(fraction.nextReport() == -1);
    fraction.acknowledge(-1);
    fraction.add(1, 1, 3);
    fraction.add(-1, 1, 3);
    assert(fraction.nextReport() == 0);

    OpticalMovement accelerated;
    accelerated.add(20, 3, 2, 2);
    assert(accelerated.nextReport() == 60);
    accelerated.acknowledge(60);
    assert(accelerated.nextReport() == 0);

    OpticalMovement wide;
    wide.add(INT32_MAX, 2, 1);
    assert(wide.nextReport() == 127);
    wide.add(-INT32_MAX, 2, 1);
    assert(wide.nextReport() == 0);

    puts("Optical decoder and movement tests passed.");
}