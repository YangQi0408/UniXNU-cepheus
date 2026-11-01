#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_fastboot.c"

static const char *VECTOR = "5346423101010001000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1fe201c4b1c4eff67ae4b7422c22e7e2a1eba0d9f59f5f03cb21228e0b03debcd0";
static int hex(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }
int main(void)
{
    uint8_t key[32], challenge[16], nonce[16], wire[128], expected[128];
    for (size_t i=0; i<32; i++) key[i] = (uint8_t)i;
    for (size_t i=0; i<16; i++) { challenge[i] = (uint8_t)i; nonce[i] = (uint8_t)(i + 16); }
    size_t vector_len = strlen(VECTOR) / 2;
    for (size_t i=0; i<vector_len; i++) expected[i] = (uint8_t)((hex(VECTOR[2*i]) << 4) | hex(VECTOR[2*i+1]));
    struct cepheus_fastboot_packet packet = { CEPHEUS_FASTBOOT_STATUS, 1, challenge, nonce, NULL, 0 };
    size_t wire_len = sizeof(wire);
    assert(cepheus_fastboot_pack(&packet, key, wire, &wire_len, cepheus_fastboot_hmac_sha256) == 0);
    assert(wire_len == vector_len && memcmp(wire, expected, wire_len) == 0);
    struct cepheus_fastboot_packet unpacked;
    assert(cepheus_fastboot_unpack(wire, wire_len, key, &unpacked, cepheus_fastboot_hmac_sha256) == 0);
    wire[wire_len - 1] ^= 1;
    assert(cepheus_fastboot_unpack(wire, wire_len, key, &unpacked, cepheus_fastboot_hmac_sha256) != 0);
    uint8_t body[8]; size_t body_len = 0;
    assert(cepheus_fastboot_make_response(CEPHEUS_FASTBOOT_COMMIT, 1, body, &body_len) == 0);
    assert(body_len == 1 && body[0] == CEPHEUS_FASTBOOT_UNSUPPORTED);
    assert(cepheus_fastboot_make_response(CEPHEUS_FASTBOOT_DRY_RUN, 0, body, &body_len) == 0);
    assert(body_len == 8 && body[0] == CEPHEUS_FASTBOOT_NOT_READY && body[1] == 1 && body[2] == 0);
    uint16_t sequences[64]; uint8_t nonces[64][16]; size_t count = 0;
    assert(cepheus_fastboot_seen(1, nonce, sequences, nonces, &count) == 0);
    assert(cepheus_fastboot_seen(1, nonce, sequences, nonces, &count) == 1);
    for (size_t i = 1; i < 64; i++) {
        nonce[0] = (uint8_t)i;
        assert(cepheus_fastboot_seen((uint16_t)i, nonce, sequences, nonces, &count) == 0);
    }
    assert(count == 64);
    nonce[0] = 0xff;
    assert(cepheus_fastboot_seen(65, nonce, sequences, nonces, &count) == 2);
    struct cepheus_fastboot_packet malformed = { CEPHEUS_FASTBOOT_STATUS, 2, challenge, nonce, (const uint8_t *)"x", 1 };
    wire_len = sizeof(wire);
    assert(cepheus_fastboot_pack(&malformed, key, wire, &wire_len, cepheus_fastboot_hmac_sha256) == 0);
    assert(cepheus_fastboot_unpack(wire, wire_len, key, &unpacked, cepheus_fastboot_hmac_sha256) != 0);
    wire[5] = CEPHEUS_FASTBOOT_RESPONSE;
    assert(cepheus_fastboot_unpack(wire, vector_len, key, &unpacked, cepheus_fastboot_hmac_sha256) != 0);
    puts("fastboot C/Python vector, tag and replay checks: PASS");
    return 0;
}
