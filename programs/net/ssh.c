#include "../lib/libc.h"
#include <stdint.h>

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0u; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d == s || n == 0u) {
        return dst;
    }
    if (d < s) {
        for (size_t i = 0u; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0u; i--) {
            d[i - 1u] = s[i - 1u];
        }
    }
    return dst;
}

void* memset(void* dst, int value, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    uint8_t v = (uint8_t)value;
    for (size_t i = 0u; i < n; i++) {
        d[i] = v;
    }
    return dst;
}

/*
 * Minimal interactive SSHv2 client for MyaOS.
 * Supported profile:
 * - KEX: curve25519-sha256 / curve25519-sha256@libssh.org
 * - Cipher: aes128-ctr
 * - MAC: hmac-sha2-256
 * - Auth: password
 *
 * Notes:
 * - Host-key signature verification is not implemented yet; this client
 *   refuses to continue unless --insecure-hostkey is explicitly set.
 * - Random source is pseudo-random (timer/pid mixed), suitable for bootstrapping,
 *   not for production-grade cryptographic assurance.
 */

#define SSH_PORT_DEFAULT 22u
#define SSH_IDENT_MAX 128u
#define SSH_LINE_MAX 256u
#define SSH_STREAM_BUF_MAX (128u * 1024u)
#define SSH_PACKET_PLAIN_MAX (64u * 1024u)
#define SSH_PAYLOAD_MAX (SSH_PACKET_PLAIN_MAX - 5u)
#define SSH_MAC_LEN 32u
#define SSH_BLOCK_SIZE 16u
#define SSH_CHANNEL_WIN (256u * 1024u)
#define SSH_CHANNEL_WIN_LOW (64u * 1024u)
#define SSH_CHANNEL_MAX_PKT 32768u
#define SSH_PENDING_TX_MAX 4096u
#define SSH_TIMEOUT_IO_SPINS 1200000u
#define SSH_TIMEOUT_IDENT_SPINS 6000000u

#define KB_UP ((char)0x11)
#define KB_DOWN ((char)0x12)
#define KB_LEFT ((char)0x13)
#define KB_RIGHT ((char)0x14)

#define SSH_MSG_DISCONNECT 1u
#define SSH_MSG_IGNORE 2u
#define SSH_MSG_UNIMPLEMENTED 3u
#define SSH_MSG_DEBUG 4u
#define SSH_MSG_SERVICE_REQUEST 5u
#define SSH_MSG_SERVICE_ACCEPT 6u
#define SSH_MSG_EXT_INFO 7u
#define SSH_MSG_KEXINIT 20u
#define SSH_MSG_NEWKEYS 21u
#define SSH_MSG_KEX_ECDH_INIT 30u
#define SSH_MSG_KEX_ECDH_REPLY 31u
#define SSH_MSG_USERAUTH_REQUEST 50u
#define SSH_MSG_USERAUTH_FAILURE 51u
#define SSH_MSG_USERAUTH_SUCCESS 52u
#define SSH_MSG_USERAUTH_BANNER 53u
#define SSH_MSG_GLOBAL_REQUEST 80u
#define SSH_MSG_REQUEST_SUCCESS 81u
#define SSH_MSG_REQUEST_FAILURE 82u
#define SSH_MSG_CHANNEL_OPEN 90u
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 91u
#define SSH_MSG_CHANNEL_OPEN_FAILURE 92u
#define SSH_MSG_CHANNEL_WINDOW_ADJUST 93u
#define SSH_MSG_CHANNEL_DATA 94u
#define SSH_MSG_CHANNEL_EXTENDED_DATA 95u
#define SSH_MSG_CHANNEL_EOF 96u
#define SSH_MSG_CHANNEL_CLOSE 97u
#define SSH_MSG_CHANNEL_REQUEST 98u
#define SSH_MSG_CHANNEL_SUCCESS 99u
#define SSH_MSG_CHANNEL_FAILURE 100u

static void mem_zero(void* dst, uint32_t len) {
    uint8_t* p = (uint8_t*)dst;
    for (uint32_t i = 0u; i < len; i++) {
        p[i] = 0u;
    }
}

static void mem_copy_local(void* dst, const void* src, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0u; i < len; i++) {
        d[i] = s[i];
    }
}

static void mem_move_local(void* dst, const void* src, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d == s || len == 0u) {
        return;
    }
    if (d < s) {
        for (uint32_t i = 0u; i < len; i++) {
            d[i] = s[i];
        }
    } else {
        for (uint32_t i = len; i > 0u; i--) {
            d[i - 1u] = s[i - 1u];
        }
    }
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static int ssh_msg_ignorable(uint8_t msg) {
    return (msg == SSH_MSG_IGNORE || msg == SSH_MSG_DEBUG || msg == SSH_MSG_UNIMPLEMENTED || msg == SSH_MSG_EXT_INFO)
               ? 1
               : 0;
}

static int str_eq_n(const char* a, const char* b, uint32_t n) {
    if (!a || !b) {
        return 0;
    }
    for (uint32_t i = 0u; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int bytes_eq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    uint8_t diff = 0u;
    if (!a || !b) {
        return 0;
    }
    for (uint32_t i = 0u; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0u;
}

static void write_be32(uint8_t* out, uint32_t v) {
    out[0] = (uint8_t)((v >> 24) & 0xFFu);
    out[1] = (uint8_t)((v >> 16) & 0xFFu);
    out[2] = (uint8_t)((v >> 8) & 0xFFu);
    out[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t read_be32(const uint8_t* in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

static int parse_ipv4(const char* text, uint32_t* out_ip) {
    uint32_t part = 0u;
    uint32_t value = 0u;
    uint32_t p0 = 0u;
    uint32_t p1 = 0u;
    uint32_t p2 = 0u;
    uint32_t p3 = 0u;
    uint8_t had_digit = 0u;

    if (!text || !out_ip) {
        return -1;
    }

    for (uint32_t i = 0u;; i++) {
        char c = text[i];

        if (c >= '0' && c <= '9') {
            value = value * 10u + (uint32_t)(c - '0');
            if (value > 255u) {
                return -1;
            }
            had_digit = 1u;
            continue;
        }

        if (c == '.' || c == '\0') {
            if (!had_digit || part >= 4u) {
                return -1;
            }
            if (part == 0u) {
                p0 = value;
            } else if (part == 1u) {
                p1 = value;
            } else if (part == 2u) {
                p2 = value;
            } else {
                p3 = value;
            }
            part++;
            value = 0u;
            had_digit = 0u;
            if (c == '\0') {
                break;
            }
            continue;
        }

        return -1;
    }

    if (part != 4u) {
        return -1;
    }
    *out_ip = (p0 << 24) | (p1 << 16) | (p2 << 8) | p3;
    return 0;
}

static int parse_port(const char* text, uint16_t* out_port) {
    uint64_t v = 0u;
    if (!text || !out_port || mya_strto_u64(text, &v) != 0 || v == 0u || v > 65535u) {
        return -1;
    }
    *out_port = (uint16_t)v;
    return 0;
}

static int cstr_starts_with(const char* text, const char* prefix) {
    uint32_t i = 0u;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int is_ascii_printable(char c) {
    return (c >= 32 && c <= 126) ? 1 : 0;
}

static void console_write_len(const uint8_t* data, uint32_t len) {
    if (!data || len == 0u) {
        return;
    }
    (void)mya_syscall(MYAOS_SYS_CONSOLE_WRITE, (uint64_t)(uintptr_t)data, len, 0u, 0u, 0u);
}

/* ------------------------- pseudo-random ------------------------- */

static uint64_t g_rng_state = 0u;

static uint64_t rng_next_u64(void) {
    if (g_rng_state == 0u) {
        myaos_sched_info_t sched = {0};
        uint64_t seed = 0x9E3779B97F4A7C15ull;
        if (mya_sched_info(&sched) == 0) {
            seed ^= sched.timer_ticks;
            seed ^= ((uint64_t)sched.current_pid << 32);
        } else {
            seed ^= (uint64_t)(uint32_t)mya_proc_getpid();
        }
        seed ^= (uint64_t)(uintptr_t)&seed;
        if (seed == 0u) {
            seed = 0xD1B54A32D192ED03ull;
        }
        g_rng_state = seed;
    }

    g_rng_state ^= g_rng_state << 13;
    g_rng_state ^= g_rng_state >> 7;
    g_rng_state ^= g_rng_state << 17;
    return g_rng_state;
}

static void rng_fill(uint8_t* out, uint32_t len) {
    uint64_t w = 0u;
    uint32_t used = 8u;
    if (!out) {
        return;
    }
    for (uint32_t i = 0u; i < len; i++) {
        if (used >= 8u) {
            w = rng_next_u64();
            used = 0u;
        }
        out[i] = (uint8_t)((w >> (used * 8u)) & 0xFFu);
        used++;
    }
}

/* ------------------------- SHA-256 / HMAC ------------------------- */

typedef struct {
    uint32_t h[8];
    uint64_t len_bits;
    uint8_t block[64];
    uint32_t block_len;
} sha256_ctx_t;

static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t sha256_bs0(uint32_t x) {
    return rotr32(x, 2u) ^ rotr32(x, 13u) ^ rotr32(x, 22u);
}

static uint32_t sha256_bs1(uint32_t x) {
    return rotr32(x, 6u) ^ rotr32(x, 11u) ^ rotr32(x, 25u);
}

static uint32_t sha256_ss0(uint32_t x) {
    return rotr32(x, 7u) ^ rotr32(x, 18u) ^ (x >> 3u);
}

static uint32_t sha256_ss1(uint32_t x) {
    return rotr32(x, 17u) ^ rotr32(x, 19u) ^ (x >> 10u);
}

static void sha256_transform(sha256_ctx_t* ctx, const uint8_t block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u,
        0x71374491u,
        0xb5c0fbcfu,
        0xe9b5dba5u,
        0x3956c25bu,
        0x59f111f1u,
        0x923f82a4u,
        0xab1c5ed5u,
        0xd807aa98u,
        0x12835b01u,
        0x243185beu,
        0x550c7dc3u,
        0x72be5d74u,
        0x80deb1feu,
        0x9bdc06a7u,
        0xc19bf174u,
        0xe49b69c1u,
        0xefbe4786u,
        0x0fc19dc6u,
        0x240ca1ccu,
        0x2de92c6fu,
        0x4a7484aau,
        0x5cb0a9dcu,
        0x76f988dau,
        0x983e5152u,
        0xa831c66du,
        0xb00327c8u,
        0xbf597fc7u,
        0xc6e00bf3u,
        0xd5a79147u,
        0x06ca6351u,
        0x14292967u,
        0x27b70a85u,
        0x2e1b2138u,
        0x4d2c6dfcu,
        0x53380d13u,
        0x650a7354u,
        0x766a0abbu,
        0x81c2c92eu,
        0x92722c85u,
        0xa2bfe8a1u,
        0xa81a664bu,
        0xc24b8b70u,
        0xc76c51a3u,
        0xd192e819u,
        0xd6990624u,
        0xf40e3585u,
        0x106aa070u,
        0x19a4c116u,
        0x1e376c08u,
        0x2748774cu,
        0x34b0bcb5u,
        0x391c0cb3u,
        0x4ed8aa4au,
        0x5b9cca4fu,
        0x682e6ff3u,
        0x748f82eeu,
        0x78a5636fu,
        0x84c87814u,
        0x8cc70208u,
        0x90befffau,
        0xa4506cebu,
        0xbef9a3f7u,
        0xc67178f2u,
    };
    uint32_t w[64];
    uint32_t a = ctx->h[0];
    uint32_t b = ctx->h[1];
    uint32_t c = ctx->h[2];
    uint32_t d = ctx->h[3];
    uint32_t e = ctx->h[4];
    uint32_t f = ctx->h[5];
    uint32_t g = ctx->h[6];
    uint32_t h = ctx->h[7];

    for (uint32_t i = 0u; i < 16u; i++) {
        w[i] = ((uint32_t)block[i * 4u] << 24u) | ((uint32_t)block[i * 4u + 1u] << 16u) |
               ((uint32_t)block[i * 4u + 2u] << 8u) | (uint32_t)block[i * 4u + 3u];
    }
    for (uint32_t i = 16u; i < 64u; i++) {
        w[i] = sha256_ss1(w[i - 2u]) + w[i - 7u] + sha256_ss0(w[i - 15u]) + w[i - 16u];
    }

    for (uint32_t i = 0u; i < 64u; i++) {
        uint32_t t1 = h + sha256_bs1(e) + sha256_ch(e, f, g) + k[i] + w[i];
        uint32_t t2 = sha256_bs0(a) + sha256_maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
    ctx->h[4] += e;
    ctx->h[5] += f;
    ctx->h[6] += g;
    ctx->h[7] += h;
}

static void sha256_init(sha256_ctx_t* ctx) {
    if (!ctx) {
        return;
    }
    ctx->h[0] = 0x6a09e667u;
    ctx->h[1] = 0xbb67ae85u;
    ctx->h[2] = 0x3c6ef372u;
    ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu;
    ctx->h[5] = 0x9b05688cu;
    ctx->h[6] = 0x1f83d9abu;
    ctx->h[7] = 0x5be0cd19u;
    ctx->len_bits = 0u;
    ctx->block_len = 0u;
}

static void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    if (!ctx || (!data && len != 0u)) {
        return;
    }
    for (uint32_t i = 0u; i < len; i++) {
        ctx->block[ctx->block_len++] = data[i];
        if (ctx->block_len == 64u) {
            sha256_transform(ctx, ctx->block);
            ctx->len_bits += 512u;
            ctx->block_len = 0u;
        }
    }
}

static void sha256_final(sha256_ctx_t* ctx, uint8_t out[32]) {
    uint64_t total_bits;

    if (!ctx || !out) {
        return;
    }

    total_bits = ctx->len_bits + ((uint64_t)ctx->block_len * 8u);

    ctx->block[ctx->block_len++] = 0x80u;
    if (ctx->block_len > 56u) {
        while (ctx->block_len < 64u) {
            ctx->block[ctx->block_len++] = 0u;
        }
        sha256_transform(ctx, ctx->block);
        ctx->block_len = 0u;
    }

    while (ctx->block_len < 56u) {
        ctx->block[ctx->block_len++] = 0u;
    }

    for (uint32_t i = 0u; i < 8u; i++) {
        ctx->block[56u + i] = (uint8_t)((total_bits >> ((7u - i) * 8u)) & 0xFFu);
    }
    sha256_transform(ctx, ctx->block);

    for (uint32_t i = 0u; i < 8u; i++) {
        out[i * 4u + 0u] = (uint8_t)((ctx->h[i] >> 24u) & 0xFFu);
        out[i * 4u + 1u] = (uint8_t)((ctx->h[i] >> 16u) & 0xFFu);
        out[i * 4u + 2u] = (uint8_t)((ctx->h[i] >> 8u) & 0xFFu);
        out[i * 4u + 3u] = (uint8_t)(ctx->h[i] & 0xFFu);
    }
}

static void hmac_sha256_seq_packet(
    const uint8_t* key,
    uint32_t key_len,
    uint32_t seq,
    const uint8_t* packet,
    uint32_t packet_len,
    uint8_t out[32]
) {
    uint8_t k0[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t ihash[32];
    uint8_t seqb[4];
    sha256_ctx_t ctx;

    if (!key || !out || (!packet && packet_len != 0u)) {
        return;
    }

    mem_zero(k0, sizeof(k0));
    if (key_len > 64u) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, k0);
    } else if (key_len > 0u) {
        mem_copy_local(k0, key, key_len);
    }

    for (uint32_t i = 0u; i < 64u; i++) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36u);
        opad[i] = (uint8_t)(k0[i] ^ 0x5cu);
    }

    write_be32(seqb, seq);

    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64u);
    sha256_update(&ctx, seqb, 4u);
    sha256_update(&ctx, packet, packet_len);
    sha256_final(&ctx, ihash);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64u);
    sha256_update(&ctx, ihash, 32u);
    sha256_final(&ctx, out);

    mem_zero(k0, sizeof(k0));
    mem_zero(ipad, sizeof(ipad));
    mem_zero(opad, sizeof(opad));
    mem_zero(ihash, sizeof(ihash));
    mem_zero(seqb, sizeof(seqb));
}

