#include "screen.h"
#include <string.h>

/* CHARS (23606) points at the character set minus 256, so the glyph for
 * character c is at CHARS + c*8. The 96 printable ones are copied to RAM so
 * we do not depend on the ROM being paged in. */
#define SYSVAR_CHARS (*(unsigned int *)23606)

static unsigned char font[96 * 8];

/* Every line written is copied here as well as to the screen. A ring
 * buffer: logical line L lives in hist[L % SCR_HIST]. */
static char hist[SCR_HIST][SCR_COLS];
static unsigned char hist_attr[SCR_HIST];
static unsigned int hist_total; /* lines started since boot */
static unsigned char hist_back; /* cuantas lineas hemos subido */

static unsigned char cur_row;
static unsigned char cur_col;
static unsigned char cur_attr;

/* Address of a pixel byte: character row, column, pixel line 0..7.
 *
 * The screen file interleaves lines:
 *   high = 0x40 | (row>>3)<<3 | line
 *   low  = (row&7)<<5 | column
 */
static unsigned char *cell(unsigned char row, unsigned char col, unsigned char pix)
{
    return (unsigned char *)(0x4000u | ((unsigned int)(((row >> 3) << 3) | pix) << 8) |
                             ((unsigned int)(row & 7) << 5) | col);
}

static unsigned char *attr_at(unsigned char row, unsigned char col)
{
    return (unsigned char *)(0x5800u + ((unsigned int)row << 5) + col);
}

static void draw(unsigned char row, unsigned char col, char ch, unsigned char a)
{
    const unsigned char *g;
    unsigned char pix;

    if (ch < 32 || ch > 127)
        ch = '?';

    g = font + ((unsigned int)(ch - 32) << 3);
    for (pix = 0; pix < 8; pix++)
        *cell(row, col, pix) = *g++;

    *attr_at(row, col) = a;
}

static void clear_row(unsigned char row, unsigned char a)
{
    unsigned char pix;
    for (pix = 0; pix < 8; pix++)
        memset(cell(row, 0, pix), 0, SCR_COLS);
    memset(attr_at(row, 0), a, SCR_COLS);
}

static void write_row(unsigned char row, const char *s, unsigned char a)
{
    unsigned char col;
    for (col = 0; col < SCR_COLS; col++)
        draw(row, col, (*s) ? *s++ : ' ', a);
}

#define SCR_ROWS (SCR_CHAT_BOT - SCR_CHAT_TOP + 1)

/* Repaints the conversation area from the history. */
static void repaint(void)
{
    unsigned char f;

    for (f = 0; f < SCR_ROWS; f++) {
        unsigned int l = hist_total - hist_back - (SCR_ROWS - 1 - f);
        unsigned char row_of = (unsigned char)(SCR_CHAT_TOP + f);
        unsigned char col;

        /* hist_total - X wraps around if there are not that many lines yet */
        if (l > hist_total || hist_total - l >= SCR_HIST) {
            clear_row(row_of, ATTR_BOT);
            continue;
        }

        for (col = 0; col < SCR_COLS; col++)
            draw(row_of, col, hist[l % SCR_HIST][col], hist_attr[l % SCR_HIST]);
    }
}

unsigned char screen_scroll(signed char delta)
{
    unsigned char previous = hist_back;
    int tokenized = (int)hist_back - (int)delta;
    unsigned int limit = hist_total;

    if (limit > SCR_HIST - SCR_ROWS)
        limit = SCR_HIST - SCR_ROWS;

    if (tokenized < 0)
        tokenized = 0;
    if (tokenized > (int)limit)
        tokenized = (int)limit;

    hist_back = (unsigned char)tokenized;
    if (hist_back == previous)
        return 0;

    repaint();
    return 1;
}

/* Scrolls the conversation area up by one line. */
static void scroll(void)
{
    unsigned char row, pix;

    for (row = SCR_CHAT_TOP; row < SCR_CHAT_BOT; row++) {
        for (pix = 0; pix < 8; pix++)
            memcpy(cell(row, 0, pix), cell(row + 1, 0, pix), SCR_COLS);
        memcpy(attr_at(row, 0), attr_at(row + 1, 0), SCR_COLS);
    }
    clear_row(SCR_CHAT_BOT, cur_attr);
}

static void newline(void)
{
    unsigned char i;

    hist_total++;
    for (i = 0; i < SCR_COLS; i++)
        hist[hist_total % SCR_HIST][i] = ' ';
    hist_attr[hist_total % SCR_HIST] = cur_attr;

    cur_col = 0;
    if (cur_row >= SCR_CHAT_BOT)
        scroll();
    else
        cur_row++;
}

