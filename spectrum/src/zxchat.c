/*
 * ZXChat as a BASIC extension. One command, three modes:
 *
 *   %chat                      opens the conversation window
 *   %chat "why does 20 crash"  asks inline, without losing the listing
 *   %chat "/fix"               applies what the answer proposed
 *
 * One command is not a design decision, it is the cartridge's limit. The
 * BASIC extension table lives at 0x3A00, six bytes per entry, and ends at
 * 0x3B00 where buf_message starts; addbasicext refuses once the low byte of
 * the pointer goes past 0xFB. The firmware boots with 41 commands of its own
 * already registered (the pointer starts at 0x3AF6), so there is room for
 * exactly one. The modes live inside it, behind a slash, which is a character
 * no real question ever starts with.
 *
 * Four things worth knowing, none of them obvious:
 *
 * 1. Each handler is called TWICE: once when the interpreter checks the
 *    syntax of the line and once when it runs it. Everything before
 *    statement_end() happens on both passes; everything after, only on
 *    execution. That is why the network request comes last.
 *
 * 2. It must link against the _np libraries (no paging). The normal ones
 *    leave the cartridge paged in a way that does not survive the jump back
 *    to BASIC. That rules out libhttp, which has no _np variant, so we talk
 *    to the API with raw sockets.
 *
 * 3. Returning to BASIC is a jump to 0x3e99, which is in the CARTRIDGE ROM:
 *    it has to be paged in right before. In the Spectrum ROM that address is
 *    character set data, and jumping there resets the machine.
 *
 * 4. While the cartridge is paged in there is no ROM, and without ROM you
 *    cannot print with putchar. The inline mode works around it by buffering
 *    the answer and painting it afterwards, paged out. The window does not
 *    need to: its renderer writes straight to video memory, which is what
 *    gives it the teletype the inline mode cannot have.
 */

#include "config.h"
#include "version.h"
#include "direct.h"
#include "input.h"
#include "lang.h"
#include "screen.h"
#include "tokenize.h"
#include "wrap.h"

#include <basicext.h>
#include <spectranet.h>
#include <stdio.h> /* putchar */
#include <string.h>

static char token_chat[] = "%chat";
static struct basic_cmd bc_chat;

static char question[CFG_MAX_QUESTION + 1];
static char answer[520];
static unsigned int rlen;
static char listing[CFG_PROG_CHARS + 1];
static unsigned int plen;
static char last_char; /* last character of the listing, to avoid doubling spaces */

/* ------------------------------------------------------------------ text */

static void emit_text(const char *s)
{
    while (*s)
        putchar(*s++);
}

static void save_char(char c)
{
    if (rlen < sizeof(answer) - 1)
        answer[rlen++] = c;
}

/* -------------------------------------------------------------- listado */

/* The BASIC program lives between PROG and VARS, tokenised: per line, two
 * bytes of line number (big-endian), two of length, then the text.
 *
 * Keywords are codes >= 0xA4 and their text sits in a ROM table starting at
 * 0x0095, where each word ends with bit 7 set on its last character. None of
 * this is reachable with the cartridge paged in, so the listing is built
 * BEFORE touching the network.
 */
#define SYSVAR_PROG (*(unsigned int *)23635)
#define SYSVAR_VARS (*(unsigned int *)23627)
#define TOKEN_TABLE 0x0095
/* The first entry is NOT 0xA5 (RND) as you would expect, but the one before:
 * skipping (token - 0xA5) entries always lands one short. Seen on screen:
 * LET (0xF1) came out as LIST (0xF0) and PRINT (0xF5) as POKE (0xF4), both
 * off by the same one. */
#define FIRST_TOKEN 0xa4

static void put_char(char c)
{
    if (plen < CFG_PROG_CHARS) {
        listing[plen++] = c;
        last_char = c;
    }
}