/* ------------------------- AES-128 CTR ------------------------- */

/* Adapted from tiny-AES-c (public domain / unlicense). */

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static const uint8_t aes_rcon[11] = { 0x8d,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36 };

typedef struct {
    uint8_t round_key[176];
    uint8_t ctr[16];
    uint8_t ks[16];
    uint32_t ks_used;
} aes_ctr_ctx_t;

static uint8_t aes_xtime(uint8_t x) {
    return (uint8_t)((x << 1u) ^ (((x >> 7u) & 1u) * 0x1bu));
}

static void aes_key_expand_128(uint8_t round_key[176], const uint8_t key[16]) {
    uint32_t i;
    uint8_t t[4];

    for (i = 0u; i < 16u; i++) {
        round_key[i] = key[i];
    }
    for (i = 4u; i < 44u; i++) {
        uint8_t* dst = &round_key[i * 4u];
        const uint8_t* src_prev = &round_key[(i - 1u) * 4u];
        const uint8_t* src_4 = &round_key[(i - 4u) * 4u];

        t[0] = src_prev[0];
        t[1] = src_prev[1];
        t[2] = src_prev[2];
        t[3] = src_prev[3];

        if ((i % 4u) == 0u) {
            uint8_t tmp = t[0];
            t[0] = aes_sbox[t[1]];
            t[1] = aes_sbox[t[2]];
            t[2] = aes_sbox[t[3]];
            t[3] = aes_sbox[tmp];
            t[0] ^= aes_rcon[i / 4u];
        }

        dst[0] = (uint8_t)(src_4[0] ^ t[0]);
        dst[1] = (uint8_t)(src_4[1] ^ t[1]);
        dst[2] = (uint8_t)(src_4[2] ^ t[2]);
        dst[3] = (uint8_t)(src_4[3] ^ t[3]);
    }
}

static void aes_add_round_key(uint8_t state[16], const uint8_t* rk) {
    for (uint32_t i = 0u; i < 16u; i++) {
        state[i] ^= rk[i];
    }
}

static void aes_sub_bytes(uint8_t state[16]) {
    for (uint32_t i = 0u; i < 16u; i++) {
        state[i] = aes_sbox[state[i]];
    }
}

static void aes_shift_rows(uint8_t s[16]) {
    uint8_t t;

    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[3]; s[3] = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = t;
}

static void aes_mix_columns(uint8_t s[16]) {
    for (uint32_t i = 0u; i < 4u; i++) {
        uint8_t* c = &s[i * 4u];
        uint8_t a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];
        uint8_t t = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
        uint8_t u = a0;
        c[0] ^= t ^ aes_xtime((uint8_t)(a0 ^ a1));
        c[1] ^= t ^ aes_xtime((uint8_t)(a1 ^ a2));
        c[2] ^= t ^ aes_xtime((uint8_t)(a2 ^ a3));
        c[3] ^= t ^ aes_xtime((uint8_t)(a3 ^ u));
    }
}

static void aes_encrypt_block_128(const uint8_t round_key[176], const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[16];
    mem_copy_local(state, in, 16u);

    aes_add_round_key(state, &round_key[0]);
    for (uint32_t round = 1u; round < 10u; round++) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, &round_key[round * 16u]);
    }
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, &round_key[160]);

    mem_copy_local(out, state, 16u);
    mem_zero(state, sizeof(state));
}

static void aes_ctr_inc(uint8_t ctr[16]) {
    for (int i = 15; i >= 0; i--) {
        ctr[i] = (uint8_t)(ctr[i] + 1u);
        if (ctr[i] != 0u) {
            break;
        }
    }
}

static void aes_ctr_init(aes_ctr_ctx_t* ctx, const uint8_t key[16], const uint8_t iv[16]) {
    if (!ctx || !key || !iv) {
        return;
    }
    aes_key_expand_128(ctx->round_key, key);
    mem_copy_local(ctx->ctr, iv, 16u);
    mem_zero(ctx->ks, sizeof(ctx->ks));
    ctx->ks_used = 16u;
}

static void aes_ctr_xor(aes_ctr_ctx_t* ctx, uint8_t* data, uint32_t len) {
    if (!ctx || (!data && len != 0u)) {
        return;
    }
    for (uint32_t i = 0u; i < len; i++) {
        if (ctx->ks_used >= 16u) {
            aes_encrypt_block_128(ctx->round_key, ctx->ctr, ctx->ks);
            aes_ctr_inc(ctx->ctr);
            ctx->ks_used = 0u;
        }
        data[i] ^= ctx->ks[ctx->ks_used++];
    }
}

/* ------------------------- curve25519 (tweetnacl subset) ------------------------- */

/* Public domain implementation style adapted from tweetnacl C reference. */

typedef int64_t i64;
typedef i64 gf[16];

static const uint8_t curve9[32] = {9u};
static const gf curve121665 = {0xDB41,1};

static void set25519(gf r, const gf a) {
    for (uint32_t i = 0u; i < 16u; i++) {
        r[i] = a[i];
    }
}

static void car25519(gf o) {
    i64 c;
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] += (1ll << 16);
        c = o[i] >> 16;
        o[(i + 1u) * (i < 15u)] += c - 1ll + 37ll * (c - 1ll) * (i == 15u);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b) {
    i64 t;
    i64 c = ~(i64)(b - 1);
    for (uint32_t i = 0u; i < 16u; i++) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t* o, const gf n) {
    gf m;
    gf t;

    set25519(t, n);
    car25519(t);
    car25519(t);
    car25519(t);

    for (uint32_t j = 0u; j < 2u; j++) {
        m[0] = t[0] - 0xffed;
        for (uint32_t i = 1u; i < 15u; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1u] >> 16) & 1ll);
            m[i - 1u] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1ll);
        {
            int b = (int)((m[15] >> 16) & 1ll);
            m[14] &= 0xffff;
            sel25519(t, m, 1 - b);
        }
    }

    for (uint32_t i = 0u; i < 16u; i++) {
        o[2u * i] = (uint8_t)(t[i] & 0xFF);
        o[2u * i + 1u] = (uint8_t)((t[i] >> 8) & 0xFF);
    }
}

static void unpack25519(gf o, const uint8_t* n) {
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = (i64)n[2u * i] + ((i64)n[2u * i + 1u] << 8);
    }
    o[15] &= 0x7fffu;
}

static void A25519(gf o, const gf a, const gf b) {
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = a[i] + b[i];
    }
}

static void Z25519(gf o, const gf a, const gf b) {
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = a[i] - b[i];
    }
}

