#include "cepheus_fastboot.h"
#if defined(KERNEL)
#define CEPHEUS_FASTBOOT_XNU 1
#endif
#include <string.h>
#if defined(CEPHEUS_FASTBOOT_XNU)
#include <corecrypto/cchmac.h>
#include <corecrypto/ccsha2.h>
#include <corecrypto/cc.h>
#include <libkern/libkern.h>
#include <libsa/string.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonHMAC.h>
#else
#include <openssl/hmac.h>
#endif
static uint16_t get16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
void cepheus_fastboot_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32]) {
#if defined(CEPHEUS_FASTBOOT_XNU)
    cchmac(&ccsha256_ltc_di, key_len, key, data_len, data, out);
#elif defined(__APPLE__)
    CCHmac(kCCHmacAlgSHA256, key, key_len, data, data_len, out);
#else
    unsigned int n=0; (void)HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out, &n);
#endif
}
static int valid_kind(uint8_t kind) { return kind == CEPHEUS_FASTBOOT_STATUS || kind == CEPHEUS_FASTBOOT_DRY_RUN || kind == CEPHEUS_FASTBOOT_COMMIT || kind == CEPHEUS_FASTBOOT_CHALLENGE || kind == CEPHEUS_FASTBOOT_RESPONSE; }
int cepheus_fastboot_pack(const struct cepheus_fastboot_packet *p, const uint8_t key[32], uint8_t *out, size_t *out_len, cepheus_fastboot_hmac_fn mac) {
    uint8_t tag[32]; size_t n;
    if (!p || !key || !out || !out_len || !mac || !valid_kind(p->kind) || p->body_len > CEPHEUS_FASTBOOT_BODY_MAX || !p->challenge || !p->client_nonce || (p->body_len && !p->body)) return -1;
    n=CEPHEUS_FASTBOOT_HEADER+p->body_len+CEPHEUS_FASTBOOT_TAG; if (*out_len < n) return -1;
    memcpy(out,"SFB1",4); out[4]=1; out[5]=p->kind; put16(out+6,p->sequence); memcpy(out+8,p->challenge,16); memcpy(out+24,p->client_nonce,16); if (p->body_len) memcpy(out+40,p->body,p->body_len);
    mac(key,32,out,40+p->body_len,tag); memcpy(out+40+p->body_len,tag,32);
#if defined(CEPHEUS_FASTBOOT_XNU)
    cc_clear(sizeof(tag), tag);
#endif
    *out_len=n; return 0;
}
int cepheus_fastboot_unpack(const uint8_t *wire, size_t wire_len, const uint8_t key[32], struct cepheus_fastboot_packet *p, cepheus_fastboot_hmac_fn mac) {
    uint8_t tag[32]; size_t signed_len;
    if (!wire || !key || !p || !mac || wire_len < 72 || wire_len > 128) return -1;
    signed_len=wire_len-32; if (memcmp(wire,"SFB1",4) || wire[4] != 1 || !valid_kind(wire[5])) return -1;
    mac(key,32,wire,signed_len,tag);
#if defined(CEPHEUS_FASTBOOT_XNU)
    int bad_tag = timingsafe_bcmp(tag, wire + signed_len, 32);
    cc_clear(sizeof(tag), tag);
    if (bad_tag) return -1;
#else
    { unsigned char diff=0; for (size_t i=0;i<32;i++) diff |= (unsigned char)(tag[i]^wire[signed_len+i]); if (diff) return -1; }
#endif
    p->kind=wire[5]; p->sequence=get16(wire+6); p->challenge=wire+8; p->client_nonce=wire+24; p->body=wire+40; p->body_len=signed_len-40;
    if ((p->kind == CEPHEUS_FASTBOOT_STATUS || p->kind == CEPHEUS_FASTBOOT_DRY_RUN || p->kind == CEPHEUS_FASTBOOT_COMMIT) && p->body_len) return -1;
    return 0;
}
int cepheus_fastboot_seen(uint16_t sequence, const uint8_t nonce[16], uint16_t *sequences, uint8_t (*nonces)[16], size_t *count) {
    if (!nonce || !sequences || !nonces || !count || *count > 64) return -1;
    for (size_t i=0;i<*count;i++) if (sequences[i] == sequence && memcmp(nonces[i],nonce,16)==0) return 1;
    if (*count == 64) return 2;
    sequences[*count]=sequence; memcpy(nonces[*count],nonce,16); (*count)++; return 0;
}

