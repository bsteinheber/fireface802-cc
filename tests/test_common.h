/*
 * Minimal test helpers without external dependencies.
 *
 * Each test program calls CHECK* macros and finishes with TEST_EXIT(),
 * which returns a non-zero exit status if any check failed. CTest
 * interprets a non-zero exit status as a test failure.
 */
#ifndef FF802_TEST_COMMON_H
#define FF802_TEST_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int test_failures = 0;
static int test_checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        test_checks++;                                                     \
        if (!(cond)) {                                                     \
            test_failures++;                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n",                   \
                    __FILE__, __LINE__, #cond);                            \
        }                                                                  \
    } while (0)

#define CHECK_EQ_U32(actual, expected)                                     \
    do {                                                                   \
        uint32_t check_a_ = (uint32_t)(actual);                            \
        uint32_t check_e_ = (uint32_t)(expected);                          \
        test_checks++;                                                     \
        if (check_a_ != check_e_) {                                        \
            test_failures++;                                               \
            fprintf(stderr, "%s:%d: %s == 0x%08X, expected 0x%08X\n",      \
                    __FILE__, __LINE__, #actual,                           \
                    (unsigned)check_a_, (unsigned)check_e_);               \
        }                                                                  \
    } while (0)

static inline void test_print_bytes(const char *label, const uint8_t *b,
                                    size_t n)
{
    fprintf(stderr, "  %s:", label);
    for (size_t i = 0; i < n; i++)
        fprintf(stderr, " %02X", b[i]);
    fprintf(stderr, "\n");
}

#define CHECK_BYTES(actual, expected, n)                                   \
    do {                                                                   \
        test_checks++;                                                     \
        if (memcmp((actual), (expected), (n)) != 0) {                      \
            test_failures++;                                               \
            fprintf(stderr, "%s:%d: byte mismatch for %s\n",               \
                    __FILE__, __LINE__, #actual);                          \
            test_print_bytes("actual  ", (actual), (n));                   \
            test_print_bytes("expected", (expected), (n));                 \
        }                                                                  \
    } while (0)

#define TEST_EXIT()                                                        \
    do {                                                                   \
        printf("%d checks, %d failures\n", test_checks, test_failures);    \
        return test_failures == 0 ? 0 : 1;                                 \
    } while (0)

#endif /* FF802_TEST_COMMON_H */