static void M25519(gf o, const gf a, const gf b) {
    i64 t[31];
    mem_zero(t, sizeof(t));

    for (uint32_t i = 0u; i < 16u; i++) {
        for (uint32_t j = 0u; j < 16u; j++) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (uint32_t i = 0u; i < 15u; i++) {
        t[i] += 38ll * t[i + 16u];
    }
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = t[i];
    }
    car25519(o);
    car25519(o);
}

static void S25519(gf o, const gf a) {
    M25519(o, a, a);
}

static void inv25519(gf o, const gf i) {
    gf c;
    set25519(c, i);
    for (int a = 253; a >= 0; a--) {
        S25519(c, c);
        if (a != 2 && a != 4) {
            M25519(c, c, i);
        }
    }
    set25519(o, c);
}

static int x25519_scalarmult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t z[32];
    gf x;
    gf a;
    gf b;
    gf c;
    gf d;
    gf e;
    gf f;

    for (uint32_t i = 0u; i < 32u; i++) {
        z[i] = scalar[i];
    }
    z[0] &= 248u;
    z[31] = (uint8_t)((z[31] & 127u) | 64u);

    unpack25519(x, point);

    mem_zero(a, sizeof(a));
    mem_zero(b, sizeof(b));
    mem_zero(c, sizeof(c));
    mem_zero(d, sizeof(d));
    mem_zero(e, sizeof(e));
    mem_zero(f, sizeof(f));

    a[0] = 1;
    /* Montgomery ladder state: x2=1,z2=0,x3=u,z3=1. */
    set25519(b, x);
    d[0] = 1;

    for (int i = 254; i >= 0; i--) {
        uint8_t r = (uint8_t)((z[i >> 3] >> (i & 7)) & 1u);

        sel25519(a, b, (int)r);
        sel25519(c, d, (int)r);

        A25519(e, a, c);
        Z25519(a, a, c);
        A25519(c, b, d);
        Z25519(b, b, d);
        S25519(d, e);
        S25519(f, a);
        M25519(a, c, a);
        M25519(c, b, e);
        A25519(e, a, c);
        Z25519(a, a, c);
        S25519(b, a);
        Z25519(c, d, f);
        M25519(a, c, curve121665);
        A25519(a, a, d);
        M25519(c, c, a);
        M25519(a, d, f);
        M25519(d, b, x);
        S25519(b, e);

        sel25519(a, b, (int)r);
        sel25519(c, d, (int)r);
    }

    inv25519(c, c);
    M25519(a, a, c);
    pack25519(out, a);

    mem_zero(z, sizeof(z));
    mem_zero(x, sizeof(x));
    mem_zero(a, sizeof(a));
    mem_zero(b, sizeof(b));
    mem_zero(c, sizeof(c));
    mem_zero(d, sizeof(d));
    mem_zero(e, sizeof(e));
    mem_zero(f, sizeof(f));
    return 0;
}

static int x25519_basepoint(uint8_t out[32], const uint8_t scalar[32]) {
    return x25519_scalarmult(out, scalar, curve9);
}

/* ------------------------- SSH packing helpers ------------------------- */

typedef struct {
    uint8_t* buf;
    uint32_t cap;
    uint32_t len;
    int err;
} wrbuf_t;

static void wr_init(wrbuf_t* w, uint8_t* buf, uint32_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0u;
    w->err = 0;
}

static void wr_put_u8(wrbuf_t* w, uint8_t v) {
    if (!w || w->err || w->len + 1u > w->cap) {
        if (w) {
            w->err = -1;
        }
        return;
    }
    w->buf[w->len++] = v;
}

static void wr_put_u32(wrbuf_t* w, uint32_t v) {
    if (!w || w->err || w->len + 4u > w->cap) {
        if (w) {
            w->err = -1;
        }
        return;
    }
    write_be32(&w->buf[w->len], v);
    w->len += 4u;
}

static void wr_put_raw(wrbuf_t* w, const uint8_t* data, uint32_t len) {
    if (!w || w->err || (len != 0u && !data) || w->len + len > w->cap) {
        if (w) {
            w->err = -1;
        }
        return;
    }
    if (len > 0u) {
        mem_copy_local(&w->buf[w->len], data, len);
        w->len += len;
    }
}

static void wr_put_string(wrbuf_t* w, const uint8_t* data, uint32_t len) {
    wr_put_u32(w, len);
    wr_put_raw(w, data, len);
}

static void wr_put_cstr(wrbuf_t* w, const char* text) {
    uint32_t len = (uint32_t)mya_strlen(text);
    wr_put_string(w, (const uint8_t*)text, len);
}

/*
 * RFC 8731 shared-secret encoding:
 * X25519 outputs X as little-endian bytes, then SSH interprets those octets as
 * a fixed-length big-endian unsigned integer for mpint encoding.
 * So we MUST keep byte order as-is here (re-interpretation, not reversal).
 */
static int mpint_from_le32(const uint8_t le[32], uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
    uint8_t be[33];
    uint32_t len = 32u;
    uint32_t i = 0u;

    if (!le || !out || !out_len) {
        return -1;
    }

    for (i = 0u; i < 32u; i++) {
        be[i] = le[i];
    }

    while (len > 0u && be[32u - len] == 0u) {
        len--;
    }

    if (len == 0u) {
        if (out_cap < 4u) {
            return -1;
        }
        write_be32(out, 0u);
        *out_len = 4u;
        return 0;
    }

    if (be[32u - len] & 0x80u) {
        if (len + 1u > 33u) {
            return -1;
        }
        for (i = len; i > 0u; i--) {
            be[i] = be[i - 1u + (32u - len)];
        }
        be[0] = 0u;
        len += 1u;
    } else {
        for (i = 0u; i < len; i++) {
            be[i] = be[i + (32u - len)];
        }
    }

    if (out_cap < 4u + len) {
        return -1;
    }
    write_be32(out, len);
    mem_copy_local(out + 4u, be, len);
    *out_len = 4u + len;
    return 0;
}

typedef struct {
    const uint8_t* buf;
    uint32_t len;
    uint32_t pos;
} rdbuf_t;

static void rd_init(rdbuf_t* r, const uint8_t* data, uint32_t len) {
    r->buf = data;
    r->len = len;
    r->pos = 0u;
}

static int rd_u8(rdbuf_t* r, uint8_t* out) {
    if (!r || !out || r->pos + 1u > r->len) {
        return -1;
    }
    *out = r->buf[r->pos++];
    return 0;
}

static int rd_u32(rdbuf_t* r, uint32_t* out) {
    if (!r || !out || r->pos + 4u > r->len) {
        return -1;
    }
    *out = read_be32(&r->buf[r->pos]);
    r->pos += 4u;
    return 0;
}

static int rd_string(rdbuf_t* r, const uint8_t** out_ptr, uint32_t* out_len) {
    uint32_t n = 0u;
    if (!r || !out_ptr || !out_len || rd_u32(r, &n) != 0 || r->pos + n > r->len) {
        return -1;
    }
    *out_ptr = &r->buf[r->pos];
    *out_len = n;
    r->pos += n;
    return 0;
}

static int namelist_contains(const uint8_t* list, uint32_t list_len, const char* name) {
    uint32_t name_len = (uint32_t)mya_strlen(name);
    uint32_t i = 0u;

    if (!list || !name || name_len == 0u) {
        return 0;
    }

    while (i < list_len) {
        uint32_t j = i;
        while (j < list_len && list[j] != ',') {
            j++;
        }
        if ((j - i) == name_len && str_eq_n((const char*)&list[i], name, name_len)) {
            return 1;
        }
        i = (j < list_len) ? (j + 1u) : j;
    }
    return 0;
}

static int bytes_eq_cstr(const uint8_t* bytes, uint32_t bytes_len, const char* text) {
    uint32_t n = (uint32_t)mya_strlen(text);
    if (!bytes || !text) {
        return 0;
    }
    if (bytes_len != n) {
        return 0;
    }
    return str_eq_n((const char*)bytes, text, n);
}

static int parse_hostkey_blob_algorithm(
    const uint8_t* hostkey_blob,
    uint32_t hostkey_blob_len,
    const uint8_t** out_alg,
    uint32_t* out_alg_len
) {
    rdbuf_t rd;
    const uint8_t* alg = NULL;
    uint32_t alg_len = 0u;

    if (!hostkey_blob || !out_alg || !out_alg_len) {
        return -1;
    }

    rd_init(&rd, hostkey_blob, hostkey_blob_len);
    if (rd_string(&rd, &alg, &alg_len) != 0 || alg_len == 0u || rd.pos > rd.len) {
        return -1;
    }

    *out_alg = alg;
    *out_alg_len = alg_len;
    return 0;
}

static int parse_signature_blob(
    const uint8_t* sig_blob,
    uint32_t sig_blob_len,
    const uint8_t** out_alg,
    uint32_t* out_alg_len,
    const uint8_t** out_sig,
    uint32_t* out_sig_len
) {
    rdbuf_t rd;
    const uint8_t* alg = NULL;
    uint32_t alg_len = 0u;
    const uint8_t* sig = NULL;
    uint32_t sig_len = 0u;

    if (!sig_blob || !out_alg || !out_alg_len || !out_sig || !out_sig_len) {
        return -1;
    }

    rd_init(&rd, sig_blob, sig_blob_len);
    if (rd_string(&rd, &alg, &alg_len) != 0 || rd_string(&rd, &sig, &sig_len) != 0) {
        return -1;
    }
    if (alg_len == 0u || sig_len == 0u || rd.pos != rd.len) {
        return -1;
    }

    *out_alg = alg;
    *out_alg_len = alg_len;
    *out_sig = sig;
    *out_sig_len = sig_len;
    return 0;
}

/* ------------------------- SSH transport ------------------------- */

typedef struct {
    int32_t fd;
    uint8_t stream[SSH_STREAM_BUF_MAX];
    uint32_t stream_pos;
    uint32_t stream_len;

    uint8_t packet_tmp[SSH_PACKET_PLAIN_MAX + SSH_MAC_LEN + 32u];

    uint8_t enc_active;
    aes_ctr_ctx_t c2s;
    aes_ctr_ctx_t s2c;
    uint8_t c2s_mac_key[SSH_MAC_LEN];
    uint8_t s2c_mac_key[SSH_MAC_LEN];
    uint32_t seq_c2s;
    uint32_t seq_s2c;

    char ident_client[SSH_IDENT_MAX];
    char ident_server[SSH_IDENT_MAX];

    uint8_t ic_payload[2048];
    uint32_t ic_len;
    uint8_t is_payload[2048];
    uint32_t is_len;

    uint8_t session_id[32];
    uint8_t session_id_set;

    uint8_t allow_insecure_hostkey;
    uint8_t remote_sig_ignored;
} ssh_conn_t;

static void stream_compact(ssh_conn_t* c) {
    if (!c) {
        return;
    }
    if (c->stream_pos == 0u) {
        return;
    }
    if (c->stream_pos >= c->stream_len) {
        c->stream_pos = 0u;
        c->stream_len = 0u;
        return;
    }
    mem_move_local(c->stream, c->stream + c->stream_pos, c->stream_len - c->stream_pos);
    c->stream_len -= c->stream_pos;
    c->stream_pos = 0u;
}

static int sock_send_all(int32_t fd, const uint8_t* data, uint32_t len) {
    uint32_t sent = 0u;
    while (sent < len) {
        uint32_t wrote = 0u;
        if (mya_sock_send(fd, 0u, data + sent, len - sent, &wrote) != 0) {
            return -1;
        }
        if (wrote == 0u) {
            mya_proc_yield();
            continue;
        }
        sent += wrote;
    }
    return 0;
}

