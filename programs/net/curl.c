#include "../lib/myaos.h"
#include "../lib/netutil.h"

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

#define CURL_REQ_MAX 1024u
#define CURL_HOST_MAX 64u
#define CURL_HOST_HEADER_MAX 80u
#define CURL_PATH_MAX 384u
#define CURL_RECV_CHUNK 1024u
#define CURL_HEADER_MAX 8192u
#define CURL_TIMEOUT_POLLS_DEFAULT 120000u
#define CURL_PROXY_SPEC_MAX 96u
#define CURL_TLS_RECORD_MAX (18u * 1024u)
#define CURL_TLS_INNER_MAX (16u * 1024u)
#define CURL_TLS_ALERT_LEVEL_WARNING 1u
#define CURL_TLS_ALERT_LEVEL_FATAL 2u
#define CURL_TLS_ALERT_CLOSE_NOTIFY 0u
#define CURL_TLS_CONTENT_CHANGE_CIPHER_SPEC 20u
#define CURL_TLS_CONTENT_ALERT 21u
#define CURL_TLS_CONTENT_HANDSHAKE 22u
#define CURL_TLS_CONTENT_APPLICATION_DATA 23u
#define CURL_TLS_HS_CLIENT_HELLO 1u
#define CURL_TLS_HS_SERVER_HELLO 2u
#define CURL_TLS_HS_FINISHED 20u
#define CURL_TLS_VER_1_2 0x0303u
#define CURL_TLS_VER_1_3 0x0304u
#define CURL_TLS_GROUP_X25519 0x001Du
#define CURL_TLS_AES_128_GCM_SHA256 0x1301u
#define CURL_TLS_EXT_SERVER_NAME 0x0000u
#define CURL_TLS_EXT_SUPPORTED_GROUPS 0x000au
#define CURL_TLS_EXT_SIGNATURE_ALGS 0x000du
#define CURL_TLS_EXT_SUPPORTED_VERSIONS 0x002bu
#define CURL_TLS_EXT_PSK_KEX_MODES 0x002du
#define CURL_TLS_EXT_KEY_SHARE 0x0033u
#define CURL_TLS_MAX_CLIENT_HELLO 2048u
#define CURL_TLS_MAX_SERVER_HELLO 4096u
#define CURL_TLS_MAX_HS_MESSAGE 65535u
#define CURL_TLS_MAX_HS_BUFFER (CURL_TLS_MAX_HS_MESSAGE + 4u)
#define CURL_TLS_MAX_PLAINTEXT (CURL_TLS_RECORD_MAX + 32u)

typedef struct {
    uint32_t connect_ip;
    uint8_t is_https;
    uint8_t use_proxy;
    uint16_t connect_port;
    uint16_t dst_port;
    char host[CURL_HOST_MAX];
    char host_header[CURL_HOST_HEADER_MAX];
    char path[CURL_PATH_MAX];
    char connect_host[CURL_HOST_MAX];
} curl_target_t;

