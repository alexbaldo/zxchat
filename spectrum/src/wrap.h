#ifndef WRAP_H
#define WRAP_H

/* Turns whatever the API sends into something a 32-column screen can print:
 *
 *   - UTF-8 down to ASCII: accented letters keep their base letter.
 *   - Word wrap, never mid-word.
 *   - A lone newline becomes a space. Models try to wrap lines themselves and
 *     get the width wrong; two newlines still mean a paragraph.
 *
 * Fed with whatever arrives from the network, in chunks of any size.
 */

/* The sink receives text that is already formatted. */
typedef void (*wrap_sink)(char);

void wrap_begin(wrap_sink sink, unsigned char cols);
void wrap_putc(char c);
void wrap_end(void);

#endif