void screen_init(void)
{
    unsigned char row;

    memcpy(font, (void *)(SYSVAR_CHARS + 32 * 8), sizeof(font));

    cur_attr = ATTR_BOT;
    cur_row = SCR_CHAT_TOP;
    cur_col = 0;

    hist_total = 0;
    hist_back = 0;
    memset(hist[0], ' ', SCR_COLS);
    hist_attr[0] = ATTR_BOT;

    for (row = 0; row < 24; row++)
        clear_row(row, ATTR_BOT);

    clear_row(SCR_STATUS, ATTR_STATUS);
    clear_row(SCR_INPUT, ATTR_INPUT);

    /* Separator rule: a row of dim dashes. */
    for (row = 0; row < SCR_COLS; row++)
        draw(SCR_RULE, row, '-', ATTR(0, 0, 1));
}

void screen_ink(unsigned char attr)
{
    cur_attr = attr;
}

void screen_putc(char ch)
{
    /* If new text arrives while we are looking back, snap to the end:
     * staying up there while the model writes below is disorienting. */
    if (hist_back) {
        hist_back = 0;
        repaint();
    }

    if (ch == '\n') {
        newline();
        return;
    }
    if (ch < 32)
        return;

    /* Deferred wrap: reaching column 32 does not break the line yet, the
     * break happens when the next character is drawn. That way a '\n' right
     * after a full line does not leave a blank one. */
    if (cur_col >= SCR_COLS)
        newline();

    draw(cur_row, cur_col, ch, cur_attr);

    hist[hist_total % SCR_HIST][cur_col] = ch;
    hist_attr[hist_total % SCR_HIST] = cur_attr;

    cur_col++;
}

void screen_print(const char *s)
{
    while (*s)
        screen_putc(*s++);
}

void screen_num(int v)
{
    char tmp[6];
    unsigned char n = 0;

    if (v < 0) {
        screen_putc('-');
        v = -v;
    }
    if (v == 0) {
        screen_putc('0');
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        screen_putc(tmp[--n]);
}

void screen_newline_if_needed(void)
{
    if (cur_col != 0)
        newline();
}

/* Doubles every bit of the byte: 0b1011 -> 0b11001111. Top or bottom half
 * depending on `half`, which is the one each cell needs. */
static unsigned char stretch_bits(unsigned char b, unsigned char half)
{
    unsigned char r = 0;
    unsigned char i;

    if (half)
        b <<= 4;

    for (i = 0; i < 4; i++) {
        r <<= 2;
        if (b & 0x80)
            r |= 3;
        b <<= 1;
    }
    return r;
}

void screen_big(unsigned char row, unsigned char col, const char *s, unsigned char attr)
{
    while (*s && col < SCR_COLS - 1) {
        char ch = *s++;
        const unsigned char *g;
        unsigned char pix;

        if (ch < 32 || ch > 127)
            ch = '?';
        g = font + ((unsigned int)(ch - 32) << 3);

        for (pix = 0; pix < 8; pix++) {
            unsigned char line = g[pix >> 1];
            unsigned char half = (unsigned char)(pix & 1);

            /* each font line gives two pixel rows, and each one splits
              * into two cells: left and right */
            *cell(row, col, pix) = stretch_bits(line, 0);
            *cell(row, col + 1, pix) = stretch_bits(line, 1);
            *cell(row + 1, col, pix) = stretch_bits(g[4 + (pix >> 1)], 0);
            *cell(row + 1, col + 1, pix) = stretch_bits(g[4 + (pix >> 1)], 1);
        }

        *attr_at(row, col) = attr;
        *attr_at(row, col + 1) = attr;
        *attr_at(row + 1, col) = attr;
        *attr_at(row + 1, col + 1) = attr;

        col += 2;
    }
}

void screen_status(const char *s)
{
    write_row(SCR_STATUS, s, ATTR_STATUS);
}

/* Draws the input line as a sliding window: if the text is wider than the
 * screen, show the end, which is where the cursor is. */
void screen_input(const char *s, unsigned char len, unsigned char cursor)
{
    unsigned char first = 0;
    unsigned char col;

    if (len >= SCR_COLS)
        first = len - SCR_COLS + 1;

    for (col = 0; col < SCR_COLS; col++) {
        unsigned char i = first + col;
        char ch = (i < len) ? s[i] : ' ';
        unsigned char a = ATTR_INPUT;

        if (cursor && i == len)
            a = ATTR(1, 7, 1); /* cursor: bloque invertido */

        draw(SCR_INPUT, col, ch, a);
    }
}