static int sock_recv_into_stream(ssh_conn_t* c) {
    uint32_t got = 0u;
    uint32_t probe_written = 0u;
    uint16_t src_port = 0u;
    int rc;

    if (!c) {
        return -1;
    }

    stream_compact(c);
    if (c->stream_len >= sizeof(c->stream)) {
        return -1;
    }

    rc = mya_sock_recv(c->fd, c->stream + c->stream_len, (uint32_t)(sizeof(c->stream) - c->stream_len), &got, &src_port);
    if (rc != 0) {
        /*
         * In this ABI, TCP recv returns -1 both for "no payload yet" and for hard socket errors.
         * Probe socket liveness via a zero-length send (does not emit traffic): if it fails, treat as closed.
         */
        if (mya_sock_send(c->fd, 0u, NULL, 0u, &probe_written) != 0) {
            return -1;
        }
        return 0;
    }
    if (src_port != 0u) {
        (void)src_port;
    }
    if (got == 0u) {
        /* Peer closed (EOF). */
        return -1;
    }
    c->stream_len += got;
    return (int)got;
}

static int recv_line(ssh_conn_t* c, char* out, uint32_t out_cap, uint32_t wait_spins) {
    uint32_t spins = 0u;

    if (!c || !out || out_cap < 2u) {
        return -1;
    }

    for (;;) {
        for (uint32_t i = c->stream_pos; i < c->stream_len; i++) {
            if (c->stream[i] == '\n') {
                uint32_t line_start = c->stream_pos;
                uint32_t line_end = i;
                uint32_t n;
                if (line_end > line_start && c->stream[line_end - 1u] == '\r') {
                    line_end--;
                }
                n = line_end - line_start;
                if (n + 1u > out_cap) {
                    return -1;
                }
                if (n > 0u) {
                    mem_copy_local(out, c->stream + line_start, n);
                }
                out[n] = '\0';
                c->stream_pos = i + 1u;
                return 0;
            }
        }

        if (wait_spins != 0u && spins >= wait_spins) {
            return -2;
        }
        spins++;

        if (sock_recv_into_stream(c) < 0) {
            return -1;
        }
        mya_proc_yield();
    }
}

static int ssh_send_packet(ssh_conn_t* c, const uint8_t* payload, uint32_t payload_len) {
    uint32_t pad_len;
    uint32_t packet_len;
    uint32_t plain_len;
    uint8_t mac[SSH_MAC_LEN];

    if (!c || (!payload && payload_len != 0u) || payload_len > SSH_PAYLOAD_MAX) {
        return -1;
    }

    pad_len = SSH_BLOCK_SIZE - ((payload_len + 5u) % SSH_BLOCK_SIZE);
    if (pad_len < 4u) {
        pad_len += SSH_BLOCK_SIZE;
    }
    packet_len = payload_len + pad_len + 1u;
    plain_len = packet_len + 4u;

    if (plain_len + SSH_MAC_LEN > sizeof(c->packet_tmp)) {
        return -1;
    }

    write_be32(c->packet_tmp, packet_len);
    c->packet_tmp[4] = (uint8_t)pad_len;
    if (payload_len > 0u) {
        mem_copy_local(c->packet_tmp + 5u, payload, payload_len);
    }
    rng_fill(c->packet_tmp + 5u + payload_len, pad_len);

    if (c->enc_active) {
        hmac_sha256_seq_packet(c->c2s_mac_key, SSH_MAC_LEN, c->seq_c2s, c->packet_tmp, plain_len, mac);

        aes_ctr_xor(&c->c2s, c->packet_tmp, plain_len);
        if (sock_send_all(c->fd, c->packet_tmp, plain_len) != 0) {
            return -1;
        }
        if (sock_send_all(c->fd, mac, SSH_MAC_LEN) != 0) {
            return -1;
        }
    } else {
        if (sock_send_all(c->fd, c->packet_tmp, plain_len) != 0) {
            return -1;
        }
    }

    c->seq_c2s++;
    return 0;
}

static int ssh_try_parse_packet_from_stream(ssh_conn_t* c, uint8_t* out_payload, uint32_t out_cap, uint32_t* out_len) {
    uint32_t avail;

    if (!c || !out_payload || !out_len) {
        return -1;
    }

    if (c->stream_pos >= c->stream_len) {
        return 0;
    }
    avail = c->stream_len - c->stream_pos;

    if (!c->enc_active) {
        uint32_t packet_len;
        uint32_t total_len;
        uint8_t pad_len;
        uint32_t payload_len;

        if (avail < 5u) {
            return 0;
        }

        packet_len = read_be32(c->stream + c->stream_pos);
        if (packet_len < 1u || packet_len > SSH_PACKET_PLAIN_MAX) {
            return -1;
        }
        total_len = packet_len + 4u;
        if (avail < total_len) {
            return 0;
        }

        pad_len = c->stream[c->stream_pos + 4u];
        if ((uint32_t)pad_len + 1u > packet_len) {
            return -1;
        }
        payload_len = packet_len - (uint32_t)pad_len - 1u;
        if (payload_len > out_cap) {
            return -1;
        }

        if (payload_len > 0u) {
            mem_copy_local(out_payload, c->stream + c->stream_pos + 5u, payload_len);
        }
        *out_len = payload_len;

        c->stream_pos += total_len;
        c->seq_s2c++;
        return 1;
    }

    {
        uint32_t packet_len;
        uint32_t plain_len;
        uint32_t wire_len;
        uint8_t pad_len;
        uint32_t payload_len;
        aes_ctr_ctx_t preview;
        uint8_t first[SSH_BLOCK_SIZE];
        uint8_t calc_mac[SSH_MAC_LEN];

        if (avail < SSH_BLOCK_SIZE + SSH_MAC_LEN) {
            return 0;
        }

        preview = c->s2c;
        mem_copy_local(first, c->stream + c->stream_pos, SSH_BLOCK_SIZE);
        aes_ctr_xor(&preview, first, SSH_BLOCK_SIZE);
        packet_len = read_be32(first);
        if (packet_len < 1u || packet_len > SSH_PACKET_PLAIN_MAX) {
            return -1;
        }
        plain_len = packet_len + 4u;
        wire_len = plain_len + SSH_MAC_LEN;

        if (plain_len > sizeof(c->packet_tmp)) {
            return -1;
        }
        if (avail < wire_len) {
            return 0;
        }

        mem_copy_local(c->packet_tmp, c->stream + c->stream_pos, plain_len);
        aes_ctr_xor(&c->s2c, c->packet_tmp, plain_len);

        hmac_sha256_seq_packet(c->s2c_mac_key, SSH_MAC_LEN, c->seq_s2c, c->packet_tmp, plain_len, calc_mac);

        if (!bytes_eq(calc_mac, c->stream + c->stream_pos + plain_len, SSH_MAC_LEN)) {
            return -1;
        }

        pad_len = c->packet_tmp[4];
        if ((uint32_t)pad_len + 1u > packet_len) {
            return -1;
        }
        payload_len = packet_len - (uint32_t)pad_len - 1u;
        if (payload_len > out_cap) {
            return -1;
        }
        if (payload_len > 0u) {
            mem_copy_local(out_payload, c->packet_tmp + 5u, payload_len);
        }
        *out_len = payload_len;

        c->stream_pos += wire_len;
        c->seq_s2c++;
        return 1;
    }
}

static int ssh_recv_packet(ssh_conn_t* c, uint8_t* out_payload, uint32_t out_cap, uint32_t* out_len, uint32_t wait_spins) {
    uint32_t spins = 0u;

    if (!c || !out_payload || !out_len) {
        return -1;
    }

    for (;;) {
        int r = ssh_try_parse_packet_from_stream(c, out_payload, out_cap, out_len);
        if (r < 0) {
            return -1;
        }
        if (r > 0) {
            return 0;
        }

        if (wait_spins != 0u && spins >= wait_spins) {
            return -1;
        }
        spins++;

        if (sock_recv_into_stream(c) < 0) {
            return -1;
        }
        mya_proc_yield();
    }
}

/* ------------------------- SSH protocol ------------------------- */

typedef struct {
    const uint8_t* kex;
    uint32_t kex_len;
    const uint8_t* hostkey;
    uint32_t hostkey_len;
    const uint8_t* enc_c2s;
    uint32_t enc_c2s_len;
    const uint8_t* enc_s2c;
    uint32_t enc_s2c_len;
    const uint8_t* mac_c2s;
    uint32_t mac_c2s_len;
    const uint8_t* mac_s2c;
    uint32_t mac_s2c_len;
    const uint8_t* comp_c2s;
    uint32_t comp_c2s_len;
    const uint8_t* comp_s2c;
    uint32_t comp_s2c_len;
} kex_lists_t;

static int parse_kexinit_lists(const uint8_t* payload, uint32_t len, kex_lists_t* out) {
    rdbuf_t rd;
    uint8_t msg;
    const uint8_t* tmp;
    uint32_t tmp_len;

    if (!payload || !out) {
        return -1;
    }

    rd_init(&rd, payload, len);
    if (rd_u8(&rd, &msg) != 0 || msg != SSH_MSG_KEXINIT) {
        return -1;
    }

    if (rd.pos + 16u > rd.len) {
        return -1;
    }
    rd.pos += 16u;

    if (rd_string(&rd, &out->kex, &out->kex_len) != 0) return -1;
    if (rd_string(&rd, &out->hostkey, &out->hostkey_len) != 0) return -1;
    if (rd_string(&rd, &out->enc_c2s, &out->enc_c2s_len) != 0) return -1;
    if (rd_string(&rd, &out->enc_s2c, &out->enc_s2c_len) != 0) return -1;
    if (rd_string(&rd, &out->mac_c2s, &out->mac_c2s_len) != 0) return -1;
    if (rd_string(&rd, &out->mac_s2c, &out->mac_s2c_len) != 0) return -1;
    if (rd_string(&rd, &out->comp_c2s, &out->comp_c2s_len) != 0) return -1;
    if (rd_string(&rd, &out->comp_s2c, &out->comp_s2c_len) != 0) return -1;
    if (rd_string(&rd, &tmp, &tmp_len) != 0) return -1;
    if (rd_string(&rd, &tmp, &tmp_len) != 0) return -1;
    {
        uint8_t follows;
        uint32_t reserved;
        if (rd_u8(&rd, &follows) != 0 || rd_u32(&rd, &reserved) != 0) {
            return -1;
        }
        (void)follows;
        (void)reserved;
    }

    return 0;
}

static int append_hash_string(sha256_ctx_t* h, const uint8_t* s, uint32_t len) {
    uint8_t lb[4];
    if (!h || (!s && len != 0u)) {
        return -1;
    }
    write_be32(lb, len);
    sha256_update(h, lb, 4u);
    if (len > 0u) {
        sha256_update(h, s, len);
    }
    return 0;
}

