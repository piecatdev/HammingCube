#ifndef PARITY3D_H
#define PARITY3D_H

#include <stdint.h>

/* A systematic (125, 64, 8) binary product code: three (5, 4, 2) parity codes. */
#define P3_SIDE 5u
#define P3_DATA_BITS 64u
#define P3_CODE_BITS 125u
#define P3_WIRE_BYTES 16u

/* Buffer contract for the entire API: array parameters decay to pointers in
 * C. Callers must provide readable/writable storage for the sizes named in
 * each declaration; no function can detect a too-short buffer. Passing one
 * is undefined behavior, not a P3_INVALID result. Input and output buffers
 * must be separate. Non-NULL bit arrays must contain only 0 or 1. */

typedef enum {
    P3_INVALID = -1,
    P3_UNCORRECTABLE = 0,
    P3_CLEAN = 1,
    P3_CORRECTED = 2
} p3_status;

typedef struct {
    p3_status status;
    unsigned errors_corrected;
    unsigned positions[3]; /* Linear codeword indices, valid up to errors_corrected. */
} p3_result;

/* Each input and output element is one bit (0 or 1). Returns 0 on success. */
int p3_encode(const uint8_t data[P3_DATA_BITS], uint8_t codeword[P3_CODE_BITS]);

/* Pack bit i into byte i/8, bit position i%8 (least significant bit first).
 * The three unused high bits of byte 15 are zero. Returns 0 on success. */
int p3_pack(const uint8_t codeword[P3_CODE_BITS],
            uint8_t wire[P3_WIRE_BYTES]);

/* Rejects nonzero padding bits; writes codeword only on success. */
int p3_unpack(const uint8_t wire[P3_WIRE_BYTES],
              uint8_t codeword[P3_CODE_BITS]);

/* Returns 1 for a valid codeword, 0 for invalid parity, -1 for invalid bits. */
int p3_is_codeword(const uint8_t codeword[P3_CODE_BITS]);

/* Corrects every pattern of at most three flipped bits.
 * CLEAN: zero syndrome; no correction attempted (not proof of no errors).
 * CORRECTED: one to three positions in result.positions were flipped.
 * UNCORRECTABLE: no codeword within distance three was found.
 * INVALID: NULL pointer or received bit outside {0,1}. This cannot detect
 * a too-short buffer; see the buffer contract above.
 * data_out and repaired change only on CLEAN/CORRECTED. Beyond three flips,
 * decoding has no guarantee of detection or correctness. */
p3_result p3_decode(const uint8_t received[P3_CODE_BITS],
                    uint8_t data_out[P3_DATA_BITS],
                    uint8_t repaired[P3_CODE_BITS]);

/* Reconstruct bits at the listed erased positions. Their received values are
 * ignored; all other bits must be correct. Every set of at most seven erasures
 * has a unique solution. Larger sets are accepted when uniquely solvable.
 * Returns CLEAN for zero erasures and valid parity, CORRECTED for a unique
 * solution, UNCORRECTABLE for inconsistent or ambiguous equations, and
 * INVALID for NULL pointers, duplicate/out-of-range positions, count > 125,
 * or received bits outside {0,1}; it cannot detect a too-short buffer.
 * Outputs change only on CLEAN/CORRECTED. */
p3_status p3_decode_erasures(const uint8_t received[P3_CODE_BITS],
                             const unsigned *positions, unsigned count,
                             uint8_t data_out[P3_DATA_BITS],
                             uint8_t repaired[P3_CODE_BITS]);

/* Returns the bitwise Hamming distance, or -1 for invalid input bits. */
int p3_hamming_distance(const uint8_t a[P3_CODE_BITS],
                        const uint8_t b[P3_CODE_BITS]);

#endif
