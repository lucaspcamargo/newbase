#pragma once

#include "string.patched.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

int nb_sgdk_sprintf(char *buf, const char *fmt, ...);
int nb_sgdk_vsprintf(char *buf, const char *fmt, va_list args);
int nb_sgdk_snprintf(char *buf, int size, const char *fmt, ...);
int nb_sgdk_vsnprintf(char *buf, int size, const char *fmt, va_list args);

#ifdef __cplusplus
}
#endif

#define sprintf   nb_sgdk_sprintf
#define vsprintf  nb_sgdk_vsprintf
#define snprintf  nb_sgdk_snprintf
#define vsnprintf nb_sgdk_vsnprintf
