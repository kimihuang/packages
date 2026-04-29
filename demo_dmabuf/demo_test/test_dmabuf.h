/*
 * test_dmabuf.h - Lightweight user-space test framework for DMA-BUF tests
 *
 * Provides assertion macros, test registration, and a test runner.
 */

#ifndef TEST_DMABUF_H
#define TEST_DMABUF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * Test statistics (file-scope)
 * ====================================================================== */

static int tests_run_count;
static int tests_passed_count;
static int tests_failed_count;

/* ======================================================================
 * Assertion macros
 * ====================================================================== */

#define ASSERT_EQ(actual, expected) do {					\
	int _a = (int)(actual);						\
	int _e = (int)(expected);						\
	if (_a != _e) {							\
		fprintf(stderr, "  FAIL: %s:%d: ASSERT_EQ(%s, %s) "	\
			"got %d, expected %d\n",				\
			__FILE__, __LINE__, #actual, #expected, _a, _e);	\
		return -1;						\
	}								\
} while (0)

#define ASSERT_NE(actual, not_expected) do {					\
	int _a = (int)(actual);						\
	int _ne = (int)(not_expected);					\
	if (_a == _ne) {							\
		fprintf(stderr, "  FAIL: %s:%d: ASSERT_NE(%s, %s) "	\
			"both are %d\n",					\
			__FILE__, __LINE__, #actual, #not_expected, _a);	\
		return -1;						\
	}								\
} while (0)

#define ASSERT_GE(actual, threshold) do {					\
	int _a = (int)(actual);						\
	int _t = (int)(threshold);					\
	if (_a < _t) {							\
		fprintf(stderr, "  FAIL: %s:%d: ASSERT_GE(%s, %s) "	\
			"got %d, need >= %d\n",				\
			__FILE__, __LINE__, #actual, #threshold, _a, _t);	\
		return -1;						\
	}								\
} while (0)

#define ASSERT_TRUE(expr) do {						\
	if (!(expr)) {							\
		fprintf(stderr, "  FAIL: %s:%d: ASSERT_TRUE(%s)\n",		\
			__FILE__, __LINE__, #expr);				\
		return -1;						\
	}								\
} while (0)

/* ======================================================================
 * Test case definition
 * ====================================================================== */

typedef int (*test_func_t)(void);

struct test_case {
	const char  *name;
	test_func_t  func;
};

#define TEST_CASE(_name)							\
	static int _name(void)

/* ======================================================================
 * Test runner
 * ====================================================================== */

/*
 * run_test - Execute a single test case, print result.
 * Returns 0 on pass, non-zero on failure.
 */
static int run_test(const struct test_case *tc)
{
	int rc;

	tests_run_count++;
	printf("  [RUN ] %s\n", tc->name);

	rc = tc->func();
	if (rc == 0) {
		tests_passed_count++;
		printf("  [PASS] %s\n", tc->name);
	} else {
		tests_failed_count++;
		printf("  [FAIL] %s (rc=%d)\n", tc->name, rc);
	}

	return rc;
}

/*
 * RUN_ALL_TESTS - Iterate over a NULL-terminated array of test_case,
 * print summary, return number of failures (0 = all pass).
 */
#define RUN_ALL_TESTS(_tests)						\
	do {								\
		const struct test_case *_tc;				\
		tests_run_count = 0;					\
		tests_passed_count = 0;				\
		tests_failed_count = 0;				\
		printf("=== Running DMA-BUF test suite ===\n");		\
		for (_tc = (_tests); _tc->name != NULL; _tc++)		\
			run_test(_tc);					\
		printf("\n--- Results ---\n");				\
		printf("  Total : %d\n", tests_run_count);		\
		printf("  Passed: %d\n", tests_passed_count);		\
		printf("  Failed: %d\n", tests_failed_count);		\
		if (tests_failed_count > 0)				\
			printf("  *** SOME TESTS FAILED ***\n");		\
		else							\
			printf("  All tests passed.\n");			\
	} while (0)

#endif /* TEST_DMABUF_H */
