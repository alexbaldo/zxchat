#ifndef SCREEN_H
#define SCREEN_H

/* Our own text renderer.
 *
 * Not the ROM's printing, for two reasons: writing column 32 makes the
 * Spectrum wrap by itself, so a following '\n' would leave a blank line; and
 * while the cartridge is paged in the ROM is not there, and we need to print
 * exactly during the download.
 *
 * So the character set is copied to RAM at startup and we draw straight into
 * video memory.
 */

#define SCR_COLS 32

/* Screen layout (rows 0..23) */
#define SCR_STATUS 0   /* title bar */
#define SCR_CHAT_TOP 1 /* conversation area, scrolls */
#define SCR_CHAT_BOT 21
#define SCR_RULE 22  /* separator */
#define SCR_INPUT 23 /* input line */

/* attr = ink | paper<<3 | bright<<6 | flash<<7 */
#define ATTR(ink, paper, bright) ((ink) | ((paper) << 3) | ((bright) << 6))

#define ATTR_BOT ATTR(7, 0, 0)    /* answer: white on black */
#define ATTR_USER ATTR(5, 0, 1)   /* your text: bright cyan */
#define ATTR_WARN ATTR(6, 0, 1)   /* warnings: bright yellow */
#define ATTR_ERROR ATTR(2, 0, 1)  /* errors: bright red */
#define ATTR_STATUS ATTR(0, 5, 0) /* bar: black on cyan */
#define ATTR_INPUT ATTR(7, 1, 1)  /* input: white on blue */

void screen_init(void);

/* Conversation area */
void screen_putc(char ch);
void screen_print(const char *s);
void screen_num(int v); /* printf would drag in half the library */
void screen_ink(unsigned char attr);
void screen_newline_if_needed(void);

/* Double-size text, for the logo. Each character takes 2x2 cells and comes
 * from the same RAM font: a bitmap logo would be 6912 bytes and there is no
 * room. `attr` applies to all four cells. */
void screen_big(unsigned char row, unsigned char col, const char *s, unsigned char attr);

/* Scrollback: the conversation area is a window over the last SCR_HIST lines,
 * not raw video memory. Without this, whatever scrolls off the top is not
 * stored anywhere and is gone for good.
 *
 * At 33 bytes a line this is the biggest single thing in RAM after the code,
 * so it is also the first place to take memory back from. 60 lines is about
 * three screens of history. */
#define SCR_HIST 45

/* Moves the window `delta` lines (negative = back) and repaints.
 * Returns 1 if it moved. */
unsigned char screen_scroll(signed char delta);

/* Status bar and input line */
void screen_status(const char *s);
void screen_input(const char *s, unsigned char len, unsigned char cursor);

#endif
