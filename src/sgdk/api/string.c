#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

int nb_sgdk_sprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int r = vsprintf(buf, fmt, args);
    va_end(args);
    return r;
}

int nb_sgdk_vsprintf(char *buf, const char *fmt, va_list args)
{
    return vsprintf(buf, fmt, args);
}

int nb_sgdk_snprintf(char *buf, int size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int r = vsnprintf(buf, (size_t)size, fmt, args);
    va_end(args);
    return r;
}

int nb_sgdk_vsnprintf(char *buf, int size, const char *fmt, va_list args)
{
    return vsnprintf(buf, (size_t)size, fmt, args);
}
