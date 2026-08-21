#ifndef TOKENIZE_H
#define TOKENIZE_H

/* BASIC text -> the tokenised form the ROM expects.
 *
 * The reverse of listing a program. Without a server in the middle there is
 * nobody else to do it, so the Spectrum tokenises what the model writes.
 *
 * CALL WITH THE CARTRIDGE PAGED OUT: the keyword table lives in the
 * Spectrum's own ROM, at 0x0095.
 *
 * Writes the whole line (number, length and body) into dst and returns the
 * bytes written, or 0 if it does not fit.
 */
unsigned int tokenize_line(unsigned int num, const char *text, unsigned char *dst,
                             unsigned int max);

#endif
