#ifndef STANDALONE_SCANF_H_
#define STANDALONE_SCANF_H_

#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
#define restrict

extern "C" {

#endif

int standalone_vcbscanf(void *restrict cb_state,
    int (*getc_cb)(void *state),
    void (*ungetc_cb)(void *state, int c),
    const char *restrict fmt, va_list ap);
int standalone_cbscanf(void *restrict cb_state,
    int (*getc_cb)(void *state),
    void (*ungetc_cb)(void *state, int c),
    const char *restrict fmt, ...);

int standalone_fscanf(FILE *restrict f, const char *restrict fmt, ...);
int standalone_sscanf(const char *restrict s, const char *restrict fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