static void put_number(unsigned int v)
{
    char tmp[6];
    unsigned char n = 0;

    if (v == 0) {
        put_char('0');
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        put_char(tmp[--n]);
}

static void put_keyword(unsigned char t)
{
    const unsigned char *p = (const unsigned char *)TOKEN_TABLE;
    unsigned char n = (unsigned char)(t - FIRST_TOKEN);

    while (n--) {
        while ((*p & 0x80) == 0)
            p++;
        p++;
    }

    if (last_char != ' ' && last_char != 0)
        put_char(' ');

    for (;;) {
        unsigned char c = *p++;
        put_char((char)(c & 0x7f));
        if (c & 0x80)
            break;
    }
    put_char(' ');
}

/* Leaves the listing in `listing`. Returns 0 if there is no program.
 * ALWAYS called with the cartridge paged out: it needs the ROM. */
static unsigned int list_program(void)
{
    const unsigned char *p = (const unsigned char *)SYSVAR_PROG;
    const unsigned char *end = (const unsigned char *)SYSVAR_VARS;

    plen = 0;
    last_char = 0;

    while (p + 4 < end && plen < CFG_PROG_CHARS) {
        unsigned int num = ((unsigned int)p[0] << 8) | p[1];
        unsigned int len = (unsigned int)p[2] | ((unsigned int)p[3] << 8);
        const unsigned char *line = p + 4;
        const unsigned char *lfin = line + len;

        p = lfin;

        put_number(num);
        put_char(' ');

        while (line < lfin && plen < CFG_PROG_CHARS) {
            unsigned char c = *line++;

            if (c == 0x0d)
                break;
            if (c == 0x0e) {
                line += 5; /* binary form of the number we just copied */
                continue;
            }
            if (c >= 0x10 && c <= 0x15) {
                line++; /* INK, PAPER, FLASH... llevan un parametro */
                continue;
            }
            if (c == 0x16 || c == 0x17) {
                line += 2; /* AT y TAB llevan dos */
                continue;
            }
            if (c >= FIRST_TOKEN)
                put_keyword(c);
            else if (c >= 32 && c < 127)
                put_char(c);
        }

        put_char('\n');
    }

    listing[plen] = '\0';
    return plen;
}

/* Builds the program listing. Pages out and back in by itself, because the
 * keyword table is in the Spectrum ROM and the network is in the cartridge:
 * the two cannot be present at the same time. */
static void build_listing(void)
{
    pageout();
    plen = 0;
    list_program(); /* goes straight into the JSON: no encoding needed */
    pagein();
}

/* ------------------------------------------------------------------- red */

static void catch_line(char c);
static void forget_proposals(void);

/* Asks, and feeds the answer to `sink`, wrapped to `cols`.
 * Returns 0 on success. Called with the cartridge paged in. */
static int ask(wrap_sink sink, unsigned char cols)
{
    int rc;

    /* Every question starts clean: /fix applies the last answer, not a
     * leftover from three turns ago. */
    forget_proposals();
    direct_raw = catch_line;

    /* A ternary with 0 made sccz80 treat the null branch as an int. */
    if (plen > 0)
        direct_listing = listing;
    else
        direct_listing = (const char *)0;

    rc = direct_ask(question, sink, cols);

    /* The answer does not end in a newline, so the last proposed line was
     * never closed and was lost. With a single-line suggestion that meant
     * capturing nothing at all: /fix said "nothing to apply" with the fix
     * right there on screen. A fake newline at the end closes it. */
    catch_line('\n');
    direct_raw = (void (*)(char))0;

    return rc;
}

/* ------------------------------------------------------- inline mode ---
 *
 * Asking inline: it does not touch the BASIC screen beyond writing below, so
 * you can ask about your program without losing the listing.
 *
 * Entered with the cartridge paged in, left paged out. */
#define VRAM ((unsigned char *)0x4000)

/* The 64-column console keeps its own cursor, in __console_x/__console_y, and
 * it knows nothing about where BASIC is printing. Left alone, answers landed
 * wherever it had got to: the first over your listing, each one after a bit
 * further down, and eventually mid-screen.
 *
 * Trying to follow BASIC's position was the wrong fix -- it made the placement
 * depend on state we do not control. Clearing and starting from the top is
 * predictable, which is what actually matters here. The listing goes, but LIST
 * brings it back.
 *
 * In C these are _console_x / _console_y: z88dk prefixes C symbols with an
 * underscore, so the asm __console_x is our _console_x. */
extern unsigned char _console_x;
extern unsigned char _console_y;

static void clear_for_answer(void)
{
    /* Pixels off and attributes to 0x38, black ink on white paper, which is
     * what a BASIC CLS leaves. Otherwise BASIC would carry on over our
     * colours. */
    memset(VRAM, 0, 6144);
    memset(VRAM + 6144, 0x38, 768);
    _console_x = 0;
    _console_y = 0;
}

static void ask_inline(void)
{
    int rc;
    unsigned int i;

    /* Without this the machine looks hung: several seconds pass between the
     * question and the answer with nothing printed, because with the
     * cartridge paged in there is no ROM to print with. At least say it is
     * working on it. */
    pageout();
    clear_for_answer();
    emit_text(L_THINKING);
    pagein();

    /* The dispatcher calls us with the cartridge ALREADY paged in, so the
     * Spectrum ROM is not here: reading the keyword table at 0x0095 returned
     * bytes of the cartridge ROM. Literals came out fine (they live in RAM)
     * and only the keywords were garbage. */
    build_listing();

    rlen = 0;
    rc = ask(save_char, CFG_EXT_COLS);
    answer[rlen] = '\0';

    /* Print with the cartridge out: without the Spectrum ROM there is no
     * print routine. */
    pageout();

    putchar('\n');
    if (rc < 0) {
        emit_text("ZXChat: sin conexion ");
        putchar((char)('0' - rc)); /* -1, -2, -3 */
        putchar('\n');
    } else {
        for (i = 0; i < rlen; i++)
            putchar(answer[i]);
        putchar('\n');
    }
}

/* --------------------------------------------------------------- window ---
 *
 * Takes the whole screen, you converse, and on the way out with CAPS+SPACE it
 * hands your listing back exactly as it was. That is why the 6912 bytes of
 * the screen file (pixels plus attributes) are saved before painting.
 *
 * Inside the window we do not use putchar but our own renderer, which writes
 * straight to video memory: it works with the cartridge paged in, and that is
 * what gives it the teletype the inline mode cannot have.
 */
static int apply_now(unsigned char replace_all);
static const char *apply_message(int rc);

/* Filled in by apply_message: the code, the HTTP status and the bytes received.
 * It lives up here because both the window and the inline mode print it. */
static char apply_detail[140];

/* A mode behind a slash: "/fix" and friends. Returns 0 if it is not one. */
static unsigned char is_command(const char *text, const char *orden)
{
    return (unsigned char)(text[0] == '/' && strcmp(text + 1, orden) == 0);
}


static char session[6];
static char status_line[SCR_COLS + 1];

#define SYSVAR_FRAMES (*(unsigned int *)23672)

static void set_status(const char *text)
{
    unsigned char i;
    unsigned char n = (unsigned char)strlen(text);

    for (i = 0; i < SCR_COLS; i++)
        status_line[i] = ' ';
    status_line[SCR_COLS] = '\0';

    memcpy(status_line + 1, "ZXCHAT", 6);

    if (n > 12)
        n = 12;
    memcpy(status_line + SCR_COLS - 1 - n, text, n);

    screen_status(status_line);
}

/* Entered with the cartridge paged in, left paged out. */
static void window(void)
{
    unsigned char len;
    int rc;

    /* Reading the ROM character set, and letting the ROM keyboard work, both
     * need the cartridge out. */
    pageout();

    screen_init();
    input_init();

    /* Fixed session id: the window and the inline mode share one conversation,
     * so /fix applies whatever was proposed in either. */
    memcpy(session, "ext", 4);

    screen_ink(ATTR_BOT);
    screen_print(L_WINDOW_HELP);
    screen_print(L_WINDOW_FIX);
    screen_print(L_WINDOW_WRITE);

    for (;;) {
        set_status(L_BAR_READY);

        len = input_line(question, CFG_MAX_QUESTION);
        if (len == INPUT_ABORT)
            break;
        if (len == 0)
            continue;

        screen_ink(ATTR_USER);
        screen_print("> ");
        screen_print(question);
        screen_newline_if_needed();
        screen_ink(ATTR_BOT);

        if (question[0] == '/') {
            if (is_command(question, "fix") || is_command(question, "write")) {
                set_status(L_BAR_APPLYING);
                pagein();
                rc = apply_now(is_command(question, "write")); /* sale despaginado */
                screen_print(apply_message(rc));
                screen_print(apply_detail);
            } else {
                screen_ink(ATTR_ERROR);
                screen_print(L_UNKNOWN_COMMAND);
            }
            screen_print("\n\n");
            screen_input("", 0, 0);
            continue;
        }

        set_status(L_BAR_THINKING);

        /* The listing is built paged out (token table in the ROM) and the
         * request paged in (the network is the cartridge's): build_listing
         * does both and leaves us paged in. */
        build_listing();
        rc = ask(screen_putc, SCR_COLS);
        pageout();

        screen_newline_if_needed();

        if (rc < 0) {
            screen_ink(ATTR_ERROR);
            screen_print(L_NETWORK_ERROR);
            screen_num(rc);
            screen_print(" ***\n");
        } else if (direct_bytes == 0) {
            screen_ink(ATTR_WARN);
            screen_print(L_NO_ANSWER);
            screen_print("\n");
        }

        screen_putc('\n');
        screen_input("", 0, 0);
    }
}

/* ------------------------------------------------- proposed lines ---
 *
 * There is nobody else to remember what the model proposed, so we catch it on
 * the fly from the unwrapped text: any line starting with a number, a space
 * and something else is a line of BASIC.
 *
 * That is why the prompt insists every line goes alone and starts with its
 * number.
 */
#define CFG_PROPOSAL_CHARS 768

static char proposals[CFG_PROPOSAL_CHARS + 1];
static unsigned int proposals_applied; /* lineas tocadas en el ultimo /fix */
static unsigned int proposals_len;
static char raw_line[80];
static unsigned char raw_len;

static void close_line(void)
{
    unsigned char i = 0;
    unsigned char digits = 0;
    unsigned char after;

    raw_line[raw_len] = '\0';

    while (raw_line[i] == ' ')
        i++;
    while (raw_line[i] >= '0' && raw_line[i] <= '9') {
        i++;
        digits++;
    }

    /* Two forms are accepted: "130 PRINT x", which changes or adds the line,
     * and a bare "130", which deletes it. The second is not our invention: it
     * is how you delete a line by typing it in BASIC, and without it /fix
     * could only add and change, never remove. */
    after = i;
    while (raw_line[after] == ' ')
        after++;

    if (digits > 0 && digits < 5 &&
        (raw_line[after] == '\0' || (after > i && raw_line[after] > ' '))) {
        unsigned char j = 0;

        while (raw_line[j] == ' ')
            j++;
        while (raw_line[j] && proposals_len < CFG_PROPOSAL_CHARS)
            proposals[proposals_len++] = raw_line[j++];
        while (proposals_len > 0 && proposals[proposals_len - 1] == ' ')
            proposals_len--; /* "130   " has to end up as "130" */
        if (proposals_len < CFG_PROPOSAL_CHARS)
            proposals[proposals_len++] = '\n';
        proposals[proposals_len] = '\0';
    }

    raw_len = 0;
}

static void forget_proposals(void)
{
    proposals_len = 0;
    proposals[0] = '\0';
    raw_len = 0;
}

static void catch_line(char c)
{
    if (c == '\n') {
        close_line();
        return;
    }
    if (raw_len < sizeof(raw_line) - 2)
        raw_line[raw_len++] = c;
}

/* Number of the first line above `desde`, or 0 if there is none left. */
static unsigned int next_line_number(const char *text, unsigned int desde)
{
    unsigned int best = 0;

    while (*text) {
        unsigned int v = 0;

        while (*text >= '0' && *text <= '9')
            v = v * 10 + (unsigned int)(*text++ - '0');

        if (v > desde && (best == 0 || v < best))
            best = v;

        while (*text && *text != '\n')
            text++;
        if (*text)
            text++;
    }

    return best;
}

/* Text of line `num`, with the number stripped, or 0 if it is not there. */
static const char *line_text(const char *text, unsigned int num)
{
    while (*text) {
        unsigned int v = 0;
        const char *start = text;

        while (*text >= '0' && *text <= '9')
            v = v * 10 + (unsigned int)(*text++ - '0');

        if (text != start && v == num) {
            while (*text == ' ')
                text++;
            return text;
        }

        while (*text && *text != '\n')
            text++;
        if (*text)
            text++;
    }

    return (const char *)0;
}

/* ------------------------------------------------------------- applying ---
 *
 * The program in memory is merged with the lines the model proposed, the
 * result is tokenised, and the whole PROG-VARS area is replaced in one go.
 *
 * Replacing wholesale is easier than splicing line by line: there is one
 * pass, one length to get right, and one place where it can go wrong.
 */
#define CFG_FIX_BYTES 1024

static unsigned char tokenized[CFG_FIX_BYTES];
static unsigned int nlen;
static int fix_http;    /* HTTP status of the last download */
static int apply_state;  /* 0 line de set_status, 1 cabeceras, 2 cuerpo */
static int apply_missing;   /* bytes Content-Length promised that never arrived */

/* System variables pointing above the program area. These are the same ones
 * the ROM's POINTERS routine fixes up when you insert a line by hand: if they
 * do not move with the block, the interpreter is lost on the way back.
 * CH-ADD is the critical one: it points at the very statement being executed,
 * which lives in the edit line, above VARS. */
static unsigned int *const movers[] = {
    (unsigned int *)23627, /* VARS   */
    (unsigned int *)23629, /* DEST   */
    (unsigned int *)23641, /* E-LINE */
    (unsigned int *)23643, /* K-CUR  */
    (unsigned int *)23645, /* CH-ADD */
    (unsigned int *)23647, /* X-PTR  */
    (unsigned int *)23649, /* WORKSP */
    (unsigned int *)23651, /* STKBOT */
    (unsigned int *)23653, /* STKEND */
};

#define SYSVAR_NXTLIN (*(unsigned int *)23637)
#define SYSVAR_DATADD (*(unsigned int *)23639)
#define SYSVAR_STKEND (*(unsigned int *)23653)

static unsigned int current_sp;

static void read_sp(void)
{
#asm
    ld hl,0
    add hl,sp
    ld (_current_sp),hl
#endasm
}

/* memmove is not guaranteed in sccz80 and here the blocks ALWAYS overlap:
 * we are sliding the rest of BASIC's memory over itself. */
static void move_block(unsigned char *dst, unsigned char *src, unsigned int n)
{
    if (dst < src) {
        while (n--)
            *dst++ = *src++;
    } else {
        dst += n;
        src += n;
        while (n--)
            *--dst = *--src;
    }
}

/* Checks the result is a valid chain of BASIC lines. If it does not add up
 * byte for byte we touch nothing: better to skip the fix than to leave the
 * user's program half-written. */
static int is_valid_basic(void)
{
    unsigned int i = 0;

    if (nlen == 0)
        return 0;

    while (i < nlen) {
        unsigned int num;
        unsigned int len;

        if (i + 4 > nlen)
            return 0;

        num = ((unsigned int)tokenized[i] << 8) | tokenized[i + 1];
        len = (unsigned int)tokenized[i + 2] | ((unsigned int)tokenized[i + 3] << 8);

        if (num == 0 || num > 9999 || len == 0)
            return 0;
        if (i + 4 + len > nlen)
            return 0;
        if (tokenized[i + 4 + len - 1] != 0x0d)
            return 0;

        i += 4 + len;
    }

    return i == nlen;
}

/* Replaces the program. Returns 0 on success. Cartridge paged out. */
static int splice_program(void)
{
    unsigned int prog_addr = SYSVAR_PROG;
    unsigned int vars_addr = SYSVAR_VARS;
    unsigned int old_len = vars_addr - prog_addr;
    unsigned int limit = SYSVAR_STKEND;
    unsigned char i;

    if (!is_valid_basic())
        return -1;

    read_sp();

    if (nlen > old_len) {
        unsigned int grow = nlen - old_len;

        /* Everything between VARS and STKEND moves up; above STKEND is BASIC's
         * stack, growing down from RAMTOP. If they meet, goodbye. */
        if (limit + grow + 128 >= current_sp)
            return -2;

        move_block((unsigned char *)(vars_addr + grow), (unsigned char *)vars_addr, limit - vars_addr);

        for (i = 0; i < sizeof(movers) / sizeof(movers[0]); i++)
            if (*movers[i] >= vars_addr)
                *movers[i] += grow;
    } else if (nlen < old_len) {
        unsigned int shrink = old_len - nlen;

        move_block((unsigned char *)(vars_addr - shrink), (unsigned char *)vars_addr, limit - vars_addr);

        for (i = 0; i < sizeof(movers) / sizeof(movers[0]); i++)
            if (*movers[i] >= vars_addr)
                *movers[i] -= shrink;
    }

    memcpy((unsigned char *)prog_addr, tokenized, nlen);

    /* These two point INSIDE the program, so they did not move with the
     * block and now point at anything at all.
     *
     * NXTLIN is "the next line to execute", and when a statement ends the ROM
     * looks at the byte it points to in order to decide whether to carry on:
     * if bit 6 or 7 is set it stops; otherwise it runs that line. Pointing it
     * at PROG was telling it "continue from the first line", so on the way
     * back from %chat it ran the whole program and blew up wherever.
     *
     * VARS always stops it: that is where the variables area starts, and
     * everything that can be there carries one of those two bits -including
     * the 0x80 that marks "no variables at all". */
    SYSVAR_NXTLIN = SYSVAR_VARS;
    SYSVAR_DATADD = prog_addr; /* RESTORE lo recoloca; solo lo usa READ */

    return 0;
}


/* Builds the corrected program and applies it. Entered paged in, left paged
 * out; the caller decides how to report it, because inside the window you
 * cannot print with the ROM and outside it you can. */
/* replace_all = 0: merge the proposal into the existing program (/fix).
 * replace_all = 1: the program becomes ONLY what was proposed (/write), which
 * is what you want when you asked for a whole one: merging would keep the old
 * lines the model never mentioned and leave you with a hybrid. */
static int apply_now(unsigned char replace_all)
{
    /* Merge the program in memory with the lines the model proposed and
     * tokenise it ourselves. All paged out, because the keyword table is in
     * the ROM. */
    unsigned int last_char = 0;
    int rc;

    pageout();

    if (proposals_len == 0) {
        fix_http = 0;
        return -10;
    }

    plen = 0;
    if (!replace_all)
        list_program();
    listing[plen] = '\0';
    nlen = 0;

    for (;;) {
        unsigned int a = next_line_number(listing, last_char);
        unsigned int b = next_line_number(proposals, last_char);
        unsigned int num;
        const char *txt;
        unsigned int n;

        if (a == 0)
            num = b;
        else if (b == 0)
            num = a;
        else
            num = (a < b) ? a : b;

        if (num == 0)
            break;

        /* The proposal wins: if the model touched a line, its version is used. */
        txt = line_text(proposals, num);
        if (txt != 0)
            proposals_applied++;

        /* Proposal with no body: the line is deleted, so we do not emit it. */
        if (txt != 0 && (*txt == '\n' || *txt == '\0')) {
            last_char = num;
            continue;
        }

        if (txt == 0)
            txt = line_text(listing, num);
        if (txt == 0)
            break;

        n = tokenize_line(num, txt, tokenized + nlen, sizeof(tokenized) - nlen);
        if (n == 0) {
            fix_http = 0;
            return -12; /* does not fit: better to touch nothing */
        }

        nlen += n;
        last_char = num;
    }

    fix_http = 200;
    return splice_program();
}

/* The codes from apply_now, in words.
 *
 * Each cause gets its own message, and apply_detail also carries the code, the
 * HTTP status and the bytes received. Lumping five different reasons under one
 * "could not fetch it" only meant having to guess which one it was. */
static void put_decimal(char *dst, unsigned char *n, int v)
{
    char tmp[6];
    unsigned char k = 0;
    unsigned int u;

    if (v < 0) {
        dst[(*n)++] = '-';
        u = (unsigned int)(-v);
    } else {
        u = (unsigned int)v;
    }

    if (u == 0)
        tmp[k++] = '0';
    while (u > 0 && k < sizeof(tmp)) {
        tmp[k++] = (char)('0' + (u % 10));
        u /= 10;
    }
    while (k > 0)
        dst[(*n)++] = tmp[--k];
}

/* Appends `s` to `dst`, keeping room for the terminator. */
static void append_text(char *dst, unsigned char *n, const char *s)
{
    while (*s && *n < sizeof(apply_detail) - 1)
        dst[(*n)++] = *s++;
}

static const char *apply_message(int rc)
{
    unsigned char n = 0;

    apply_detail[n++] = '[';
    put_decimal(apply_detail, &n, rc);
    if (rc <= -10) {
        apply_detail[n++] = ' ';
        apply_detail[n++] = 'h';
        put_decimal(apply_detail, &n, fix_http);
        apply_detail[n++] = ' ';
        apply_detail[n++] = 'n';
        put_decimal(apply_detail, &n, (int)nlen);
        apply_detail[n++] = ' ';
        apply_detail[n++] = 'e';
        put_decimal(apply_detail, &n, apply_state);
        apply_detail[n++] = ' ';
        apply_detail[n++] = 'p';
        put_decimal(apply_detail, &n, apply_missing);
    }
    apply_detail[n++] = ']';
    apply_detail[n] = '\0';

    switch (rc) {
    case 0:
        /* How many lines were touched. Without the count there is no way to
         * tell "applied a change that fixes nothing" from "applied nothing at
         * all", and the two look identical on screen. */
        n = 0;
        put_decimal(apply_detail, &n, (int)proposals_applied);
        append_text(apply_detail, &n, L_APPLY_OK2);
        apply_detail[n] = '\0';
        return L_APPLY_OK;
    case -1:
        return L_APPLY_NOT_BASIC;
    case -2:
        return L_APPLY_NO_MEMORY;
    case -10:
        return L_APPLY_NOTHING;
    case -11:
        return L_APPLY_REFUSED;
    case -12:
        return L_APPLY_CUT;
    default:
        return L_APPLY_NO_LINK;
    }
}

/* ------------------------------------------------------------ dispatcher --
 *
 * %chat with a string or without one. We cannot call expectStringExp() blind:
 * with a bare %chat there is no expression and it would raise a syntax error.
 * So we look at what follows first, without consuming it, by reading CH-ADD
 * ourselves. The library's next_char() advances, and advancing here would eat
 * the argument.
 */
#define SYSVAR_CHADD (*(unsigned int *)23645)

static unsigned char has_argument(void)
{
    const unsigned char *p = (const unsigned char *)SYSVAR_CHADD;

    while (*p == ' ')
        p++;

    /* End of statement: end of line, separator, or the THEN of an IF. */
    return (unsigned char)(*p != 0x0d && *p != ':' && *p != 0xcb);
}

/* The cartridge's statement_end() compares the ACCUMULATOR, not CH-ADD: it
 * assumes A already holds the current character. With %chat "..."
 * expectStringExp() leaves it there, which is why that form works; with a bare
 * %chat nobody sets it, A arrives with whatever the compiler left, and you get
 * "nonsense in BASIC".
 *
 * 0x3E96 is F_statement_end's entry in the cartridge jump table, the same one
 * the library uses. CALL rather than JP: sccz80 may give this function a
 * prologue -- it does under -DZXCHAT_DEBUG=ON -- and a JP would then return to
 * whatever that prologue left on the stack. At syntax time it makes no
 * difference either way, because F_statement_end falls into J_exit_success and
 * that does ld sp,(ERR_SP). */
static void end_of_statement(void)
{
#asm
    ld hl,(23645)       ; CH-ADD
fds_espacios:
    ld a,(hl)
    cp ' '
    jr nz,fds_listo
    inc hl
    jr fds_espacios
fds_listo:
    call 0x3e96         ; F_statement_end
#endasm
}

static void chat_cmd(void)
{
    unsigned char has_text = has_argument();

    /* Syntax pass. Everything here also runs when the line is checked. */
    if (has_text) {
        expectStringExp();
        statement_end();
    } else {
        end_of_statement();
    }

    /* From here on, execution only. */
    if (!has_text) {
        window();
    } else {
        string_fetch(question, sizeof(question));

        if (question[0] == '/') {
            int rc = -99;

            /* Applying takes a while: there is a network request in the middle
             * and without this the machine looks hung, exactly as it did when
             * asking. */
            pageout();
            emit_text(L_APPLYING);
            pagein();

            if (is_command(question, "fix"))
                rc = apply_now(0);
            else if (is_command(question, "write"))
                rc = apply_now(1);

            if (rc == -99)
                pageout();
            putchar('\n');
            if (rc == -99) {
                emit_text(L_UNKNOWN_COMMAND);
            } else {
                emit_text(apply_message(rc));
                emit_text(apply_detail);
            }
            putchar('\n');
        } else {
            ask_inline();
        }
    }

    /* And the way back to BASIC is 0x3e99, which is in the CARTRIDGE ROM.
     * It has to be paged in again: in the Spectrum ROM that address falls
     * inside the character set, so jumping there paged out means executing
     * letter bitmaps. The machine used to reset. */
    pagein();

#asm
    jp 0x3e99
#endasm
}

/* -------------------------------------------------------------- registro */

/* Paging is on us. We link the _np libraries (no paging), which is what a
 * BASIC extension requires, and that means they no longer page anything
 * themselves: addbasicext writes into the cartridge's own structures, and
 * without paging it in there is nowhere to write. */
static unsigned int table_top;

/* addbasicext can only say yes or no: inside it is a CALL into the cartridge
 * ROM and a `ret nc`, so it returns 0 or -1 and there is no more detail.
 *
 * The command table lives in the cartridge, not in Spectrum RAM, and is NOT
 * cleared when you load another version: every load stacks its entries on top
 * of the previous ones. So reset the machine before reloading. Otherwise you
 * eventually reach the point where nothing else fits, and the first genuinely
 * new name is the one that fails, while the already-registered ones still go
 * through. */
/* Pointer to the first free slot of the table, inside the cartridge's RAM.
 * Only readable with the cartridge paged in. The table starts at 0x3A00 and
 * addbasicext fails once the LOW byte of this pointer goes past 0xFB. */
#define SNET_TABLETOP (*(unsigned int *)0x3f91)

static void put_hex16(unsigned int v)
{
    static const char d[] = "0123456789ABCDEF";
    unsigned char i;

    for (i = 0; i < 4; i++)
        putchar(d[(v >> (12 - i * 4)) & 15]);
}

static int register_command(struct basic_cmd *cmd, char *name, void *fn)
{
    int rc;

    cmd->errorcode = TRAP_NONSENSE;
    cmd->command = name;
    cmd->rompage = 0; /* the handler lives here, in main RAM */
    cmd->function = fn;

    pagein();
    table_top = SNET_TABLETOP;
    rc = addbasicext(cmd);
    pageout();

    return rc;
}

/* The loader does NOT end in NEW. We tried, so line 10 would not sit in the
 * middle of the user's program: NEW clears the program and respects memory
 * above RAMTOP, which is the classic trick for resident utilities. On this
 * cartridge it resets the machine.
 *
 * The way to load without leaving a trace is not to use the .tap's BASIC
 * block at all:
 *   CLEAR VAL "32767": %tapein "zxchat": LOAD ""CODE : RANDOMIZE USR VAL "32768"
 */

/* Welcome screen shown on load. The banner used to go out through putchar
 * and got tangled with the "Bytes: zxchat.bin" the ROM prints while loading,
 * which made it unreadable. Now it is a real screen, and it waits for you.
 *
 * On the way out the screen is left as a BASIC CLS leaves it: black ink on
 * white paper, attribute 0x38. Otherwise BASIC would carry on printing its
 * "0 OK" over our colours. */
static void welcome(void)
{
    /* ZX in white, like the machine, and CHAT in the usual rainbow: red,
     * yellow, green and cyan, all bright. */
    static const unsigned char inks[] = {7, 7, 2, 6, 4, 5};
    static const char logo[] = "ZXCHAT";
    unsigned char i;
    char letter[2];

    screen_init();
    input_init();

    /* 6 double-size letters are 12 columns: (32-12)/2 = 10 to centre. */
    letter[1] = '\0';
    for (i = 0; i < 6; i++) {
        letter[0] = logo[i];
        screen_big(SCR_CHAT_TOP + 1, (unsigned char)(10 + i * 2), letter,
                   ATTR(inks[i], 0, 1));
    }

    /* La version, centrada bajo el logo. */
    screen_ink(ATTR_WARN);
    screen_print("\n\n\n\n");
    screen_print("              v" ZXCHAT_VERSION "\n\n");
    screen_ink(ATTR_BOT);
    screen_print("%chat\n");
    screen_ink(ATTR_USER);
    screen_print(L_HELP_CHAT);
    screen_ink(ATTR_BOT);

    screen_print("%chat " L_YOUR_QUESTION "\n");
    screen_ink(ATTR_USER);
    screen_print(L_HELP_ASK);
    screen_ink(ATTR_BOT);

    screen_print("%chat \"/fix\"\n");
    screen_ink(ATTR_USER);
    screen_print(L_HELP_FIX);
    screen_ink(ATTR_BOT);
    screen_print("%chat \"/write\"\n");
    screen_ink(ATTR_USER);
    screen_print(L_HELP_WRITE);

    screen_ink(ATTR_WARN);
    screen_print(L_HELP_ENTER);

    while (key_poll() != 13)
        ;

    clear_for_answer();
}

int main(void)
{

    if (register_command(&bc_chat, token_chat, chat_cmd) < 0) {
        emit_text(L_TABLE_FULL_1);
        emit_text(L_TABLE_FULL_2);
        emit_text(L_TABLE_FULL_3);
        put_hex16(table_top);
        emit_text(L_TABLE_FULL_4);
        emit_text(L_TABLE_FULL_5);
        emit_text(L_TABLE_FULL_6);
        return 1;
    }

    welcome();
    return 0;
}
