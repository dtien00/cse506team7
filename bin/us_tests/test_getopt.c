/*
 * test_getopt.c — correctness tests for getopt() and getopt_long().
 *
 * Covers: short options, clusters, required/optional arguments, '--'
 * terminator, POSIX non-option stop, unknown options, missing arguments,
 * silent-error mode, optind/optarg/optopt values, long options (exact and
 * prefix match, '=' and separate-arg forms, optional-argument, flag
 * pointer, longindex), mixed short+long, and state reset.
 */

#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include "us_test_common.h"

/* Reset parser state between sub-tests.
 * optind = 0 is the BSD/GNU reset sentinel: getopt resets optpos on entry. */
static void reset_getopt(void)
{
    optind = 0;
    optarg = (char *)0;
    optopt = 0;
    opterr = 0; /* suppress error messages during tests */
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  getopt() tests                                                             */
/* ─────────────────────────────────────────────────────────────────────────── */

void run_test_getopt(us_test_group_t *group)
{
    ut_group_begin(group, "getopt");

    /* ── basic independent short options ─────────────────────────────────── */
    {
        char *argv[] = {"prog", "-a", "-b", (char *)0};
        int argc = 3;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "ab") == 'a', "basic -a");
        ut_assert(group, getopt(argc, argv, "ab") == 'b', "basic -b");
        ut_assert(group, getopt(argc, argv, "ab") == -1,  "basic: done");
    }

    /* ── clustered short options: -ab ────────────────────────────────────── */
    {
        char *argv[] = {"prog", "-ab", (char *)0};
        int argc = 2;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "ab") == 'a', "cluster -ab: a");
        ut_assert(group, getopt(argc, argv, "ab") == 'b', "cluster -ab: b");
        ut_assert(group, getopt(argc, argv, "ab") == -1,  "cluster -ab: done");
    }

    /* ── required argument: inline (-oVALUE) ─────────────────────────────── */
    {
        char *argv[] = {"prog", "-oVALUE", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt(argc, argv, "o:");
        ut_assert(group, r == 'o', "reqarg inline: char");
        ut_assert(group, optarg && strcmp(optarg, "VALUE") == 0,
                  "reqarg inline: optarg==VALUE");
    }

    /* ── required argument: separate (-o VALUE) ──────────────────────────── */
    {
        char *argv[] = {"prog", "-o", "VALUE", (char *)0};
        int argc = 3;
        reset_getopt();
        int r = getopt(argc, argv, "o:");
        ut_assert(group, r == 'o', "reqarg sep: char");
        ut_assert(group, optarg && strcmp(optarg, "VALUE") == 0,
                  "reqarg sep: optarg==VALUE");
    }

    /* ── optional argument: present (adjacent) ───────────────────────────── */
    {
        char *argv[] = {"prog", "-oVALUE", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt(argc, argv, "o::");
        ut_assert(group, r == 'o', "optarg present: char");
        ut_assert(group, optarg && strcmp(optarg, "VALUE") == 0,
                  "optarg present: optarg==VALUE");
    }

    /* ── optional argument: absent (no adjacent value) ───────────────────── */
    {
        char *argv[] = {"prog", "-o", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt(argc, argv, "o::");
        ut_assert(group, r == 'o',       "optarg absent: char");
        ut_assert(group, optarg == (char *)0, "optarg absent: optarg null");
    }

    /* ── optional arg: next argv NOT consumed ────────────────────────────── */
    {
        char *argv[] = {"prog", "-o", "next", (char *)0};
        int argc = 3;
        reset_getopt();
        getopt(argc, argv, "o::");
        /* optind should point to "next" — it was not consumed */
        ut_assert(group, optind == 2 && strcmp(argv[optind], "next") == 0,
                  "optarg absent: next argv not consumed");
    }

    /* ── '--' terminates option processing ───────────────────────────────── */
    {
        char *argv[] = {"prog", "-a", "--", "-b", (char *)0};
        int argc = 4;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "ab") == 'a', "-- : consume -a");
        ut_assert(group, getopt(argc, argv, "ab") == -1,  "-- : stops");
        ut_assert(group, optind == 3, "-- : optind past --");
    }

    /* ── POSIX: non-option stops processing ──────────────────────────────── */
    {
        char *argv[] = {"prog", "file", "-a", (char *)0};
        int argc = 3;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "a") == -1, "noopt: stops");
        ut_assert(group, optind == 1, "noopt: optind unchanged");
    }

    /* ── unknown option: sets optopt, returns '?' ────────────────────────── */
    {
        char *argv[] = {"prog", "-z", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt(argc, argv, "ab");
        ut_assert(group, r == '?',       "unknown: returns ?");
        ut_assert(group, optopt == 'z',  "unknown: optopt==z");
    }

    /* ── missing required argument: silent mode returns ':' ─────────────── */
    {
        char *argv[] = {"prog", "-o", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt(argc, argv, ":o:");
        ut_assert(group, r == ':',       "missing (silent): returns :");
        ut_assert(group, optopt == 'o',  "missing (silent): optopt==o");
    }

    /* ── missing required argument: non-silent returns '?' ──────────────── */
    {
        char *argv[] = {"prog", "-o", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt(argc, argv, "o:");
        ut_assert(group, r == '?',       "missing (noisy): returns ?");
        ut_assert(group, optopt == 'o',  "missing (noisy): optopt==o");
    }

    /* ── optind positioned correctly after all options ───────────────────── */
    {
        char *argv[] = {"prog", "-a", "file1", "file2", (char *)0};
        int argc = 4;
        reset_getopt();
        getopt(argc, argv, "a"); /* consumes -a */
        getopt(argc, argv, "a"); /* hits non-option, returns -1 */
        ut_assert(group, optind == 2, "optind: points to first non-opt");
    }

    /* ── reset via optind = 0 allows re-parsing ──────────────────────────── */
    {
        char *argv[] = {"prog", "-a", (char *)0};
        int argc = 2;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "a") == 'a', "reset: first parse");
        ut_assert(group, getopt(argc, argv, "a") == -1,  "reset: exhausted");
        optind = 0; /* reset */
        ut_assert(group, getopt(argc, argv, "a") == 'a', "reset: second parse");
    }

    /* ── lone '-' is treated as a non-option (POSIX) ─────────────────────── */
    {
        char *argv[] = {"prog", "-", "file", (char *)0};
        int argc = 3;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "a") == -1, "lone -: stops");
        ut_assert(group, optind == 1, "lone -: optind at 1");
    }

    /* ── no arguments at all ─────────────────────────────────────────────── */
    {
        char *argv[] = {"prog", (char *)0};
        int argc = 1;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "a") == -1, "no args: -1");
    }

    /* ── cluster with required arg in middle: -ao VALUE ─────────────────── */
    {
        char *argv[] = {"prog", "-ao", "VALUE", (char *)0};
        int argc = 3;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "ao:") == 'a', "cluster+arg: a");
        int r = getopt(argc, argv, "ao:");
        ut_assert(group, r == 'o', "cluster+arg: o");
        ut_assert(group, optarg && strcmp(optarg, "VALUE") == 0,
                  "cluster+arg: optarg");
    }

    /* ── cluster with inline arg: -aoVALUE ───────────────────────────────── */
    {
        char *argv[] = {"prog", "-aoVALUE", (char *)0};
        int argc = 2;
        reset_getopt();
        ut_assert(group, getopt(argc, argv, "ao:") == 'a', "cluster inline arg: a");
        int r = getopt(argc, argv, "ao:");
        ut_assert(group, r == 'o', "cluster inline arg: o");
        ut_assert(group, optarg && strcmp(optarg, "VALUE") == 0,
                  "cluster inline arg: optarg==VALUE");
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  getopt_long() tests                                                        */
/* ─────────────────────────────────────────────────────────────────────────── */

void run_test_getopt_long(us_test_group_t *group)
{
    ut_group_begin(group, "getopt_long");

    static const struct option long_opts[] = {
        /* index 0 */ {"verbose", no_argument,       (int *)0, 'v'},
        /* index 1 */ {"output",  required_argument, (int *)0, 'o'},
        /* index 2 */ {"mode",    optional_argument, (int *)0, 'm'},
        /* index 3 */ {"help",    no_argument,       (int *)0, 'h'},
        {(char *)0, 0, (int *)0, 0}
    };

    /* ── basic long option (no argument) ────────────────────────────────── */
    {
        char *argv[] = {"prog", "--verbose", (char *)0};
        int argc = 2;
        reset_getopt();
        ut_assert(group, getopt_long(argc, argv, "vomh", long_opts, 0) == 'v',
                  "long --verbose");
        ut_assert(group, getopt_long(argc, argv, "vomh", long_opts, 0) == -1,
                  "long: done");
    }

    /* ── long option with required argument via '=' ──────────────────────── */
    {
        char *argv[] = {"prog", "--output=file.txt", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt_long(argc, argv, "o:", long_opts, 0);
        ut_assert(group, r == 'o', "long --output=: char");
        ut_assert(group, optarg && strcmp(optarg, "file.txt") == 0,
                  "long --output=: optarg");
    }

    /* ── long option with required argument in next argv ────────────────── */
    {
        char *argv[] = {"prog", "--output", "file.txt", (char *)0};
        int argc = 3;
        reset_getopt();
        int r = getopt_long(argc, argv, "o:", long_opts, 0);
        ut_assert(group, r == 'o', "long --output sep: char");
        ut_assert(group, optarg && strcmp(optarg, "file.txt") == 0,
                  "long --output sep: optarg");
    }

    /* ── long option with optional argument present (=value) ─────────────── */
    {
        char *argv[] = {"prog", "--mode=fast", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt_long(argc, argv, "m::", long_opts, 0);
        ut_assert(group, r == 'm', "long --mode=fast: char");
        ut_assert(group, optarg && strcmp(optarg, "fast") == 0,
                  "long --mode=fast: optarg");
    }

    /* ── long option with optional argument absent ───────────────────────── */
    {
        char *argv[] = {"prog", "--mode", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt_long(argc, argv, "m::", long_opts, 0);
        ut_assert(group, r == 'm',            "long --mode no arg: char");
        ut_assert(group, optarg == (char *)0, "long --mode no arg: null");
    }

    /* ── longindex is set to the correct index ───────────────────────────── */
    {
        char *argv[] = {"prog", "--help", (char *)0};
        int argc = 2;
        int idx = -1;
        reset_getopt();
        int r = getopt_long(argc, argv, "h", long_opts, &idx);
        ut_assert(group, r == 'h',  "longindex: char");
        ut_assert(group, idx == 3,  "longindex: idx==3");
    }

    /* ── flag pointer: returns 0, writes val into *flag ─────────────────── */
    {
        int flagval = 0;
        struct option flag_opts[] = {
            {"flag", no_argument, &flagval, 42},
            {(char *)0, 0, (int *)0, 0}
        };
        char *argv[] = {"prog", "--flag", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt_long(argc, argv, "", flag_opts, 0);
        ut_assert(group, r == 0,        "flag ptr: returns 0");
        ut_assert(group, flagval == 42, "flag ptr: *flag==42");
    }

    /* ── short option still works via getopt_long ────────────────────────── */
    {
        char *argv[] = {"prog", "-v", "-o", "out.txt", (char *)0};
        int argc = 4;
        reset_getopt();
        ut_assert(group, getopt_long(argc, argv, "vo:", long_opts, 0) == 'v',
                  "long+short: -v");
        int r = getopt_long(argc, argv, "vo:", long_opts, 0);
        ut_assert(group, r == 'o', "long+short: -o");
        ut_assert(group, optarg && strcmp(optarg, "out.txt") == 0,
                  "long+short: optarg");
    }

    /* ── unknown long option returns '?' ─────────────────────────────────── */
    {
        char *argv[] = {"prog", "--unknown", (char *)0};
        int argc = 2;
        reset_getopt();
        ut_assert(group, getopt_long(argc, argv, "", long_opts, 0) == '?',
                  "unknown long: ?");
    }

    /* ── prefix match: --verb matches --verbose ──────────────────────────── */
    {
        char *argv[] = {"prog", "--verb", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt_long(argc, argv, "v", long_opts, 0);
        ut_assert(group, r == 'v', "prefix: --verb→verbose");
    }

    /* ── exact match wins over prefix when names share a stem ───────────── */
    {
        struct option stem_opts[] = {
            {"output",      required_argument, (int *)0, 'o'},
            {"output-file", required_argument, (int *)0, 'f'},
            {(char *)0, 0, (int *)0, 0}
        };
        char *argv[] = {"prog", "--output=x", (char *)0};
        int argc = 2;
        int idx = -1;
        reset_getopt();
        int r = getopt_long(argc, argv, "of:", stem_opts, &idx);
        ut_assert(group, r == 'o',  "exact over prefix: char");
        ut_assert(group, idx == 0,  "exact over prefix: index==0");
    }

    /* ── ambiguous prefix returns '?' ────────────────────────────────────── */
    {
        struct option amb_opts[] = {
            {"foobar", no_argument, (int *)0, 1},
            {"foobaz", no_argument, (int *)0, 2},
            {(char *)0, 0, (int *)0, 0}
        };
        char *argv[] = {"prog", "--foo", (char *)0};
        int argc = 2;
        reset_getopt();
        ut_assert(group, getopt_long(argc, argv, "", amb_opts, 0) == '?',
                  "ambig prefix: ?");
    }

    /* ── '--' terminates in getopt_long ──────────────────────────────────── */
    {
        char *argv[] = {"prog", "--verbose", "--", "--help", (char *)0};
        int argc = 4;
        reset_getopt();
        ut_assert(group, getopt_long(argc, argv, "vh", long_opts, 0) == 'v',
                  "long -- : -v");
        ut_assert(group, getopt_long(argc, argv, "vh", long_opts, 0) == -1,
                  "long -- : stops");
        ut_assert(group, optind == 3, "long -- : optind past --");
    }

    /* ── missing required argument for long option (silent) ─────────────── */
    {
        char *argv[] = {"prog", "--output", (char *)0};
        int argc = 2;
        reset_getopt();
        int r = getopt_long(argc, argv, ":o:", long_opts, 0);
        ut_assert(group, r == ':',           "long missing (silent): :");
        ut_assert(group, optopt == 'o',      "long missing (silent): optopt");
    }

    /* ── long option with no-argument disallows '=value' ────────────────── */
    {
        char *argv[] = {"prog", "--verbose=oops", (char *)0};
        int argc = 2;
        reset_getopt();
        ut_assert(group, getopt_long(argc, argv, "v", long_opts, 0) == '?',
                  "long no-arg with =: ?");
    }

    /* ── mixed sequence: short, long, short, long ────────────────────────── */
    {
        char *argv[] = {"prog", "-v", "--output=a.out", "-h", "--mode", (char *)0};
        int argc = 5;
        reset_getopt();
        ut_assert(group, getopt_long(argc, argv, "voh m", long_opts, 0) == 'v',
                  "mixed: -v");
        ut_assert(group, getopt_long(argc, argv, "vohm", long_opts, 0) == 'o',
                  "mixed: --output");
        ut_assert(group, optarg && strcmp(optarg, "a.out") == 0,
                  "mixed: --output optarg");
        ut_assert(group, getopt_long(argc, argv, "vohm", long_opts, 0) == 'h',
                  "mixed: -h");
        ut_assert(group, getopt_long(argc, argv, "vohm", long_opts, 0) == 'm',
                  "mixed: --mode");
        ut_assert(group, optarg == (char *)0, "mixed: --mode optarg null");
        ut_assert(group, getopt_long(argc, argv, "vohm", long_opts, 0) == -1,
                  "mixed: done");
    }
}