static int str_eq(const char* a, const char* b) {
    uint32_t i = 0u;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int str_starts_with(const char* text, const char* prefix) {
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

static void str_copy(char* dst, uint32_t dst_cap, const char* src) {
    uint32_t i = 0u;

    if (!dst || dst_cap == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1u < dst_cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void mem_copy(void* dst, const void* src, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;

    for (uint32_t i = 0u; i < len; i++) {
        d[i] = s[i];
    }
}

static void mem_zero(void* dst, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0u; i < len; i++) {
        d[i] = 0u;
    }
}

static int mem_eq(const void* a, const void* b, uint32_t len) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    uint8_t diff = 0u;

    for (uint32_t i = 0u; i < len; i++) {
        diff |= (uint8_t)(pa[i] ^ pb[i]);
    }
    return diff == 0u;
}

static int parse_u16(const char* text, uint16_t* out) {
    uint64_t value = 0u;

    if (!text || !text[0] || !out || mya_strto_u64(text, &value) != 0 || value == 0u || value > 65535u) {
        return -1;
    }
    *out = (uint16_t)value;
    return 0;
}

static int parse_u32(const char* text, uint32_t* out) {
    uint64_t value = 0u;

    if (!text || !text[0] || !out || mya_strto_u64(text, &value) != 0 || value > 0xFFFFFFFFull) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static uint32_t read_u24_be(const uint8_t in[3]) {
    return ((uint32_t)in[0] << 16u) | ((uint32_t)in[1] << 8u) | (uint32_t)in[2];
}

static void write_u24_be(uint8_t out[3], uint32_t v) {
    out[0] = (uint8_t)((v >> 16u) & 0xFFu);
    out[1] = (uint8_t)((v >> 8u) & 0xFFu);
    out[2] = (uint8_t)(v & 0xFFu);
}

static int append_u8(uint8_t* out, uint32_t out_cap, uint32_t* pos, uint8_t v) {
    if (!out || !pos || *pos + 1u > out_cap) {
        return -1;
    }
    out[*pos] = v;
    (*pos)++;
    return 0;
}

static int append_u16_be(uint8_t* out, uint32_t out_cap, uint32_t* pos, uint16_t v) {
    if (!out || !pos || *pos + 2u > out_cap) {
        return -1;
    }
    mya_net_write_be16(out + *pos, v);
    *pos += 2u;
    return 0;
}

static int append_raw(uint8_t* out, uint32_t out_cap, uint32_t* pos, const uint8_t* data, uint32_t len) {
    if (!out || !pos || (len != 0u && !data) || *pos + len > out_cap) {
        return -1;
    }
    if (len > 0u) {
        mem_copy(out + *pos, data, len);
        *pos += len;
    }
    return 0;
}

static int parse_proxy_spec(const char* spec, char* out_host, uint32_t out_host_cap, uint16_t* out_port) {
    const char* p = spec;
    char authority[CURL_PROXY_SPEC_MAX];
    uint32_t authority_len = 0u;
    int32_t colon_pos = -1;

    if (!spec || !spec[0] || !out_host || out_host_cap == 0u || !out_port) {
        return -1;
    }

    if (str_starts_with(p, "http://")) {
        p += 7u;
    } else if (str_starts_with(p, "https://")) {
        return -1;
    }

    while (p[authority_len] && p[authority_len] != '/') {
        authority_len++;
    }
    if (authority_len == 0u || authority_len + 1u > sizeof(authority)) {
        return -1;
    }

    for (uint32_t i = 0u; i < authority_len; i++) {
        authority[i] = p[i];
        if (p[i] == ':') {
            colon_pos = (int32_t)i;
        }
    }
    authority[authority_len] = '\0';

    *out_port = 80u;
    if (colon_pos >= 0) {
        authority[colon_pos] = '\0';
        if (!authority[0] || parse_u16(authority + (uint32_t)colon_pos + 1u, out_port) != 0) {
            return -1;
        }
    }
    if (!authority[0]) {
        return -1;
    }
    {
        uint32_t host_len = 0u;
        while (authority[host_len]) {
            host_len++;
        }
        if (host_len + 1u > out_host_cap) {
            return -1;
        }
    }

    str_copy(out_host, out_host_cap, authority);
    return 0;
}

static int resolve_host_ipv4(const char* host, uint32_t timeout_polls, uint32_t* out_ip) {
    if (!host || !out_ip) {
        return -1;
    }
    if (mya_net_parse_ipv4(host, out_ip) == 0) {
        return 0;
    }
    return mya_dns_resolve_ipv4(host, timeout_polls, out_ip);
}

static void weak_random_fill(uint8_t* out, uint32_t len, uint32_t salt) {
    uint64_t x = mya_time_ticks();
    x ^= ((uint64_t)(uint32_t)mya_proc_getpid() << 32u);
    x ^= ((uint64_t)salt << 16u) | (uint64_t)len;
    if (x == 0u) {
        x = 0x9E3779B97F4A7C15ull;
    }

    for (uint32_t i = 0u; i < len; i++) {
        x ^= x << 13u;
        x ^= x >> 7u;
        x ^= x << 17u;
        out[i] = (uint8_t)((x >> ((i & 7u) * 8u)) & 0xFFu);
    }
}

static int append_text(char* out, uint32_t out_cap, uint32_t* pos, const char* text) {
    uint32_t i = 0u;

    if (!out || !pos || !text || *pos >= out_cap) {
        return -1;
    }
    while (text[i]) {
        if (*pos + 1u >= out_cap) {
            return -1;
        }
        out[*pos] = text[i];
        (*pos)++;
        i++;
    }
    out[*pos] = '\0';
    return 0;
}

static char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int str_has_prefix_ci(const uint8_t* text, uint32_t text_len, const char* prefix) {
    uint32_t i = 0u;

    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (i >= text_len) {
            return 0;
        }
        if (ascii_lower((char)text[i]) != ascii_lower(prefix[i])) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void parse_http_content_length(
    const uint8_t* header,
    uint32_t header_len,
    uint8_t* out_has_content_length,
    uint32_t* out_content_length
) {
    uint32_t i = 0u;

    if (!out_has_content_length || !out_content_length) {
        return;
    }
    *out_has_content_length = 0u;
    *out_content_length = 0u;
    if (!header || header_len == 0u) {
        return;
    }

    while (i < header_len) {
        uint32_t line_start = i;
        uint32_t line_end = i;
        uint32_t j = 0u;
        uint64_t value = 0u;
        uint8_t saw_digit = 0u;
        const char* key = "content-length:";

        while (line_end < header_len && header[line_end] != '\n') {
            line_end++;
        }
        i = (line_end < header_len) ? (line_end + 1u) : line_end;
        if (line_end > line_start && header[line_end - 1u] == '\r') {
            line_end--;
        }
        if (line_end == line_start) {
            break;
        }

        if (!str_has_prefix_ci(header + line_start, line_end - line_start, key)) {
            continue;
        }

        j = (uint32_t)(line_start + 15u);
        while (j < line_end && (header[j] == ' ' || header[j] == '\t')) {
            j++;
        }

        while (j < line_end) {
            uint8_t c = header[j];
            if (c >= '0' && c <= '9') {
                saw_digit = 1u;
                value = value * 10u + (uint64_t)(c - '0');
                if (value > 0xFFFFFFFFull) {
                    return;
                }
                j++;
                continue;
            }
            if (c == ' ' || c == '\t') {
                j++;
                while (j < line_end && (header[j] == ' ' || header[j] == '\t')) {
                    j++;
                }
                if (j == line_end && saw_digit) {
                    *out_has_content_length = 1u;
                    *out_content_length = (uint32_t)value;
                }
                return;
            }
            return;
        }

        if (saw_digit) {
            *out_has_content_length = 1u;
            *out_content_length = (uint32_t)value;
        }
        return;
    }
}

static int console_write_all(const uint8_t* data, uint32_t len) {
    int64_t rc;

    if (!data && len != 0u) {
        return -1;
    }
    if (len == 0u) {
        return 0;
    }

    rc = mya_syscall(MYAOS_SYS_CONSOLE_WRITE, (uint64_t)(uintptr_t)data, (uint64_t)len, 0, 0, 0);
    return rc < 0 ? -1 : 0;
}

static int file_write_all(int32_t fd, const uint8_t* data, uint32_t len) {
    uint32_t pos = 0u;

    if (fd < 0 || (!data && len != 0u)) {
        return -1;
    }
    while (pos < len) {
        uint32_t wrote = 0u;
        if (mya_posix_write(fd, data + pos, len - pos, &wrote) != 0 || wrote == 0u) {
            return -1;
        }
        pos += wrote;
    }
    return 0;
}

static int socket_send_all(int32_t fd, const uint8_t* data, uint32_t len) {
    uint32_t sent = 0u;

    if (fd < 0 || (!data && len != 0u)) {
        return -1;
    }
    while (sent < len) {
        uint32_t wrote = 0u;
        if (mya_sock_send(fd, 0u, data + sent, len - sent, &wrote) != 0 || wrote == 0u) {
            return -1;
        }
        sent += wrote;
    }
    return 0;
}

static int parse_url(const char* url, curl_target_t* out) {
    const char* host_start;
    const char* path_start;
    uint32_t host_len = 0u;
    char authority[CURL_HOST_HEADER_MAX];
    int32_t colon_pos = -1;
    uint16_t default_port = 80u;

    if (!url || !out) {
        return -1;
    }

    out->is_https = str_starts_with(url, "https://") ? 1u : 0u;
    out->use_proxy = 0u;
    out->connect_ip = 0u;
    out->connect_port = 0u;
    out->connect_host[0] = '\0';

    if (out->is_https) {
        host_start = url + 8u;
        default_port = 443u;
    } else {
        host_start = str_starts_with(url, "http://") ? url + 7u : url;
        default_port = 80u;
    }
    path_start = host_start;
    while (*path_start && *path_start != '/') {
        path_start++;
    }
    host_len = (uint32_t)(path_start - host_start);

    if (host_len == 0u || host_len + 1u > sizeof(authority)) {
        return -1;
    }

    for (uint32_t i = 0u; i < host_len; i++) {
        authority[i] = host_start[i];
        if (host_start[i] == ':') {
            colon_pos = (int32_t)i;
        }
    }
    authority[host_len] = '\0';

    out->dst_port = default_port;
    if (colon_pos >= 0) {
        authority[colon_pos] = '\0';
        if (!authority[0] || parse_u16(authority + (uint32_t)colon_pos + 1u, &out->dst_port) != 0) {
            return -1;
        }
    }
    {
        uint32_t authority_host_len = 0u;
        while (authority[authority_host_len]) {
            authority_host_len++;
        }
        if (authority_host_len + 1u > sizeof(out->host)) {
            return -1;
        }
    }

    str_copy(out->host, sizeof(out->host), authority);

    if (*path_start) {
        str_copy(out->path, sizeof(out->path), path_start);
    } else {
        str_copy(out->path, sizeof(out->path), "/");
    }

    if (out->dst_port == default_port) {
        str_copy(out->host_header, sizeof(out->host_header), out->host);
    } else {
        char port_text[8];
        uint32_t pos = 0u;

        out->host_header[0] = '\0';
        mya_u32_to_dec((uint32_t)out->dst_port, port_text, sizeof(port_text));
        if (append_text(out->host_header, sizeof(out->host_header), &pos, out->host) != 0 ||
            append_text(out->host_header, sizeof(out->host_header), &pos, ":") != 0 ||
            append_text(out->host_header, sizeof(out->host_header), &pos, port_text) != 0) {
            return -1;
        }
    }

    return 0;
}

static int prepare_connection_target(curl_target_t* target, const char* proxy_opt, uint32_t timeout_polls) {
    char proxy_spec[CURL_PROXY_SPEC_MAX];
    uint16_t proxy_port = 0u;

    if (!target) {
        return -1;
    }

    proxy_spec[0] = '\0';
    if (proxy_opt && proxy_opt[0]) {
        str_copy(proxy_spec, sizeof(proxy_spec), proxy_opt);
    }

    if (proxy_spec[0]) {
        if (parse_proxy_spec(proxy_spec, target->connect_host, sizeof(target->connect_host), &proxy_port) != 0) {
            return -1;
        }
        target->use_proxy = 1u;
        target->connect_port = proxy_port;
    } else {
        target->use_proxy = 0u;
        target->connect_port = target->dst_port;
        str_copy(target->connect_host, sizeof(target->connect_host), target->host);
    }

    if (resolve_host_ipv4(target->connect_host, timeout_polls, &target->connect_ip) != 0) {
        return -3;
    }
    return 0;
}

static int build_request(const curl_target_t* target, char* out_req, uint32_t out_cap, uint32_t* out_len) {
    uint32_t pos = 0u;

    if (!target || !out_req || !out_len || out_cap == 0u) {
        return -1;
    }
    out_req[0] = '\0';

    if (append_text(out_req, out_cap, &pos, "GET ") != 0) {
        return -1;
    }
    if (target->use_proxy) {
        if (append_text(out_req, out_cap, &pos, target->is_https ? "https://" : "http://") != 0 ||
            append_text(out_req, out_cap, &pos, target->host_header) != 0 ||
            append_text(out_req, out_cap, &pos, target->path) != 0) {
            return -1;
        }
    } else {
        if (append_text(out_req, out_cap, &pos, target->path) != 0) {
            return -1;
        }
    }

    if (append_text(out_req, out_cap, &pos, " HTTP/1.1\r\nHost: ") != 0 ||
        append_text(out_req, out_cap, &pos, target->host_header) != 0 ||
        append_text(out_req, out_cap, &pos, "\r\nUser-Agent: MyaOS-curl/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n") != 0) {
        return -1;
    }

    *out_len = pos;
    return 0;
}

static int output_chunk(const uint8_t* data, uint32_t len, int32_t out_fd);

typedef struct {
    uint32_t h[8];
    uint64_t len_bits;
    uint8_t block[64];
    uint32_t block_len;
} tls_sha256_ctx_t;

typedef struct {
    uint8_t key[16];
    uint8_t iv[12];
    uint8_t round_key[176];
    uint64_t seq;
} tls13_aead_state_t;

typedef struct {
    uint8_t hdr[4];
    uint32_t hdr_used;
    uint8_t msg_type;
    uint32_t msg_len;
    uint32_t msg_read;
    uint8_t finished_data[32];
    uint32_t finished_used;
} tls13_hs_parser_t;

static uint8_t g_tls_rx_record[CURL_TLS_RECORD_MAX];
static uint8_t g_tls_tx_record[CURL_TLS_RECORD_MAX + 64u];
static uint8_t g_tls_plain[CURL_TLS_MAX_PLAINTEXT];
static uint8_t g_tls_hs_accum[CURL_TLS_MAX_HS_BUFFER];

static uint32_t tls_rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static uint32_t tls_sha_ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static uint32_t tls_sha_maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t tls_sha_bs0(uint32_t x) {
    return tls_rotr32(x, 2u) ^ tls_rotr32(x, 13u) ^ tls_rotr32(x, 22u);
}

static uint32_t tls_sha_bs1(uint32_t x) {
    return tls_rotr32(x, 6u) ^ tls_rotr32(x, 11u) ^ tls_rotr32(x, 25u);
}

static uint32_t tls_sha_ss0(uint32_t x) {
    return tls_rotr32(x, 7u) ^ tls_rotr32(x, 18u) ^ (x >> 3u);
}

static uint32_t tls_sha_ss1(uint32_t x) {
    return tls_rotr32(x, 17u) ^ tls_rotr32(x, 19u) ^ (x >> 10u);
}

static void tls_sha256_transform(tls_sha256_ctx_t* ctx, const uint8_t block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
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
        w[i] = tls_sha_ss1(w[i - 2u]) + w[i - 7u] + tls_sha_ss0(w[i - 15u]) + w[i - 16u];
    }

    for (uint32_t i = 0u; i < 64u; i++) {
        uint32_t t1 = h + tls_sha_bs1(e) + tls_sha_ch(e, f, g) + k[i] + w[i];
        uint32_t t2 = tls_sha_bs0(a) + tls_sha_maj(a, b, c);
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

static void tls_sha256_init(tls_sha256_ctx_t* ctx) {
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

static void tls_sha256_update(tls_sha256_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    if (!ctx || (!data && len != 0u)) {
        return;
    }
    for (uint32_t i = 0u; i < len; i++) {
        ctx->block[ctx->block_len++] = data[i];
        if (ctx->block_len == 64u) {
            tls_sha256_transform(ctx, ctx->block);
            ctx->len_bits += 512u;
            ctx->block_len = 0u;
        }
    }
}

static void tls_sha256_final(tls_sha256_ctx_t* ctx, uint8_t out[32]) {
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
        tls_sha256_transform(ctx, ctx->block);
        ctx->block_len = 0u;
    }

    while (ctx->block_len < 56u) {
        ctx->block[ctx->block_len++] = 0u;
    }

    for (uint32_t i = 0u; i < 8u; i++) {
        ctx->block[56u + i] = (uint8_t)((total_bits >> ((7u - i) * 8u)) & 0xFFu);
    }
    tls_sha256_transform(ctx, ctx->block);

    for (uint32_t i = 0u; i < 8u; i++) {
        out[i * 4u + 0u] = (uint8_t)((ctx->h[i] >> 24u) & 0xFFu);
        out[i * 4u + 1u] = (uint8_t)((ctx->h[i] >> 16u) & 0xFFu);
        out[i * 4u + 2u] = (uint8_t)((ctx->h[i] >> 8u) & 0xFFu);
        out[i * 4u + 3u] = (uint8_t)(ctx->h[i] & 0xFFu);
    }
}

static void tls_sha256_digest(const uint8_t* data, uint32_t len, uint8_t out[32]) {
    tls_sha256_ctx_t ctx;
    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, data, len);
    tls_sha256_final(&ctx, out);
}

static void tls_transcript_hash(const tls_sha256_ctx_t* transcript, uint8_t out[32]) {
    tls_sha256_ctx_t copy;
    copy = *transcript;
    tls_sha256_final(&copy, out);
}

static void tls_hmac_sha256(const uint8_t* key, uint32_t key_len, const uint8_t* data, uint32_t data_len, uint8_t out[32]) {
    uint8_t k0[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t ihash[32];
    tls_sha256_ctx_t ctx;

    if (!key || !out || (!data && data_len != 0u)) {
        return;
    }

    mem_zero(k0, sizeof(k0));
    if (key_len > 64u) {
        tls_sha256_digest(key, key_len, k0);
    } else if (key_len > 0u) {
        mem_copy(k0, key, key_len);
    }

    for (uint32_t i = 0u; i < 64u; i++) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36u);
        opad[i] = (uint8_t)(k0[i] ^ 0x5cu);
    }

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, ipad, 64u);
    tls_sha256_update(&ctx, data, data_len);
    tls_sha256_final(&ctx, ihash);

    tls_sha256_init(&ctx);
    tls_sha256_update(&ctx, opad, 64u);
    tls_sha256_update(&ctx, ihash, 32u);
    tls_sha256_final(&ctx, out);

    mem_zero(k0, sizeof(k0));
    mem_zero(ipad, sizeof(ipad));
    mem_zero(opad, sizeof(opad));
    mem_zero(ihash, sizeof(ihash));
}

static void tls_hkdf_extract(
    const uint8_t* salt,
    uint32_t salt_len,
    const uint8_t* ikm,
    uint32_t ikm_len,
    uint8_t out_prk[32]
) {
    uint8_t zero_salt[32];

    if (!out_prk) {
        return;
    }

    if (!salt || salt_len == 0u) {
        mem_zero(zero_salt, sizeof(zero_salt));
        tls_hmac_sha256(zero_salt, sizeof(zero_salt), ikm, ikm_len, out_prk);
        return;
    }
    tls_hmac_sha256(salt, salt_len, ikm, ikm_len, out_prk);
}

static int tls_hkdf_expand(
    const uint8_t prk[32],
    const uint8_t* info,
    uint32_t info_len,
    uint8_t* out,
    uint32_t out_len
) {
    uint8_t t[32];
    uint8_t input[96];
    uint32_t produced = 0u;
    uint32_t t_len = 0u;
    uint8_t counter = 1u;

    if (!prk || !out) {
        return -1;
    }
    if (out_len == 0u) {
        return 0;
    }

    while (produced < out_len) {
        uint32_t pos = 0u;
        uint32_t take;

        if (t_len > 0u) {
            if (pos + t_len > sizeof(input)) {
                return -1;
            }
            mem_copy(input + pos, t, t_len);
            pos += t_len;
        }
        if (info_len > 0u) {
            if (!info || pos + info_len > sizeof(input)) {
                return -1;
            }
            mem_copy(input + pos, info, info_len);
            pos += info_len;
        }
        if (pos + 1u > sizeof(input)) {
            return -1;
        }
        input[pos++] = counter++;

        tls_hmac_sha256(prk, 32u, input, pos, t);
        t_len = 32u;
        take = out_len - produced;
        if (take > 32u) {
            take = 32u;
        }
        mem_copy(out + produced, t, take);
        produced += take;
    }

    mem_zero(t, sizeof(t));
    mem_zero(input, sizeof(input));
    return 0;
}

static int tls_hkdf_expand_label(
    const uint8_t secret[32],
    const char* label,
    const uint8_t* context,
    uint32_t context_len,
    uint8_t* out,
    uint16_t out_len
) {
    uint8_t info[128];
    uint32_t pos = 0u;
    uint32_t label_len = 0u;
    static const char prefix[] = "tls13 ";

    if (!secret || !label || !out) {
        return -1;
    }
    while (label[label_len]) {
        label_len++;
    }
    if (label_len > 64u || context_len > 64u) {
        return -1;
    }

    if (append_u16_be(info, sizeof(info), &pos, out_len) != 0) {
        return -1;
    }
    if (append_u8(info, sizeof(info), &pos, (uint8_t)(sizeof(prefix) - 1u + label_len)) != 0) {
        return -1;
    }
    if (append_raw(info, sizeof(info), &pos, (const uint8_t*)prefix, sizeof(prefix) - 1u) != 0 ||
        append_raw(info, sizeof(info), &pos, (const uint8_t*)label, label_len) != 0) {
        return -1;
    }
    if (append_u8(info, sizeof(info), &pos, (uint8_t)context_len) != 0) {
        return -1;
    }
    if (context_len > 0u && append_raw(info, sizeof(info), &pos, context, context_len) != 0) {
        return -1;
    }

    return tls_hkdf_expand(secret, info, pos, out, out_len);
}

static const uint8_t tls_aes_sbox[256] = {
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

static const uint8_t tls_aes_rcon[11] = { 0x8d,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36 };

static uint8_t tls_aes_xtime(uint8_t x) {
    return (uint8_t)((x << 1u) ^ (((x >> 7u) & 1u) * 0x1bu));
}

static void tls_aes_key_expand_128(uint8_t round_key[176], const uint8_t key[16]) {
    uint8_t t[4];

    for (uint32_t i = 0u; i < 16u; i++) {
        round_key[i] = key[i];
    }
    for (uint32_t i = 4u; i < 44u; i++) {
        uint8_t* dst = &round_key[i * 4u];
        const uint8_t* src_prev = &round_key[(i - 1u) * 4u];
        const uint8_t* src_4 = &round_key[(i - 4u) * 4u];

        t[0] = src_prev[0];
        t[1] = src_prev[1];
        t[2] = src_prev[2];
        t[3] = src_prev[3];
        if ((i % 4u) == 0u) {
            uint8_t tmp = t[0];
            t[0] = tls_aes_sbox[t[1]];
            t[1] = tls_aes_sbox[t[2]];
            t[2] = tls_aes_sbox[t[3]];
            t[3] = tls_aes_sbox[tmp];
            t[0] ^= tls_aes_rcon[i / 4u];
        }

        dst[0] = (uint8_t)(src_4[0] ^ t[0]);
        dst[1] = (uint8_t)(src_4[1] ^ t[1]);
        dst[2] = (uint8_t)(src_4[2] ^ t[2]);
        dst[3] = (uint8_t)(src_4[3] ^ t[3]);
    }
}

static void tls_aes_add_round_key(uint8_t state[16], const uint8_t* rk) {
    for (uint32_t i = 0u; i < 16u; i++) {
        state[i] ^= rk[i];
    }
}

static void tls_aes_sub_bytes(uint8_t state[16]) {
    for (uint32_t i = 0u; i < 16u; i++) {
        state[i] = tls_aes_sbox[state[i]];
    }
}

static void tls_aes_shift_rows(uint8_t s[16]) {
    uint8_t t;

    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[3]; s[3] = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = t;
}

static void tls_aes_mix_columns(uint8_t s[16]) {
    for (uint32_t i = 0u; i < 4u; i++) {
        uint8_t* c = &s[i * 4u];
        uint8_t a0 = c[0];
        uint8_t a1 = c[1];
        uint8_t a2 = c[2];
        uint8_t a3 = c[3];
        uint8_t t = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
        uint8_t u = a0;

        c[0] ^= t ^ tls_aes_xtime((uint8_t)(a0 ^ a1));
        c[1] ^= t ^ tls_aes_xtime((uint8_t)(a1 ^ a2));
        c[2] ^= t ^ tls_aes_xtime((uint8_t)(a2 ^ a3));
        c[3] ^= t ^ tls_aes_xtime((uint8_t)(a3 ^ u));
    }
}

static void tls_aes_encrypt_block_128(const uint8_t round_key[176], const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[16];
    mem_copy(state, in, 16u);

    tls_aes_add_round_key(state, &round_key[0]);
    for (uint32_t round = 1u; round < 10u; round++) {
        tls_aes_sub_bytes(state);
        tls_aes_shift_rows(state);
        tls_aes_mix_columns(state);
        tls_aes_add_round_key(state, &round_key[round * 16u]);
    }
    tls_aes_sub_bytes(state);
    tls_aes_shift_rows(state);
    tls_aes_add_round_key(state, &round_key[160]);

    mem_copy(out, state, 16u);
    mem_zero(state, sizeof(state));
}

static void tls_inc32(uint8_t ctr[16]) {
    for (int i = 15; i >= 12; i--) {
        ctr[i] = (uint8_t)(ctr[i] + 1u);
        if (ctr[i] != 0u) {
            break;
        }
    }
}

static void tls_xor_block(uint8_t out[16], const uint8_t a[16], const uint8_t b[16]) {
    for (uint32_t i = 0u; i < 16u; i++) {
        out[i] = (uint8_t)(a[i] ^ b[i]);
    }
}

static void tls_shift_right_one(uint8_t x[16]) {
    uint8_t carry = 0u;
    for (uint32_t i = 0u; i < 16u; i++) {
        uint8_t next = (uint8_t)(x[i] & 1u);
        x[i] = (uint8_t)((x[i] >> 1u) | (carry << 7u));
        carry = next;
    }
}

static void tls_gf_mul(const uint8_t x[16], const uint8_t y[16], uint8_t out[16]) {
    uint8_t z[16];
    uint8_t v[16];

    mem_zero(z, sizeof(z));
    mem_copy(v, y, sizeof(v));

    for (uint32_t i = 0u; i < 128u; i++) {
        uint8_t bit = (uint8_t)((x[i / 8u] >> (7u - (i & 7u))) & 1u);
        if (bit) {
            for (uint32_t j = 0u; j < 16u; j++) {
                z[j] ^= v[j];
            }
        }

        {
            uint8_t lsb = (uint8_t)(v[15] & 1u);
            tls_shift_right_one(v);
            if (lsb) {
                v[0] ^= 0xe1u;
            }
        }
    }

    mem_copy(out, z, sizeof(z));
    mem_zero(z, sizeof(z));
    mem_zero(v, sizeof(v));
}

static void tls_ghash_update(uint8_t y[16], const uint8_t h[16], const uint8_t* data, uint32_t data_len) {
    uint32_t off = 0u;
    while (off < data_len) {
        uint8_t block[16];
        uint8_t t[16];
        uint32_t chunk = data_len - off;

        if (chunk > 16u) {
            chunk = 16u;
        }
        mem_zero(block, sizeof(block));
        mem_copy(block, data + off, chunk);

        tls_xor_block(t, y, block);
        tls_gf_mul(t, h, y);
        off += chunk;
    }
}

static int tls_aes_gcm_encrypt(
    const uint8_t round_key[176],
    const uint8_t nonce[12],
    const uint8_t* aad,
    uint32_t aad_len,
    const uint8_t* in,
    uint32_t in_len,
    uint8_t* out,
    uint8_t tag[16]
) {
    uint8_t h[16];
    uint8_t j0[16];
    uint8_t ctr[16];
    uint8_t s[16];
    uint8_t e_j0[16];
    uint32_t off = 0u;

    if (!round_key || !nonce || (!aad && aad_len != 0u) || (!in && in_len != 0u) || (!out && in_len != 0u) || !tag) {
        return -1;
    }

    mem_zero(h, sizeof(h));
    tls_aes_encrypt_block_128(round_key, h, h);

    mem_zero(j0, sizeof(j0));
    mem_copy(j0, nonce, 12u);
    j0[15] = 1u;

    mem_copy(ctr, j0, sizeof(ctr));
    tls_inc32(ctr);
    while (off < in_len) {
        uint8_t ks[16];
        uint32_t chunk = in_len - off;
        if (chunk > 16u) {
            chunk = 16u;
        }
        tls_aes_encrypt_block_128(round_key, ctr, ks);
        for (uint32_t i = 0u; i < chunk; i++) {
            out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
        }
        tls_inc32(ctr);
        off += chunk;
    }

    mem_zero(s, sizeof(s));
    if (aad_len > 0u) {
        tls_ghash_update(s, h, aad, aad_len);
    }
    if (in_len > 0u) {
        tls_ghash_update(s, h, out, in_len);
    }
    {
        uint8_t lens[16];
        mem_zero(lens, sizeof(lens));
        {
            uint64_t aad_bits = (uint64_t)aad_len * 8u;
            uint64_t in_bits = (uint64_t)in_len * 8u;
            for (uint32_t i = 0u; i < 8u; i++) {
                lens[i] = (uint8_t)((aad_bits >> ((7u - i) * 8u)) & 0xFFu);
                lens[8u + i] = (uint8_t)((in_bits >> ((7u - i) * 8u)) & 0xFFu);
            }
        }
        tls_ghash_update(s, h, lens, sizeof(lens));
    }

    tls_aes_encrypt_block_128(round_key, j0, e_j0);
    tls_xor_block(tag, e_j0, s);

    mem_zero(h, sizeof(h));
    mem_zero(j0, sizeof(j0));
    mem_zero(ctr, sizeof(ctr));
    mem_zero(s, sizeof(s));
    mem_zero(e_j0, sizeof(e_j0));
    return 0;
}

static int tls_aes_gcm_decrypt(
    const uint8_t round_key[176],
    const uint8_t nonce[12],
    const uint8_t* aad,
    uint32_t aad_len,
    const uint8_t* in,
    uint32_t in_len,
    const uint8_t tag[16],
    uint8_t* out
) {
    uint8_t calc_tag[16];
    uint8_t h[16];
    uint8_t j0[16];
    uint8_t ctr[16];
    uint8_t s[16];
    uint8_t e_j0[16];
    uint32_t off = 0u;

    if (!round_key || !nonce || (!aad && aad_len != 0u) || (!in && in_len != 0u) || (!out && in_len != 0u) || !tag) {
        return -1;
    }

    mem_zero(h, sizeof(h));
    tls_aes_encrypt_block_128(round_key, h, h);

    mem_zero(j0, sizeof(j0));
    mem_copy(j0, nonce, 12u);
    j0[15] = 1u;

    mem_copy(ctr, j0, sizeof(ctr));
    tls_inc32(ctr);
    while (off < in_len) {
        uint8_t ks[16];
        uint32_t chunk = in_len - off;
        if (chunk > 16u) {
            chunk = 16u;
        }
        tls_aes_encrypt_block_128(round_key, ctr, ks);
        for (uint32_t i = 0u; i < chunk; i++) {
            out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
        }
        tls_inc32(ctr);
        off += chunk;
    }

    mem_zero(s, sizeof(s));
    if (aad_len > 0u) {
        tls_ghash_update(s, h, aad, aad_len);
    }
    if (in_len > 0u) {
        /* GCM tag is always over ciphertext (not plaintext). */
        tls_ghash_update(s, h, in, in_len);
    }
    {
        uint8_t lens[16];
        mem_zero(lens, sizeof(lens));
        {
            uint64_t aad_bits = (uint64_t)aad_len * 8u;
            uint64_t in_bits = (uint64_t)in_len * 8u;
            for (uint32_t i = 0u; i < 8u; i++) {
                lens[i] = (uint8_t)((aad_bits >> ((7u - i) * 8u)) & 0xFFu);
                lens[8u + i] = (uint8_t)((in_bits >> ((7u - i) * 8u)) & 0xFFu);
            }
        }
        tls_ghash_update(s, h, lens, sizeof(lens));
    }

    tls_aes_encrypt_block_128(round_key, j0, e_j0);
    tls_xor_block(calc_tag, e_j0, s);

    if (!mem_eq(calc_tag, tag, 16u)) {
        mem_zero(out, in_len);
        mem_zero(h, sizeof(h));
        mem_zero(j0, sizeof(j0));
        mem_zero(ctr, sizeof(ctr));
        mem_zero(s, sizeof(s));
        mem_zero(e_j0, sizeof(e_j0));
        mem_zero(calc_tag, sizeof(calc_tag));
        return -1;
    }

    mem_zero(h, sizeof(h));
    mem_zero(j0, sizeof(j0));
    mem_zero(ctr, sizeof(ctr));
    mem_zero(s, sizeof(s));
    mem_zero(e_j0, sizeof(e_j0));
    mem_zero(calc_tag, sizeof(calc_tag));
    return 0;
}

typedef int64_t tls_i64_t;
typedef tls_i64_t tls_gf_t[16];

static const uint8_t tls_curve9[32] = { 9u };
static const tls_gf_t tls_curve121665 = { 0xDB41, 1 };

static void tls_set25519(tls_gf_t r, const tls_gf_t a) {
    for (uint32_t i = 0u; i < 16u; i++) {
        r[i] = a[i];
    }
}

static void tls_car25519(tls_gf_t o) {
    tls_i64_t c;
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] += (1ll << 16);
        c = o[i] >> 16;
        o[(i + 1u) * (i < 15u)] += c - 1ll + 37ll * (c - 1ll) * (i == 15u);
        o[i] -= c << 16;
    }
}

static void tls_sel25519(tls_gf_t p, tls_gf_t q, int b) {
    tls_i64_t c = ~(tls_i64_t)(b - 1);
    for (uint32_t i = 0u; i < 16u; i++) {
        tls_i64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void tls_pack25519(uint8_t* o, const tls_gf_t n) {
    tls_gf_t m;
    tls_gf_t t;

    tls_set25519(t, n);
    tls_car25519(t);
    tls_car25519(t);
    tls_car25519(t);

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
            tls_sel25519(t, m, 1 - b);
        }
    }

    for (uint32_t i = 0u; i < 16u; i++) {
        o[2u * i] = (uint8_t)(t[i] & 0xFF);
        o[2u * i + 1u] = (uint8_t)((t[i] >> 8) & 0xFF);
    }
}

static void tls_unpack25519(tls_gf_t o, const uint8_t* n) {
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = (tls_i64_t)n[2u * i] + ((tls_i64_t)n[2u * i + 1u] << 8);
    }
    o[15] &= 0x7fffu;
}

