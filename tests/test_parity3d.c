#include "parity3d.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check_pattern(const uint8_t sent[P3_CODE_BITS],
                          const uint8_t expected[P3_DATA_BITS],
                          const unsigned *flips, unsigned count) {
    uint8_t received[P3_CODE_BITS], repaired[P3_CODE_BITS], output[P3_DATA_BITS];
    memcpy(received, sent, sizeof received);
    for (unsigned i = 0; i < count; ++i) received[flips[i]] ^= 1u;
    p3_result result = p3_decode(received, output, repaired);
    assert(result.status == (count ? P3_CORRECTED : P3_CLEAN));
    assert(result.errors_corrected == count);
    assert(memcmp(expected, output, P3_DATA_BITS) == 0);
    assert(memcmp(sent, repaired, P3_CODE_BITS) == 0);
    assert(p3_hamming_distance(sent, received) == (int)count);
}

static void check_erasures(const uint8_t sent[P3_CODE_BITS],
                           const uint8_t expected[P3_DATA_BITS],
                           const unsigned *positions, unsigned count) {
    uint8_t received[P3_CODE_BITS], repaired[P3_CODE_BITS], output[P3_DATA_BITS];
    memcpy(received, sent, P3_CODE_BITS);
    /* Fill erased slots with values that may differ from the sent bits. */
    for (unsigned i = 0; i < count; ++i) received[positions[i]] = 0;
    assert(p3_decode_erasures(received, positions, count, output, repaired) ==
           (count ? P3_CORRECTED : P3_CLEAN));
    assert(memcmp(output, expected, P3_DATA_BITS) == 0);
    assert(memcmp(repaired, sent, P3_CODE_BITS) == 0);
}