void
cepheus_fastboot_budget_init(struct cepheus_fastboot_budget *budget, uint64_t now)
{
    *budget = (struct cepheus_fastboot_budget){
        .started_ns = now, .window_ns = now, .last_ns = now,
    };
}

int
cepheus_fastboot_budget_take(struct cepheus_fastboot_budget *budget, uint64_t now)
{
    if (now < budget->last_ns || now - budget->started_ns >= CEPHEUS_FASTBOOT_LIFETIME_NS)
        return -1;
    budget->last_ns = now;
    if (now - budget->window_ns >= CEPHEUS_FASTBOOT_WINDOW_NS) {
        budget->window_ns = now;
        budget->used = 0;
    }
    if (budget->used >= CEPHEUS_FASTBOOT_WINDOW_BUDGET)
        return 0;
    budget->used++;
    return 1;
}

int
cepheus_fastboot_make_response(uint8_t kind, int entropy_ready,
    uint8_t body[8], size_t *body_len)
{
    if (!body || !body_len) return -1;
    memset(body, 0, 8);
    if (kind == CEPHEUS_FASTBOOT_COMMIT) {
        body[0] = CEPHEUS_FASTBOOT_UNSUPPORTED;
        *body_len = 1;
        return 0;
    }
    if (kind != CEPHEUS_FASTBOOT_STATUS && kind != CEPHEUS_FASTBOOT_DRY_RUN)
        return -1;
    body[0] = kind == CEPHEUS_FASTBOOT_STATUS ? CEPHEUS_FASTBOOT_OK : CEPHEUS_FASTBOOT_NOT_READY;
    body[1] = 1; /* optional endpoint enabled */
    body[2] = entropy_ready != 0;
    /* Storage, worker quiescence and reset backend remain unavailable. */
    *body_len = 8;
    return 0;
}

#if defined(CEPHEUS_FASTBOOT_XNU)
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <net/kpi_interface.h>
#include <netinet/in.h>
#include <pexpert/pexpert.h>
#include <sys/kpi_socket.h>
#include <sys/random.h>
#include <prng/entropy.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/uio_internal.h>
#if __has_include("cepheus_fastboot_private.h")
#include "cepheus_fastboot_private.h"
#else
#define CEPHEUS_FASTBOOT_PROVISIONED 0
static const unsigned char cepheus_fastboot_key[32] = { 0 };
#endif

/* Volatile read keeps the full runtime referenced in public builds for link
 * validation, without provisioning a key or enabling the public endpoint. */
static volatile unsigned cepheus_fastboot_provisioned = CEPHEUS_FASTBOOT_PROVISIONED;
static socket_t cepheus_fastboot_socket;
static ifnet_t cepheus_fastboot_interface;
static uint8_t cepheus_fastboot_challenge[16];
static uint16_t cepheus_fastboot_sequences[64];
static uint8_t cepheus_fastboot_nonces[64][16];
static size_t cepheus_fastboot_seen_count;
static int cepheus_fastboot_sleep_channel;
/* Start is one-shot, even after cleanup. Only the owner thread uses the socket. */
static unsigned cepheus_fastboot_started;
static unsigned cepheus_fastboot_stop_requested;
static unsigned cepheus_fastboot_done;
extern int sweet_prng_entropy_ready(void);
/* BSD driver convention: thread_terminate is not in the BSD-private headers. */
extern kern_return_t thread_terminate(thread_t);

