#ifndef DIRECT_H
#define DIRECT_H

#include "wrap.h"

/* Our own errors, outside the range of anything the socket library returns. */
#define DIRECT_EDNS (-101)  /* name does not resolve */
#define DIRECT_ESOCK (-102) /* no socket available */
#define DIRECT_ECONN (-103) /* cannot connect (TLS would die here) */

extern unsigned int direct_bytes;   /* characters of answer painted */
extern int direct_status;           /* HTTP code the API returned */
extern const char *direct_listing;     /* BASIC listing sent as context, or 0 */
extern void (*direct_raw)(char);  /* copy of the unwrapped text, or 0 */

/* Asks the API and feeds the answer to `sink`, wrapped to `cols`.
 * Returns 0, a DIRECT_E* or -HTTP code. Cartridge paged in. */
int direct_ask(const char *question, wrap_sink sink, unsigned char cols);

#endif
