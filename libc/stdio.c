/* libc/stdio.c */
#include "stdio.h"
#include "errno.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

/* Static backing storage for the three standard streams.
   fd values match what the kernel guarantees on exec. */
static FILE _stdin = {.fd = STDIN_FILENO, .writable = 0};
static FILE _stdout = {.fd = STDOUT_FILENO, .writable = 1};
static FILE _stderr = {.fd = STDERR_FILENO, .writable = 1};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    if (mode[0] == 'r')
        flags = O_RDONLY;
    else if (mode[0] == 'w')
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a')
        flags = O_WRONLY | O_CREAT | O_APPEND;
    else {
        errno = EINVAL;
        return NULL;
    }
    if (mode[1] == '+' || (mode[1] && mode[2] == '+'))
        flags = O_RDWR;

    int fd = open(path, flags, 0644);
    if (fd < 0)
        return NULL;

    FILE *f = malloc(sizeof(FILE));
    if (!f) {
        close(fd);
        return NULL;
    }
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    f->buf_pos = 0;
    f->buf_len = 0;
    f->writable = (mode[0] != 'r');
    return f;
}

int fclose(FILE *stream) {
    if (!stream)
        return EOF;
    fflush(stream);
    int r = close(stream->fd);
    /* Never free the static stdin/stdout/stderr */
    if (stream != stdin && stream != stdout && stream != stderr)
        free(stream);
    return r;
}

/* Drain write buffer */
int fflush(FILE *stream) {
    if (!stream || !stream->writable)
        return 0;
    if (stream->buf_pos == 0)
        return 0;
    ssize_t n = write(stream->fd, stream->buf, stream->buf_pos);
    stream->buf_pos = 0;
    return (n < 0) ? EOF : 0;
}

int fputc(int c, FILE *stream) {
    /* Line-buffered: flush on newline or when buf full */
    stream->buf[stream->buf_pos++] = (char) c;
    if (c == '\n' || stream->buf_pos >= (int) sizeof(stream->buf))
        if (fflush(stream) == EOF)
            return EOF;
    return (unsigned char) c;
}

int fputs(const char *s, FILE *stream) {
    while (*s)
        if (fputc((unsigned char) *s++, stream) == EOF)
            return EOF;
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    fflush(stream); /* flush buffer before raw write */
    size_t total = size * nmemb;
    ssize_t n = write(stream->fd, ptr, total);
    if (n < 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t) n / (size ? size : 1);
}

/* Fill read buffer */
static int fill_buf(FILE *stream) {
    stream->buf_len = (int) read(stream->fd, stream->buf, sizeof(stream->buf));
    stream->buf_pos = 0;
    if (stream->buf_len <= 0) {
        stream->eof = (stream->buf_len == 0);
        stream->error = (stream->buf_len < 0);
        return EOF;
    }
    return 0;
}

int fgetc(FILE *stream) {
    if (stream->buf_pos >= stream->buf_len)
        if (fill_buf(stream) == EOF)
            return EOF;
    return (unsigned char) stream->buf[stream->buf_pos++];
}

char *fgets(char *s, int n, FILE *stream) {
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0)
                return NULL;
            break;
        }
        s[i++] = (char) c;
        if (c == '\n')
            break;
    }
    s[i] = '\0';
    return i ? s : NULL;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    size_t got = 0;
    char *dst = ptr;
    while (got < total) {
        int c = fgetc(stream);
        if (c == EOF)
            break;
        dst[got++] = (char) c;
    }
    return got / (size ? size : 1);
}

int fseek(FILE *stream, long offset, int whence) {
    fflush(stream);
    /* For SEEK_CUR, the fd is ahead of the logical position by the number
       of buffered-but-unread bytes.  Compensate before invalidating. */
    if (whence == SEEK_CUR)
        offset -= (stream->buf_len - stream->buf_pos);
    stream->buf_pos = stream->buf_len = 0; /* invalidate read buffer */
    stream->eof = 0;
    return (lseek(stream->fd, offset, whence) < 0) ? -1 : 0;
}

long ftell(FILE *stream) {
    long pos = (long) lseek(stream->fd, 0, SEEK_CUR);
    /* Adjust for buffered-but-unread bytes */
    pos -= (stream->buf_len - stream->buf_pos);
    return pos;
}

void rewind(FILE *stream) {
    fseek(stream, 0, SEEK_SET);
    stream->error = 0;
}

int feof(FILE *stream) { return stream->eof; }
int ferror(FILE *stream) { return stream->error; }
int fileno(FILE *stream) { return stream->fd; }
