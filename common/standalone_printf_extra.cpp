#include <standalone_printf.h>
#include <standalone_printf_extra.h>

static void string_out(void *cb_state, const char *s, size_t l)
{
    std::string *str = (std::string *)cb_state;

    if (l)
    {
        str->append(s, l);
    }
}

int standalone_stdstringprintf(std::string *s, const char *fmt, ...)
{
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = standalone_vcbprintf(s, string_out, fmt, ap);
    va_end(ap);
    return ret;
}
