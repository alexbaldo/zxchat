#include "tokenize.h"

/* Rules checked against makebas, byte for byte, on nine programs:
 *
 *   - A number is stored TWICE: its digits in ASCII and, after them, 0x0E
 *     plus five bytes with the value. For integers from 0 to 65535 the ROM
 *     uses a short form, 00 00 low high 00. For anything else, floating
 *     point: exponent biased by 128 and a 32-bit mantissa normalised to
 *     [0.5,1), whose top bit -always 1- is replaced by the sign.
 *
 *     Computed here with 32-bit integers, pulling the mantissa out bit by
 *     bit, to avoid dragging in the whole floating point library. Validated
 *     against makebas with 24 numbers, from 0.000001 to 99999.99.
 *   - Keywords absorb the space next to them: the ROM already carries it in
 *     their text. "LET a" is stored as <LET>a, with no space.
 *   - Nothing is tokenised inside quotes: PRINT "LET x" keeps LET as text.
 *   - Nor after REM: the rest of the line is literal.
 *   - The LONGEST match wins. Otherwise "ATN" would be stored as AT followed
 *     by a stray N, and "INKEY$" as IN.
 */

#define TOKEN_TABLE 0x0095
#define FIRST_TOKEN 0xa4
#define TOK_REM 0xea
#define LAST_TOKEN 0xff

/* Compares a table keyword with the text, ignoring the spaces the ROM puts
 * around it. Returns how many characters of text it consumes, or 0. */
static unsigned char keyword_matches(const unsigned char *tabla, const char *text)
{
    unsigned char n = 0;

    /* The ROM stores some keywords with a leading space ("GO TO" is "GO "
     * plus "TO"), so skip any extra ones on the left. */
    while ((*tabla & 0x7f) == ' ' && (*tabla & 0x80) == 0)
        tabla++;

    for (;;) {
        unsigned char c = *tabla++;
        unsigned char letter = (unsigned char)(c & 0x7f);

        if (letter == ' ' && (c & 0x80)) /* trailing space: does not count */
            return n;

        if (letter != (unsigned char)text[n])
            return 0;

        n++;
        if (c & 0x80)
            return n;
    }
}

/* Finds the longest keyword starting at `text`.
 * Returns its code, or 0 if there is none. `match_len` receives its length. */
static unsigned char find_keyword(const char *text, unsigned char *match_len)
{
    const unsigned char *p = (const unsigned char *)TOKEN_TABLE;
    unsigned int t;
    unsigned char best = 0;
    unsigned char best_len = 0;

    for (t = FIRST_TOKEN; t <= LAST_TOKEN; t++) {
        unsigned char n = keyword_matches(p, text);

        if (n > best_len) {
            best_len = n;
            best = (unsigned char)t;
        }

        while ((*p & 0x80) == 0) /* on to the next entry */
            p++;
        p++;
    }

    *match_len = best_len;
    return best;
}

/* Leaves in dst the five bytes of the binary form of whole + rest/frac_den. */
static void float_form(unsigned char *dst, unsigned long whole, unsigned long rest,
                       unsigned long frac_den)
{
    unsigned long m = 0;
    unsigned long t;
    unsigned int e = 128;
    unsigned char n = 0;
    unsigned char k = 0;
    unsigned char guard = 0;

    for (t = whole; t; t >>= 1)
        k++;
    e += k;

    while (k > 0) { /* integer part bits, from highest to lowest */
        k--;
        m = (m << 1) | (unsigned long)((whole >> k) & 1);
        n++;
    }

    /* With no integer part we must find the first 1 of the fraction: that is
     * what sets the exponent. The guard stops it spinning if the number is 0. */
    while (n == 0 && guard < 64) {
        rest <<= 1;
        if (rest >= frac_den) {
            rest -= frac_den;
            m = 1;
            n = 1;
        } else {
            e--;
        }
        guard++;
    }

    while (n < 32) {
        unsigned char bit = 0;

        rest <<= 1;
        if (rest >= frac_den) {
            rest -= frac_den;
            bit = 1;
        }
        m = (m << 1) | (unsigned long)bit;
        n++;
    }

    rest <<= 1; /* one more bit, only to round */
    if (rest >= frac_den) {
        m++;
        if (m == 0) { /* overflowed: 0xFFFFFFFF + 1 */
            m = 0x80000000UL;
            e++;
        }
    }

    m &= 0x7fffffffUL; /* the top bit becomes the sign, and it is positive */

    dst[0] = (unsigned char)e;
    dst[1] = (unsigned char)(m >> 24);
    dst[2] = (unsigned char)(m >> 16);
    dst[3] = (unsigned char)(m >> 8);
    dst[4] = (unsigned char)m;
}