static void tls_A25519(tls_gf_t o, const tls_gf_t a, const tls_gf_t b) {
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = a[i] + b[i];
    }
}

static void tls_Z25519(tls_gf_t o, const tls_gf_t a, const tls_gf_t b) {
    for (uint32_t i = 0u; i < 16u; i++) {
        o[i] = a[i] - b[i];
    }
}

static void tls_M25519(tls_gf_t o, const tls_gf_t a, const tls_gf_t b) {
    tls_i64_t t[31];

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
    tls_car25519(o);
    tls_car25519(o);
}

static void tls_S25519(tls_gf_t o, const tls_gf_t a) {
    tls_M25519(o, a, a);
}

static void tls_inv25519(tls_gf_t o, const tls_gf_t i) {
    tls_gf_t c;
    tls_set25519(c, i);
    for (int a = 253; a >= 0; a--) {
        tls_S25519(c, c);
        if (a != 2 && a != 4) {
            tls_M25519(c, c, i);
        }
    }
    tls_set25519(o, c);
}

static int tls_x25519_scalarmult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t z[32];
    tls_gf_t x;
    tls_gf_t a;
    tls_gf_t b;
    tls_gf_t c;
    tls_gf_t d;
    tls_gf_t e;
    tls_gf_t f;

    if (!out || !scalar || !point) {
        return -1;
    }

    for (uint32_t i = 0u; i < 32u; i++) {
        z[i] = scalar[i];
    }
    z[0] &= 248u;
    z[31] = (uint8_t)((z[31] & 127u) | 64u);

    tls_unpack25519(x, point);
    mem_zero(a, sizeof(a));
    mem_zero(b, sizeof(b));
    mem_zero(c, sizeof(c));
    mem_zero(d, sizeof(d));
    mem_zero(e, sizeof(e));
    mem_zero(f, sizeof(f));

    a[0] = 1;
    tls_set25519(b, x);
    d[0] = 1;

    for (int i = 254; i >= 0; i--) {
        uint8_t r = (uint8_t)((z[i >> 3] >> (i & 7)) & 1u);

        tls_sel25519(a, b, (int)r);
        tls_sel25519(c, d, (int)r);

        tls_A25519(e, a, c);
        tls_Z25519(a, a, c);
        tls_A25519(c, b, d);
        tls_Z25519(b, b, d);
        tls_S25519(d, e);
        tls_S25519(f, a);
        tls_M25519(a, c, a);
        tls_M25519(c, b, e);
        tls_A25519(e, a, c);
        tls_Z25519(a, a, c);
        tls_S25519(b, a);
        tls_Z25519(c, d, f);
        tls_M25519(a, c, tls_curve121665);
        tls_A25519(a, a, d);
        tls_M25519(c, c, a);
        tls_M25519(a, d, f);
        tls_M25519(d, b, x);
        tls_S25519(b, e);

        tls_sel25519(a, b, (int)r);
        tls_sel25519(c, d, (int)r);
    }

    tls_inv25519(c, c);
    tls_M25519(a, a, c);
    tls_pack25519(out, a);
    return 0;
}

