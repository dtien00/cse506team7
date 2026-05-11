/*
 * getopt / getopt_long / getopt_long_only
 *
 * POSIX getopt plus GNU long-option extensions.
 *
 * Reset protocol: set optind = 0 before the first call (or between parses)
 * to reinitialize internal state.  Setting optind = 1 without clearing the
 * internal position counter can leave the parser mid-cluster; optind = 0 is
 * the reliable reset (BSD / glibc convention).
 */

#include "unistd.h"
#include "string.h"
#include "stdio.h"
#include "getopt.h"

/* POSIX-required globals; declared extern in unistd.h and getopt.h */
char *optarg = (char *)0;
int   opterr = 1;
int   optind = 1;
int   optopt = 0;

/* position within the current argv[optind] cluster (e.g. -abc: tracks b, c) */
static int optpos = 0;

/* Returns non-zero if optstring requests silent error mode (':' prefix). */
static int is_silent(const char *os)
{
    return *os == ':' || ((*os == '+' || *os == '-') && *(os + 1) == ':');
}

/*
 * getopt — parse short options.
 *
 * Behaviour matches POSIX.1-2024:
 *   - Stops at the first non-option argument (POSIX mode).
 *   - '--' terminates option processing; optind is left past '--'.
 *   - Unknown option: sets optopt, returns '?'.
 *   - Missing required argument: sets optopt, returns ':' (silent) or '?'.
 *   - Optional argument (::): only consumed when adjacent (no space).
 */
int getopt(int argc, char * const argv[], const char *optstring)
{
    /* optind == 0 is the reset sentinel */
    if (optind == 0) {
        optind = 1;
        optpos = 0;
    }
    optarg = (char *)0;

    /* Start of a new argument: decide whether it looks like an option. */
    if (optpos == 0) {
        if (optind >= argc || !argv[optind])
            return -1;
        /* '-' alone is a non-option (often means stdin) */
        if (argv[optind][0] != '-' || argv[optind][1] == '\0')
            return -1;
        /* '--' terminates option processing */
        if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
            optind++;
            return -1;
        }
        optpos = 1; /* skip the leading '-' */
    }

    int c = (unsigned char)argv[optind][optpos++];
    int at_end = (argv[optind][optpos] == '\0');
    if (at_end) { optind++; optpos = 0; }

    /* Locate c in optstring, skipping GNU mode prefix and the silence ':'. */
    const char *os = optstring;
    if (*os == '+' || *os == '-') os++;
    if (*os == ':') os++;
    /* ':' is not a valid option character */
    const char *p = (c != ':') ? strchr(os, c) : (char *)0;

    if (!p) {
        optopt = c;
        if (!is_silent(optstring) && opterr)
            fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
        return '?';
    }

    /* No argument required */
    if (p[1] != ':')
        return c;

    int opt_optional = (p[2] == ':');

    if (!at_end) {
        /* Argument is the rest of the current cluster. */
        optarg = &argv[optind][optpos];
        optind++;
        optpos = 0;
    } else if (opt_optional) {
        /* Optional argument absent (no adjacent value). */
        optarg = (char *)0;
    } else if (optind >= argc) {
        optopt = c;
        if (!is_silent(optstring) && opterr)
            fprintf(stderr, "%s: option requires an argument -- %c\n",
                    argv[0], c);
        return is_silent(optstring) ? ':' : '?';
    } else {
        optarg = argv[optind++];
    }

    return c;
}

/*
 * match_long — try to match argv[optind] (which starts with '--' or '-' for
 * getopt_long_only) against longopts.
 *
 * name     : points to the option name (past the leading dashes).
 * namelen  : length of the name (up to '=' if present).
 * eq       : pointer to '=' in argv[optind], or NULL.
 *
 * Returns the matched index or -1.  Sets *ambiguous if multiple prefix
 * matches exist with no exact match.
 */
static int match_long(const char *name, size_t namelen,
                      const struct option *longopts, int *ambiguous)
{
    int match = -1;
    *ambiguous = 0;
    int i;
    for (i = 0; longopts[i].name; i++) {
        if (strncmp(longopts[i].name, name, namelen) != 0)
            continue;
        if (strlen(longopts[i].name) == namelen) {
            /* Exact match — immediately wins */
            *ambiguous = 0;
            return i;
        }
        /* Prefix match */
        if (match == -1)
            match = i;
        else
            *ambiguous = 1;
    }
    return match;
}

