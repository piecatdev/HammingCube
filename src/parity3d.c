#include "parity3d.h"

#include <string.h>

typedef struct { uint64_t lo, hi; } syndrome;

static unsigned code_index(unsigned x, unsigned y, unsigned z) {
    return x + 5u * y + 25u * z;
}

static int valid_bits(const uint8_t *bits, unsigned count) {
    if (!bits) return 0;
    for (unsigned i = 0; i < count; ++i)
        if (bits[i] > 1u) return 0;
    return 1;
}

static void set_bit(syndrome *s, unsigned bit) {
    if (bit < 64u) s->lo |= UINT64_C(1) << bit;
    else s->hi |= UINT64_C(1) << (bit - 64u);
}

static syndrome signature(unsigned index) {
    unsigned x = index % 5u;
    unsigned y = (index / 5u) % 5u;
    unsigned z = index / 25u;
    syndrome s = {0, 0};
    /* One parity-check line through this cell for each of the three axes. */
    set_bit(&s, y + 5u * z);
    set_bit(&s, 25u + x + 5u * z);
    set_bit(&s, 50u + x + 5u * y);
    return s;
}

static syndrome xor_syndrome(syndrome a, syndrome b) {
    syndrome s = {a.lo ^ b.lo, a.hi ^ b.hi};
    return s;
}

static int same(syndrome a, syndrome b) {
    return a.lo == b.lo && a.hi == b.hi;
}

static int syndrome_bit(syndrome s, unsigned bit) {
    return (int)((bit < 64u ? s.lo >> bit : s.hi >> (bit - 64u)) & 1u);
}

static syndrome compute_syndrome(const uint8_t bits[P3_CODE_BITS]) {
    syndrome s = {0, 0};
    for (unsigned i = 0; i < P3_CODE_BITS; ++i)
        if (bits[i]) s = xor_syndrome(s, signature(i));
    return s;
}

int p3_encode(const uint8_t data[P3_DATA_BITS], uint8_t codeword[P3_CODE_BITS]) {
    if (!valid_bits(data, P3_DATA_BITS) || !codeword) return -1;
    /* Every parity cell is the XOR of the data cells in its corresponding
       line, plane, or entire cube. Data cells copy through unchanged. */
    for (unsigned z = 0; z < 5u; ++z)
        for (unsigned y = 0; y < 5u; ++y)
            for (unsigned x = 0; x < 5u; ++x) {
                uint8_t parity = 0;
                for (unsigned dz = 0; dz < 4u; ++dz)
                    for (unsigned dy = 0; dy < 4u; ++dy)
                        for (unsigned dx = 0; dx < 4u; ++dx)
                            if ((x == 4u || x == dx) &&
                                (y == 4u || y == dy) &&
                                (z == 4u || z == dz))
                                parity ^= data[dx + 4u * dy + 16u * dz];
                codeword[code_index(x, y, z)] = parity;
            }
    return 0;
}

int p3_pack(const uint8_t codeword[P3_CODE_BITS],
            uint8_t wire[P3_WIRE_BYTES]) {
    if (!valid_bits(codeword, P3_CODE_BITS) || !wire) return -1;
    memset(wire, 0, P3_WIRE_BYTES);
    for (unsigned i = 0; i < P3_CODE_BITS; ++i)
        wire[i / 8u] |= (uint8_t)(codeword[i] << (i % 8u));
    return 0;
}

int p3_unpack(const uint8_t wire[P3_WIRE_BYTES],
              uint8_t codeword[P3_CODE_BITS]) {
    if (!wire || !codeword || (wire[15] & UINT8_C(0xe0)) != 0) return -1;
    for (unsigned i = 0; i < P3_CODE_BITS; ++i)
        codeword[i] = (uint8_t)((wire[i / 8u] >> (i % 8u)) & 1u);
    return 0;
}

int p3_is_codeword(const uint8_t codeword[P3_CODE_BITS]) {
    if (!valid_bits(codeword, P3_CODE_BITS)) return -1;
    syndrome s = compute_syndrome(codeword);
    return s.lo == 0 && s.hi == 0;
}

static void extract_data(const uint8_t codeword[P3_CODE_BITS],
                         uint8_t data[P3_DATA_BITS]) {
    for (unsigned z = 0; z < 4u; ++z)
        for (unsigned y = 0; y < 4u; ++y)
            for (unsigned x = 0; x < 4u; ++x)
                data[x + 4u * y + 16u * z] = codeword[code_index(x, y, z)];
}