static int tls_x25519_basepoint(uint8_t out[32], const uint8_t scalar[32]) {
    return tls_x25519_scalarmult(out, scalar, tls_curve9);
}

static int tls_socket_recv_exact(int32_t sock_fd, uint8_t* out, uint32_t need, uint32_t timeout_polls) {
    uint32_t done = 0u;
    uint32_t idle = 0u;

    if (!out || (need == 0u)) {
        return need == 0u ? 0 : -1;
    }

    while (done < need) {
        uint32_t got = 0u;
        uint16_t src_port = 0u;

        if (mya_sock_recv(sock_fd, out + done, need - done, &got, &src_port) == 0 && got > 0u) {
            done += got;
            idle = 0u;
            continue;
        }

        {
            uint32_t probe_written = 0u;
            if (mya_sock_send(sock_fd, 0u, NULL, 0u, &probe_written) != 0) {
                return -2;
            }
        }

        if (idle++ >= timeout_polls) {
            return -1;
        }
        mya_proc_yield();
    }

    return 0;
}

static int tls_read_record(
    int32_t sock_fd,
    uint32_t timeout_polls,
    uint8_t* out_type,
    uint8_t out_hdr[5],
    uint8_t* out_payload,
    uint32_t payload_cap,
    uint32_t* out_payload_len
) {
    uint8_t hdr[5];
    uint32_t payload_len;

    if (!out_type || !out_payload || !out_payload_len) {
        return -1;
    }

    {
        int rc = tls_socket_recv_exact(sock_fd, hdr, sizeof(hdr), timeout_polls);
        if (rc != 0) {
            return rc;
        }
    }

    payload_len = (uint32_t)mya_net_read_be16(hdr + 3u);
    if (payload_len > payload_cap) {
        return -1;
    }
    {
        int rc = tls_socket_recv_exact(sock_fd, out_payload, payload_len, timeout_polls);
        if (rc != 0) {
            return rc;
        }
    }

    *out_type = hdr[0];
    if (out_hdr) {
        mem_copy(out_hdr, hdr, sizeof(hdr));
    }
    *out_payload_len = payload_len;
    return 0;
}

