#ifndef CONFIG_H
#define CONFIG_H

/* ZXChat talks to the API straight from the cartridge: no server in between.
 * Provider, model and key are set by the build wizard, which writes secrets.h
 * and deletes it when the build ends. */
#include "secrets.h"

#if CFG_OPENAI
#define CFG_API_HOST "api.openai.com"
#define CFG_API_PATH "/v1/chat/completions"
#else
#define CFG_API_HOST "api.anthropic.com"
#define CFG_API_PATH "/v1/messages"
#endif

/* Port 443 turns TLS on by itself: the cartridge does the encryption. Both
 * providers require SNI, and that is where an older firmware would fail. */
#define CFG_API_PORT 443

/* ---------------------------------------------------------------- limits */

/* Ceiling on the whole completion, and it includes the reasoning tokens.
 * Measured: with reasoning at medium the model needs about 4000 before it
 * stops thinking and writes, and at high about 8000. Below that it spends the
 * entire budget reasoning and returns an EMPTY answer -- which on screen looks
 * exactly like a crash.
 *
 * Generous on purpose. It is a ceiling, not a target: the answer length is
 * governed by the prompt, so raising this costs nothing when the model is not
 * thinking hard, and prevents a silent blank when it is. */
#define CFG_MAX_TOKENS 16000

/* Conversation memory: CFG_TURNS exchanges, each answer trimmed to
 * CFG_HIST_CHARS. Every stored turn is RAM the Spectrum does not have and
 * tokens paid on every request, so it is deliberately short. */
#define CFG_TURNS 3
#define CFG_HIST_CHARS 200

#define CFG_MAX_QUESTION 200 /* longer than a screen line: it scrolls */

/* Width the answer is wrapped to in inline mode. Not 32: z88dk's stdio
 * installs a console with a 4x8 font, so 64 columns fit. One less, because the
 * console wraps by itself at the edge and a newline of ours right there would
 * leave a blank line. */
#define CFG_EXT_COLS 63

/* How much BASIC listing travels as context with the question. Careful: if
 * your program is longer than this, the model sees it truncated, silently. */
#define CFG_PROG_CHARS 512

/* Empty reads before giving up. Deliberately high: the last bytes of a
 * response coincide with the connection closing, and the cartridge is slow to
 * hand them over. With 300 the tail was lost. */
#define CFG_IDLE_READS 3000
#define CFG_RXBUF 256

#endif
