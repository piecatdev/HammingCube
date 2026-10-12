#ifndef PARITY3D_H
#define PARITY3D_H

#include <stdint.h>

/* A systematic (125, 64, 8) binary product code: three (5, 4, 2) parity codes. */
#define P3_SIDE 5u
#define P3_DATA_BITS 64u
#define P3_CODE_BITS 125u
#define P3_WIRE_BYTES 16u

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

/* Corrects every pattern of at most three flipped bits. On CLEAN/CORRECTED,
 * writes the recovered data and repaired codeword. Other outputs are untouched.
 * Beyond three flips, decoding has no guarantee of detection or correctness. */
p3_result p3_decode(const uint8_t received[P3_CODE_BITS],
                    uint8_t data_out[P3_DATA_BITS],
                    uint8_t repaired[P3_CODE_BITS]);

/* Returns the bitwise Hamming distance, or -1 for invalid input bits. */
int p3_hamming_distance(const uint8_t a[P3_CODE_BITS],
                        const uint8_t b[P3_CODE_BITS]);

#endif