static int compute_exchange_hash(
    const ssh_conn_t* c,
    const uint8_t* k_s,
    uint32_t k_s_len,
    const uint8_t q_c[32],
    const uint8_t q_s[32],
    const uint8_t* k_mpint,
    uint32_t k_mpint_len,
    uint8_t out_h[32]
) {
    sha256_ctx_t h;

    if (!c || !k_s || !q_c || !q_s || !k_mpint || !out_h) {
        return -1;
    }

    sha256_init(&h);

    if (append_hash_string(&h, (const uint8_t*)c->ident_client, (uint32_t)mya_strlen(c->ident_client)) != 0) return -1;
    if (append_hash_string(&h, (const uint8_t*)c->ident_server, (uint32_t)mya_strlen(c->ident_server)) != 0) return -1;
    if (append_hash_string(&h, c->ic_payload, c->ic_len) != 0) return -1;
    if (append_hash_string(&h, c->is_payload, c->is_len) != 0) return -1;
    if (append_hash_string(&h, k_s, k_s_len) != 0) return -1;
    if (append_hash_string(&h, q_c, 32u) != 0) return -1;
    if (append_hash_string(&h, q_s, 32u) != 0) return -1;

    sha256_update(&h, k_mpint, k_mpint_len);
    sha256_final(&h, out_h);
    return 0;
}

static int kdf_derive(
    const uint8_t* k_mpint,
    uint32_t k_mpint_len,
    const uint8_t h[32],
    const uint8_t session_id[32],
    uint8_t letter,
    uint8_t* out,
    uint32_t out_len
) {
    uint32_t produced = 0u;

    if (!k_mpint || !h || !session_id || !out) {
        return -1;
    }

    while (produced < out_len) {
        sha256_ctx_t ctx;
        uint8_t block[32];
        uint32_t take;

        sha256_init(&ctx);
        sha256_update(&ctx, k_mpint, k_mpint_len);
        sha256_update(&ctx, h, 32u);

        if (produced == 0u) {
            sha256_update(&ctx, &letter, 1u);
            sha256_update(&ctx, session_id, 32u);
        } else {
            sha256_update(&ctx, out, produced);
        }

        sha256_final(&ctx, block);

        take = min_u32((uint32_t)sizeof(block), out_len - produced);
        mem_copy_local(out + produced, block, take);
        produced += take;
    }

    return 0;
}

static int ssh_handshake_transport(ssh_conn_t* c) {
    uint8_t payload[SSH_PAYLOAD_MAX];
    uint32_t payload_len = 0u;
    uint8_t kex_payload[1024];
    uint8_t ecdh_init[64];
    uint8_t q_c[32];
    uint8_t q_s[32];
    uint8_t secret[32];
    uint8_t shared[32];
    uint8_t k_mpint[40];
    uint32_t k_mpint_len = 0u;
    uint8_t ex_hash[32];
    uint8_t msg;
    rdbuf_t rd;
    kex_lists_t lists;
    uint8_t iv_c2s[16];
    uint8_t iv_s2c[16];
    uint8_t key_c2s[16];
    uint8_t key_s2c[16];

    /* Build and send client KEXINIT. */
    {
        wrbuf_t w;
        wr_init(&w, kex_payload, sizeof(kex_payload));
        wr_put_u8(&w, SSH_MSG_KEXINIT);
        {
            uint8_t cookie[16];
            rng_fill(cookie, sizeof(cookie));
            wr_put_raw(&w, cookie, sizeof(cookie));
        }
        wr_put_cstr(&w, "curve25519-sha256,curve25519-sha256@libssh.org");
        wr_put_cstr(&w, "ssh-ed25519,rsa-sha2-256,rsa-sha2-512,ssh-rsa,ecdsa-sha2-nistp256");
        wr_put_cstr(&w, "aes128-ctr");
        wr_put_cstr(&w, "aes128-ctr");
        wr_put_cstr(&w, "hmac-sha2-256");
        wr_put_cstr(&w, "hmac-sha2-256");
        wr_put_cstr(&w, "none");
        wr_put_cstr(&w, "none");
        wr_put_cstr(&w, "");
        wr_put_cstr(&w, "");
        wr_put_u8(&w, 0u);
        wr_put_u32(&w, 0u);
        if (w.err != 0 || w.len > sizeof(c->ic_payload)) {
            mya_putln("ssh: transport: client kexinit build overflow");
            return -1;
        }
        c->ic_len = w.len;
        mem_copy_local(c->ic_payload, kex_payload, w.len);
        if (ssh_send_packet(c, kex_payload, w.len) != 0) {
            mya_putln("ssh: transport: failed to send client kexinit");
            return -1;
        }
    }

    /* Receive server KEXINIT. */
    for (;;) {
        if (ssh_recv_packet(c, payload, sizeof(payload), &payload_len, SSH_TIMEOUT_IO_SPINS) != 0) {
            mya_putln("ssh: transport: recv failed waiting server kexinit");
            return -1;
        }
        if (payload_len == 0u) {
            continue;
        }
        msg = payload[0];
        if (ssh_msg_ignorable(msg)) {
            continue;
        }
        if (msg != SSH_MSG_KEXINIT) {
            mya_puts("ssh: transport: unexpected msg while waiting kexinit: ");
            mya_put_u32((uint32_t)msg);
            mya_puts("\n");
            return -1;
        }
        if (payload_len > sizeof(c->is_payload)) {
            mya_putln("ssh: transport: server kexinit payload too large");
            return -1;
        }
        c->is_len = payload_len;
        mem_copy_local(c->is_payload, payload, payload_len);
        break;
    }

    if (parse_kexinit_lists(c->is_payload, c->is_len, &lists) != 0) {
        mya_putln("ssh: transport: failed to parse server kexinit");
        return -1;
    }

    if (!namelist_contains(lists.kex, lists.kex_len, "curve25519-sha256") &&
        !namelist_contains(lists.kex, lists.kex_len, "curve25519-sha256@libssh.org")) {
        mya_putln("ssh: server does not offer supported KEX");
        return -1;
    }
    if (!namelist_contains(lists.enc_c2s, lists.enc_c2s_len, "aes128-ctr") ||
        !namelist_contains(lists.enc_s2c, lists.enc_s2c_len, "aes128-ctr")) {
        mya_putln("ssh: server does not offer aes128-ctr");
        return -1;
    }
    if (!namelist_contains(lists.mac_c2s, lists.mac_c2s_len, "hmac-sha2-256") ||
        !namelist_contains(lists.mac_s2c, lists.mac_s2c_len, "hmac-sha2-256")) {
        mya_putln("ssh: server does not offer hmac-sha2-256");
        return -1;
    }
    if (!namelist_contains(lists.comp_c2s, lists.comp_c2s_len, "none") ||
        !namelist_contains(lists.comp_s2c, lists.comp_s2c_len, "none")) {
        mya_putln("ssh: server does not offer no-compression profile");
        return -1;
    }

    rng_fill(secret, sizeof(secret));
    if (x25519_basepoint(q_c, secret) != 0) {
        mya_putln("ssh: transport: x25519 public key failed");
        return -1;
    }

    {
        wrbuf_t w;
        wr_init(&w, ecdh_init, sizeof(ecdh_init));
        wr_put_u8(&w, SSH_MSG_KEX_ECDH_INIT);
        wr_put_string(&w, q_c, 32u);
        if (w.err != 0) {
            mya_putln("ssh: transport: failed to build ecdh init");
            return -1;
        }
        if (ssh_send_packet(c, ecdh_init, w.len) != 0) {
            mya_putln("ssh: transport: failed to send ecdh init");
            return -1;
        }
    }

    /* Receive KEX_ECDH_REPLY. */
    for (;;) {
        const uint8_t* k_s = NULL;
        uint32_t k_s_len = 0u;
        const uint8_t* q_s_blob = NULL;
        uint32_t q_s_len = 0u;
        const uint8_t* sig = NULL;
        uint32_t sig_len = 0u;
        const uint8_t* hostkey_alg = NULL;
        uint32_t hostkey_alg_len = 0u;
        const uint8_t* sig_alg = NULL;
        uint32_t sig_alg_len = 0u;
        const uint8_t* sig_raw = NULL;
        uint32_t sig_raw_len = 0u;

        if (ssh_recv_packet(c, payload, sizeof(payload), &payload_len, SSH_TIMEOUT_IO_SPINS) != 0) {
            mya_putln("ssh: transport: recv failed waiting kex ecdh reply");
            return -1;
        }
        if (payload_len == 0u) {
            continue;
        }
        msg = payload[0];
        if (ssh_msg_ignorable(msg)) {
            continue;
        }
        if (msg != SSH_MSG_KEX_ECDH_REPLY) {
            mya_puts("ssh: transport: unexpected msg while waiting kex reply: ");
            mya_put_u32((uint32_t)msg);
            mya_puts("\n");
            return -1;
        }

        rd_init(&rd, payload, payload_len);
        if (rd_u8(&rd, &msg) != 0 || msg != SSH_MSG_KEX_ECDH_REPLY) {
            mya_putln("ssh: transport: malformed kex ecdh reply header");
            return -1;
        }
        if (rd_string(&rd, &k_s, &k_s_len) != 0 || rd_string(&rd, &q_s_blob, &q_s_len) != 0 ||
            rd_string(&rd, &sig, &sig_len) != 0) {
            mya_putln("ssh: transport: malformed kex ecdh reply fields");
            return -1;
        }
        if (parse_hostkey_blob_algorithm(k_s, k_s_len, &hostkey_alg, &hostkey_alg_len) != 0 ||
            parse_signature_blob(sig, sig_len, &sig_alg, &sig_alg_len, &sig_raw, &sig_raw_len) != 0) {
            mya_putln("ssh: transport: malformed host-key/signature blob");
            return -1;
        }
        if (sig_alg_len != hostkey_alg_len || !bytes_eq(sig_alg, hostkey_alg, sig_alg_len)) {
            mya_putln("ssh: transport: host-key/signature algorithm mismatch");
            return -1;
        }
        if (!bytes_eq_cstr(hostkey_alg, hostkey_alg_len, "ssh-ed25519") &&
            !bytes_eq_cstr(hostkey_alg, hostkey_alg_len, "rsa-sha2-256") &&
            !bytes_eq_cstr(hostkey_alg, hostkey_alg_len, "rsa-sha2-512") &&
            !bytes_eq_cstr(hostkey_alg, hostkey_alg_len, "ssh-rsa") &&
            !bytes_eq_cstr(hostkey_alg, hostkey_alg_len, "ecdsa-sha2-nistp256")) {
            mya_putln("ssh: transport: unsupported host-key algorithm");
            return -1;
        }
        (void)sig_raw;
        (void)sig_raw_len;

        if (q_s_len != 32u) {
            mya_putln("ssh: transport: unexpected server ecdh key size");
            return -1;
        }
        mem_copy_local(q_s, q_s_blob, 32u);

        if (x25519_scalarmult(shared, secret, q_s) != 0) {
            mya_putln("ssh: transport: x25519 shared secret failed");
            return -1;
        }
        if (mpint_from_le32(shared, k_mpint, sizeof(k_mpint), &k_mpint_len) != 0) {
            mya_putln("ssh: transport: shared secret mpint conversion failed");
            return -1;
        }
        if (compute_exchange_hash(c, k_s, k_s_len, q_c, q_s, k_mpint, k_mpint_len, ex_hash) != 0) {
            mya_putln("ssh: transport: exchange hash computation failed");
            return -1;
        }

        if (!c->session_id_set) {
            mem_copy_local(c->session_id, ex_hash, sizeof(c->session_id));
            c->session_id_set = 1u;
        }

        if (!c->allow_insecure_hostkey) {
            mya_putln("ssh: transport: host-key signature verification is required");
            mya_putln("ssh: transport: this build cannot verify yet; rerun with --insecure-hostkey only if trusted");
            return -1;
        }
        c->remote_sig_ignored = 1u;
        break;
    }

    {
        uint8_t newkeys = SSH_MSG_NEWKEYS;
        if (ssh_send_packet(c, &newkeys, 1u) != 0) {
            mya_putln("ssh: transport: failed to send NEWKEYS");
            return -1;
        }
    }

    for (;;) {
        if (ssh_recv_packet(c, payload, sizeof(payload), &payload_len, SSH_TIMEOUT_IO_SPINS) != 0) {
            mya_putln("ssh: transport: recv failed waiting NEWKEYS");
            return -1;
        }
        if (payload_len == 0u) {
            continue;
        }
        msg = payload[0];
        if (ssh_msg_ignorable(msg)) {
            continue;
        }
        if (msg != SSH_MSG_NEWKEYS) {
            mya_puts("ssh: transport: unexpected msg while waiting NEWKEYS: ");
            mya_put_u32((uint32_t)msg);
            mya_puts("\n");
            return -1;
        }
        break;
    }

    if (kdf_derive(k_mpint, k_mpint_len, ex_hash, c->session_id, 'A', iv_c2s, sizeof(iv_c2s)) != 0) {
        mya_putln("ssh: transport: kdf failed (A)");
        return -1;
    }
    if (kdf_derive(k_mpint, k_mpint_len, ex_hash, c->session_id, 'B', iv_s2c, sizeof(iv_s2c)) != 0) {
        mya_putln("ssh: transport: kdf failed (B)");
        return -1;
    }
    if (kdf_derive(k_mpint, k_mpint_len, ex_hash, c->session_id, 'C', key_c2s, sizeof(key_c2s)) != 0) {
        mya_putln("ssh: transport: kdf failed (C)");
        return -1;
    }
    if (kdf_derive(k_mpint, k_mpint_len, ex_hash, c->session_id, 'D', key_s2c, sizeof(key_s2c)) != 0) {
        mya_putln("ssh: transport: kdf failed (D)");
        return -1;
    }
    if (kdf_derive(k_mpint, k_mpint_len, ex_hash, c->session_id, 'E', c->c2s_mac_key, sizeof(c->c2s_mac_key)) != 0) {
        mya_putln("ssh: transport: kdf failed (E)");
        return -1;
    }
    if (kdf_derive(k_mpint, k_mpint_len, ex_hash, c->session_id, 'F', c->s2c_mac_key, sizeof(c->s2c_mac_key)) != 0) {
        mya_putln("ssh: transport: kdf failed (F)");
        return -1;
    }

    aes_ctr_init(&c->c2s, key_c2s, iv_c2s);
    aes_ctr_init(&c->s2c, key_s2c, iv_s2c);
    c->enc_active = 1u;

    mem_zero(secret, sizeof(secret));
    mem_zero(shared, sizeof(shared));
    mem_zero(k_mpint, sizeof(k_mpint));
    mem_zero(ex_hash, sizeof(ex_hash));
    mem_zero(iv_c2s, sizeof(iv_c2s));
    mem_zero(iv_s2c, sizeof(iv_s2c));
    mem_zero(key_c2s, sizeof(key_c2s));
    mem_zero(key_s2c, sizeof(key_s2c));

    return 0;
}