static uint64_t
cepheus_fastboot_now_ns(void)
{
    uint64_t now;
    absolutetime_to_nanoseconds(mach_absolute_time(), &now);
    return now;
}

/* Request only; never close another thread's socket or block an IRQ caller. */
void
cepheus_fastboot_request_stop(void)
{
    __atomic_store_n(&cepheus_fastboot_stop_requested, 1u, __ATOMIC_RELEASE);
}

int
cepheus_fastboot_is_stopped(void)
{
    return !__atomic_load_n(&cepheus_fastboot_started, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&cepheus_fastboot_done, __ATOMIC_ACQUIRE);
}

static void
cepheus_fastboot_sleep(void)
{
    uint64_t deadline;
    clock_interval_to_deadline(10, NSEC_PER_MSEC, &deadline);
    (void)assert_wait_deadline(&cepheus_fastboot_sleep_channel, THREAD_UNINT, deadline);
    thread_block(THREAD_CONTINUE_NULL);
}

static void
cepheus_fastboot_send(socket_t so, const struct sockaddr_in *peer,
    const struct cepheus_fastboot_packet *packet)
{
    uint8_t wire[CEPHEUS_FASTBOOT_MAX_PACKET];
    size_t length = sizeof(wire);
    struct iovec iov = { .iov_base = wire, .iov_len = sizeof(wire) };
    struct msghdr message = {
        .msg_name = (void *)peer,
        .msg_namelen = sizeof(*peer),
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };
    if (cepheus_fastboot_pack(packet, cepheus_fastboot_key, wire, &length,
        cepheus_fastboot_hmac_sha256) != 0) return;
    iov.iov_len = length;
    size_t sent = 0;
    (void)sock_send(so, &message, MSG_DONTWAIT, &sent);
}