static int tls_write_record_plain(int32_t sock_fd, uint8_t content_type, const uint8_t* payload, uint16_t payload_len) {
    uint8_t hdr[5];

    if ((!payload && payload_len != 0u)) {
        return -1;
    }
    hdr[0] = content_type;
    mya_net_write_be16(hdr + 1u, CURL_TLS_VER_1_2);
    mya_net_write_be16(hdr + 3u, payload_len);

    if (socket_send_all(sock_fd, hdr, sizeof(hdr)) != 0) {
        return -1;
    }
    if (payload_len > 0u && socket_send_all(sock_fd, payload, payload_len) != 0) {
        return -1;
    }
    return 0;
}

static void tls13_make_nonce(const uint8_t iv[12], uint64_t seq, uint8_t out_nonce[12]) {
    mem_copy(out_nonce, iv, 12u);
    for (uint32_t i = 0u; i < 8u; i++) {
        out_nonce[4u + i] ^= (uint8_t)((seq >> ((7u - i) * 8u)) & 0xFFu);
    }
}

static void tls13_aead_state_init(tls13_aead_state_t* st, const uint8_t key[16], const uint8_t iv[12]) {
    if (!st || !key || !iv) {
        return;
    }
    mem_copy(st->key, key, 16u);
    mem_copy(st->iv, iv, 12u);
    tls_aes_key_expand_128(st->round_key, st->key);
    st->seq = 0u;
}

static int tls13_write_encrypted_record(
    int32_t sock_fd,
    tls13_aead_state_t* st,
    uint8_t inner_content_type,
    const uint8_t* plain,
    uint16_t plain_len
) {
    uint8_t hdr[5];
    uint8_t nonce[12];
    uint8_t tag[16];
    uint32_t inner_len = (uint32_t)plain_len + 1u;

    if (!st || (!plain && plain_len != 0u) || inner_len + 16u > sizeof(g_tls_tx_record)) {
        return -1;
    }

    if (plain_len > 0u) {
        mem_copy(g_tls_plain, plain, plain_len);
    }
    g_tls_plain[plain_len] = inner_content_type;

    hdr[0] = CURL_TLS_CONTENT_APPLICATION_DATA;
    mya_net_write_be16(hdr + 1u, CURL_TLS_VER_1_2);
    mya_net_write_be16(hdr + 3u, (uint16_t)(inner_len + 16u));

    tls13_make_nonce(st->iv, st->seq, nonce);
    if (tls_aes_gcm_encrypt(
            st->round_key,
            nonce,
            hdr,
            sizeof(hdr),
            g_tls_plain,
            inner_len,
            g_tls_tx_record,
            tag
        ) != 0) {
        return -1;
    }
    mem_copy(g_tls_tx_record + inner_len, tag, 16u);

    if (socket_send_all(sock_fd, hdr, sizeof(hdr)) != 0 ||
        socket_send_all(sock_fd, g_tls_tx_record, inner_len + 16u) != 0) {
        return -1;
    }

    st->seq++;
    return 0;
}

static int tls13_decrypt_record(
    tls13_aead_state_t* st,
    const uint8_t rec_hdr[5],
    const uint8_t* rec_payload,
    uint32_t rec_payload_len,
    uint8_t* out_inner_type,
    uint8_t* out_plain,
    uint32_t out_plain_cap,
    uint32_t* out_plain_len
) {
    uint8_t nonce[12];
    uint32_t cipher_len;
    const uint8_t* tag;

    if (!st || !rec_hdr || !rec_payload || !out_inner_type || !out_plain || !out_plain_len) {
        return -1;
    }
    if (rec_payload_len < 16u) {
        return -1;
    }
    cipher_len = rec_payload_len - 16u;
    if (cipher_len > out_plain_cap) {
        return -1;
    }
    tag = rec_payload + cipher_len;

    tls13_make_nonce(st->iv, st->seq, nonce);
    if (tls_aes_gcm_decrypt(st->round_key, nonce, rec_hdr, 5u, rec_payload, cipher_len, tag, out_plain) != 0) {
        return -1;
    }
    st->seq++;

    {
        uint32_t i = cipher_len;
        while (i > 0u && out_plain[i - 1u] == 0u) {
            i--;
        }
        if (i == 0u) {
            return -1;
        }
        *out_inner_type = out_plain[i - 1u];
        *out_plain_len = i - 1u;
    }
    return 0;
}

static int tls13_build_client_hello(
    const char* sni_host,
    const uint8_t client_random[32],
    const uint8_t session_id[32],
    const uint8_t key_share[32],
    uint8_t* out,
    uint32_t out_cap,
    uint32_t* out_len
) {
    uint8_t body[CURL_TLS_MAX_CLIENT_HELLO];
    uint8_t exts[CURL_TLS_MAX_CLIENT_HELLO];
    uint32_t pos = 0u;
    uint32_t epos = 0u;
    uint32_t host_len = 0u;
    uint32_t suites_start;
    uint32_t exts_start;
    const uint8_t session_id_len = 32u;

    if (!sni_host || !client_random || !session_id || !key_share || !out || !out_len) {
        return -1;
    }
    while (sni_host[host_len]) {
        host_len++;
    }
    if (host_len == 0u || host_len > 255u) {
        return -1;
    }

    if (append_u16_be(body, sizeof(body), &pos, CURL_TLS_VER_1_2) != 0 ||
        append_raw(body, sizeof(body), &pos, client_random, 32u) != 0 ||
        append_u8(body, sizeof(body), &pos, session_id_len) != 0 ||
        (session_id_len > 0u && append_raw(body, sizeof(body), &pos, session_id, session_id_len) != 0)) {
        return -1;
    }

    suites_start = pos;
    if (append_u16_be(body, sizeof(body), &pos, 0u) != 0 ||
        append_u16_be(body, sizeof(body), &pos, CURL_TLS_AES_128_GCM_SHA256) != 0) {
        return -1;
    }
    mya_net_write_be16(body + suites_start, (uint16_t)(pos - suites_start - 2u));

    if (append_u8(body, sizeof(body), &pos, 1u) != 0 ||
        append_u8(body, sizeof(body), &pos, 0u) != 0) {
        return -1;
    }

    {
        uint32_t ext_start = epos;
        if (append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_EXT_SERVER_NAME) != 0 ||
            append_u16_be(exts, sizeof(exts), &epos, 0u) != 0) {
            return -1;
        }
        {
            uint32_t list_start = epos;
            if (append_u16_be(exts, sizeof(exts), &epos, 0u) != 0 ||
                append_u8(exts, sizeof(exts), &epos, 0u) != 0 ||
                append_u16_be(exts, sizeof(exts), &epos, (uint16_t)host_len) != 0 ||
                append_raw(exts, sizeof(exts), &epos, (const uint8_t*)sni_host, host_len) != 0) {
                return -1;
            }
            mya_net_write_be16(exts + list_start, (uint16_t)(epos - list_start - 2u));
        }
        mya_net_write_be16(exts + ext_start + 2u, (uint16_t)(epos - ext_start - 4u));
    }

    {
        uint32_t ext_start = epos;
        if (append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_EXT_SUPPORTED_GROUPS) != 0 ||
            append_u16_be(exts, sizeof(exts), &epos, 4u) != 0 ||
            append_u16_be(exts, sizeof(exts), &epos, 2u) != 0 ||
            append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_GROUP_X25519) != 0) {
            return -1;
        }
        (void)ext_start;
    }

    {
        static const uint16_t sigs[] = { 0x0403u, 0x0804u, 0x0401u, 0x0809u };
        uint32_t ext_start = epos;
        if (append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_EXT_SIGNATURE_ALGS) != 0 ||
            append_u16_be(exts, sizeof(exts), &epos, 0u) != 0) {
            return -1;
        }
        {
            uint32_t list_start = epos;
            if (append_u16_be(exts, sizeof(exts), &epos, 0u) != 0) {
                return -1;
            }
            for (uint32_t i = 0u; i < (uint32_t)(sizeof(sigs) / sizeof(sigs[0])); i++) {
                if (append_u16_be(exts, sizeof(exts), &epos, sigs[i]) != 0) {
                    return -1;
                }
            }
            mya_net_write_be16(exts + list_start, (uint16_t)(epos - list_start - 2u));
        }
        mya_net_write_be16(exts + ext_start + 2u, (uint16_t)(epos - ext_start - 4u));
    }

    if (append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_EXT_SUPPORTED_VERSIONS) != 0 ||
        append_u16_be(exts, sizeof(exts), &epos, 3u) != 0 ||
        append_u8(exts, sizeof(exts), &epos, 2u) != 0 ||
        append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_VER_1_3) != 0) {
        return -1;
    }

    if (append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_EXT_PSK_KEX_MODES) != 0 ||
        append_u16_be(exts, sizeof(exts), &epos, 2u) != 0 ||
        append_u8(exts, sizeof(exts), &epos, 1u) != 0 ||
        append_u8(exts, sizeof(exts), &epos, 1u) != 0) {
        return -1;
    }

    {
        uint32_t ext_start = epos;
        if (append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_EXT_KEY_SHARE) != 0 ||
            append_u16_be(exts, sizeof(exts), &epos, 0u) != 0) {
            return -1;
        }
        {
            uint32_t list_start = epos;
            if (append_u16_be(exts, sizeof(exts), &epos, 0u) != 0 ||
                append_u16_be(exts, sizeof(exts), &epos, CURL_TLS_GROUP_X25519) != 0 ||
                append_u16_be(exts, sizeof(exts), &epos, 32u) != 0 ||
                append_raw(exts, sizeof(exts), &epos, key_share, 32u) != 0) {
                return -1;
            }
            mya_net_write_be16(exts + list_start, (uint16_t)(epos - list_start - 2u));
        }
        mya_net_write_be16(exts + ext_start + 2u, (uint16_t)(epos - ext_start - 4u));
    }

    exts_start = pos;
    if (append_u16_be(body, sizeof(body), &pos, (uint16_t)epos) != 0 ||
        append_raw(body, sizeof(body), &pos, exts, epos) != 0) {
        return -1;
    }
    (void)exts_start;

    if (out_cap < pos + 4u) {
        return -1;
    }
    out[0] = CURL_TLS_HS_CLIENT_HELLO;
    write_u24_be(out + 1u, pos);
    mem_copy(out + 4u, body, pos);
    *out_len = pos + 4u;
    return 0;
}