int main(void) {
    uint8_t data[P3_DATA_BITS], sent[P3_CODE_BITS];
    for (unsigned i = 0; i < P3_DATA_BITS; ++i)
        data[i] = (uint8_t)(((i * 17u + i / 5u) % 7u) < 3u);
    assert(p3_encode(data, sent) == 0);
    assert(p3_is_codeword(sent) == 1);
    uint8_t wire[P3_WIRE_BYTES], unpacked[P3_CODE_BITS];
    assert(p3_pack(sent, wire) == 0);
    assert((wire[15] & 0xe0u) == 0);
    assert(p3_unpack(wire, unpacked) == 0);
    assert(memcmp(sent, unpacked, P3_CODE_BITS) == 0);
    /* A flipped wire bit maps to the same linear codeword bit. */
    wire[10] ^= 1u << 6;
    assert(p3_unpack(wire, unpacked) == 0);
    assert(unpacked[86] == (uint8_t)(sent[86] ^ 1u));
    wire[15] |= 0x80u;
    assert(p3_unpack(wire, unpacked) == -1);
    check_pattern(sent, data, NULL, 0);
    check_erasures(sent, data, NULL, 0);

    /* Every single-bit pattern, including every parity-plane cell. */
    for (unsigned i = 0; i < P3_CODE_BITS; ++i) {
        unsigned flips[] = {i};
        check_pattern(sent, data, flips, 1);
        check_erasures(sent, data, flips, 1);
    }
    /* All pairs; sample triples spanning data and parity cells. */
    for (unsigned i = 0; i < P3_CODE_BITS; ++i)
        for (unsigned j = i + 1; j < P3_CODE_BITS; ++j) {
            unsigned flips[] = {i, j};
            check_pattern(sent, data, flips, 2);
            check_erasures(sent, data, flips, 2);
        }
    for (unsigned i = 0; i < 30u; ++i) {
        unsigned flips[] = {i, 40u + i, 90u + i};
        check_pattern(sent, data, flips, 3);
    }
    /* Many distinct 3..7 erasure sets, including data and parity cells. */
    for (unsigned count = 3; count <= 7u; ++count)
        for (unsigned trial = 0; trial < 100u; ++trial) {
            unsigned erasures[7];
            for (unsigned i = 0; i < count; ++i)
                erasures[i] = (trial * 37u + i * 19u) % P3_CODE_BITS;
            check_erasures(sent, data, erasures, count);
        }

    /* An 8-bit rectangular-corner pattern is another valid codeword:
       this witnesses d_min <= 8; the product-code proof gives >= 8. */
    uint8_t zero[P3_DATA_BITS] = {0}, one[P3_DATA_BITS] = {0};
    uint8_t a[P3_CODE_BITS], b[P3_CODE_BITS];
    one[0] = 1;
    assert(p3_encode(zero, a) == 0 && p3_encode(one, b) == 0);
    assert(p3_hamming_distance(a, b) == 8);
    assert(p3_is_codeword(b) == 1);
    uint8_t arbitrary[P3_CODE_BITS], arbitrary_repaired[P3_CODE_BITS];
    uint8_t arbitrary_data[P3_DATA_BITS];
    unsigned three_erased[] = {0, 4, 20};
    memcpy(arbitrary, a, P3_CODE_BITS);
    for (unsigned i = 0; i < 3u; ++i) arbitrary[three_erased[i]] = 1;
    assert(p3_decode_erasures(arbitrary, three_erased, 3,
                              arbitrary_data, arbitrary_repaired) == P3_CORRECTED);
    assert(memcmp(arbitrary_data, zero, P3_DATA_BITS) == 0);
    assert(memcmp(arbitrary_repaired, a, P3_CODE_BITS) == 0);

    /* Four errors are outside every radius-three decoding ball. Five can
       move the word into a different codeword's radius-three ball. */
    unsigned support[8], count = 0;
    for (unsigned i = 0; i < P3_CODE_BITS; ++i)
        if (b[i]) support[count++] = i;
    assert(count == 8);
    for (unsigned omitted = 0; omitted < 8u; ++omitted) {
        unsigned seven[7], used = 0;
        for (unsigned i = 0; i < 8u; ++i)
            if (i != omitted) seven[used++] = support[i];
        check_erasures(a, zero, seven, 7);
        check_erasures(b, one, seven, 7);
    }
    /* The radius-seven guarantee is conservative: some eight-cell sets
       also have full column rank and are uniquely recoverable. */
    unsigned unique_eight[] = {0, 1, 2, 3, 4, 5, 6, 7};
    check_erasures(a, zero, unique_eight, 8);
    check_erasures(b, one, unique_eight, 8);
    /* Erasing the support of a weight-eight codeword leaves at least two
       possible completions. The decoder must not guess. */
    uint8_t erased_received[P3_CODE_BITS], untouched[P3_CODE_BITS];
    memcpy(erased_received, a, P3_CODE_BITS);
    memset(untouched, 0xa5, P3_CODE_BITS);
    uint8_t erasure_output[P3_DATA_BITS];
    memset(erasure_output, 0xa5, P3_DATA_BITS);
    assert(p3_decode_erasures(erased_received, support, 8,
                              erasure_output, untouched) == P3_UNCORRECTABLE);
    for (unsigned i = 0; i < P3_CODE_BITS; ++i) assert(untouched[i] == 0xa5);
    for (unsigned i = 0; i < P3_DATA_BITS; ++i) assert(erasure_output[i] == 0xa5);

    /* A known wrong bit makes these parity equations inconsistent. */
    erased_received[1] ^= 1u;
    assert(p3_decode_erasures(erased_received, support, 1,
                              erasure_output, untouched) == P3_UNCORRECTABLE);
    unsigned duplicate[] = {0, 0}, out_of_range[] = {P3_CODE_BITS};
    assert(p3_decode_erasures(a, duplicate, 2, erasure_output, untouched) == P3_INVALID);
    assert(p3_decode_erasures(a, out_of_range, 1, erasure_output, untouched) == P3_INVALID);
    assert(p3_decode_erasures(a, NULL, 1, erasure_output, untouched) == P3_INVALID);
    assert(p3_decode_erasures(a, NULL, 126, erasure_output, untouched) == P3_INVALID);
    uint8_t received[P3_CODE_BITS], repaired[P3_CODE_BITS], output[P3_DATA_BITS];
    memcpy(received, a, sizeof received);
    for (unsigned i = 0; i < 4u; ++i) received[support[i]] ^= 1u;
    assert(p3_decode(received, output, repaired).status == P3_UNCORRECTABLE);
    received[support[4]] ^= 1u;
    assert(p3_decode(received, output, repaired).status == P3_CORRECTED);
    assert(memcmp(repaired, b, P3_CODE_BITS) == 0);
    assert(memcmp(output, zero, P3_DATA_BITS) != 0);
    for (unsigned i = 5; i < 8u; ++i) received[support[i]] ^= 1u;
    assert(p3_decode(received, output, repaired).status == P3_CLEAN);

    uint8_t bad_data[P3_DATA_BITS] = {0};
    bad_data[0] = 2;
    assert(p3_encode(bad_data, a) == -1);
    b[0] = 2;
    assert(p3_is_codeword(b) == -1);
    assert(p3_decode(b, data, a).status == P3_INVALID);
    puts("All parity3d tests passed.");
    return 0;
}