static void
cepheus_fastboot_receive_loop(void)
{
    uint8_t wire[CEPHEUS_FASTBOOT_MAX_PACKET];
    uint8_t response_body[8];
    struct sockaddr_in peer;
    union { struct cmsghdr alignment; uint8_t bytes[CMSG_SPACE(sizeof(struct in_pktinfo))]; } control;
    struct cepheus_fastboot_budget budget;
    cepheus_fastboot_budget_init(&budget, cepheus_fastboot_now_ns());
    for (;;) {
        if (__atomic_load_n(&cepheus_fastboot_stop_requested, __ATOMIC_ACQUIRE))
            return;
        int allowance = cepheus_fastboot_budget_take(&budget, cepheus_fastboot_now_ns());
        if (allowance < 0) return;
        if (allowance == 0) {
            cepheus_fastboot_sleep();
            continue;
        }
        if (!entropy_health_is_ready()) return;
        bzero(&peer, sizeof(peer));
        bzero(&control, sizeof(control));
        struct iovec iov = { .iov_base = wire, .iov_len = sizeof(wire) };
        struct msghdr message = {
            .msg_name = &peer,
            .msg_namelen = sizeof(peer),
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = control.bytes,
            .msg_controllen = sizeof(control.bytes),
        };
        size_t received = sizeof(wire);
        errno_t error = sock_receive(cepheus_fastboot_socket, &message,
            MSG_DONTWAIT, &received);
        if (error == EWOULDBLOCK || error == EAGAIN) {
            cepheus_fastboot_sleep();
            continue;
        }
        if (error != 0) return;
        if ((message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
            message.msg_controllen > sizeof(control.bytes) ||
            message.msg_namelen != sizeof(peer)) continue;
        struct in_pktinfo *packet_info = NULL;
        bool malformed_control = false;
        for (struct cmsghdr *header = CMSG_FIRSTHDR(&message); header != NULL;
            header = CMSG_NXTHDR(&message, header)) {
            if (header->cmsg_len < CMSG_LEN(0) ||
                (uint8_t *)header + header->cmsg_len >
                (uint8_t *)message.msg_control + message.msg_controllen) {
                malformed_control = true;
                break;
            }
            if (header->cmsg_level == IPPROTO_IP && header->cmsg_type == IP_PKTINFO) {
                if (packet_info != NULL ||
                    header->cmsg_len != CMSG_LEN(sizeof(struct in_pktinfo))) {
                    malformed_control = true;
                    break;
                }
                packet_info = (struct in_pktinfo *)CMSG_DATA(header);
            }
        }
        if (malformed_control || received > sizeof(wire) || packet_info == NULL ||
            packet_info->ipi_ifindex != ifnet_index(cepheus_fastboot_interface) ||
            packet_info->ipi_addr.s_addr != htonl(0xc0a80702) ||
            peer.sin_len != sizeof(peer) || peer.sin_family != AF_INET ||
            peer.sin_addr.s_addr != htonl(0xc0a80701)) {
            continue;
        }
        struct cepheus_fastboot_packet request;
        if (cepheus_fastboot_unpack(wire, received, cepheus_fastboot_key,
            &request, cepheus_fastboot_hmac_sha256) != 0) continue;
        if (request.kind == CEPHEUS_FASTBOOT_STATUS && request.sequence == 0 &&
            !memcmp(request.challenge, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) &&
            !memcmp(request.client_nonce, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16)) {
            struct cepheus_fastboot_packet challenge = {
                .kind = CEPHEUS_FASTBOOT_CHALLENGE, .sequence = 0,
                .challenge = cepheus_fastboot_challenge,
                .client_nonce = (const uint8_t *)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
            };
            cepheus_fastboot_send(cepheus_fastboot_socket, &peer, &challenge);
            continue;
        }
        if (request.kind != CEPHEUS_FASTBOOT_STATUS && request.kind != CEPHEUS_FASTBOOT_DRY_RUN && request.kind != CEPHEUS_FASTBOOT_COMMIT) continue;
        if (memcmp(request.challenge, cepheus_fastboot_challenge, 16) != 0) continue;
        if (cepheus_fastboot_seen_count == 64) {
            if (!sweet_prng_entropy_ready() || !entropy_health_is_ready()) return;
            read_random(cepheus_fastboot_challenge, sizeof(cepheus_fastboot_challenge));
            if (!entropy_health_is_ready()) return;
            bzero(cepheus_fastboot_sequences, sizeof(cepheus_fastboot_sequences));
            bzero(cepheus_fastboot_nonces, sizeof(cepheus_fastboot_nonces));
            cepheus_fastboot_seen_count = 0;
            struct cepheus_fastboot_packet challenge = {
                .kind = CEPHEUS_FASTBOOT_CHALLENGE, .sequence = 0,
                .challenge = cepheus_fastboot_challenge,
                .client_nonce = (const uint8_t *)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
            };
            cepheus_fastboot_send(cepheus_fastboot_socket, &peer, &challenge);
            continue;
        }
        if (cepheus_fastboot_seen(request.sequence, request.client_nonce,
                cepheus_fastboot_sequences, cepheus_fastboot_nonces,
                &cepheus_fastboot_seen_count) != 0) continue;
        size_t response_length;
        if (cepheus_fastboot_make_response(request.kind,
            entropy_health_is_ready(), response_body, &response_length) != 0) continue;
        struct cepheus_fastboot_packet response = {
            .kind = CEPHEUS_FASTBOOT_RESPONSE, .sequence = request.sequence,
            .challenge = cepheus_fastboot_challenge,
            .client_nonce = request.client_nonce,
            .body = response_body, .body_len = response_length,
        };
        cepheus_fastboot_send(cepheus_fastboot_socket, &peer, &response);
    }
}

static int
cepheus_fastboot_initialize(ifnet_t interface)
{
    if (sock_socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL,
        &cepheus_fastboot_socket) != 0) return -1;
    struct sockaddr_in local = { .sin_len = sizeof(local), .sin_family = AF_INET,
        .sin_port = htons(5141), .sin_addr = { .s_addr = htonl(0xc0a80702) } };
    uint32_t ifindex = ifnet_index(interface);
    int receive_packet_info = 1;
    int receive_buffer = 4096;
    if (sock_setsockopt(cepheus_fastboot_socket, SOL_SOCKET, SO_RCVBUF,
        &receive_buffer, sizeof(receive_buffer)) != 0 ||
        sock_setsockopt(cepheus_fastboot_socket, IPPROTO_IP, IP_BOUND_IF,
        &ifindex, sizeof(ifindex)) != 0 ||
        sock_setsockopt(cepheus_fastboot_socket, IPPROTO_IP, IP_PKTINFO,
        &receive_packet_info, sizeof(receive_packet_info)) != 0 || sock_bind(cepheus_fastboot_socket,
        (const struct sockaddr *)&local) != 0) {
        return -1; /* Single owner cleanup closes it. */
    }
    return 0;
}

