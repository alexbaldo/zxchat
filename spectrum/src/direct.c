/*
 * Talks to the API. Anthropic or OpenAI, depending on CFG_OPENAI, which the
 * build wizard writes into secrets.h.
 *
 * Writing the request by hand turns out to be easier than parsing one: open a
 * socket on 443 (the cartridge does the encryption itself), send the POST with
 * our own headers, and read the SSE stream back.
 *
 * We do not really parse JSON. We look for the text key ("text":" on
 * Anthropic, "content":" on OpenAI) and dump whatever follows until the
 * closing quote, unescaping as we go. With a stream where each chunk carries a
 * fragment of text, that is enough, and it saves fitting a whole parser into
 * 48K.
 */

#include "direct.h"
#include "config.h"
#include "lang.h"
#include "screen.h"
#include "secrets.h"
#include "wrap.h"

#include <netdb.h>
#include <spectranet.h>
#include <string.h>
#include <sys/socket.h>

unsigned int direct_bytes = 0;
int direct_status = 0;

static char rxbuf[CFG_RXBUF];

/* Conversation memory, as a ring of CFG_TURNS exchanges. One turn was enough
 * for "go on" to work, but not for the model to remember what the program was
 * supposed to do: after a couple of fixes it would quietly change the point of
 * it. The listing travels as context on every request, but the intent behind
 * it only lives in what was said. */
static char hist_user[CFG_TURNS][CFG_MAX_QUESTION + 1];
static char hist_bot[CFG_TURNS][CFG_HIST_CHARS + 1];
static unsigned char hist_n;   /* exchanges stored, up to CFG_TURNS */
static unsigned char hist_head; /* where the next one goes */
static unsigned int bot_len;

/* ---------------------------------------------------------------- output */

/* The body is generated twice: first with fd < 0, only to measure it (the API
 * demands Content-Length), then again to actually send it. That way the whole
 * JSON never has to be held in memory. */
static int body_fd;
static unsigned int body_len;
static char obuf[128];
static unsigned char on;

static void ob_flush(void)
{
    if (body_fd >= 0 && on > 0)
        send(body_fd, obuf, on, 0);
    on = 0;
}

static void ob_putc(char c)
{
    body_len++;
    if (body_fd < 0)
        return;
    obuf[on++] = c;
    if (on >= sizeof(obuf))
        ob_flush();
}

static void ob_puts(const char *s)
{
    while (*s)
        ob_putc(*s++);
}