static int ssh_service_userauth(ssh_conn_t* c) {
    uint8_t payload[SSH_PAYLOAD_MAX];
    uint32_t payload_len = 0u;
    uint8_t req[64];
    wrbuf_t w;

    if (!c) {
        return -1;
    }

    wr_init(&w, req, sizeof(req));
    wr_put_u8(&w, SSH_MSG_SERVICE_REQUEST);
    wr_put_cstr(&w, "ssh-userauth");
    if (w.err != 0 || ssh_send_packet(c, req, w.len) != 0) {
        return -1;
    }

    for (;;) {
        uint8_t msg;
        if (ssh_recv_packet(c, payload, sizeof(payload), &payload_len, SSH_TIMEOUT_IO_SPINS) != 0) {
            mya_putln("ssh: userauth service: recv packet failed");
            return -1;
        }
        if (payload_len == 0u) {
            continue;
        }
        msg = payload[0];
        if (ssh_msg_ignorable(msg)) {
            continue;
        }
        if (msg == SSH_MSG_DISCONNECT) {
            mya_putln("ssh: userauth service: server disconnected");
            return -1;
        }
        if (msg != SSH_MSG_SERVICE_ACCEPT) {
            mya_puts("ssh: userauth service: unexpected message ");
            mya_put_u32((uint32_t)msg);
            mya_puts("\n");
            return -1;
        }
        return 0;
    }
}

static int ssh_userauth_password(ssh_conn_t* c, const char* user, const char* password) {
    uint8_t payload[SSH_PAYLOAD_MAX];
    uint32_t payload_len = 0u;
    uint8_t req[512];
    wrbuf_t w;

    if (!c || !user || !password || !user[0]) {
        return -1;
    }

    wr_init(&w, req, sizeof(req));
    wr_put_u8(&w, SSH_MSG_USERAUTH_REQUEST);
    wr_put_cstr(&w, user);
    wr_put_cstr(&w, "ssh-connection");
    wr_put_cstr(&w, "password");
    wr_put_u8(&w, 0u);
    wr_put_cstr(&w, password);
    if (w.err != 0 || ssh_send_packet(c, req, w.len) != 0) {
        return -1;
    }

    for (;;) {
        uint8_t msg;
        if (ssh_recv_packet(c, payload, sizeof(payload), &payload_len, SSH_TIMEOUT_IO_SPINS) != 0) {
            return -1;
        }
        if (payload_len == 0u) {
            continue;
        }

        msg = payload[0];
        if (ssh_msg_ignorable(msg)) {
            continue;
        }
        if (msg == SSH_MSG_USERAUTH_BANNER) {
            rdbuf_t rd;
            const uint8_t* banner;
            uint32_t banner_len;
            const uint8_t* lang;
            uint32_t lang_len;
            rd_init(&rd, payload, payload_len);
            if (rd_u8(&rd, &msg) == 0 && rd_string(&rd, &banner, &banner_len) == 0 && rd_string(&rd, &lang, &lang_len) == 0) {
                console_write_len(banner, banner_len);
                mya_puts("\n");
                (void)lang;
                (void)lang_len;
            }
            continue;
        }
        if (msg == SSH_MSG_USERAUTH_SUCCESS) {
            return 0;
        }
        if (msg == SSH_MSG_USERAUTH_FAILURE) {
            rdbuf_t rd;
            const uint8_t* methods;
            uint32_t methods_len;
            uint8_t partial;
            rd_init(&rd, payload, payload_len);
            if (rd_u8(&rd, &msg) == 0 && rd_string(&rd, &methods, &methods_len) == 0 && rd_u8(&rd, &partial) == 0) {
                mya_puts("ssh: auth failed; server methods: ");
                console_write_len(methods, methods_len);
                if (partial) {
                    mya_puts(" (partial)");
                }
                mya_puts("\n");
            } else {
                mya_putln("ssh: auth failed");
            }
            return -1;
        }
        if (msg == SSH_MSG_DISCONNECT) {
            return -1;
        }
    }
}

typedef struct {
    uint32_t local_id;
    uint32_t remote_id;
    uint32_t remote_window;
    uint32_t remote_max_packet;
    uint32_t local_window;
    uint8_t opened;
    uint8_t local_closed;
    uint8_t remote_closed;
    int32_t exit_status;
    uint8_t exit_status_set;
} ssh_channel_t;

static int ssh_channel_open_session(ssh_conn_t* c, ssh_channel_t* ch) {
    uint8_t req[256];
    uint8_t payload[SSH_PAYLOAD_MAX];
    uint32_t payload_len = 0u;
    wrbuf_t w;

    if (!c || !ch) {
        return -1;
    }

    ch->local_id = 0u;
    ch->local_window = SSH_CHANNEL_WIN;
    ch->remote_window = 0u;
    ch->remote_max_packet = 0u;
    ch->opened = 0u;
    ch->local_closed = 0u;
    ch->remote_closed = 0u;
    ch->exit_status = 0;
    ch->exit_status_set = 0u;

    wr_init(&w, req, sizeof(req));
    wr_put_u8(&w, SSH_MSG_CHANNEL_OPEN);
    wr_put_cstr(&w, "session");
    wr_put_u32(&w, ch->local_id);
    wr_put_u32(&w, SSH_CHANNEL_WIN);
    wr_put_u32(&w, SSH_CHANNEL_MAX_PKT);
    if (w.err != 0 || ssh_send_packet(c, req, w.len) != 0) {
        return -1;
    }

    for (;;) {
        uint8_t msg;
        if (ssh_recv_packet(c, payload, sizeof(payload), &payload_len, SSH_TIMEOUT_IO_SPINS) != 0) {
            return -1;
        }
        if (payload_len == 0u) {
            continue;
        }
        msg = payload[0];
        if (ssh_msg_ignorable(msg)) {
            continue;
        }
        if (msg == SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
            rdbuf_t rd;
            uint32_t recipient;
            uint32_t sender;
            uint32_t wnd;
            uint32_t mpkt;
            rd_init(&rd, payload, payload_len);
            if (rd_u8(&rd, &msg) != 0 || rd_u32(&rd, &recipient) != 0 || rd_u32(&rd, &sender) != 0 ||
                rd_u32(&rd, &wnd) != 0 || rd_u32(&rd, &mpkt) != 0) {
                return -1;
            }
            if (recipient != ch->local_id) {
                return -1;
            }
            ch->remote_id = sender;
            ch->remote_window = wnd;
            ch->remote_max_packet = (mpkt == 0u) ? SSH_CHANNEL_MAX_PKT : mpkt;
            ch->opened = 1u;
            return 0;
        }
        if (msg == SSH_MSG_CHANNEL_OPEN_FAILURE) {
            mya_putln("ssh: channel open failed");
            return -1;
        }
        if (msg == SSH_MSG_DISCONNECT) {
            return -1;
        }
    }
}

static int ssh_channel_send_request(
    ssh_conn_t* c,
    const ssh_channel_t* ch,
    const char* req_name,
    uint8_t want_reply,
    const uint8_t* extra,
    uint32_t extra_len
) {
    uint8_t pkt[256];
    wrbuf_t w;

    if (!c || !ch || !req_name || (extra_len != 0u && !extra)) {
        return -1;
    }

    wr_init(&w, pkt, sizeof(pkt));
    wr_put_u8(&w, SSH_MSG_CHANNEL_REQUEST);
    wr_put_u32(&w, ch->remote_id);
    wr_put_cstr(&w, req_name);
    wr_put_u8(&w, want_reply ? 1u : 0u);
    wr_put_raw(&w, extra, extra_len);

    if (w.err != 0) {
        return -1;
    }
    return ssh_send_packet(c, pkt, w.len);
}

