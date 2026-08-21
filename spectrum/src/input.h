#ifndef INPUT_H
#define INPUT_H

/* Sets up the ROM system variables the keyboard uses. Call once at startup. */
void input_init(void);

/* 0 if there is no new key, or its ASCII code. Debounce and auto-repeat are
 * done by the ROM interrupt, which is far more reliable than polling. */
unsigned int key_poll(void);

/* The user asked to leave, with CAPS+SPACE. */
#define INPUT_ABORT 255

/* One-line editor on the input row. Blocks until ENTER. Returns the length
 * of the text (0 for an empty ENTER) or INPUT_ABORT. */
unsigned char input_line(char *buf, unsigned char max);

/* CAPS SHIFT + SPACE, read straight from the keyboard. Does not use the ROM,
 * so it works with the cartridge paged in. */
unsigned char input_break(void);

#endif
