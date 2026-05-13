#!/bin/bash
# symbol_audit.sh — detect libc functions declared in headers but missing a definition.
#
# Usage:
#   tools/symbol_audit.sh              # run from repo root
#   tools/symbol_audit.sh /path/to/repo
#
# Exit 0 if all declared symbols have definitions, 1 if any are missing.
# Works purely from source — no build artifacts needed.

set -euo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
INCLUDES="$ROOT/libc/include"
LIBC="$ROOT/libc"

# C keyword / type names that are never function names.
C_KEYWORDS='int|char|void|long|short|unsigned|signed|const|static|extern|inline|
            restrict|volatile|register|auto|struct|union|enum|
            size_t|ssize_t|off_t|pid_t|uid_t|gid_t|mode_t|time_t|suseconds_t|
            clockid_t|clock_t|timer_t|locale_t|wchar_t|wint_t|
            intmax_t|uintmax_t|ptrdiff_t|intptr_t|uintptr_t|
            uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|
            nfds_t|ino_t|dev_t|nlink_t|blksize_t|blkcnt_t|fsblkcnt_t|fsfilcnt_t|
            socklen_t|sa_family_t|in_port_t|in_addr_t|
            sigset_t|sig_atomic_t|sigjmp_buf|jmp_buf|va_list|FILE|DIR|
            if|while|for|switch|return|sizeof|typeof|offsetof|
            __attribute__|__extension__|__asm__|__volatile__'

# AWK program shared by both declared() and defined_c().
# For each input line it finds all non-overlapping occurrences of
#   identifier(
# advances past the full match each time (avoiding in-word re-entry), keeps
# the last valid hit, and prints it.  Requires GNU awk for 3-arg match().
read -r -d '' AWK_EXTRACT <<'AWKEOF' || true
BEGIN {
    n = split(kw, arr, /[|[:space:]]+/)
    for (i = 1; i <= n; i++) kwtab[arr[i]] = 1
}
{
    line = $0
    # strip inline // comments so words in comments don't look like identifiers
    sub(/[[:space:]]*\/\/.*$/, "", line)
    # strip inline /* ... */ comments
    gsub(/\/\*[^*]*\*\//, "", line)
    # skip lines with function-pointer syntax: (* or (*name)
    if (line ~ /\(\s*\*/) next

    name = ""
    s = line
    # Use (^|[^a-zA-Z0-9_]) as a word-boundary guard before the identifier so
    # we never match a suffix of a mixed-case name (e.g. "ork" inside "_Fork").
    while (match(s, /(^|[^a-zA-Z0-9_])([a-z_][a-z0-9_]+)[[:space:]]*\(/, m)) {
        cand = m[2]
        if (!(cand in kwtab))
            name = cand
        s = substr(s, RSTART + RLENGTH)
    }
    if (name != "") print name
}
AWKEOF

# ── Phase 1: declared function names from headers ─────────────────────────────
declared() {
    grep -rh --include='*.h' -E '\(' "$INCLUDES"/ \
    | grep ';' \
    | grep -v -E '^\s*#' \
    | grep -v -E '^\s*/[/*]' \
    | grep -v -E '\*/' \
    | awk -v kw="$C_KEYWORDS" "$AWK_EXTRACT" \
    | sort -u
}

# ── Phase 2: defined function names from libc sources ─────────────────────────

# From .c files: lines starting at column 0 (top-level definitions).
defined_c() {
    grep -rh --include='*.c' -E '^[a-zA-Z_]' "$LIBC"/*.c \
    | grep -v -E '^#|^//' \
    | grep '(' \
    | awk -v kw="$C_KEYWORDS" "$AWK_EXTRACT" \
    | sort -u
}

# From .S files: .global labels with lowercase/underscore start.
defined_s() {
    grep -h -E '^\.global[[:space:]]+[a-z_]' "$LIBC"/*.S \
    | sed -E 's/^\.global[[:space:]]+//' \
    | sort -u
}

# ── Phase 3: diff and report ──────────────────────────────────────────────────

DECLARED=$(declared)
DEFINED=$(sort -u <(defined_c) <(defined_s))

MISSING=$(comm -23 \
    <(echo "$DECLARED") \
    <(echo "$DEFINED"))

# Symbols intentionally provided by the thirdparty compat layer
# (thirdparty/compat.c, thirdparty/dummy.c) rather than libc/ itself.
COMPAT_PROVIDED="
    abort arc4random arc4random_buf arc4random_uniform
    atof atoi atol atoll
    bsearch calloc cfgetispeed cfgetospeed cfsetispeed cfsetospeed
    clearenv clock confstr ctermid difftime
    execl execle execlp execv execvp
    fdopen fgetpos flockfile fmemopen freopen fseeko fsetpos ftello
    ftrylockfile funlockfile
    getenv getlogin getlogin_r gethostname
    gmtime gmtime_r localtime localtime_r mktime
    localeconv newlocale duplocale freelocale uselocale setlocale getlocalename_l
    memccpy memmem mempcpy
    mkstemp mkfifo
    open_memstream opendir readdir closedir rewinddir dirfd alphasort scandir
    pclose popen posix_close posix_madvise
    pthread_kill pthread_sigmask
    putenv rand rand_r random setenv srand srandom unsetenv
    remove rewind scanf fscanf sscanf vscanf vfscanf vsscanf
    setbuf setvbuf
    sig2str str2sig sigqueue sigwait sigwaitinfo psignal psiginfo
    siglongjmp sigsetjmp
    strcoll strcoll_l strxfrm
    strtod strtof strtold
    system tmpfile tmpnam
    ttyname ttyname_r tzset
    vdprintf
"

for sym in $COMPAT_PROVIDED; do
    MISSING=$(echo "$MISSING" | grep -v "^${sym}$" || true)
done

MISSING=$(echo "$MISSING" | grep -E '^[a-z_][a-z0-9_]{2,}' || true)

if [ -z "$MISSING" ]; then
    echo "symbol_audit: OK — all declared libc symbols have definitions"
    exit 0
else
    COUNT=$(echo "$MISSING" | wc -l | tr -d ' ')
    echo "symbol_audit: FAIL — $COUNT declared-only symbol(s) (no definition in libc/ sources):"
    echo "$MISSING" | sed 's/^/  MISSING: /'
    exit 1
fi