static int ssh_channel_wait_req_reply(ssh_conn_t* c, ssh_channel_t* ch) {
    uint8_t payload[SSH_PAYLOAD_MAX];
    uint32_t payload_len = 0u;

    if (!c || !ch) {
        return -1;
    }

    for (;;) {
        uint8_t msg;
        if (ssh_recv_packet(c, payload, sizeof(payload), &payload_len, SSH_TIMEOUT_IO_SPINS) != 0) {
            return -1;
        }
        if (payload_len == 0u) {
            continue;
        }
        msg = payload[0];
        if (ssh_msg_ignorable(msg)) {
            continue;
        }
        if (msg == SSH_MSG_CHANNEL_SUCCESS) {
            return 0;
        }
        if (msg == SSH_MSG_CHANNEL_FAILURE) {
            return -1;
        }

        /* Process common async channel packets while waiting. */
        if (msg == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
            rdbuf_t rd;
            uint32_t recipient;
            uint32_t add;
            rd_init(&rd, payload, payload_len);
            if (rd_u8(&rd, &msg) == 0 && rd_u32(&rd, &recipient) == 0 && rd_u32(&rd, &add) == 0 && recipient == ch->local_id) {
                ch->remote_window += add;
            }
            continue;
        }
        if (msg == SSH_MSG_CHANNEL_DATA || msg == SSH_MSG_CHANNEL_EXTENDED_DATA) {
            rdbuf_t rd;
            uint32_t recipient;
            const uint8_t* data;
            uint32_t data_len;
            rd_init(&rd, payload, payload_len);
            if (rd_u8(&rd, &msg) == 0 && rd_u32(&rd, &recipient) == 0) {
                if (msg == SSH_MSG_CHANNEL_EXTENDED_DATA) {
                    uint32_t dtype;
                    if (rd_u32(&rd, &dtype) != 0) {
                        continue;
                    }
                    (void)dtype;
                }
                if (rd_string(&rd, &data, &data_len) == 0 && recipient == ch->local_id) {
                    console_write_len(data, data_len);
                    if (data_len <= ch->local_window) {
                        ch->local_window -= data_len;
                    } else {
                        ch->local_window = 0u;
                    }
                }
            }
            continue;
        }
        if (msg == SSH_MSG_CHANNEL_EOF || msg == SSH_MSG_CHANNEL_CLOSE) {
            rdbuf_t rd;
            uint32_t recipient;
            rd_init(&rd, payload, payload_len);
            if (rd_u8(&rd, &msg) == 0 && rd_u32(&rd, &recipient) == 0 && recipient == ch->local_id) {
                if (msg == SSH_MSG_CHANNEL_EOF) {
                    ch->remote_closed = 1u;
                } else {
                    ch->remote_closed = 1u;
                }
            }
            continue;
        }
        if (msg == SSH_MSG_DISCONNECT) {
            return -1;
        }
    }
}

static int ssh_channel_request_pty_and_shell(ssh_conn_t* c, ssh_channel_t* ch) {
    uint8_t extra[96];
    wrbuf_t w;

    if (!c || !ch) {
        return -1;
    }

    wr_init(&w, extra, sizeof(extra));
    wr_put_cstr(&w, "xterm");
    wr_put_u32(&w, 80u);
    wr_put_u32(&w, 25u);
    wr_put_u32(&w, 0u);
    wr_put_u32(&w, 0u);
    wr_put_string(&w, NULL, 0u);
    if (w.err != 0) {
        return -1;
    }

    if (ssh_channel_send_request(c, ch, "pty-req", 1u, extra, w.len) != 0) {
        return -1;
    }
    if (ssh_channel_wait_req_reply(c, ch) != 0) {
        return -1;
    }

    if (ssh_channel_send_request(c, ch, "shell", 1u, NULL, 0u) != 0) {
        return -1;
    }
    if (ssh_channel_wait_req_reply(c, ch) != 0) {
        return -1;
    }

    return 0;
}

static int ssh_channel_send_data(
    ssh_conn_t* c,
    ssh_channel_t* ch,
    const uint8_t* data,
    uint32_t len,
    uint32_t* out_sent
) {
    uint32_t sent = 0u;

    if (!c || !ch || (len != 0u && !data) || !out_sent) {
        return -1;
    }

    while (sent < len) {
        uint32_t chunk;
        uint8_t pkt[SSH_CHANNEL_MAX_PKT + 64u];
        wrbuf_t w;

        if (ch->remote_window == 0u) {
            break;
        }

        chunk = len - sent;
        if (chunk > ch->remote_window) {
            chunk = ch->remote_window;
        }
        if (chunk > ch->remote_max_packet && ch->remote_max_packet != 0u) {
            chunk = ch->remote_max_packet;
        }
        if (chunk > SSH_CHANNEL_MAX_PKT) {
            chunk = SSH_CHANNEL_MAX_PKT;
        }
        if (chunk == 0u) {
            break;
        }

        wr_init(&w, pkt, sizeof(pkt));
        wr_put_u8(&w, SSH_MSG_CHANNEL_DATA);
        wr_put_u32(&w, ch->remote_id);
        wr_put_string(&w, data + sent, chunk);
        if (w.err != 0 || ssh_send_packet(c, pkt, w.len) != 0) {
            return -1;
        }

        ch->remote_window -= chunk;
        sent += chunk;
    }

    *out_sent = sent;
    return 0;
}

static int ssh_send_global_request_failure(ssh_conn_t* c) {
    uint8_t msg = SSH_MSG_REQUEST_FAILURE;
    return ssh_send_packet(c, &msg, 1u);
}

static int ssh_channel_send_close(ssh_conn_t* c, ssh_channel_t* ch) {
    uint8_t pkt[8];
    wrbuf_t w;

    if (!c || !ch || ch->local_closed) {
        return 0;
    }

    wr_init(&w, pkt, sizeof(pkt));
    wr_put_u8(&w, SSH_MSG_CHANNEL_CLOSE);
    wr_put_u32(&w, ch->remote_id);
    if (w.err != 0) {
        return -1;
    }
    if (ssh_send_packet(c, pkt, w.len) != 0) {
        return -1;
    }
    ch->local_closed = 1u;
    return 0;
}

static int ssh_interactive_shell(ssh_conn_t* c, ssh_channel_t* ch) {
    uint8_t payload[SSH_PAYLOAD_MAX];
    uint32_t payload_len = 0u;
    uint8_t pending[SSH_PENDING_TX_MAX];
    uint32_t pending_len = 0u;
    uint8_t running = 1u;

    if (!c || !ch || !ch->opened) {
        return -1;
    }

    while (running) {
        int key = mya_console_readchar();

        if (key != 0) {
            uint8_t input_bytes[8];
            uint32_t input_len = 0u;

            if (key == 0x1d) { /* Ctrl-] */
                running = 0u;
            } else if ((uint8_t)key == (uint8_t)KB_UP) {
                input_bytes[0] = 0x1b; input_bytes[1] = '['; input_bytes[2] = 'A'; input_len = 3u;
            } else if ((uint8_t)key == (uint8_t)KB_DOWN) {
                input_bytes[0] = 0x1b; input_bytes[1] = '['; input_bytes[2] = 'B'; input_len = 3u;
            } else if ((uint8_t)key == (uint8_t)KB_RIGHT) {
                input_bytes[0] = 0x1b; input_bytes[1] = '['; input_bytes[2] = 'C'; input_len = 3u;
            } else if ((uint8_t)key == (uint8_t)KB_LEFT) {
                input_bytes[0] = 0x1b; input_bytes[1] = '['; input_bytes[2] = 'D'; input_len = 3u;
            } else if (key == '\n' || key == '\r') {
                input_bytes[0] = '\r';
                input_len = 1u;
            } else if (key == '\b') {
                input_bytes[0] = 0x7f;
                input_len = 1u;
            } else if (key == 0x03) {
                input_bytes[0] = 0x03;
                input_len = 1u;
            } else if ((uint8_t)key == 0x04u) {
                input_bytes[0] = 0x04;
                input_len = 1u;
            } else if (is_ascii_printable((char)key) || (uint8_t)key == '\t') {
                input_bytes[0] = (uint8_t)key;
                input_len = 1u;
            }

            if (input_len > 0u && pending_len + input_len <= sizeof(pending)) {
                mem_copy_local(pending + pending_len, input_bytes, input_len);
                pending_len += input_len;
            }
        }

        if (pending_len > 0u && !ch->remote_closed && !ch->local_closed) {
            uint32_t sent = 0u;
            if (ssh_channel_send_data(c, ch, pending, pending_len, &sent) != 0) {
                return -1;
            }
            if (sent > 0u) {
                if (sent < pending_len) {
                    mem_move_local(pending, pending + sent, pending_len - sent);
                }
                pending_len -= sent;
            }
        }

        for (;;) {
            int got;
            got = ssh_try_parse_packet_from_stream(c, payload, sizeof(payload), &payload_len);
            if (got < 0) {
                return -1;
            }
            if (got == 0) {
                break;
            }

            if (payload_len == 0u) {
                continue;
            }

            {
                uint8_t msg = payload[0];
                if (ssh_msg_ignorable(msg)) {
                    continue;
                }
                if (msg == SSH_MSG_DISCONNECT) {
                    running = 0u;
                    break;
                }
                if (msg == SSH_MSG_GLOBAL_REQUEST) {
                    rdbuf_t rd;
                    uint8_t want_reply = 0u;
                    const uint8_t* req_name;
                    uint32_t req_name_len;
                    rd_init(&rd, payload, payload_len);
                    if (rd_u8(&rd, &msg) == 0 && rd_string(&rd, &req_name, &req_name_len) == 0 && rd_u8(&rd, &want_reply) == 0) {
                        (void)req_name;
                        (void)req_name_len;
                        if (want_reply) {
                            (void)ssh_send_global_request_failure(c);
                        }
                    }
                    continue;
                }
                if (msg == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
                    rdbuf_t rd;
                    uint32_t recipient;
                    uint32_t add;
                    rd_init(&rd, payload, payload_len);
                    if (rd_u8(&rd, &msg) == 0 && rd_u32(&rd, &recipient) == 0 && rd_u32(&rd, &add) == 0 && recipient == ch->local_id) {
                        ch->remote_window += add;
                    }
                    continue;
                }
                if (msg == SSH_MSG_CHANNEL_DATA || msg == SSH_MSG_CHANNEL_EXTENDED_DATA) {
                    rdbuf_t rd;
                    uint32_t recipient;
                    const uint8_t* data;
                    uint32_t data_len;
                    rd_init(&rd, payload, payload_len);
                    if (rd_u8(&rd, &msg) != 0 || rd_u32(&rd, &recipient) != 0) {
                        continue;
                    }
                    if (recipient != ch->local_id) {
                        continue;
                    }
                    if (msg == SSH_MSG_CHANNEL_EXTENDED_DATA) {
                        uint32_t dtype;
                        if (rd_u32(&rd, &dtype) != 0) {
                            continue;
                        }
                        (void)dtype;
                    }
                    if (rd_string(&rd, &data, &data_len) != 0) {
                        continue;
                    }
                    if (data_len > 0u) {
                        console_write_len(data, data_len);
                        if (data_len <= ch->local_window) {
                            ch->local_window -= data_len;
                        } else {
                            ch->local_window = 0u;
                        }
                        if (ch->local_window < SSH_CHANNEL_WIN_LOW) {
                            uint32_t add = SSH_CHANNEL_WIN - ch->local_window;
                            uint8_t pkt[16];
                            wrbuf_t w;
                            wr_init(&w, pkt, sizeof(pkt));
                            wr_put_u8(&w, SSH_MSG_CHANNEL_WINDOW_ADJUST);
                            wr_put_u32(&w, ch->remote_id);
                            wr_put_u32(&w, add);
                            if (w.err == 0 && ssh_send_packet(c, pkt, w.len) == 0) {
                                ch->local_window += add;
                            }
                        }
                    }
                    continue;
                }
                if (msg == SSH_MSG_CHANNEL_REQUEST) {
                    rdbuf_t rd;
                    uint32_t recipient;
                    const uint8_t* req_name;
                    uint32_t req_name_len;
                    uint8_t want_reply;
                    rd_init(&rd, payload, payload_len);
                    if (rd_u8(&rd, &msg) != 0 || rd_u32(&rd, &recipient) != 0 || rd_string(&rd, &req_name, &req_name_len) != 0 ||
                        rd_u8(&rd, &want_reply) != 0) {
                        continue;
                    }
                    if (recipient != ch->local_id) {
                        continue;
                    }
                    if (req_name_len == 11u && str_eq_n((const char*)req_name, "exit-status", 11u)) {
                        uint32_t st;
                        if (rd_u32(&rd, &st) == 0) {
                            ch->exit_status = (int32_t)st;
                            ch->exit_status_set = 1u;
                        }
                    }
                    if (want_reply) {
                        uint8_t rep[8];
                        wrbuf_t w;
                        wr_init(&w, rep, sizeof(rep));
                        wr_put_u8(&w, SSH_MSG_CHANNEL_FAILURE);
                        wr_put_u32(&w, ch->remote_id);
                        if (w.err == 0) {
                            (void)ssh_send_packet(c, rep, w.len);
                        }
                    }
                    continue;
                }
                if (msg == SSH_MSG_CHANNEL_EOF) {
                    rdbuf_t rd;
                    uint32_t recipient;
                    rd_init(&rd, payload, payload_len);
                    if (rd_u8(&rd, &msg) == 0 && rd_u32(&rd, &recipient) == 0 && recipient == ch->local_id) {
                        ch->remote_closed = 1u;
                    }
                    continue;
                }
                if (msg == SSH_MSG_CHANNEL_CLOSE) {
                    rdbuf_t rd;
                    uint32_t recipient;
                    rd_init(&rd, payload, payload_len);
                    if (rd_u8(&rd, &msg) == 0 && rd_u32(&rd, &recipient) == 0 && recipient == ch->local_id) {
                        ch->remote_closed = 1u;
                        if (!ch->local_closed) {
                            (void)ssh_channel_send_close(c, ch);
                        }
                        running = 0u;
                        break;
                    }
                    continue;
                }
            }
        }

        if (running) {
            int recv_rc = sock_recv_into_stream(c);
            if (recv_rc < 0) {
                return -1;
            }
            if (recv_rc == 0 && pending_len == 0u) {
                mya_proc_yield();
            }
        }
    }

    if (!ch->local_closed) {
        (void)ssh_channel_send_close(c, ch);
    }

    return ch->exit_status_set ? ch->exit_status : 0;
}