static void ob_num(unsigned int v)
{
    char tmp[6];
    unsigned char n = 0;

    if (v == 0) {
        ob_putc('0');
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        ob_putc(tmp[--n]);
}

/* The body of a JSON string, without the quotes, so several pieces (the
 * prompt and the user's listing) can be glued into the same string. */
static void ob_json_body(const char *s)
{
    while (*s) {
        unsigned char c = (unsigned char)*s++;

        if (c == '"' || c == '\\') {
            ob_putc('\\');
            ob_putc((char)c);
        } else if (c == '\n') {
            ob_puts("\\n");
        } else if (c >= 32) {
            ob_putc((char)c);
        }
    }
}

static void ob_json(const char *s)
{
    ob_putc('"');
    ob_json_body(s);
    ob_putc('"');
}

/* BASIC listing that goes with the question, or 0 if there is none. */
const char *direct_listing = 0;

/* The system prompt, plus the user's program if there is one. It goes in as
 * context, not as the question: glued to the message, the model answered about
 * the BASIC even when you just said hello. */
static void ob_system_message(void)
{
    ob_putc('"');
    ob_json_body(SYSTEM_PROMPT);
    if (direct_listing && direct_listing[0]) {
        ob_json_body("\n\nPrograma BASIC que el usuario tiene ahora mismo "
                       "en memoria. Es contexto, no es su pregunta:\n\n");
        ob_json_body(direct_listing);
    }
    ob_putc('"');
}

static void emit_body(const char *question)
{
    ob_puts("{\"model\":");
    ob_json(CFG_MODEL);

#if CFG_OPENAI
    /* Newer OpenAI models reject max_tokens and want this one instead.
     * There is no room here to negotiate by reading the error and retrying,
     * so the new name goes in. With an older model, this is the line to
     * change. */
#ifdef CFG_REASONING
    /* Only set when the user picked a level in the build wizard. Left out
     * entirely otherwise: the default is the model's own. */
    ob_puts(",\"reasoning_effort\":\"" CFG_REASONING "\"");
#endif
    ob_puts(",\"max_completion_tokens\":");
    ob_num(CFG_MAX_TOKENS);
    /* OpenAI has no "system" field: the prompt is the first message. */
    ob_puts(",\"stream\":true,\"messages\":[{\"role\":\"system\",\"content\":");
    ob_system_message();
    ob_puts("},");
#else
    ob_puts(",\"max_tokens\":");
    ob_num(CFG_MAX_TOKENS);
    ob_puts(",\"stream\":true,\"system\":");
    ob_system_message();
    ob_puts(",\"messages\":[");
#endif

    /* Oldest first, so the model reads the conversation in order. */
    {
        unsigned char i;

        for (i = 0; i < hist_n; i++) {
            unsigned char t = (unsigned char)((hist_head + CFG_TURNS - hist_n + i) % CFG_TURNS);

            if (!hist_user[t][0] || !hist_bot[t][0])
                continue;

            ob_puts("{\"role\":\"user\",\"content\":");
            ob_json(hist_user[t]);
            ob_puts("},{\"role\":\"assistant\",\"content\":");
            ob_json(hist_bot[t]);
            ob_puts("},");
        }
    }

    ob_puts("{\"role\":\"user\",\"content\":");
    ob_json(question);
    ob_puts("}]}");
}

/* ----------------------------------------------------------------- input */

/* State of the hunt for the text key inside the SSE stream. */
#define SEEKING 0
#define IN_STRING 1
#define AFTER_BACKSLASH 2
#define IN_UNICODE 3

#if CFG_OPENAI
/* {"choices":[{"delta":{"content":"Hi"}}]} */
static const char TEXT_KEY[] = "\"content\":\"";
#else
/* {"delta":{"type":"text_delta","text":"Hi"}} */
static const char TEXT_KEY[] = "\"text\":\"";
#endif

static unsigned char sse_estado;
static unsigned char sse_match;
static unsigned char uni_n;

static void save_char(char c)
{
    if (bot_len < CFG_HIST_CHARS)
        hist_bot[hist_head][bot_len++] = c;
    hist_bot[hist_head][bot_len] = '\0';
}

/* Anyone wanting the UNWRAPPED text hooks in here. The extension uses it to
 * catch the BASIC lines the model proposes: after wrap.c they are no longer
 * recognisable, because wrapping inserts newlines in the middle of them. */
void (*direct_raw)(char) = 0;

static void emit(char c)
{
    direct_bytes++;
    save_char(c);
    if (direct_raw)
        direct_raw(c);
    wrap_putc(c);
}

static void sse_byte(char c)
{
    switch (sse_estado) {
    case SEEKING:
        if (c == TEXT_KEY[sse_match]) {
            sse_match++;
            if (TEXT_KEY[sse_match] == '\0') {
                sse_match = 0;
                sse_estado = IN_STRING;
            }
        } else {
            /* Retry in case the mismatch was the start of another match */
            sse_match = (c == TEXT_KEY[0]) ? 1 : 0;
        }
        break;

    case IN_STRING:
        if (c == '\\')
            sse_estado = AFTER_BACKSLASH;
        else if (c == '"')
            sse_estado = SEEKING;
        else
            emit(c);
        break;

    case AFTER_BACKSLASH:
        sse_estado = IN_STRING;
        switch (c) {
        case 'n':
            emit('\n');
            break;
        case 't':
            emit(' ');
            break;
        case 'u':
            uni_n = 0;
            sse_estado = IN_UNICODE;
            break;
        case 'r':
        case 'b':
        case 'f':
            break;
        default:
            emit(c); /* quote, backslash, whatever */
            break;
        }
        break;

    case IN_UNICODE:
        /* Dropped: anything non-ASCII does not fit the screen, and the
         * prompt already asks the model not to use it. */
        if (++uni_n >= 4)
            sse_estado = IN_STRING;
        break;
    }
}

/* ---------------------------------------------------------------- socket */

static int open_socket(void)
{
    struct hostent *he;
    struct sockaddr_in addr;
    int fd;

    he = gethostbyname((char *)CFG_API_HOST);
    if (he == 0 || he->h_addr_list == 0)
        return DIRECT_EDNS;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return DIRECT_ESOCK;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(CFG_API_PORT); /* htons es un no-op aqui */
    addr.sin_addr.s_addr = he->h_addr_list[0];
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));

    /* Port 443 turns TLS on by itself: the cartridge does the encryption. */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        sockclose(fd);
        return DIRECT_ECONN;
    }

    return fd;
}