/*
 * handle_long_match — consume argv[optind] for a confirmed long-option match
 * and set optarg / longindex.  Advances optind.
 *
 * Returns the value to return from getopt_long (0 or option val, or '?'/':').
 */
static int handle_long_match(int argc, char * const argv[],
                             const char *optstring,
                             const struct option *longopts, int *longindex,
                             int match, const char *eq, const char *dash_str)
{
    if (longindex)
        *longindex = match;

    switch (longopts[match].has_arg) {
    case no_argument:
        if (eq) {
            if (!is_silent(optstring) && opterr)
                fprintf(stderr, "%s: option '%s%s' doesn't allow an argument\n",
                        argv[0], dash_str, longopts[match].name);
            optopt = longopts[match].val;
            optind++;
            return '?';
        }
        optarg = (char *)0;
        break;

    case required_argument:
        if (eq) {
            optarg = (char *)(eq + 1);
        } else {
            optind++;
            if (optind >= argc) {
                if (!is_silent(optstring) && opterr)
                    fprintf(stderr,
                            "%s: option '%s%s' requires an argument\n",
                            argv[0], dash_str, longopts[match].name);
                optopt = longopts[match].val;
                return is_silent(optstring) ? ':' : '?';
            }
            optarg = argv[optind];
        }
        break;

    case optional_argument:
        optarg = eq ? (char *)(eq + 1) : (char *)0;
        break;
    }

    optind++;

    if (longopts[match].flag) {
        *longopts[match].flag = longopts[match].val;
        return 0;
    }
    return longopts[match].val;
}

/*
 * getopt_long — parse long options (--name, --name=value) plus short options.
 */
int getopt_long(int argc, char * const argv[], const char *optstring,
                const struct option *longopts, int *longindex)
{
    if (optind == 0) { optind = 1; optpos = 0; }
    optarg = (char *)0;

    if (!optstring) optstring = "";

    /* Mid-cluster: delegate to short-option parser */
    if (optpos != 0)
        return getopt(argc, argv, optstring);

    if (optind >= argc || !argv[optind])
        return -1;

    /* Long option: starts with '--' (and isn't just '--') */
    if (argv[optind][0] == '-' && argv[optind][1] == '-') {
        if (argv[optind][2] == '\0') { optind++; return -1; }

        const char *name    = argv[optind] + 2;
        const char *eq      = strchr(name, '=');
        size_t      namelen = eq ? (size_t)(eq - name) : strlen(name);

        int ambiguous;
        int match = match_long(name, namelen, longopts, &ambiguous);

        if (ambiguous) {
            if (!is_silent(optstring) && opterr)
                fprintf(stderr, "%s: option '--%.*s' is ambiguous\n",
                        argv[0], (int)namelen, name);
            optopt = 0;
            optind++;
            return '?';
        }
        if (match == -1) {
            if (!is_silent(optstring) && opterr)
                fprintf(stderr, "%s: unrecognized option '--%.*s'\n",
                        argv[0], (int)namelen, name);
            optopt = 0;
            optind++;
            return '?';
        }

        return handle_long_match(argc, argv, optstring, longopts,
                                 longindex, match, eq, "--");
    }

    /* Short option or non-option: delegate */
    return getopt(argc, argv, optstring);
}

/*
 * getopt_long_only — like getopt_long but also recognises long options that
 * start with a single '-'.  A single-dash argument is tried as a long option
 * first; if it doesn't match any long option it falls back to short-option
 * parsing so that single-character options still work.
 */
int getopt_long_only(int argc, char * const argv[], const char *optstring,
                     const struct option *longopts, int *longindex)
{
    if (optind == 0) { optind = 1; optpos = 0; }
    optarg = (char *)0;

    if (!optstring) optstring = "";

    if (optpos != 0)
        return getopt(argc, argv, optstring);

    if (optind >= argc || !argv[optind])
        return -1;

    /* '--…' always uses getopt_long logic */
    if (argv[optind][0] == '-' && argv[optind][1] == '-')
        return getopt_long(argc, argv, optstring, longopts, longindex);

    /* '-name': try long match before falling back to short */
    if (argv[optind][0] == '-' && argv[optind][1] != '\0') {
        const char *name    = argv[optind] + 1;
        const char *eq      = strchr(name, '=');
        size_t      namelen = eq ? (size_t)(eq - name) : strlen(name);

        int ambiguous;
        int match = match_long(name, namelen, longopts, &ambiguous);

        if (!ambiguous && match != -1)
            return handle_long_match(argc, argv, optstring, longopts,
                                     longindex, match, eq, "-");
    }

    return getopt(argc, argv, optstring);
}