static void
cepheus_fastboot_cleanup(void)
{
    if (cepheus_fastboot_socket != NULL) {
        (void)sock_close(cepheus_fastboot_socket);
        cepheus_fastboot_socket = NULL;
    }
    cc_clear(sizeof(cepheus_fastboot_challenge), cepheus_fastboot_challenge);
    cc_clear(sizeof(cepheus_fastboot_nonces), cepheus_fastboot_nonces);
    bzero(cepheus_fastboot_sequences, sizeof(cepheus_fastboot_sequences));
    cepheus_fastboot_seen_count = 0;
    if (cepheus_fastboot_interface != NULL) {
        (void)ifnet_release(cepheus_fastboot_interface);
        cepheus_fastboot_interface = NULL;
    }
    __atomic_store_n(&cepheus_fastboot_done, 1u, __ATOMIC_RELEASE);
}

static void
cepheus_fastboot_bootstrap(void *argument __unused, wait_result_t result __unused)
{
    uint64_t start = cepheus_fastboot_now_ns();
    for (;;) {
        uint64_t now = cepheus_fastboot_now_ns();
        if (__atomic_load_n(&cepheus_fastboot_stop_requested, __ATOMIC_ACQUIRE) ||
            now < start || now - start >= UINT64_C(60000000000)) break;
        /* read_random refreshes the SWEET DRBG. A health snapshot by itself
         * does not establish that any credited entropy reached the DRBG. */
        read_random(cepheus_fastboot_challenge, sizeof(cepheus_fastboot_challenge));
        if (sweet_prng_entropy_ready() && entropy_health_is_ready()) {
            /* Re-generate AFTER the synchronized credited-reseed check: the
             * preceding refresh can lose try-lock to another CPU's reseed. */
            read_random(cepheus_fastboot_challenge, sizeof(cepheus_fastboot_challenge));
            if (!entropy_health_is_ready()) break;
            if (cepheus_fastboot_initialize(cepheus_fastboot_interface) == 0) {
                printf("SWEET FASTBOOT: read-only endpoint, COMMIT disabled, lease=300s\n");
                cepheus_fastboot_receive_loop();
            }
            break;
        }
        cc_clear(sizeof(cepheus_fastboot_challenge), cepheus_fastboot_challenge);
        cepheus_fastboot_sleep();
    }
    cepheus_fastboot_cleanup();
    printf("SWEET FASTBOOT: stopped (no reset)\n");
    (void)thread_terminate(current_thread());
    thread_block(THREAD_CONTINUE_NULL);
}

void
cepheus_fastboot_start(ifnet_t interface)
{
    if (!interface || !cepheus_fastboot_provisioned) return;
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&cepheus_fastboot_started, &expected, 1u,
        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    if (ifnet_reference(interface) != 0) {
        __atomic_store_n(&cepheus_fastboot_done, 1u, __ATOMIC_RELEASE);
        return;
    }
    cepheus_fastboot_interface = interface;
    thread_t thread;
    if (kernel_thread_start(cepheus_fastboot_bootstrap, interface, &thread) == KERN_SUCCESS)
        thread_deallocate(thread);
    else
        cepheus_fastboot_cleanup();
}
#endif
