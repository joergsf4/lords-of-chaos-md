/*
 * The libc subset the platform-free core uses (see inc/string.h, inc/stdio.h).
 * Built against SGDK only, never against the shims.
 */
#include <genesis.h>
#include <stdarg.h>

void *loc_memcpy(void *dst, const void *src, u32 n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n > 0x8000) {
        memcpy(d, s, 0x8000);
        d += 0x8000;
        s += 0x8000;
        n -= 0x8000;
    }
    if (n)
        memcpy(d, s, (u16)n);
    return dst;
}

void *loc_memmove(void *dst, const void *src, u32 n)
{
    u8 *d = dst;
    const u8 *s = src;
    if (d == s || !n)
        return dst;
    if (d + n <= s || s + n <= d)          /* no overlap: the fast routine */
        return loc_memcpy(dst, src, n);
    if (d < s) {                           /* overlapping, copy forwards */
        u32 i;
        for (i = 0; i < n; i++)
            d[i] = s[i];
    } else {                               /* overlapping, copy backwards */
        while (n--)
            d[n] = s[n];
    }
    return dst;
}

void *loc_memset(void *dst, int c, u32 n)
{
    u8 *d = dst;
    while (n > 0x8000) {
        memset(d, (u8)c, 0x8000);
        d += 0x8000;
        n -= 0x8000;
    }
    if (n)
        memset(d, (u8)c, (u16)n);
    return dst;
}

int loc_memcmp(const void *a, const void *b, u32 n)
{
    const u8 *p = a, *q = b;
    while (n--) {
        if (*p != *q)
            return *p < *q ? -1 : 1;
        p++;
        q++;
    }
    return 0;
}

u32 loc_strlen(const char *s)
{
    u32 n = 0;
    while (s[n])
        n++;
    return n;
}

int loc_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (u8)*a - (u8)*b;
}

char *loc_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

/* printf subset: %s %c %% %d %u %x %X with the flags - and 0, a width, a precision (strings: at most that many
 * characters; numbers: at least that many digits) and the l modifier. */
int loc_snprintf(char *buf, unsigned long n, const char *fmt, ...)
{
    va_list ap;
    u32 pos = 0;
    va_start(ap, fmt);
#define PUT(ch) do { if (pos + 1 < n) buf[pos] = (ch); pos++; } while (0)
#define PAD(count, ch) do { s16 _k; for (_k = 0; _k < (s16)(count); _k++) PUT(ch); } while (0)
    for (; *fmt; fmt++) {
        char tmp[12];
        u8 len, zero = 0, left = 0, lng = 0, neg = 0;
        s16 width = 0, prec = -1;
        u16 base = 10;
        u32 v;
        if (*fmt != '%') {
            PUT(*fmt);
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                zero = 1;
            else
                break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + (*fmt++ - '0');
        }
        if (*fmt == 'l') {
            lng = 1;
            fmt++;
        }
        switch (*fmt) {
        case 's': {
            const char *str = va_arg(ap, const char *);
            s16 sl = 0, k;
            while (str[sl] && (prec < 0 || sl < prec))
                sl++;
            if (!left)
                PAD(width - sl, ' ');
            for (k = 0; k < sl; k++)
                PUT(str[k]);
            if (left)
                PAD(width - sl, ' ');
            continue;
        }
        case 'c':
            if (!left)
                PAD(width - 1, ' ');
            PUT((char)va_arg(ap, int));
            if (left)
                PAD(width - 1, ' ');
            continue;
        case '%':
            PUT('%');
            continue;
        case 'd': {
            s32 sv = lng ? va_arg(ap, s32) : (s32)va_arg(ap, int);
            neg = sv < 0;
            v = neg ? (u32)-sv : (u32)sv;
            break;
        }
        case 'x':
        case 'X':
            base = 16;
            /* fall through */
        case 'u':
            v = lng ? va_arg(ap, u32) : (u32)va_arg(ap, unsigned int);
            break;
        default:
            continue;
        }
        len = 0;
        do {
            u8 dgt = (u8)(v % base);
            tmp[len++] = dgt < 10 ? '0' + dgt : (*fmt == 'x' ? 'a' : 'A') + dgt - 10;
            v /= base;
        } while (v);
        {
            s16 digits = len, zeros = prec > digits ? prec - digits : 0, total = digits + zeros + neg;
            if (!left && !(zero && prec < 0))
                PAD(width - total, ' ');
            if (neg)
                PUT('-');
            if (!left && zero && prec < 0)
                PAD(width - total, '0');
            PAD(zeros, '0');
            while (len) {                  /* (PUT does not evaluate its argument once the buffer is full) */
                char dch = tmp[len - 1];
                len--;
                PUT(dch);
            }
            if (left)
                PAD(width - total, ' ');
        }
    }
#undef PUT
#undef PAD
    va_end(ap);
    if (n)
        buf[pos < n ? pos : n - 1] = 0;
    return (int)pos;
}