static int tls13_parse_server_hello(
    const uint8_t* hs_msg,
    uint32_t hs_len,
    uint8_t out_server_pubkey[32],
    uint16_t* out_cipher
) {
    const uint8_t* p;
    uint32_t body_len;
    uint32_t rem;
    uint8_t got_tls13 = 0u;
    uint8_t got_key = 0u;

    if (!hs_msg || !out_server_pubkey || !out_cipher || hs_len < 4u || hs_msg[0] != CURL_TLS_HS_SERVER_HELLO) {
        return -1;
    }

    body_len = read_u24_be(hs_msg + 1u);
    if (body_len + 4u != hs_len) {
        return -1;
    }

    p = hs_msg + 4u;
    rem = body_len;
    if (rem < 2u + 32u + 1u + 2u + 1u + 2u) {
        return -1;
    }
    if (mya_net_read_be16(p) != CURL_TLS_VER_1_2) {
        return -1;
    }
    p += 2u;
    rem -= 2u;

    p += 32u;
    rem -= 32u;

    {
        uint32_t sid_len = p[0];
        p++;
        rem--;
        if (sid_len > 32u || rem < sid_len + 2u + 1u + 2u) {
            return -1;
        }
        p += sid_len;
        rem -= sid_len;
    }

    *out_cipher = mya_net_read_be16(p);
    p += 2u;
    rem -= 2u;

    if (*out_cipher != CURL_TLS_AES_128_GCM_SHA256) {
        return -1;
    }

    if (p[0] != 0u) {
        return -1;
    }
    p++;
    rem--;

    {
        uint32_t exts_len = (uint32_t)mya_net_read_be16(p);
        p += 2u;
        rem -= 2u;
        if (exts_len != rem) {
            return -1;
        }

        while (rem >= 4u) {
            uint16_t ext_type = mya_net_read_be16(p);
            uint16_t ext_len = mya_net_read_be16(p + 2u);
            const uint8_t* ext = p + 4u;
            if ((uint32_t)ext_len > rem - 4u) {
                return -1;
            }

            if (ext_type == CURL_TLS_EXT_SUPPORTED_VERSIONS) {
                if (ext_len == 2u && mya_net_read_be16(ext) == CURL_TLS_VER_1_3) {
                    got_tls13 = 1u;
                }
            } else if (ext_type == CURL_TLS_EXT_KEY_SHARE) {
                if (ext_len >= 4u) {
                    uint16_t group = mya_net_read_be16(ext);
                    uint16_t klen = mya_net_read_be16(ext + 2u);
                    if (group == CURL_TLS_GROUP_X25519 && klen == 32u && ext_len == 4u + 32u) {
                        mem_copy(out_server_pubkey, ext + 4u, 32u);
                        got_key = 1u;
                    }
                }
            }

            p += 4u + ext_len;
            rem -= 4u + ext_len;
        }
    }

    return (got_tls13 && got_key) ? 0 : -1;
}

static void tls13_hs_parser_reset(tls13_hs_parser_t* p) {
    if (!p) {
        return;
    }
    p->hdr_used = 0u;
    p->msg_type = 0u;
    p->msg_len = 0u;
    p->msg_read = 0u;
    p->finished_used = 0u;
}

static int tls13_hs_feed_server(
    tls13_hs_parser_t* p,
    tls_sha256_ctx_t* transcript,
    const uint8_t server_finished_key[32],
    const uint8_t* data,
    uint32_t data_len,
    uint8_t* out_got_finished
) {
    uint32_t pos = 0u;

    if (!p || !transcript || !server_finished_key || (!data && data_len != 0u) || !out_got_finished) {
        return -1;
    }

    while (pos < data_len) {
        if (p->hdr_used < 4u) {
            uint32_t take = 4u - p->hdr_used;
            if (take > data_len - pos) {
                take = data_len - pos;
            }
            mem_copy(p->hdr + p->hdr_used, data + pos, take);
            p->hdr_used += take;
            pos += take;
            if (p->hdr_used < 4u) {
                continue;
            }
            p->msg_type = p->hdr[0];
            p->msg_len = read_u24_be(p->hdr + 1u);
            p->msg_read = 0u;
            p->finished_used = 0u;
            if (p->msg_len > CURL_TLS_MAX_HS_MESSAGE) {
                return -1;
            }
            if (p->msg_type != CURL_TLS_HS_FINISHED) {
                tls_sha256_update(transcript, p->hdr, 4u);
            } else if (p->msg_len != 32u) {
                return -1;
            }
        }

        {
            uint32_t remain_msg = p->msg_len - p->msg_read;
            uint32_t take = remain_msg;
            if (take > data_len - pos) {
                take = data_len - pos;
            }

            if (p->msg_type == CURL_TLS_HS_FINISHED) {
                if (p->finished_used + take > sizeof(p->finished_data)) {
                    return -1;
                }
                mem_copy(p->finished_data + p->finished_used, data + pos, take);
                p->finished_used += take;
            } else {
                tls_sha256_update(transcript, data + pos, take);
            }
            p->msg_read += take;
            pos += take;
        }

        if (p->msg_read == p->msg_len) {
            if (p->msg_type == CURL_TLS_HS_FINISHED) {
                uint8_t thash[32];
                uint8_t expected[32];

                tls_transcript_hash(transcript, thash);
                tls_hmac_sha256(server_finished_key, 32u, thash, sizeof(thash), expected);
                if (!mem_eq(expected, p->finished_data, 32u)) {
                    return -1;
                }

                tls_sha256_update(transcript, p->hdr, 4u);
                tls_sha256_update(transcript, p->finished_data, 32u);
                *out_got_finished = 1u;
            }
            tls13_hs_parser_reset(p);
        }
    }

    return 0;
}

