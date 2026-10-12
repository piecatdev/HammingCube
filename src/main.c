#include "parity3d.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s encode HEX64\n", program);
    fprintf(stderr, "  %s decode FRAMEHEX\n", program);
    fprintf(stderr, "  %s demo HEX64 [FLIP_INDEX ...]\n", program);
    fprintf(stderr, "  %s erase HEX64 [ERASED_INDEX ...]\n", program);
    fprintf(stderr, "  %s recover FRAMEHEX [ERASED_INDEX ...]\n", program);
    fprintf(stderr, "HEX64 has 16 digits; FRAMEHEX has 32; bit indices are 0..124.\n");
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_data(const char *hex, uint8_t data[P3_DATA_BITS]) {
    if (strlen(hex) != 16u) return -1;
    for (unsigned digit = 0; digit < 16u; ++digit) {
        int value = hex_digit(hex[15u - digit]);
        if (value < 0) return -1;
        for (unsigned bit = 0; bit < 4u; ++bit)
            data[4u * digit + bit] = (uint8_t)((value >> bit) & 1);
    }
    return 0;
}

static int parse_wire(const char *hex, uint8_t wire[P3_WIRE_BYTES]) {
    if (strlen(hex) != P3_WIRE_BYTES * 2u) return -1;
    for (unsigned i = 0; i < P3_WIRE_BYTES; ++i) {
        int high = hex_digit(hex[2u * i]);
        int low = hex_digit(hex[2u * i + 1u]);
        if (high < 0 || low < 0) return -1;
        wire[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
}

static void print_data(const uint8_t data[P3_DATA_BITS]) {
    const char digits[] = "0123456789abcdef";
    for (int nibble = 15; nibble >= 0; --nibble) {
        unsigned value = 0;
        for (unsigned bit = 0; bit < 4u; ++bit)
            value |= (unsigned)data[4u * (unsigned)nibble + bit] << bit;
        putchar(digits[value]);
    }
}

static void print_wire(const uint8_t wire[P3_WIRE_BYTES]) {
    for (unsigned i = 0; i < P3_WIRE_BYTES; ++i) printf("%02x", wire[i]);
}

static int make_frame(const char *hex, uint8_t data[P3_DATA_BITS],
                      uint8_t sent[P3_CODE_BITS], uint8_t wire[P3_WIRE_BYTES]) {
    return parse_data(hex, data) == 0 && p3_encode(data, sent) == 0 &&
           p3_pack(sent, wire) == 0 ? 0 : -1;
}

static int show_decoded(const uint8_t received[P3_CODE_BITS]) {
    uint8_t recovered[P3_DATA_BITS], repaired[P3_CODE_BITS];
    p3_result result = p3_decode(received, recovered, repaired);
    if (result.status == P3_CLEAN) {
        printf("Receiver:          valid codeword (zero syndrome)\n");
    } else if (result.status == P3_CORRECTED) {
        printf("Receiver:          corrected %u bit(s) at", result.errors_corrected);
        for (unsigned i = 0; i < result.errors_corrected; ++i)
            printf(" %u", result.positions[i]);
        putchar('\n');
    } else if (result.status == P3_UNCORRECTABLE) {
        printf("Receiver:          no codeword within Hamming distance 3\n");
        return 1;
    } else {
        fprintf(stderr, "Decoder input error\n");
        return 2;
    }
    printf("Recovered data:    "); print_data(recovered); putchar('\n');
    return 0;
}

static int parse_erasure_indices(int argc, char **argv, int first,
                                 unsigned positions[P3_CODE_BITS]) {
    if (argc - first > (int)P3_CODE_BITS) return -1;
    uint8_t seen[P3_CODE_BITS] = {0};
    for (int arg = first; arg < argc; ++arg) {
        char *end = NULL;
        errno = 0;
        unsigned long index = strtoul(argv[arg], &end, 10);
        if (errno || end == argv[arg] || *end != '\0' ||
            index >= P3_CODE_BITS || seen[index]) return -1;
        seen[index] = 1;
        positions[arg - first] = (unsigned)index;
    }
    return 0;
}

static int show_erasures(const uint8_t received[P3_CODE_BITS],
                         const unsigned *positions, unsigned count) {
    uint8_t recovered[P3_DATA_BITS], repaired[P3_CODE_BITS];
    p3_status status = p3_decode_erasures(received, positions, count,
                                         recovered, repaired);
    if (status == P3_CLEAN)
        printf("Receiver:          valid codeword (no erasures)\n");
    else if (status == P3_CORRECTED)
        printf("Receiver:          recovered %u erased bit(s)\n", count);
    else if (status == P3_UNCORRECTABLE) {
        printf("Receiver:          erasures are ambiguous or inconsistent\n");
        return 1;
    } else {
        fprintf(stderr, "Decoder input error\n");
        return 2;
    }
    printf("Recovered data:    "); print_data(recovered); putchar('\n');
    return 0;
}

static int run_erasures(int argc, char **argv, int is_demo) {
    unsigned positions[P3_CODE_BITS];
    if (argc < 3 || parse_erasure_indices(argc, argv, 3, positions) != 0) {
        usage(argv[0]);
        return 2;
    }
    unsigned count = (unsigned)(argc - 3);
    uint8_t wire[P3_WIRE_BYTES], received[P3_CODE_BITS];
    if (is_demo) {
        uint8_t data[P3_DATA_BITS], sent[P3_CODE_BITS];
        if (make_frame(argv[2], data, sent, wire) != 0) {
            usage(argv[0]);
            return 2;
        }
        printf("Sender data:       "); print_data(data); putchar('\n');
        printf("Wire frame:        "); print_wire(wire); putchar('\n');
        /* The simulated channel fills erased slots with zero; their values
           are ignored by the erasure decoder. */
        for (unsigned i = 0; i < count; ++i)
            wire[positions[i] / 8u] &= (uint8_t)~(1u << (positions[i] % 8u));
        printf("Received frame:    "); print_wire(wire); putchar('\n');
    } else if (parse_wire(argv[2], wire) != 0) {
        fprintf(stderr, "Invalid frame: expected 32 hex digits\n");
        return 2;
    }
    if (p3_unpack(wire, received) != 0) {
        fprintf(stderr, "Invalid padding bits in received frame\n");
        return 2;
    }
    printf("Erased positions:  ");
    if (!count) printf("none");
    for (unsigned i = 0; i < count; ++i)
        printf("%s%u", i ? ", " : "", positions[i]);
    putchar('\n');
    return show_erasures(received, positions, count);
}

static int run_demo(int argc, char **argv, int data_arg) {
    uint8_t data[P3_DATA_BITS], sent[P3_CODE_BITS], received[P3_CODE_BITS];
    uint8_t wire[P3_WIRE_BYTES], channel_wire[P3_WIRE_BYTES];
    if (data_arg >= argc || make_frame(argv[data_arg], data, sent, wire) != 0) {
        usage(argv[0]);
        return 2;
    }
    memcpy(channel_wire, wire, sizeof wire);
    printf("Sender data:       "); print_data(data); putchar('\n');
    printf("Encoded:           64 data bits + 61 parity bits = 125 bits\n");
    printf("Wire frame:        "); print_wire(wire); printf(" (16 bytes)\n");
    printf("Channel flips:     ");
    if (data_arg + 1 == argc) printf("none");
    for (int arg = data_arg + 1; arg < argc; ++arg) {
        char *end = NULL;
        errno = 0;
        unsigned long index = strtoul(argv[arg], &end, 10);
        if (errno || end == argv[arg] || *end != '\0' || index >= P3_CODE_BITS) {
            fprintf(stderr, "\nInvalid bit index: %s\n", argv[arg]);
            return 2;
        }
        channel_wire[index / 8u] ^= (uint8_t)(1u << (index % 8u));
        printf("%s%lu", arg == data_arg + 1 ? "" : ", ", index);
    }
    putchar('\n');
    if (p3_unpack(channel_wire, received) != 0) {
        fprintf(stderr, "Invalid padding bits in received frame\n");
        return 2;
    }
    int status = show_decoded(received);
    if (status == 0)
        printf("Channel distance:  %d bit(s)\n", p3_hamming_distance(sent, received));
    return status;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 2; }
    if (strcmp(argv[1], "encode") == 0) {
        uint8_t data[P3_DATA_BITS], sent[P3_CODE_BITS], wire[P3_WIRE_BYTES];
        if (argc != 3 || make_frame(argv[2], data, sent, wire) != 0) {
            usage(argv[0]); return 2;
        }
        print_wire(wire); putchar('\n');
        return 0;
    }
    if (strcmp(argv[1], "decode") == 0) {
        uint8_t wire[P3_WIRE_BYTES], received[P3_CODE_BITS];
        if (argc != 3 || parse_wire(argv[2], wire) != 0 ||
            p3_unpack(wire, received) != 0) {
            fprintf(stderr, "Invalid frame: expected 32 hex digits and zero padding\n");
            return 2;
        }
        return show_decoded(received);
    }
    if (strcmp(argv[1], "demo") == 0) return run_demo(argc, argv, 2);
    if (strcmp(argv[1], "erase") == 0) return run_erasures(argc, argv, 1);
    if (strcmp(argv[1], "recover") == 0) return run_erasures(argc, argv, 0);
    /* Keep the original positional demo form working. */
    return run_demo(argc, argv, 1);
}
