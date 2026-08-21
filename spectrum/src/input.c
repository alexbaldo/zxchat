#include "input.h"
#include "screen.h"
#include <intrinsic.h> /* intrinsic_di / intrinsic_ei */
#include <stdlib.h>   /* inp() */

/* ROM system variables.
 *
 * The ROM interrupt scans the keyboard 50 times a second: it debounces, does
 * auto-repeat and leaves the code in LAST-K, flagging it with bit 5 of FLAGS.
 * Using that is far more reliable than scanning ourselves with in_Inkey(),
 * which only looks at the keyboard the instant it is called: a short keypress
 * between two polls was simply lost.
 *
 * It needs the ROM paged in, which is the case while editing.
 */
#define SYSVAR_LASTK (*(unsigned char *)23560)
#define SYSVAR_REPDEL (*(unsigned char *)23561)
#define SYSVAR_REPPER (*(unsigned char *)23562)
#define SYSVAR_FLAGS (*(unsigned char *)23611)
#define SYSVAR_MODE (*(unsigned char *)23617)
#define SYSVAR_FRAMES (*(unsigned char *)23672)

#define FLAGS_LMODE 0x08  /* 1 = L mode: letters, not keywords */
#define FLAGS_NEWKEY 0x20 /* 1 = a new key is waiting in LAST-K */

#define KEY_ENTER 13
#define KEY_DELETE 12
#define KEY_DOWN 10 /* CAPS SHIFT + 6 */
#define KEY_UP 11   /* CAPS SHIFT + 7 */

void input_init(void)
{
    /* Without this the ROM can return BASIC tokens ("NEW", "LOAD"...)
     * instead of single letters. */
    SYSVAR_MODE = 0;
    SYSVAR_FLAGS |= FLAGS_LMODE;

    SYSVAR_REPDEL = 25; /* frames before auto-repeat starts */
    SYSVAR_REPPER = 4;  /* frames between repeats */

    SYSVAR_FLAGS &= (unsigned char)~FLAGS_NEWKEY; /* drop anything pending */
}

unsigned int key_poll(void)
{
    unsigned char k = 0;

    /* Reading the flag and clearing it has to be atomic: if the interrupt
     * lands between the two, it marks a new key and we clear it right after.
     * That keypress vanishes without a trace. */
    intrinsic_di();
    if (SYSVAR_FLAGS & FLAGS_NEWKEY) {
        k = SYSVAR_LASTK;
        SYSVAR_FLAGS &= (unsigned char)~FLAGS_NEWKEY;
    }
    intrinsic_ei();

    return k;
}

unsigned char input_break(void)
{
    /* CAPS SHIFT is on the 0xFEFE row (bit 0), SPACE on 0x7FFE (bit 0).
     * Pressed = bit clear. */
    return ((inp(0xfefe) & 1) == 0) && ((inp(0x7ffe) & 1) == 0);
}

unsigned char input_line(char *buf, unsigned char max)
{
    unsigned char len = 0;
    unsigned char blink;
    unsigned char last_blink = 2; /* forces the first repaint */
    unsigned char dirty = 1;

    buf[0] = '\0';

    for (;;) {
        unsigned int k = key_poll();

        /* Without this there was no way out, and on autoboot that hijacks
         * the machine: you never even reach BASIC. */
        if (input_break())
            return INPUT_ABORT;

        if (k == KEY_ENTER)
            break;

        /* The arrows move the conversation history, not the cursor: the
         * input is a single line, so there is nowhere to go up to. */
        if (k == KEY_UP) {
            screen_scroll(-1);
        } else if (k == KEY_DOWN) {
            screen_scroll(1);
        } else if (k == KEY_DELETE) {
            if (len > 0) {
                buf[--len] = '\0';
                dirty = 1;
            }
        } else if (k >= 32 && k < 127) {
            if (len < max) {
                buf[len++] = (char)k;
                buf[len] = '\0';
                dirty = 1;
            }
        }

        /* Repaint only when something changed. Repainting every pass made
         * the loop so slow it dropped keypresses. */
        blink = (SYSVAR_FRAMES & 16) ? 1 : 0;
        if (dirty || blink != last_blink) {
            screen_input(buf, len, blink);
            last_blink = blink;
            dirty = 0;
        }
    }

    screen_input(buf, len, 0);
    return len;
}