static int stream_https_response_tls13(
    int32_t sock_fd,
    const curl_target_t* target,
    const uint8_t* request,
    uint32_t request_len,
    int32_t out_fd,
    uint8_t include_headers,
    uint32_t timeout_polls,
    uint32_t* out_body_bytes
) {
    uint8_t client_secret[32];
    uint8_t client_pub[32];
    uint8_t client_random[32];
    uint8_t session_id[32];
    uint8_t server_pub[32];
    uint8_t shared_secret[32];
    uint8_t client_hello[CURL_TLS_MAX_CLIENT_HELLO + 4u];
    uint32_t client_hello_len = 0u;
    uint16_t selected_cipher = 0u;
    tls_sha256_ctx_t transcript;
    uint8_t hs_accum_len_ready = 0u;
    uint32_t hs_accum_len = 0u;
    uint8_t early_secret[32];
    uint8_t hs_derived[32];
    uint8_t handshake_secret[32];
    uint8_t client_hs_secret[32];
    uint8_t server_hs_secret[32];
    uint8_t client_finished_key[32];
    uint8_t server_finished_key[32];
    uint8_t app_derived[32];
    uint8_t master_secret[32];
    uint8_t empty_hash[32];
    uint8_t transcript_hash[32];
    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    uint8_t key[16];
    uint8_t iv[12];
    tls13_aead_state_t hs_tx;
    tls13_aead_state_t hs_rx;
    tls13_aead_state_t app_tx;
    tls13_aead_state_t app_rx;
    tls13_hs_parser_t hs_parser;
    uint8_t got_server_finished = 0u;
    uint8_t hdr_done = 0u;
    uint8_t hdr_state = 0u;
    uint32_t body_bytes = 0u;
    uint8_t close_notify_seen = 0u;
    uint8_t header_buf[CURL_HEADER_MAX];
    uint32_t header_len = 0u;
    uint8_t header_overflow = 0u;
    uint8_t has_content_length = 0u;
    uint32_t content_length = 0u;
    uint8_t header_meta_ready = 0u;

    if (!target || !request || request_len == 0u) {
        return -1;
    }

    weak_random_fill(client_secret, sizeof(client_secret), 0x544C5313u);
    weak_random_fill(client_random, sizeof(client_random), 0x43484C4Fu);
    weak_random_fill(session_id, sizeof(session_id), 0x53494431u);
    tls_sha256_digest(NULL, 0u, empty_hash);
    client_secret[0] &= 248u;
    client_secret[31] = (uint8_t)((client_secret[31] & 127u) | 64u);

    if (tls_x25519_basepoint(client_pub, client_secret) != 0) {
        mya_putln("curl: tls: x25519 public key failed");
        return -1;
    }
    if (tls13_build_client_hello(target->host, client_random, session_id, client_pub, client_hello, sizeof(client_hello), &client_hello_len) != 0) {
        mya_putln("curl: tls: failed to build ClientHello");
        return -1;
    }

    tls_sha256_init(&transcript);
    tls_sha256_update(&transcript, client_hello, client_hello_len);

    if (tls_write_record_plain(sock_fd, CURL_TLS_CONTENT_HANDSHAKE, client_hello, (uint16_t)client_hello_len) != 0) {
        mya_putln("curl: tls: send ClientHello failed");
        return -1;
    }

    {
        static const uint8_t ccs_payload[1] = { 0x01u };
        (void)tls_write_record_plain(sock_fd, CURL_TLS_CONTENT_CHANGE_CIPHER_SPEC, ccs_payload, 1u);
    }

    while (!hs_accum_len_ready) {
        uint8_t rec_type = 0u;
        uint32_t rec_len = 0u;
        int rc = tls_read_record(sock_fd, timeout_polls, &rec_type, NULL, g_tls_rx_record, sizeof(g_tls_rx_record), &rec_len);
        if (rc == -1) {
            mya_putln("curl: tls: timeout waiting for ServerHello");
            return -1;
        }
        if (rc != 0) {
            mya_putln("curl: tls: connection closed before ServerHello");
            return -1;
        }
        if (rec_type == CURL_TLS_CONTENT_CHANGE_CIPHER_SPEC) {
            continue;
        }
        if (rec_type != CURL_TLS_CONTENT_HANDSHAKE) {
            mya_putln("curl: tls: unexpected record before ServerHello");
            return -1;
        }
        if (hs_accum_len + rec_len > sizeof(g_tls_hs_accum)) {
            mya_putln("curl: tls: ServerHello too large");
            return -1;
        }
        mem_copy(g_tls_hs_accum + hs_accum_len, g_tls_rx_record, rec_len);
        hs_accum_len += rec_len;

        if (hs_accum_len >= 4u) {
            uint32_t msg_len = read_u24_be(g_tls_hs_accum + 1u);
            uint32_t total = msg_len + 4u;
            if (total > sizeof(g_tls_hs_accum)) {
                mya_putln("curl: tls: malformed ServerHello length");
                return -1;
            }
            if (hs_accum_len >= total) {
                if (tls13_parse_server_hello(g_tls_hs_accum, total, server_pub, &selected_cipher) != 0) {
                    mya_putln("curl: tls: unsupported or malformed ServerHello");
                    return -1;
                }
                tls_sha256_update(&transcript, g_tls_hs_accum, total);
                if (hs_accum_len > total) {
                    mya_putln("curl: tls: unexpected extra plaintext handshake bytes");
                    return -1;
                }
                hs_accum_len_ready = 1u;
            }
        }
    }

    if (selected_cipher != CURL_TLS_AES_128_GCM_SHA256) {
        mya_putln("curl: tls: server selected unsupported cipher suite");
        return -1;
    }
    if (tls_x25519_scalarmult(shared_secret, client_secret, server_pub) != 0) {
        mya_putln("curl: tls: x25519 shared secret failed");
        return -1;
    }

    {
        uint8_t zero_psk[32];
        mem_zero(zero_psk, sizeof(zero_psk));
        tls_hkdf_extract(NULL, 0u, zero_psk, sizeof(zero_psk), early_secret);
    }
    if (tls_hkdf_expand_label(early_secret, "derived", empty_hash, sizeof(empty_hash), hs_derived, sizeof(hs_derived)) != 0) {
        return -1;
    }
    tls_hkdf_extract(hs_derived, sizeof(hs_derived), shared_secret, sizeof(shared_secret), handshake_secret);
    tls_transcript_hash(&transcript, transcript_hash);
    if (tls_hkdf_expand_label(handshake_secret, "c hs traffic", transcript_hash, sizeof(transcript_hash), client_hs_secret, sizeof(client_hs_secret)) != 0 ||
        tls_hkdf_expand_label(handshake_secret, "s hs traffic", transcript_hash, sizeof(transcript_hash), server_hs_secret, sizeof(server_hs_secret)) != 0 ||
        tls_hkdf_expand_label(client_hs_secret, "finished", NULL, 0u, client_finished_key, sizeof(client_finished_key)) != 0 ||
        tls_hkdf_expand_label(server_hs_secret, "finished", NULL, 0u, server_finished_key, sizeof(server_finished_key)) != 0) {
        return -1;
    }

    if (tls_hkdf_expand_label(client_hs_secret, "key", NULL, 0u, key, sizeof(key)) != 0 ||
        tls_hkdf_expand_label(client_hs_secret, "iv", NULL, 0u, iv, sizeof(iv)) != 0) {
        return -1;
    }
    tls13_aead_state_init(&hs_tx, key, iv);
    if (tls_hkdf_expand_label(server_hs_secret, "key", NULL, 0u, key, sizeof(key)) != 0 ||
        tls_hkdf_expand_label(server_hs_secret, "iv", NULL, 0u, iv, sizeof(iv)) != 0) {
        return -1;
    }
    tls13_aead_state_init(&hs_rx, key, iv);

    tls13_hs_parser_reset(&hs_parser);
    while (!got_server_finished) {
        uint8_t rec_type = 0u;
        uint32_t rec_len = 0u;
        uint8_t rec_hdr[5];
        uint8_t inner_type = 0u;
        uint32_t plain_len = 0u;
        int rc = tls_read_record(sock_fd, timeout_polls, &rec_type, rec_hdr, g_tls_rx_record, sizeof(g_tls_rx_record), &rec_len);

        if (rc == -1) {
            mya_putln("curl: tls: timeout during handshake");
            return -1;
        }
        if (rc != 0) {
            mya_putln("curl: tls: connection closed during handshake");
            return -1;
        }
        if (rec_type == CURL_TLS_CONTENT_CHANGE_CIPHER_SPEC) {
            continue;
        }
        if (rec_type != CURL_TLS_CONTENT_APPLICATION_DATA) {
            mya_putln("curl: tls: unexpected handshake record type");
            return -1;
        }

        if (tls13_decrypt_record(&hs_rx, rec_hdr, g_tls_rx_record, rec_len, &inner_type, g_tls_plain, sizeof(g_tls_plain), &plain_len) != 0) {
            mya_putln("curl: tls: failed to decrypt handshake record");
            return -1;
        }

        if (inner_type == CURL_TLS_CONTENT_HANDSHAKE) {
            if (tls13_hs_feed_server(
                    &hs_parser,
                    &transcript,
                    server_finished_key,
                    g_tls_plain,
                    plain_len,
                    &got_server_finished
                ) != 0) {
                mya_putln("curl: tls: server Finished verify failed");
                return -1;
            }
            continue;
        }

        if (inner_type == CURL_TLS_CONTENT_ALERT) {
            mya_putln("curl: tls: server sent alert during handshake");
            return -1;
        }
    }

    tls_transcript_hash(&transcript, transcript_hash);
    if (tls_hkdf_expand_label(handshake_secret, "derived", empty_hash, sizeof(empty_hash), app_derived, sizeof(app_derived)) != 0) {
        return -1;
    }
    {
        uint8_t zero_ikm[32];
        mem_zero(zero_ikm, sizeof(zero_ikm));
        tls_hkdf_extract(app_derived, sizeof(app_derived), zero_ikm, sizeof(zero_ikm), master_secret);
    }
    if (tls_hkdf_expand_label(master_secret, "c ap traffic", transcript_hash, sizeof(transcript_hash), client_app_secret, sizeof(client_app_secret)) != 0 ||
        tls_hkdf_expand_label(master_secret, "s ap traffic", transcript_hash, sizeof(transcript_hash), server_app_secret, sizeof(server_app_secret)) != 0) {
        return -1;
    }

    {
        uint8_t finished_msg[36];
        tls_transcript_hash(&transcript, transcript_hash);
        tls_hmac_sha256(client_finished_key, sizeof(client_finished_key), transcript_hash, sizeof(transcript_hash), finished_msg + 4u);
        finished_msg[0] = CURL_TLS_HS_FINISHED;
        finished_msg[1] = 0u;
        finished_msg[2] = 0u;
        finished_msg[3] = 32u;
        if (tls13_write_encrypted_record(sock_fd, &hs_tx, CURL_TLS_CONTENT_HANDSHAKE, finished_msg, sizeof(finished_msg)) != 0) {
            mya_putln("curl: tls: failed to send client Finished");
            return -1;
        }
        tls_sha256_update(&transcript, finished_msg, sizeof(finished_msg));
    }

    if (tls_hkdf_expand_label(client_app_secret, "key", NULL, 0u, key, sizeof(key)) != 0 ||
        tls_hkdf_expand_label(client_app_secret, "iv", NULL, 0u, iv, sizeof(iv)) != 0) {
        return -1;
    }
    tls13_aead_state_init(&app_tx, key, iv);
    if (tls_hkdf_expand_label(server_app_secret, "key", NULL, 0u, key, sizeof(key)) != 0 ||
        tls_hkdf_expand_label(server_app_secret, "iv", NULL, 0u, iv, sizeof(iv)) != 0) {
        return -1;
    }
    tls13_aead_state_init(&app_rx, key, iv);

    if (tls13_write_encrypted_record(sock_fd, &app_tx, CURL_TLS_CONTENT_APPLICATION_DATA, request, (uint16_t)request_len) != 0) {
        mya_putln("curl: tls: failed to send HTTP request");
        return -1;
    }

    while (!close_notify_seen) {
        uint8_t rec_type = 0u;
        uint32_t rec_len = 0u;
        uint8_t rec_hdr[5];
        uint8_t inner_type = 0u;
        uint32_t plain_len = 0u;
        int rc = tls_read_record(sock_fd, timeout_polls, &rec_type, rec_hdr, g_tls_rx_record, sizeof(g_tls_rx_record), &rec_len);

        if (rc == -1) {
            mya_putln("curl: timeout waiting for HTTPS response");
            return -1;
        }
        if (rc == -2) {
            break;
        }
        if (rc != 0) {
            return -1;
        }
        if (rec_type == CURL_TLS_CONTENT_CHANGE_CIPHER_SPEC) {
            continue;
        }
        if (rec_type != CURL_TLS_CONTENT_APPLICATION_DATA) {
            continue;
        }

        if (tls13_decrypt_record(&app_rx, rec_hdr, g_tls_rx_record, rec_len, &inner_type, g_tls_plain, sizeof(g_tls_plain), &plain_len) != 0) {
            mya_putln("curl: tls: failed to decrypt application record");
            return -1;
        }

        if (inner_type == CURL_TLS_CONTENT_ALERT) {
            if (plain_len >= 2u && g_tls_plain[0] == CURL_TLS_ALERT_LEVEL_WARNING && g_tls_plain[1] == CURL_TLS_ALERT_CLOSE_NOTIFY) {
                close_notify_seen = 1u;
                break;
            }
            return -1;
        }
        if (inner_type == CURL_TLS_CONTENT_HANDSHAKE) {
            continue;
        }
        if (inner_type != CURL_TLS_CONTENT_APPLICATION_DATA) {
            continue;
        }

        {
            uint32_t body_start = 0u;
            if (!hdr_done) {
                for (uint32_t i = 0u; i < plain_len; i++) {
                    uint8_t c = g_tls_plain[i];

                    if (!header_overflow) {
                        if (header_len + 1u < sizeof(header_buf)) {
                            header_buf[header_len++] = c;
                        } else {
                            header_overflow = 1u;
                        }
                    }

                    if (hdr_state == 0u) {
                        hdr_state = (c == '\r') ? 1u : 0u;
                    } else if (hdr_state == 1u) {
                        hdr_state = (c == '\n') ? 2u : ((c == '\r') ? 1u : 0u);
                    } else if (hdr_state == 2u) {
                        hdr_state = (c == '\r') ? 3u : 0u;
                    } else {
                        if (c == '\n') {
                            hdr_done = 1u;
                            hdr_state = 0u;
                            body_start = i + 1u;
                            break;
                        }
                        hdr_state = (c == '\r') ? 1u : 0u;
                    }
                }

                if (!hdr_done) {
                    if (include_headers && output_chunk(g_tls_plain, plain_len, out_fd) != 0) {
                        return -1;
                    }
                    continue;
                }

                if (!header_meta_ready) {
                    if (!header_overflow) {
                        parse_http_content_length(header_buf, header_len, &has_content_length, &content_length);
                    }
                    header_meta_ready = 1u;
                }

                if (include_headers && body_start > 0u) {
                    if (output_chunk(g_tls_plain, body_start, out_fd) != 0) {
                        return -1;
                    }
                }
            }

            if (body_start < plain_len) {
                uint32_t body_len = plain_len - body_start;
                if (output_chunk(g_tls_plain + body_start, body_len, out_fd) != 0) {
                    return -1;
                }
                body_bytes += body_len;
            }

            if (header_meta_ready && has_content_length && body_bytes >= content_length) {
                if (out_body_bytes) {
                    *out_body_bytes = body_bytes;
                }
                return 0;
            }
        }
    }

    if (!hdr_done) {
        mya_putln("curl: invalid HTTPS response");
        return -1;
    }

    if (out_body_bytes) {
        *out_body_bytes = body_bytes;
    }
    return 0;
}

