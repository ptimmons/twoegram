/*
 * The session with the a2 gateway (docs/PROTOCOL.md): the hello that
 * proves both sides hold the device key, and signed requests whose
 * replies are verified before anything in them is believed.
 *
 * Built on cogs_http.c, hmac.c and proto.c; shared by Twoegram and the
 * probe, so the probe tests on the real card the code the app runs.
 * Plain C, tested on the Mac against a model card and a model gateway.
 */
#ifndef SESSION_H
#define SESSION_H

#include "proto.h"

#define SES_AGENTS_MAX  8
#define SES_AGENT_LEN   16
#define SES_BASE_MAX    63          /* http://host:port */

/* Set by the caller before ses_hello(). */
extern char    ses_base[SES_BASE_MAX + 1];
extern uint8_t ses_key[A2_KEY_LEN];

/* After ses_hello(). */
extern char    ses_id[9];           /* this device's id, 8 hex digits */
extern char    ses_sid[9];          /* the session's id */
extern uint8_t ses_open;            /* a session is up */
extern uint8_t ses_nagents;
extern char    ses_agents[SES_AGENTS_MAX][SES_AGENT_LEN + 1];
extern char    ses_server[61];      /* the server's own name and build, if it said */
extern uint32_t ses_seq;            /* the last sequence number used */

/* The last reply, good until the next request. ses_genuine: it carried a
 * signature, and the signature checked. */
extern a2_reply_t ses_reply;
extern uint8_t    ses_genuine;

/* Outcomes, beside 0 and the card's own codes (cogs.h). */
#define SES_E_NOTSERVER 0xE0        /* the answer was not this protocol */
#define SES_E_REFUSED   0xE1        /* the gateway said no: ses_reply.code */
#define SES_E_FORGED    0xE2        /* a reply whose signature did not check */

/* Work out ses_id from the key (the card hashes). */
uint8_t ses_device_id(void);

/* Start a session: ses_open, the agent list and ses_server on success. */
uint8_t ses_hello(void);

/*
 * One signed request. seq 0 means the next number; forge spoils the
 * signature (the probe's test only). 0 means a genuine reply with code
 * OK; SES_E_REFUSED a reply with another code, which ses_genuine says
 * can be trusted or not.
 */
uint8_t ses_request(const char *method, const char *target,
                    const uint8_t *body, uint16_t nbody,
                    uint32_t seq, uint8_t forge);

/* ses_request, saying hello again first if the session has gone. */
uint8_t ses_call(const char *method, const char *target,
                 const uint8_t *body, uint16_t nbody);

/* A line from the last reply's payload: 1 and its value, or 0. */
uint8_t ses_line(const char *word, const char **val, uint8_t *vlen);

/* What went wrong, short and uppercase, for the status bar. */
const char *ses_strerror(uint8_t rc);

/* Wipe the key and session key from memory (at quit). */
void ses_forget(void);

#endif
