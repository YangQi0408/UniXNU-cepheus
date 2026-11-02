#ifndef CEPHEUS_FASTBOOT_H
#define CEPHEUS_FASTBOOT_H
#include <stddef.h>
#include <stdint.h>
#define CEPHEUS_FASTBOOT_MAX_PACKET 128u
#define CEPHEUS_FASTBOOT_HEADER 40u
#define CEPHEUS_FASTBOOT_TAG 32u
#define CEPHEUS_FASTBOOT_BODY_MAX 56u
#define CEPHEUS_FASTBOOT_STATUS 1u
#define CEPHEUS_FASTBOOT_DRY_RUN 2u
#define CEPHEUS_FASTBOOT_COMMIT 3u
#define CEPHEUS_FASTBOOT_CHALLENGE 4u
#define CEPHEUS_FASTBOOT_RESPONSE 5u
enum cepheus_fastboot_status { CEPHEUS_FASTBOOT_OK=0, CEPHEUS_FASTBOOT_MALFORMED=1, CEPHEUS_FASTBOOT_UNAUTH=2, CEPHEUS_FASTBOOT_REPLAY=3, CEPHEUS_FASTBOOT_DISABLED=4, CEPHEUS_FASTBOOT_NOT_READY=5, CEPHEUS_FASTBOOT_UNSUPPORTED=6, CEPHEUS_FASTBOOT_BUSY=7 };
struct cepheus_fastboot_packet { uint8_t kind; uint16_t sequence; const uint8_t *challenge; const uint8_t *client_nonce; const uint8_t *body; size_t body_len; };
typedef void (*cepheus_fastboot_hmac_fn)(const uint8_t *, size_t, const uint8_t *, size_t, uint8_t[32]);
int cepheus_fastboot_pack(const struct cepheus_fastboot_packet *, const uint8_t[32], uint8_t *, size_t *, cepheus_fastboot_hmac_fn);
int cepheus_fastboot_unpack(const uint8_t *, size_t, const uint8_t[32], struct cepheus_fastboot_packet *, cepheus_fastboot_hmac_fn);
int cepheus_fastboot_seen(uint16_t, const uint8_t[16], uint16_t *, uint8_t (*)[16], size_t *);
int cepheus_fastboot_make_response(uint8_t, int, uint8_t[8], size_t *);
void cepheus_fastboot_hmac_sha256(const uint8_t *, size_t, const uint8_t *, size_t, uint8_t[32]);

/* Single-owner thread. Fixed monotonic windows bound work even for bad packets. */
#define CEPHEUS_FASTBOOT_WINDOW_NS UINT64_C(1000000000)
#define CEPHEUS_FASTBOOT_LIFETIME_NS UINT64_C(300000000000)
#define CEPHEUS_FASTBOOT_WINDOW_BUDGET 32u
struct cepheus_fastboot_budget {
    uint64_t started_ns;
    uint64_t window_ns;
    uint64_t last_ns;
    unsigned used;
};
void cepheus_fastboot_budget_init(struct cepheus_fastboot_budget *, uint64_t);
/* -1: lifetime expired/clock invalid; 0: yield; 1: one receive attempt allowed. */
int cepheus_fastboot_budget_take(struct cepheus_fastboot_budget *, uint64_t);

#if defined(KERNEL) || defined(CEPHEUS_FASTBOOT_XNU)
struct ifnet;
void cepheus_fastboot_start(struct ifnet *);
/* Asynchronous stop; acknowledgement covers this endpoint's resources only. */
void cepheus_fastboot_request_stop(void);
int cepheus_fastboot_is_stopped(void);
#endif
#endif