p3_result p3_decode(const uint8_t received[P3_CODE_BITS],
                    uint8_t data_out[P3_DATA_BITS],
                    uint8_t repaired[P3_CODE_BITS]) {
    p3_result result = {P3_INVALID, 0, {0, 0, 0}};
    if (!valid_bits(received, P3_CODE_BITS) || !data_out || !repaired) return result;
    syndrome target = compute_syndrome(received);
    syndrome zero = {0, 0};
    if (same(target, zero)) {
        result.status = P3_CLEAN;
    } else {
        syndrome sig[P3_CODE_BITS];
        for (unsigned i = 0; i < P3_CODE_BITS; ++i) sig[i] = signature(i);
        /* Search by increasing error weight. d_min=8 makes the answer
           unique within radius three of a valid codeword. */
        for (unsigned i = 0; i < P3_CODE_BITS; ++i) {
            if (same(sig[i], target)) {
                result.errors_corrected = 1;
                result.positions[0] = i;
                goto found;
            }
        }
        for (unsigned i = 0; i < P3_CODE_BITS; ++i)
            for (unsigned j = i + 1; j < P3_CODE_BITS; ++j)
                if (same(xor_syndrome(sig[i], sig[j]), target)) {
                    result.errors_corrected = 2;
                    result.positions[0] = i;
                    result.positions[1] = j;
                    goto found;
                }
        for (unsigned i = 0; i < P3_CODE_BITS; ++i)
            for (unsigned j = i + 1; j < P3_CODE_BITS; ++j) {
                syndrome pair = xor_syndrome(sig[i], sig[j]);
                for (unsigned k = j + 1; k < P3_CODE_BITS; ++k)
                    if (same(xor_syndrome(pair, sig[k]), target)) {
                        result.errors_corrected = 3;
                        result.positions[0] = i;
                        result.positions[1] = j;
                        result.positions[2] = k;
                        goto found;
                    }
            }
        result.status = P3_UNCORRECTABLE;
        return result;
    }
found:
    memcpy(repaired, received, P3_CODE_BITS);
    for (unsigned i = 0; i < result.errors_corrected; ++i)
        repaired[result.positions[i]] ^= 1u;
    extract_data(repaired, data_out);
    if (result.errors_corrected) result.status = P3_CORRECTED;
    return result;
}

typedef struct { syndrome coefficients; uint8_t rhs; } equation;

p3_status p3_decode_erasures(const uint8_t received[P3_CODE_BITS],
                             const unsigned *positions, unsigned count,
                             uint8_t data_out[P3_DATA_BITS],
                             uint8_t repaired[P3_CODE_BITS]) {
    if (!valid_bits(received, P3_CODE_BITS) || !data_out || !repaired ||
        count > P3_CODE_BITS || (count && !positions)) return P3_INVALID;

    uint8_t erased[P3_CODE_BITS] = {0};
    for (unsigned col = 0; col < count; ++col) {
        if (positions[col] >= P3_CODE_BITS || erased[positions[col]])
            return P3_INVALID;
        erased[positions[col]] = 1;
    }

    uint8_t known[P3_CODE_BITS];
    memcpy(known, received, P3_CODE_BITS);
    for (unsigned col = 0; col < count; ++col) known[positions[col]] = 0;
    syndrome target = compute_syndrome(known);
    equation rows[75] = {0};
    for (unsigned row = 0; row < 75u; ++row)
        rows[row].rhs = (uint8_t)syndrome_bit(target, row);
    for (unsigned col = 0; col < count; ++col) {
        syndrome sig = signature(positions[col]);
        for (unsigned row = 0; row < 75u; ++row)
            if (syndrome_bit(sig, row)) set_bit(&rows[row].coefficients, col);
    }

    /* Reduced row echelon form over GF(2): addition is XOR. The first
       independent row for each column becomes its pivot equation. */
    unsigned rank = 0;
    for (unsigned col = 0; col < count && rank < 75u; ++col) {
        unsigned pivot = rank;
        while (pivot < 75u && !syndrome_bit(rows[pivot].coefficients, col))
            ++pivot;
        if (pivot == 75u) continue;
        equation tmp = rows[rank];
        rows[rank] = rows[pivot];
        rows[pivot] = tmp;
        for (unsigned row = 0; row < 75u; ++row) {
            if (row == rank || !syndrome_bit(rows[row].coefficients, col))
                continue;
            rows[row].coefficients = xor_syndrome(rows[row].coefficients,
                                                 rows[rank].coefficients);
            rows[row].rhs ^= rows[rank].rhs;
        }
        ++rank;
    }
    for (unsigned row = rank; row < 75u; ++row)
        if (rows[row].rhs) return P3_UNCORRECTABLE;
    if (rank != count) return P3_UNCORRECTABLE;

    /* Full column rank leaves one unknown per pivot row. Since elimination
       ran in both directions, its RHS is that erased bit's value. */
    uint8_t solved[P3_CODE_BITS];
    memcpy(solved, known, P3_CODE_BITS);
    for (unsigned row = 0; row < rank; ++row) {
        for (unsigned col = 0; col < count; ++col)
            if (syndrome_bit(rows[row].coefficients, col)) {
                solved[positions[col]] = rows[row].rhs;
                break;
            }
    }
    memcpy(repaired, solved, P3_CODE_BITS);
    extract_data(solved, data_out);
    return count ? P3_CORRECTED : P3_CLEAN;
}

int p3_hamming_distance(const uint8_t a[P3_CODE_BITS],
                        const uint8_t b[P3_CODE_BITS]) {
    if (!valid_bits(a, P3_CODE_BITS) || !valid_bits(b, P3_CODE_BITS)) return -1;
    int distance = 0;
    for (unsigned i = 0; i < P3_CODE_BITS; ++i) distance += a[i] != b[i];
    return distance;
}
