#ifndef FERRET_TEST_HARNESS_H
#define FERRET_TEST_HARNESS_H

#include <stdio.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do {                                   \
    tests_run++;                                                \
    if (!(cond)) {                                              \
        tests_failed++;                                         \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);    \
    }                                                           \
} while (0)

#define CHECK_STR(got, want, msg) do {                          \
    tests_run++;                                                \
    const char *g_ = (got), *w_ = (want);                       \
    if (!g_ || strcmp(g_, w_) != 0) {                           \
        tests_failed++;                                         \
        printf("FAIL %s:%d: %s\n  want: %s\n  got:  %s\n",      \
               __FILE__, __LINE__, msg, w_, g_ ? g_ : "(null)"); \
    }                                                           \
} while (0)

#define TEST_RESULTS() do {                                     \
    if (tests_failed) {                                         \
        printf("%d/%d checks FAILED\n", tests_failed, tests_run); \
        return 1;                                               \
    }                                                           \
    printf("%d checks passed\n", tests_run);                    \
    return 0;                                                   \
} while (0)

#endif
