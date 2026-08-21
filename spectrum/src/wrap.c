#include "wrap.h"

#define MAX_WORD 32

static char word[MAX_WORD + 1];
static unsigned char wlen;
static unsigned char col;
static unsigned char pending_space;
static unsigned char pending_nl;
static unsigned char utf8_lead; /* first byte of a multibyte sequence */
static wrap_sink out;
static unsigned char width;
static unsigned char src_col;     /* length of the line the model sent */
static unsigned char last_src_len;
/* Whether the line being read looks like BASIC: digits, a space, and more.
 * 0 = not yet, 1 = digits so far, 2 = confirmed. */
static unsigned char src_basic;
static unsigned char last_src_basic;

/* Accents down to their base letter. Only the C3 block of UTF-8, which is
 * where accented vowels and n-tilde live; that covers Spanish completely. */
static char fold_c3(unsigned char b)
{
    if (b >= 0x80 && b <= 0x85)
        return 'A';
    if (b == 0x87)
        return 'C';
    if (b >= 0x88 && b <= 0x8b)
        return 'E';
    if (b >= 0x8c && b <= 0x8f)
        return 'I';
    if (b == 0x91)
        return 'N';
    if (b >= 0x92 && b <= 0x96)
        return 'O';
    if (b >= 0x99 && b <= 0x9c)
        return 'U';
    if (b >= 0xa0 && b <= 0xa5)
        return 'a';
    if (b == 0xa7)
        return 'c';
    if (b >= 0xa8 && b <= 0xab)
        return 'e';
    if (b >= 0xac && b <= 0xaf)
        return 'i';
    if (b == 0xb1)
        return 'n';
    if (b >= 0xb2 && b <= 0xb6)
        return 'o';
    if (b >= 0xb9 && b <= 0xbc)
        return 'u';
    return 0; /* anything we do not know about, dropped */
}

void wrap_begin(wrap_sink sink, unsigned char cols)
{
    out = sink;
    width = cols;
    wlen = 0;
    col = 0;
    pending_space = 0;
    pending_nl = 0;
    utf8_lead = 0;
    src_col = 0;
    last_src_len = 0;
    src_basic = 0;
    last_src_basic = 0;
}

static void emit_newline(void)
{
    out('\n');
    col = 0;
    pending_space = 0;
}

/* Flushes the pending word, splitting it if it does not fit on one line. */
static void flush_word(void)
{
    unsigned char i = 0;

    while (i < wlen) {
        unsigned char frac_num = wlen - i;

        if (col == 0) {
            unsigned char fits = (frac_num > width) ? width : frac_num;
            unsigned char j;

            for (j = 0; j < fits; j++)
                out(word[i + j]);
            i += fits;
            col = fits;
            pending_space = 0;
            if (i < wlen)
                emit_newline();
        } else if (col + (pending_space ? 1 : 0) + frac_num <= width) {
            unsigned char j;

            if (pending_space) {
                out(' ');
                col++;
            }
            for (j = 0; j < frac_num; j++)
                out(word[i + j]);
            col += frac_num;
            i = wlen;
            pending_space = 0;
        } else {
            emit_newline();
        }
    }

    wlen = 0;
}

/* Decides what to do with the newlines the model sent. */
static void resolve_nl(unsigned char intencionado)
{
    unsigned char n = pending_nl;

    pending_nl = 0;

    if (n == 1 && !intencionado) {
        flush_word();
        if (col > 0)
            pending_space = 1;
        return;
    }

    flush_word();
    if (col > 0)
        emit_newline();
    if (n >= 2)
        emit_newline(); /* line en blanco entre parrafos */
}

void wrap_putc(char c)
{
    unsigned char b = (unsigned char)c;

    /* UTF-8: keep the C3 block (accents) and drop the rest. */
    if (utf8_lead) {
        unsigned char lead = utf8_lead;
        utf8_lead = 0;
        if (lead == 0xc3) {
            char base = fold_c3(b);
            if (!base)
                return;
            b = (unsigned char)base;
        } else {
            return; /* signos de apertura, comillas raras, emoji... */
        }
    } else if (b >= 0xc0) {
        utf8_lead = b;
        return;
    } else if (b >= 0x80) {
        return; /* stray continuation byte: junk */
    }

    if (b == '\n') {
        if (pending_nl == 0) {
            last_src_len = src_col;
            last_src_basic = (unsigned char)(src_basic == 2);
        }
        pending_nl++;
        src_col = 0;
        src_basic = 0;
        return;
    }

    if (b < 32 || b > 126)
        return;

    if (pending_nl) {
        /* Not every newline from the model is noise: the ones closing an
         * almost-full line are its own wrapping (and fight with ours), but
         * the ones closing a short line were deliberate. A leading dash opens
         * a list item, which is respected too.
         *
         * A line of BASIC always keeps its newline, however long it is. A
         * long one -"20 IF x1<0 OR x1>255 THEN GO TO 20"- looked exactly like
         * the model wrapping its own prose, so the break after it was eaten
         * and the next sentence ended up glued to the code. */
        unsigned char was_wrapping = (last_src_len + 8 >= width);
        resolve_nl((b == '-' && wlen == 0) || last_src_basic || !was_wrapping);
    }

    /* Track whether this source line starts like a BASIC line. */
    if (src_basic != 2) {
        if (b >= '0' && b <= '9')
            src_basic = (unsigned char)(src_col < 4 ? 1 : src_basic);
        else if (b == ' ' && src_basic == 1)
            src_basic = 2;
        else
            src_basic = 3; /* neither: give up on this line */
    }

    src_col++;

    if (b == ' ') {
        flush_word();
        if (col > 0)
            pending_space = 1;
        return;
    }

    word[wlen++] = (char)b;
    if (wlen >= MAX_WORD)
        flush_word();
}

void wrap_end(void)
{
    pending_nl = 0;
    src_col = 0;
    flush_word();
    if (col > 0)
        emit_newline();
}
