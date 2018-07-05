/*
 * Fadecandy Firmware: Low-level draw buffer update code
 * (Included into fadecandy.cpp)
 * 
 * Copyright (c) 2013 Micah Elizabeth Scott
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define MAP(x) MAP_P##x
#define OUT__(mx, y) p##mx##y
#define OUT_(mx, y) OUT__(mx, y)
#define OUT(n, x, y) o##n.OUT_(MAP(x), y)

static void FCP_FN(updateDrawBuffer)(unsigned interpCoefficient)
{
    /*
     * Update the LED draw buffer. In one step, we do the interpolation,
     * gamma correction, dithering, and we convert packed-pixel data to the
     * planar format used for OctoWS2811 DMAs.
     *
     * "interpCoefficient" indicates how far between fbPrev and fbNext
     * we are. It is a fixed point value in the range [0x0000, 0x10000],
     * corresponding to 100% fbPrev and 100% fbNext, respectively.
     */

    // For each pixel, this is a 24-byte stream of bits (6 words)
    uint32_t *out = (uint32_t*) leds.getDrawBuffer();

    /*
     * Interpolation coefficients, including a multiply by 257 to convert 8-bit color to 16-bit color.
     * You'd think that it would save clock cycles to calculate icPrev in updatePixel(), but this doesn't
     * seem to be the case.
     *
     * icPrev in range [0, 0x1010000]
     * icNext in range [0, 0x1010000]
     * icPrev + icNext = 0x1010000
     */

    uint32_t icPrev = 257 * (0x10000 - interpCoefficient);
    uint32_t icNext = 257 * interpCoefficient;

    /*
     * Pointer to the residual buffer for this pixel. Calculating this here rather than in updatePixel
     * saves a lot of clock cycles, since otherwise updatePixel() immediately needs to do a load from
     * constant pool and some multiplication.
     */

    residual_t *pResidual = residual;

    for (int i = 0; i < LEDS_PER_STRIP; ++i, pResidual += 3) {

        // Six output words
        union {
            uint32_t word;
            struct {
                uint32_t p0a:1, p1a:1, p2a:1, p3a:1, p4a:1, p5a:1, p6a:1, p7a:1,
                         p0b:1, p1b:1, p2b:1, p3b:1, p4b:1, p5b:1, p6b:1, p7b:1,
                         p0c:1, p1c:1, p2c:1, p3c:1, p4c:1, p5c:1, p6c:1, p7c:1,
                         p0d:1, p1d:1, p2d:1, p3d:1, p4d:1, p5d:1, p6d:1, p7d:1;
            };
        } o0, o1, o2, o3, o4, o5;

        /*
         * Remap bits.
         *
         * This generates compact and efficient code using the BFI instruction.
         */

        uint32_t p0 = FCP_FN(updatePixel)(icPrev, icNext,
            buffers.fbPrev->pixel(i + LEDS_PER_STRIP * 0),
            buffers.fbNext->pixel(i + LEDS_PER_STRIP * 0),
            pResidual + LEDS_PER_STRIP * 3 * 0);

        OUT(5, 0, d) = p0;
        OUT(5, 0, c) = p0 >> 1;
        OUT(5, 0, b) = p0 >> 2;
        OUT(5, 0, a) = p0 >> 3;
        OUT(4, 0, d) = p0 >> 4;
        OUT(4, 0, c) = p0 >> 5;
        OUT(4, 0, b) = p0 >> 6;
        OUT(4, 0, a) = p0 >> 7;
        OUT(3, 0, d) = p0 >> 8;
        OUT(3, 0, c) = p0 >> 9;
        OUT(3, 0, b) = p0 >> 10;
        OUT(3, 0, a) = p0 >> 11;
        OUT(2, 0, d) = p0 >> 12;
        OUT(2, 0, c) = p0 >> 13;
        OUT(2, 0, b) = p0 >> 14;
        OUT(2, 0, a) = p0 >> 15;
        OUT(1, 0, d) = p0 >> 16;
        OUT(1, 0, c) = p0 >> 17;
        OUT(1, 0, b) = p0 >> 18;
        OUT(1, 0, a) = p0 >> 19;
        OUT(0, 0, d) = p0 >> 20;
        OUT(0, 0, c) = p0 >> 21;
        OUT(0, 0, b) = p0 >> 22;
        OUT(0, 0, a) = p0 >> 23;

#if LED_STRIPS >= 2
        uint32_t p1 = FCP_FN(updatePixel)(icPrev, icNext,
            buffers.fbPrev->pixel(i + LEDS_PER_STRIP * 1),
            buffers.fbNext->pixel(i + LEDS_PER_STRIP * 1),
            pResidual + LEDS_PER_STRIP * 3 * 1);

        OUT(5, 1, d) = p1;
        OUT(5, 1, c) = p1 >> 1;
        OUT(5, 1, b) = p1 >> 2;
        OUT(5, 1, a) = p1 >> 3;
        OUT(4, 1, d) = p1 >> 4;
        OUT(4, 1, c) = p1 >> 5;
        OUT(4, 1, b) = p1 >> 6;
        OUT(4, 1, a) = p1 >> 7;
        OUT(3, 1, d) = p1 >> 8;
        OUT(3, 1, c) = p1 >> 9;
        OUT(3, 1, b) = p1 >> 10;
        OUT(3, 1, a) = p1 >> 11;
        OUT(2, 1, d) = p1 >> 12;
        OUT(2, 1, c) = p1 >> 13;
        OUT(2, 1, b) = p1 >> 14;
        OUT(2, 1, a) = p1 >> 15;
        OUT(1, 1, d) = p1 >> 16;
        OUT(1, 1, c) = p1 >> 17;
        OUT(1, 1, b) = p1 >> 18;
        OUT(1, 1, a) = p1 >> 19;
        OUT(0, 1, d) = p1 >> 20;
        OUT(0, 1, c) = p1 >> 21;
        OUT(0, 1, b) = p1 >> 22;
        OUT(0, 1, a) = p1 >> 23;
#endif

#if LED_STRIPS >= 3
        uint32_t p2 = FCP_FN(updatePixel)(icPrev, icNext,
            buffers.fbPrev->pixel(i + LEDS_PER_STRIP * 2),
            buffers.fbNext->pixel(i + LEDS_PER_STRIP * 2),
            pResidual + LEDS_PER_STRIP * 3 * 2);

        OUT(5, 2, d) = p2;
        OUT(5, 2, c) = p2 >> 1;
        OUT(5, 2, b) = p2 >> 2;
        OUT(5, 2, a) = p2 >> 3;
        OUT(4, 2, d) = p2 >> 4;
        OUT(4, 2, c) = p2 >> 5;
        OUT(4, 2, b) = p2 >> 6;
        OUT(4, 2, a) = p2 >> 7;
        OUT(3, 2, d) = p2 >> 8;
        OUT(3, 2, c) = p2 >> 9;
        OUT(3, 2, b) = p2 >> 10;
        OUT(3, 2, a) = p2 >> 11;
        OUT(2, 2, d) = p2 >> 12;
        OUT(2, 2, c) = p2 >> 13;
        OUT(2, 2, b) = p2 >> 14;
        OUT(2, 2, a) = p2 >> 15;
        OUT(1, 2, d) = p2 >> 16;
        OUT(1, 2, c) = p2 >> 17;
        OUT(1, 2, b) = p2 >> 18;
        OUT(1, 2, a) = p2 >> 19;
        OUT(0, 2, d) = p2 >> 20;
        OUT(0, 2, c) = p2 >> 21;
        OUT(0, 2, b) = p2 >> 22;
        OUT(0, 2, a) = p2 >> 23;
#endif

#if LED_STRIPS >= 4
        uint32_t p3 = FCP_FN(updatePixel)(icPrev, icNext,
            buffers.fbPrev->pixel(i + LEDS_PER_STRIP * 3),
            buffers.fbNext->pixel(i + LEDS_PER_STRIP * 3),
            pResidual + LEDS_PER_STRIP * 3 * 3);

        OUT(5, 3, d) = p3;
        OUT(5, 3, c) = p3 >> 1;
        OUT(5, 3, b) = p3 >> 2;
        OUT(5, 3, a) = p3 >> 3;
        OUT(4, 3, d) = p3 >> 4;
        OUT(4, 3, c) = p3 >> 5;
        OUT(4, 3, b) = p3 >> 6;
        OUT(4, 3, a) = p3 >> 7;
        OUT(3, 3, d) = p3 >> 8;
        OUT(3, 3, c) = p3 >> 9;
        OUT(3, 3, b) = p3 >> 10;
        OUT(3, 3, a) = p3 >> 11;
        OUT(2, 3, d) = p3 >> 12;
        OUT(2, 3, c) = p3 >> 13;
        OUT(2, 3, b) = p3 >> 14;
        OUT(2, 3, a) = p3 >> 15;
        OUT(1, 3, d) = p3 >> 16;
        OUT(1, 3, c) = p3 >> 17;
        OUT(1, 3, b) = p3 >> 18;
        OUT(1, 3, a) = p3 >> 19;
        OUT(0, 3, d) = p3 >> 20;
        OUT(0, 3, c) = p3 >> 21;
        OUT(0, 3, b) = p3 >> 22;
        OUT(0, 3, a) = p3 >> 23;
#endif

#if LED_STRIPS >= 5
        uint32_t p4 = FCP_FN(updatePixel)(icPrev, icNext,
            buffers.fbPrev->pixel(i + LEDS_PER_STRIP * 4),
            buffers.fbNext->pixel(i + LEDS_PER_STRIP * 4),
            pResidual + LEDS_PER_STRIP * 3 * 4);

        OUT(5, 4, d) = p4;
        OUT(5, 4, c) = p4 >> 1;
        OUT(5, 4, b) = p4 >> 2;
        OUT(5, 4, a) = p4 >> 3;
        OUT(4, 4, d) = p4 >> 4;
        OUT(4, 4, c) = p4 >> 5;
        OUT(4, 4, b) = p4 >> 6;
        OUT(4, 4, a) = p4 >> 7;
        OUT(3, 4, d) = p4 >> 8;
        OUT(3, 4, c) = p4 >> 9;
        OUT(3, 4, b) = p4 >> 10;
        OUT(3, 4, a) = p4 >> 11;
        OUT(2, 4, d) = p4 >> 12;
        OUT(2, 4, c) = p4 >> 13;
        OUT(2, 4, b) = p4 >> 14;
        OUT(2, 4, a) = p4 >> 15;
        OUT(1, 4, d) = p4 >> 16;
        OUT(1, 4, c) = p4 >> 17;
        OUT(1, 4, b) = p4 >> 18;
        OUT(1, 4, a) = p4 >> 19;
        OUT(0, 4, d) = p4 >> 20;
        OUT(0, 4, c) = p4 >> 21;
        OUT(0, 4, b) = p4 >> 22;
        OUT(0, 4, a) = p4 >> 23;
#endif

#if LED_STRIPS >= 6
        uint32_t p5 = FCP_FN(updatePixel)(icPrev, icNext,
            buffers.fbPrev->pixel(i + LEDS_PER_STRIP * 5),
            buffers.fbNext->pixel(i + LEDS_PER_STRIP * 5),
            pResidual + LEDS_PER_STRIP * 3 * 5);

        OUT(5, 5, d) = p5;
        OUT(5, 5, c) = p5 >> 1;
        OUT(5, 5, b) = p5 >> 2;
        OUT(5, 5, a) = p5 >> 3;
        OUT(4, 5, d) = p5 >> 4;
        OUT(4, 5, c) = p5 >> 5;
        OUT(4, 5, b) = p5 >> 6;
        OUT(4, 5, a) = p5 >> 7;
        OUT(3, 5, d) = p5 >> 8;
        OUT(3, 5, c) = p5 >> 9;
        OUT(3, 5, b) = p5 >> 10;
        OUT(3, 5, a) = p5 >> 11;
        OUT(2, 5, d) = p5 >> 12;
        OUT(2, 5, c) = p5 >> 13;
        OUT(2, 5, b) = p5 >> 14;
        OUT(2, 5, a) = p5 >> 15;
        OUT(1, 5, d) = p5 >> 16;
        OUT(1, 5, c) = p5 >> 17;
        OUT(1, 5, b) = p5 >> 18;
        OUT(1, 5, a) = p5 >> 19;
        OUT(0, 5, d) = p5 >> 20;
        OUT(0, 5, c) = p5 >> 21;
        OUT(0, 5, b) = p5 >> 22;
        OUT(0, 5, a) = p5 >> 23;
#endif

#if LED_STRIPS >= 7
        uint32_t p6 = FCP_FN(updatePixel)(icPrev, icNext,
            buffers.fbPrev->pixel(i + LEDS_PER_STRIP * 6),
            buffers.fbNext->pixel(i + LEDS_PER_STRIP * 6),
            pResidual + LEDS_PER_STRIP * 3 * 6);

        OUT(5, 6, d) = p6;
        OUT(5, 6, c) = p6 >> 1;
        OUT(5, 6, b) = p6 >> 2;
        OUT(5, 6, a) = p6 >> 3;
        OUT(4, 6, d) = p6 >> 4;
        OUT(4, 6, c) = p6 >> 5;
        OUT(4, 6, b) = p6 >> 6;
        OUT(4, 6, a) = p6 >> 7;
        OUT(3, 6, d) = p6 >> 8;
        OUT(3, 6, c) = p6 >> 9;
        OUT(3, 6, b) = p6 >> 10;
        OUT(3, 6, a) = p6 >> 11;
        OUT(2, 6, d) = p6 >> 12;
        OUT(2, 6, c) = p6 >> 13;
        OUT(2, 6, b) = p6 >> 14;
        OUT(2, 6, a) = p6 >> 15;
        OUT(1, 6, d) = p6 >> 16;
        OUT(1, 6, c) = p6 >> 17;
        OUT(1, 6, b) = p6 >> 18;
        OUT(1, 6, a) = p6 >> 19;
        OUT(0, 6, d) = p6 >> 20;
        OUT(0, 6, c) = p6 >> 21;
        OUT(0, 6, b) = p6 >> 22;
        OUT(0, 6, a) = p6 >> 23;
#endif

#if LED_STRIPS >= 8
        uint32_t p7 = FCP_FN(updatePixel)(icPrev, icNext,
            buffers.fbPrev->pixel(i + LEDS_PER_STRIP * 7),
            buffers.fbNext->pixel(i + LEDS_PER_STRIP * 7),
            pResidual + LEDS_PER_STRIP * 3 * 7);

        OUT(5, 7, d) = p7;
        OUT(5, 7, c) = p7 >> 1;
        OUT(5, 7, b) = p7 >> 2;
        OUT(5, 7, a) = p7 >> 3;
        OUT(4, 7, d) = p7 >> 4;
        OUT(4, 7, c) = p7 >> 5;
        OUT(4, 7, b) = p7 >> 6;
        OUT(4, 7, a) = p7 >> 7;
        OUT(3, 7, d) = p7 >> 8;
        OUT(3, 7, c) = p7 >> 9;
        OUT(3, 7, b) = p7 >> 10;
        OUT(3, 7, a) = p7 >> 11;
        OUT(2, 7, d) = p7 >> 12;
        OUT(2, 7, c) = p7 >> 13;
        OUT(2, 7, b) = p7 >> 14;
        OUT(2, 7, a) = p7 >> 15;
        OUT(1, 7, d) = p7 >> 16;
        OUT(1, 7, c) = p7 >> 17;
        OUT(1, 7, b) = p7 >> 18;
        OUT(1, 7, a) = p7 >> 19;
        OUT(0, 7, d) = p7 >> 20;
        OUT(0, 7, c) = p7 >> 21;
        OUT(0, 7, b) = p7 >> 22;
        OUT(0, 7, a) = p7 >> 23;
#endif

        *(out++) = o0.word;
        *(out++) = o1.word;
        *(out++) = o2.word;
        *(out++) = o3.word;
        *(out++) = o4.word;
        *(out++) = o5.word;
    }
}