unsigned int tokenize_line(unsigned int num, const char *text, unsigned char *dst,
                             unsigned int max)
{
    unsigned int n = 4; /* leave room for the number and the length */
    unsigned char rem = 0;

    if (max < 8)
        return 0;

    while (*text && *text != '\n') {
        unsigned char c = (unsigned char)*text;
        unsigned char match_len;
        unsigned char tok;

        if (n + 8 > max)
            return 0;

        if (rem) {
            dst[n++] = c;
            text++;
            continue;
        }

        if (c == '"') {
            dst[n++] = c;
            text++;
            while (*text && *text != '\n' && n + 8 <= max) {
                dst[n++] = (unsigned char)*text;
                if (*text++ == '"')
                    break;
            }
            continue;
        }

        tok = find_keyword(text, &match_len);
        if (match_len > 0) {
            if (n > 4 && dst[n - 1] == ' ') /* the space before is redundant */
                n--;
            dst[n++] = tok;
            text += match_len;
            if (*text == ' ') /* and so is the one after */
                text++;
            if (tok == TOK_REM)
                rem = 1;
            continue;
        }

        if ((c >= '0' && c <= '9') || (c == '.' && text[1] >= '0' && text[1] <= '9')) {
            unsigned char previous = (n > 4) ? dst[n - 1] : 0;
            unsigned long whole = 0;
            unsigned long frac = 0;
            unsigned long frac_den = 1;
            unsigned char decimals = 0;

            /* A digit stuck to a LETTER is part of a variable name -X1, Y1,
              * a1- and gets no binary form. Only ASCII letters count: keyword
              * codes are above 0x80 and are not letters, however much the
              * character they map to may look like one. */
            if ((previous >= 'A' && previous <= 'Z') || (previous >= 'a' && previous <= 'z')) {
                while (*text >= '0' && *text <= '9' && n + 8 <= max)
                    dst[n++] = (unsigned char)*text++;
                continue;
            }

            while (*text >= '0' && *text <= '9' && n + 8 <= max) {
                whole = whole * 10 + (unsigned long)(*text - '0');
                dst[n++] = (unsigned char)*text++;
            }

            if (*text == '.' && text[1] >= '0' && text[1] <= '9') {
                dst[n++] = (unsigned char)*text++;
                /* nine digits is what the denominator takes in 32 bits */
                while (*text >= '0' && *text <= '9' && n + 8 <= max) {
                    if (decimals < 9) {
                        frac = frac * 10 + (unsigned long)(*text - '0');
                        frac_den *= 10;
                        decimals++;
                    }
                    dst[n++] = (unsigned char)*text++;
                }
            }

            dst[n++] = 0x0e;

            if (decimals == 0 && whole <= 65535UL) {
                /* short integer form */
                dst[n++] = 0x00;
                dst[n++] = 0x00;
                dst[n++] = (unsigned char)(whole & 0xff);
                dst[n++] = (unsigned char)(whole >> 8);
                dst[n++] = 0x00;
            } else {
                float_form(dst + n, whole, frac, frac_den);
                n += 5;
            }
            continue;
        }

        dst[n++] = c;
        text++;
    }

    if (n + 1 > max)
        return 0;
    dst[n++] = 0x0d;

    dst[0] = (unsigned char)(num >> 8);
    dst[1] = (unsigned char)(num & 0xff);
    dst[2] = (unsigned char)((n - 4) & 0xff);
    dst[3] = (unsigned char)((n - 4) >> 8);

    return n;
}