static void send_text(int fd, const char *s)
{
    send(fd, (void *)s, (int)strlen(s), 0);
}

static void send_request(int fd, const char *question)
{
    /* First pass: measure only. */
    body_fd = -1;
    body_len = 0;
    emit_body(question);

    send_text(fd, "POST " CFG_API_PATH " HTTP/1.1\r\n");
    send_text(fd, "Host: " CFG_API_HOST "\r\n");
#if CFG_OPENAI
    send_text(fd, "authorization: Bearer " CFG_API_KEY "\r\n");
#else
    send_text(fd, "x-api-key: " CFG_API_KEY "\r\n");
    send_text(fd, "anthropic-version: 2023-06-01\r\n");
#endif
    send_text(fd, "content-type: application/json\r\n");
    send_text(fd, "accept: text/event-stream\r\n");
    send_text(fd, "connection: close\r\n");
    send_text(fd, "content-length: ");

    body_fd = fd;
    on = 0;
    ob_num(body_len); /* body_len is done with: the count is already made */
    ob_flush();

    send_text(fd, "\r\n\r\n");

    /* Second pass: actually send it. */
    body_fd = fd;
    on = 0;
    emit_body(question);
    ob_flush();
}

int direct_ask(const char *question, wrap_sink sink, unsigned char cols)
{
    int fd;
    int bytes;
    int i;
    int idle = 0;
    unsigned char set_status = 0; /* 0 line de set_status, 1 cabeceras, 2 cuerpo */
    unsigned char n = 0;
    char line[40];

    direct_bytes = 0;
    direct_status = 0;
    bot_len = 0;
    hist_bot[hist_head][0] = '\0';
    sse_estado = SEEKING;
    sse_match = 0;

    fd = open_socket();
    if (fd < 0)
        return fd;

    send_request(fd, question);
    wrap_begin(sink, cols);

    for (;;) {
        bytes = recv(fd, rxbuf, sizeof(rxbuf), 0);

        if (bytes <= 0) {
            if (++idle > CFG_IDLE_READS)
                break;
            continue;
        }
        idle = 0;

        for (i = 0; i < bytes; i++) {
            char c = rxbuf[i];

            if (set_status == 2) {
                sse_byte(c);
                continue;
            }

            if (c == '\r')
                continue;

            if (c == '\n') {
                if (set_status == 0) {
                    line[n] = '\0';
                    /* "HTTP/1.1 200 OK" */
                    {
                        char *p = line;
                        while (*p && *p != ' ')
                            p++;
                        while (*p == ' ')
                            p++;
                        while (*p >= '0' && *p <= '9') {
                            direct_status = direct_status * 10 + (*p - '0');
                            p++;
                        }
                    }
                    set_status = 1;
                } else if (n == 0) {
                    set_status = 2;
                }
                n = 0;
                continue;
            }

            if (n < sizeof(line) - 1)
                line[n++] = c;
        }
    }

    wrap_end();
    sockclose(fd);

    if (direct_status != 200)
        return -direct_status;

    /* The question only enters the history if there was an answer. */
    strncpy(hist_user[hist_head], question, CFG_MAX_QUESTION);
    hist_user[hist_head][CFG_MAX_QUESTION] = '\0';

    hist_head = (unsigned char)((hist_head + 1) % CFG_TURNS);
    if (hist_n < CFG_TURNS)
        hist_n++;

    return 0;
}