static int parse_target_user_host(
    const char* arg,
    char* out_user,
    uint32_t out_user_cap,
    char* out_host,
    uint32_t out_host_cap
) {
    uint32_t at = 0u;
    uint32_t len;

    if (!arg || !out_host || out_host_cap == 0u) {
        return -1;
    }

    len = (uint32_t)mya_strlen(arg);
    for (at = 0u; at < len; at++) {
        if (arg[at] == '@') {
            uint32_t ulen = at;
            uint32_t hlen = len - at - 1u;
            if (!out_user || out_user_cap == 0u || ulen == 0u || ulen + 1u > out_user_cap || hlen == 0u || hlen + 1u > out_host_cap) {
                return -1;
            }
            mem_copy_local(out_user, arg, ulen);
            out_user[ulen] = '\0';
            mem_copy_local(out_host, arg + at + 1u, hlen);
            out_host[hlen] = '\0';
            return 0;
        }
    }

    if (len + 1u > out_host_cap) {
        return -1;
    }
    mem_copy_local(out_host, arg, len);
    out_host[len] = '\0';
    if (out_user && out_user_cap > 0u) {
        out_user[0] = '\0';
    }
    return 0;
}

static int collect_credentials(
    int argc,
    char** argv,
    char* out_user,
    uint32_t user_cap,
    char* out_host,
    uint32_t host_cap,
    uint16_t* out_port,
    char* out_password,
    uint32_t pass_cap,
    uint8_t* out_allow_insecure_hostkey
) {
    const char* target = NULL;
    const char* opt_user = NULL;
    const char* opt_pass = NULL;
    uint16_t port = SSH_PORT_DEFAULT;

    if (!out_user || !out_host || !out_port || !out_password || !out_allow_insecure_hostkey ||
        user_cap == 0u || host_cap == 0u || pass_cap == 0u) {
        return -1;
    }

    out_user[0] = '\0';
    out_host[0] = '\0';
    out_password[0] = '\0';
    *out_allow_insecure_hostkey = 0u;

    for (int i = 1; i < argc; i++) {
        if (mya_streq(argv[i], "-p")) {
            if (i + 1 >= argc || parse_port(argv[i + 1], &port) != 0) {
                return -1;
            }
            i++;
            continue;
        }
        if (mya_streq(argv[i], "-l")) {
            if (i + 1 >= argc) {
                return -1;
            }
            opt_user = argv[++i];
            continue;
        }
        if (mya_streq(argv[i], "-pw")) {
            if (i + 1 >= argc) {
                return -1;
            }
            opt_pass = argv[++i];
            continue;
        }
        if (mya_streq(argv[i], "--insecure-hostkey")) {
            *out_allow_insecure_hostkey = 1u;
            continue;
        }
        if (!target) {
            target = argv[i];
            continue;
        }
        if (parse_port(argv[i], &port) == 0) {
            continue;
        }
        return -1;
    }

    if (!target) {
        return -1;
    }

    if (parse_target_user_host(target, out_user, user_cap, out_host, host_cap) != 0) {
        return -1;
    }

    if (opt_user) {
        uint32_t ulen = (uint32_t)mya_strlen(opt_user);
        if (ulen + 1u > user_cap) {
            return -1;
        }
        mem_copy_local(out_user, opt_user, ulen + 1u);
    }

    if (!out_user[0]) {
        mya_puts("user: ");
        if (mya_console_readline(out_user, user_cap, 1u) < 0 || !out_user[0]) {
            return -1;
        }
    }

    if (opt_pass) {
        uint32_t plen = (uint32_t)mya_strlen(opt_pass);
        if (plen + 1u > pass_cap) {
            return -1;
        }
        mem_copy_local(out_password, opt_pass, plen + 1u);
    } else {
        mya_puts("password: ");
        if (mya_console_readline(out_password, pass_cap, 0u) < 0) {
            return -1;
        }
        mya_puts("\n");
    }

    *out_port = port;
    return 0;
}

int program_main(int argc, char** argv) {
    ssh_conn_t conn;
    ssh_channel_t ch;
    char user[64];
    char host[64];
    char pass[128];
    uint16_t port = SSH_PORT_DEFAULT;
    uint8_t allow_insecure_hostkey = 0u;
    uint32_t dst_ip = 0u;

    if (argc < 2) {
        mya_putln("usage: ssh [--insecure-hostkey] [-l user] [-p port] [-pw password] <user@ipv4|ipv4> [port]");
        mya_putln("hotkeys: Ctrl-] to exit local session");
        return 1;
    }

    if (collect_credentials(
            argc,
            argv,
            user,
            sizeof(user),
            host,
            sizeof(host),
            &port,
            pass,
            sizeof(pass),
            &allow_insecure_hostkey
        ) != 0) {
        mya_putln("ssh: invalid arguments");
        mya_putln("usage: ssh [--insecure-hostkey] [-l user] [-p port] [-pw password] <user@ipv4|ipv4> [port]");
        return 1;
    }

    if (parse_ipv4(host, &dst_ip) != 0) {
        mya_putln("ssh: only numeric IPv4 is supported right now");
        return 1;
    }

    mem_zero(&conn, sizeof(conn));
    conn.fd = -1;
    conn.enc_active = 0u;
    conn.allow_insecure_hostkey = allow_insecure_hostkey;

    {
        static const char ident[] = "SSH-2.0-MyaOS_0.2";
        mem_copy_local(conn.ident_client, ident, sizeof(ident));
    }

    if (mya_sock_open_ex(MYAOS_SOCK_PROTO_TCP, 0u, &conn.fd) != 0) {
        mya_putln("ssh: socket open failed");
        return 1;
    }
    if (mya_sock_connect4(conn.fd, dst_ip, port) != 0) {
        (void)mya_sock_close(conn.fd);
        mya_putln("ssh: connect failed");
        mya_putln("ssh: check network config (ip/route) and reachability (ping <host>)");
        return 1;
    }

    {
        char line[SSH_LINE_MAX];
        char send_line[SSH_LINE_MAX];
        uint32_t n = (uint32_t)mya_strlen(conn.ident_client);

        if (n + 3u > sizeof(send_line)) {
            (void)mya_sock_close(conn.fd);
            return 1;
        }
        mem_copy_local(send_line, conn.ident_client, n);
        send_line[n + 0u] = '\r';
        send_line[n + 1u] = '\n';
        send_line[n + 2u] = '\0';
        if (sock_send_all(conn.fd, (const uint8_t*)send_line, n + 2u) != 0) {
            (void)mya_sock_close(conn.fd);
            mya_putln("ssh: failed to send identification");
            return 1;
        }

        for (;;) {
            int line_rc = recv_line(&conn, line, sizeof(line), SSH_TIMEOUT_IDENT_SPINS);
            if (line_rc != 0) {
                (void)mya_sock_close(conn.fd);
                if (line_rc == -2) {
                    mya_putln("ssh: timeout waiting server identification");
                    mya_putln("ssh: check that destination is an SSH server on port 22");
                    mya_putln("ssh: in QEMU user-net, host is usually reachable as 10.0.2.2");
                } else {
                    mya_putln("ssh: connection closed before server identification");
                }
                return 1;
            }
            if (cstr_starts_with(line, "SSH-")) {
                uint32_t l = (uint32_t)mya_strlen(line);
                if (l + 1u > sizeof(conn.ident_server)) {
                    (void)mya_sock_close(conn.fd);
                    mya_putln("ssh: server identification too long");
                    return 1;
                }
                mem_copy_local(conn.ident_server, line, l + 1u);
                break;
            }
        }

        if (!cstr_starts_with(conn.ident_server, "SSH-2.0-") && !cstr_starts_with(conn.ident_server, "SSH-1.99-")) {
            (void)mya_sock_close(conn.fd);
            mya_putln("ssh: server does not support SSHv2");
            return 1;
        }
    }

    if (ssh_handshake_transport(&conn) != 0) {
        (void)mya_sock_close(conn.fd);
        mya_putln("ssh: transport handshake failed");
        return 1;
    }

    if (conn.remote_sig_ignored) {
        mya_putln("ssh: warning: host-key verification bypassed (--insecure-hostkey)");
    }

    if (ssh_service_userauth(&conn) != 0) {
        (void)mya_sock_close(conn.fd);
        mya_putln("ssh: userauth service failed");
        return 1;
    }

    if (ssh_userauth_password(&conn, user, pass) != 0) {
        (void)mya_sock_close(conn.fd);
        return 1;
    }

    if (ssh_channel_open_session(&conn, &ch) != 0) {
        (void)mya_sock_close(conn.fd);
        mya_putln("ssh: failed to open session channel");
        return 1;
    }

    if (ssh_channel_request_pty_and_shell(&conn, &ch) != 0) {
        (void)mya_sock_close(conn.fd);
        mya_putln("ssh: failed to request remote shell");
        return 1;
    }

    {
        int rc = ssh_interactive_shell(&conn, &ch);
        (void)mya_sock_close(conn.fd);
        mem_zero(pass, sizeof(pass));
        return (rc == 0) ? 0 : 1;
    }
}