static int output_chunk(const uint8_t* data, uint32_t len, int32_t out_fd) {
    if (out_fd >= 0) {
        return file_write_all(out_fd, data, len);
    }
    return console_write_all(data, len);
}

static int stream_http_response(
    int32_t sock_fd,
    int32_t out_fd,
    uint8_t include_headers,
    uint32_t timeout_polls,
    uint32_t* out_body_bytes
) {
    uint8_t buf[CURL_RECV_CHUNK];
    uint8_t hdr_done = 0u;
    uint8_t hdr_state = 0u;
    uint32_t body_bytes = 0u;
    uint32_t idle_polls = 0u;
    uint8_t header_buf[CURL_HEADER_MAX];
    uint32_t header_len = 0u;
    uint8_t header_overflow = 0u;
    uint8_t has_content_length = 0u;
    uint32_t content_length = 0u;
    uint8_t header_meta_ready = 0u;

    while (1) {
        uint32_t got = 0u;
        uint16_t src_port = 0u;

        if (mya_sock_recv(sock_fd, buf, sizeof(buf), &got, &src_port) == 0 && got > 0u) {
            uint32_t body_start = 0u;
            idle_polls = 0u;

            if (!hdr_done) {
                for (uint32_t i = 0u; i < got; i++) {
                    uint8_t c = buf[i];

                    if (!header_overflow) {
                        if (header_len + 1u < sizeof(header_buf)) {
                            header_buf[header_len++] = c;
                        } else {
                            header_overflow = 1u;
                        }
                    }

                    if (hdr_state == 0u) {
                        hdr_state = (c == '\r') ? 1u : 0u;
                    } else if (hdr_state == 1u) {
                        hdr_state = (c == '\n') ? 2u : ((c == '\r') ? 1u : 0u);
                    } else if (hdr_state == 2u) {
                        hdr_state = (c == '\r') ? 3u : 0u;
                    } else {
                        if (c == '\n') {
                            hdr_done = 1u;
                            hdr_state = 0u;
                            body_start = i + 1u;
                            break;
                        }
                        hdr_state = (c == '\r') ? 1u : 0u;
                    }
                }

                if (!hdr_done) {
                    if (include_headers && output_chunk(buf, got, out_fd) != 0) {
                        return -1;
                    }
                    continue;
                }

                if (!header_meta_ready) {
                    if (!header_overflow) {
                        parse_http_content_length(header_buf, header_len, &has_content_length, &content_length);
                    }
                    header_meta_ready = 1u;
                }

                if (include_headers && body_start > 0u) {
                    if (output_chunk(buf, body_start, out_fd) != 0) {
                        return -1;
                    }
                }
            }

            if (body_start < got) {
                uint32_t body_len = got - body_start;
                if (output_chunk(buf + body_start, body_len, out_fd) != 0) {
                    return -1;
                }
                body_bytes += body_len;
            }

            if (header_meta_ready && has_content_length && body_bytes >= content_length) {
                if (out_body_bytes) {
                    *out_body_bytes = body_bytes;
                }
                return 0;
            }
            continue;
        }

        {
            uint32_t probe_written = 0u;
            if (mya_sock_send(sock_fd, 0u, NULL, 0u, &probe_written) != 0) {
                break;
            }
        }

        if (idle_polls++ >= timeout_polls) {
            mya_putln("curl: timeout waiting for response");
            return -1;
        }
        mya_proc_yield();
    }

    if (!hdr_done) {
        mya_putln("curl: invalid HTTP response");
        return -1;
    }

    if (out_body_bytes) {
        *out_body_bytes = body_bytes;
    }
    return 0;
}

static void print_usage(void) {
    mya_putln("usage: curl [-i] [-o FILE] [--timeout-polls N] [--proxy host[:port]] <http[s]://host[:port][/path]>");
    mya_putln("notes:");
    mya_putln("  - HTTP works directly, and through proxy with absolute-form request");
    mya_putln("  - HTTPS works natively (TLS 1.3); proxy is used only with --proxy");
    mya_putln("  - host can be domain name or numeric IPv4");
}

int program_main(int argc, char** argv) {
    const char* url = NULL;
    const char* out_path = NULL;
    const char* proxy_opt = NULL;
    uint32_t timeout_polls = CURL_TIMEOUT_POLLS_DEFAULT;
    uint8_t include_headers = 0u;
    curl_target_t target;
    int32_t sock_fd = -1;
    int32_t out_fd = -1;
    char request[CURL_REQ_MAX];
    uint32_t request_len = 0u;
    uint32_t body_bytes = 0u;
    int exit_code = 1;

    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-i") || str_eq(argv[i], "--include")) {
            include_headers = 1u;
            continue;
        }
        if (str_eq(argv[i], "-o") || str_eq(argv[i], "--output")) {
            if (i + 1 >= argc) {
                mya_putln("curl: missing path after -o/--output");
                print_usage();
                return 1;
            }
            out_path = argv[++i];
            continue;
        }
        if (str_eq(argv[i], "--timeout-polls")) {
            if (i + 1 >= argc || parse_u32(argv[i + 1], &timeout_polls) != 0 || timeout_polls == 0u) {
                mya_putln("curl: invalid --timeout-polls value");
                return 1;
            }
            i++;
            continue;
        }
        if (str_eq(argv[i], "--proxy")) {
            if (i + 1 >= argc) {
                mya_putln("curl: missing value after --proxy");
                return 1;
            }
            proxy_opt = argv[++i];
            continue;
        }
        if (str_eq(argv[i], "-h") || str_eq(argv[i], "--help")) {
            print_usage();
            return 0;
        }
        if (argv[i][0] == '-') {
            mya_puts("curl: unknown option: ");
            mya_putln(argv[i]);
            print_usage();
            return 1;
        }
        if (url) {
            mya_putln("curl: only one URL argument is supported");
            print_usage();
            return 1;
        }
        url = argv[i];
    }

    if (!url) {
        print_usage();
        return 1;
    }

    {
        int rc = parse_url(url, &target);
        if (rc != 0) {
            mya_putln("curl: invalid URL");
            print_usage();
            return 1;
        }

        rc = prepare_connection_target(&target, proxy_opt, timeout_polls);
        if (rc == -3) {
            mya_puts("curl: cannot resolve host: ");
            mya_putln(target.connect_host);
            return 1;
        }
        if (rc != 0) {
            mya_putln("curl: invalid proxy settings");
            return 1;
        }
    }

    if (build_request(&target, request, sizeof(request), &request_len) != 0) {
        mya_putln("curl: request is too large");
        return 1;
    }

    if (out_path) {
        uint32_t flags = MYAOS_POSIX_O_WRONLY | MYAOS_POSIX_O_CREAT | MYAOS_POSIX_O_TRUNC;
        if (mya_posix_open(out_path, flags, &out_fd) != 0) {
            mya_puts("curl: cannot open output file: ");
            mya_putln(out_path);
            return 1;
        }
    }

    if (mya_sock_open_ex(MYAOS_SOCK_PROTO_TCP, 0u, &sock_fd) != 0) {
        mya_putln("curl: socket open failed");
        goto cleanup;
    }
    if (mya_sock_connect4(sock_fd, target.connect_ip, target.connect_port) != 0) {
        mya_putln("curl: connect failed");
        goto cleanup;
    }
    if (target.is_https && !target.use_proxy) {
        if (stream_https_response_tls13(
                sock_fd,
                &target,
                (const uint8_t*)request,
                request_len,
                out_fd,
                include_headers,
                timeout_polls,
                &body_bytes
            ) != 0) {
            goto cleanup;
        }
    } else {
        if (socket_send_all(sock_fd, (const uint8_t*)request, request_len) != 0) {
            mya_putln("curl: send failed");
            goto cleanup;
        }
        if (stream_http_response(sock_fd, out_fd, include_headers, timeout_polls, &body_bytes) != 0) {
            goto cleanup;
        }
    }

    if (out_path) {
        mya_puts("curl: wrote ");
        mya_put_u32(body_bytes);
        mya_puts(" bytes to ");
        mya_putln(out_path);
    }

    exit_code = 0;

cleanup:
    if (sock_fd >= 0) {
        (void)mya_sock_close(sock_fd);
    }
    if (out_fd >= 0) {
        (void)mya_posix_close(out_fd);
    }
    return exit_code;
}
