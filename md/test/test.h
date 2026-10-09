/* Tiny test framework for the host tests of the Mega Drive frontend (md/test). */
#ifndef MD_TEST_H
#define MD_TEST_H

#include <stdio.h>

extern int test_checks, test_failures;
void test_fail_now(const char *why);

#define CHECK(cond, what)                                                            \
    do {                                                                             \
        test_checks++;                                                               \
        if (!(cond)) {                                                               \
            test_failures++;                                                         \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, what);                   \
        }                                                                            \
    } while (0)

#define CHECK_EQ(a, b, what)                                                         \
    do {                                                                             \
        long _a = (long)(a), _b = (long)(b);                                         \
        test_checks++;                                                               \
        if (_a != _b) {                                                              \
            test_failures++;                                                         \
            printf("  FAIL %s:%d  %s (%ld != %ld)\n", __FILE__, __LINE__, what, _a, _b); \
        }                                                                            \
    } while (0)

#endif
