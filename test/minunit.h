/*
 * minunit.h
 *
 * See: http://www.jera.com/techinfo/jtns/jtn002.html
 *
 */

#ifndef MINUNIT_H
#define MINUNIT_H

#define mu_assert(test, message) do { if (!(test)) return message; } while (0)
#define mu_run_test(test, ...) do { char *message = test(__VA_ARGS__); tests_run++; \
                               if (message) return message; } while (0)

extern int tests_run;

#endif /* !MINUNIT_H */
