#include "../lib/myaos.h"
#include <stdint.h>

#define PKG_FILE_MAX (512u * 1024u)
#define PKG_TEXT_MAX 16384u
#define PKG_META_MAX 4096u
#define PKG_VERSION_MAX 24u
#define PKG_REPO_MAX 64u
#define PKG_INSTALLED_MAX 64u
#define PKG_FILES_META_MAX 2048u
#define PKG_SHA256_HEX_LEN 64u
#define PKG_SHA256_DIGEST_LEN 32u

#define PKG_INDEX_DEFAULT "/repo/index.pkg"
#define PKG_DB_DIR "/var/pkg"
#define PKG_DB_FILE "/var/pkg/installed.db"
#define PKG_KMOD_DIR_PREFIX "/lib/modules/"
#define PKG_KMOD_SUFFIX ".kmod"
#define PKG_CURL_EXEC "/bin/curl.elf"

#define MPKG_HEADER_SIZE 64u
#define MPKG_VERSION 1u
#define MPKG_FILE_ENTRY_SIZE 40u
#define MPKG_FILETABLE_HEAD_SIZE 8u
#define MPKG_ARCH_MAX 32u
#define MPKG_DESC_MAX 256u
#define MPKG_SCRIPT_MAX 1024u
#define MPKG_LIST_MAX 32u

#define MPKG_FLAG_COMPRESSED (1u << 0)
#define MPKG_FLAG_SIGNED (1u << 1)
#define MPKG_FLAG_DELTA (1u << 2)

#define MPKG_META_NAME 1u
#define MPKG_META_VERSION 2u
#define MPKG_META_ARCH 3u
#define MPKG_META_ABI 4u
#define MPKG_META_DEPENDS 5u
#define MPKG_META_PROVIDES 6u
#define MPKG_META_DESCRIPTION 7u
#define MPKG_META_SCRIPT_PRE 8u
#define MPKG_META_SCRIPT_POST 9u

#define MPKG_FILE_FLAG_EXEC (1u << 0)
#define MPKG_FILE_FLAG_CONFIG (1u << 1)
#define MPKG_FILE_FLAG_DIR (1u << 2)

typedef struct {
    char name[MYAOS_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char path[MYAOS_PATH_MAX];
    uint32_t abi;
} pkg_repo_entry_t;

typedef struct {
    char name[MYAOS_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char source[MYAOS_PATH_MAX];
} pkg_installed_entry_t;

static uint8_t g_pkg_file_buf[PKG_FILE_MAX];
static uint8_t g_text_buf[PKG_TEXT_MAX];

typedef struct {
    char name[MYAOS_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char arch[MPKG_ARCH_MAX];
    char description[MPKG_DESC_MAX];
    char script_pre[MPKG_SCRIPT_MAX];
    char script_post[MPKG_SCRIPT_MAX];
    char depends[MPKG_LIST_MAX][MYAOS_NAME_MAX];
    char provides[MPKG_LIST_MAX][MYAOS_NAME_MAX];
    uint32_t depends_count;
    uint32_t provides_count;
    uint32_t abi;
    uint8_t abi_set;
} mpkg_meta_t;

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static int str_eq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int str_starts_with(const char* text, const char* prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int is_remote_url(const char* text) {
    if (!text) {
        return 0;
    }
    return str_starts_with(text, "http://") || str_starts_with(text, "https://");
}

static int str_ends_with(const char* text, const char* suffix) {
    size_t text_len = str_len(text);
    size_t suffix_len = str_len(suffix);

    if (suffix_len > text_len) {
        return 0;
    }

    for (size_t i = 0; i < suffix_len; i++) {
        if (text[text_len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

static int is_kernel_module_path(const char* path) {
    if (!path) {
        return 0;
    }
    return str_starts_with(path, PKG_KMOD_DIR_PREFIX) && str_ends_with(path, PKG_KMOD_SUFFIX);
}

static char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int str_contains_ci(const char* text, const char* needle) {
    size_t text_len = str_len(text);
    size_t needle_len = str_len(needle);

    if (!needle || !needle[0]) {
        return 1;
    }
    if (!text || needle_len > text_len) {
        return 0;
    }

    for (size_t i = 0; i + needle_len <= text_len; i++) {
        size_t j = 0;
        while (j < needle_len && to_lower_char(text[i + j]) == to_lower_char(needle[j])) {
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
    }
    return 0;
}

static void str_copy(char* dst, const char* src, uint32_t dst_size) {
    uint32_t i = 0;

    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1u < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int parse_u32(const char* text, uint32_t* out) {
    uint64_t value = 0;

    if (!text || !text[0] || !out) {
        return -1;
    }

    for (uint32_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
        if (value > 0xFFFFFFFFull) {
            return -1;
        }
    }

    *out = (uint32_t)value;
    return 0;
}

static uint16_t read_u16_le(const uint8_t* p) {
    if (!p) {
        return 0u;
    }
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}

static uint32_t read_u32_le(const uint8_t* p) {
    if (!p) {
        return 0u;
    }
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static uint64_t read_u64_le(const uint8_t* p) {
    uint64_t lo = read_u32_le(p);
    uint64_t hi = read_u32_le(p ? p + 4u : NULL);
    return lo | (hi << 32u);
}

static uint32_t mpkg_crc32_compute(const uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFu;

    if (!data) {
        return 0u;
    }
    for (uint32_t i = 0u; i < size; i++) {
        uint8_t byte = data[i];
        uint32_t v;

        /* Checksum excludes header crc32 field bytes [44..47] by treating them as zero. */
        if (i >= 44u && i < 48u) {
            byte = 0u;
        }

        v = (crc ^ (uint32_t)byte) & 0xFFu;
        for (uint32_t b = 0u; b < 8u; b++) {
            if ((v & 1u) != 0u) {
                v = (v >> 1u) ^ 0xEDB88320u;
            } else {
                v >>= 1u;
            }
        }
        crc = (crc >> 8u) ^ v;
    }
    return ~crc;
}

static int parse_version(const char* text, uint32_t out_parts[3]) {
    uint32_t index = 0;
    uint32_t value = 0;
    uint8_t saw_digit = 0;

    if (!text || !text[0]) {
        return -1;
    }

    out_parts[0] = 0;
    out_parts[1] = 0;
    out_parts[2] = 0;

    for (uint32_t i = 0;; i++) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            saw_digit = 1u;
            value = value * 10u + (uint32_t)(c - '0');
            if (value > 1000000u) {
                return -1;
            }
            continue;
        }

        if (c == '.' || c == '\0') {
            if (!saw_digit || index > 2u) {
                return -1;
            }
            out_parts[index++] = value;
            value = 0;
            saw_digit = 0u;
            if (c == '\0') {
                break;
            }
            continue;
        }

        return -1;
    }

    return 0;
}

static int compare_version(const char* a, const char* b) {
    uint32_t va[3];
    uint32_t vb[3];

    if (parse_version(a, va) != 0 || parse_version(b, vb) != 0) {
        return 0;
    }

    for (uint32_t i = 0; i < 3u; i++) {
        if (va[i] < vb[i]) {
            return -1;
        }
        if (va[i] > vb[i]) {
            return 1;
        }
    }
    return 0;
}

static char* next_line(char** cursor) {
    char* line;
    char* p;

    if (!cursor || !*cursor || !(*cursor)[0]) {
        return NULL;
    }

    line = *cursor;
    p = line;

    while (*p && *p != '\n' && *p != '\r') {
        p++;
    }
    while (*p == '\n' || *p == '\r') {
        *p = '\0';
        p++;
    }

    *cursor = p;
    return line;
}

static int append_text(char* out, uint32_t out_size, uint32_t* io_pos, const char* text) {
    uint32_t pos;

    if (!out || !io_pos || !text || out_size == 0u) {
        return -1;
    }

    pos = *io_pos;
    for (uint32_t i = 0; text[i]; i++) {
        if (pos + 1u >= out_size) {
            return -1;
        }
        out[pos++] = text[i];
    }

    out[pos] = '\0';
    *io_pos = pos;
    return 0;
}

static int is_valid_package_name(const char* name) {
    uint32_t n = 0;

    if (!name || !name[0]) {
        return 0;
    }

    for (n = 0; name[n]; n++) {
        char c = name[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return 0;
        }
    }

    return n < MYAOS_NAME_MAX;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (int)(c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (int)(c - 'A');
    }
    return -1;
}

static int decode_hex(const char* hex, uint8_t* out, uint32_t out_size, uint32_t* out_len) {
    uint32_t hex_len = (uint32_t)str_len(hex);
    uint32_t out_len_local = hex_len / 2u;

    if ((hex_len & 1u) != 0u || out_len_local > out_size) {
        return -1;
    }

    for (uint32_t i = 0; i < out_len_local; i++) {
        int hi = hex_nibble(hex[i * 2u]);
        int lo = hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)((uint8_t)(hi << 4) | (uint8_t)lo);
    }

    if (out_len) {
        *out_len = out_len_local;
    }
    return 0;
}

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    uint8_t block[64];
    uint32_t block_used;
} sha256_ctx_t;

static uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ ((~x) & z);
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
    uint32_t a, b, c, d, e, f, g, h;

    for (uint32_t i = 0; i < 16u; i++) {
        w[i] = ((uint32_t)block[i * 4u] << 24u) |
               ((uint32_t)block[i * 4u + 1u] << 16u) |
               ((uint32_t)block[i * 4u + 2u] << 8u) |
               ((uint32_t)block[i * 4u + 3u]);
    }
    for (uint32_t i = 16u; i < 64u; i++) {
        w[i] = sha256_ss1(w[i - 2u]) + w[i - 7u] + sha256_ss0(w[i - 15u]) + w[i - 16u];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (uint32_t i = 0; i < 64u; i++) {
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

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t* ctx) {
    if (!ctx) {
        return;
    }
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bits = 0u;
    ctx->block_used = 0u;
}

static void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    uint32_t i = 0;

    if (!ctx || (!data && len != 0u)) {
        return;
    }

    while (i < len) {
        ctx->block[ctx->block_used++] = data[i++];
        if (ctx->block_used == 64u) {
            sha256_transform(ctx, ctx->block);
            ctx->bits += 512u;
            ctx->block_used = 0u;
        }
    }
}

static void sha256_final(sha256_ctx_t* ctx, uint8_t out[PKG_SHA256_DIGEST_LEN]) {
    uint64_t total_bits;

    if (!ctx || !out) {
        return;
    }

    total_bits = ctx->bits + (uint64_t)ctx->block_used * 8u;
    ctx->block[ctx->block_used++] = 0x80u;

    if (ctx->block_used > 56u) {
        while (ctx->block_used < 64u) {
            ctx->block[ctx->block_used++] = 0u;
        }
        sha256_transform(ctx, ctx->block);
        ctx->block_used = 0u;
    }

    while (ctx->block_used < 56u) {
        ctx->block[ctx->block_used++] = 0u;
    }

    for (uint32_t i = 0; i < 8u; i++) {
        ctx->block[56u + i] = (uint8_t)((total_bits >> ((7u - i) * 8u)) & 0xFFu);
    }
    sha256_transform(ctx, ctx->block);

    for (uint32_t i = 0; i < 8u; i++) {
        out[i * 4u] = (uint8_t)((ctx->state[i] >> 24u) & 0xFFu);
        out[i * 4u + 1u] = (uint8_t)((ctx->state[i] >> 16u) & 0xFFu);
        out[i * 4u + 2u] = (uint8_t)((ctx->state[i] >> 8u) & 0xFFu);
        out[i * 4u + 3u] = (uint8_t)(ctx->state[i] & 0xFFu);
    }
}

static char hex_lower(uint8_t n) {
    return (n < 10u) ? (char)('0' + n) : (char)('a' + (n - 10u));
}

static void sha256_hex(const uint8_t* data, uint32_t len, char out_hex[PKG_SHA256_HEX_LEN + 1u]) {
    uint8_t digest[PKG_SHA256_DIGEST_LEN];
    sha256_ctx_t ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);

    for (uint32_t i = 0; i < PKG_SHA256_DIGEST_LEN; i++) {
        out_hex[i * 2u] = hex_lower((uint8_t)(digest[i] >> 4u));
        out_hex[i * 2u + 1u] = hex_lower((uint8_t)(digest[i] & 0xFu));
    }
    out_hex[PKG_SHA256_HEX_LEN] = '\0';
}

static int str_eq_ci(const char* a, const char* b) {
    uint32_t i = 0u;

    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (to_lower_char(a[i]) != to_lower_char(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int is_valid_sha256_hex(const char* text) {
    uint32_t len = 0u;

    if (!text) {
        return 0;
    }
    while (text[len]) {
        char c = text[len];
        int is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!is_hex) {
            return 0;
        }
        len++;
    }
    return len == PKG_SHA256_HEX_LEN;
}

static void ensure_dir_tree(const char* path) {
    char temp[MYAOS_PATH_MAX];

    if (!path || path[0] != '/') {
        return;
    }

    str_copy(temp, path, sizeof(temp));
    for (uint32_t i = 1; temp[i]; i++) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            if (temp[1] != '\0') {
                (void)mya_fs_mkdir(temp);
            }
            temp[i] = '/';
        }
    }

    (void)mya_fs_mkdir(temp);
}

static void ensure_parent_dirs(const char* file_path) {
    char temp[MYAOS_PATH_MAX];

    if (!file_path || file_path[0] != '/') {
        return;
    }

    str_copy(temp, file_path, sizeof(temp));
    for (uint32_t i = 1; temp[i]; i++) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            if (temp[1] != '\0') {
                (void)mya_fs_mkdir(temp);
            }
            temp[i] = '/';
        }
    }
}

static int read_text_file(const char* path, uint8_t* buf, uint32_t cap, uint32_t* out_size) {
    uint32_t size = 0;

    if (!path || !buf || cap < 2u) {
        return -1;
    }
    if (mya_fs_read(path, buf, cap - 1u, &size) != 0) {
        return -1;
    }
    if (size + 1u >= cap) {
        return -1;
    }
    buf[size] = '\0';

    if (out_size) {
        *out_size = size;
    }
    return 0;
}

static int build_meta_path(const char* name, char* out, uint32_t out_size) {
    uint32_t pos = 0;

    if (!is_valid_package_name(name) || !out || out_size == 0u) {
        return -1;
    }

    if (append_text(out, out_size, &pos, PKG_DB_DIR "/") != 0) {
        return -1;
    }
    if (append_text(out, out_size, &pos, name) != 0) {
        return -1;
    }
    if (append_text(out, out_size, &pos, ".meta") != 0) {
        return -1;
    }

    return 0;
}

static int can_append_file_meta_entry(uint32_t pos, uint32_t cap, const char* path, const char* sha256) {
    uint64_t need = 0u;
    uint64_t cap64 = cap;
    uint64_t pos64 = pos;

    if (!path || !path[0]) {
        return 0;
    }

    need += 5u + (uint64_t)str_len(path) + 1u; /* file=<path>\n */
    if (sha256 && sha256[0]) {
        need += 7u + (uint64_t)str_len(sha256) + 1u; /* sha256=<hex>\n */
    }

    /* keep trailing NUL slot for append_text contract */
    return (pos64 + need + 1u <= cap64) ? 1 : 0;
}

static int wait_child_exit(int32_t pid, int32_t* out_exit_code) {
    int32_t exit_code = 0;

    if (pid <= 0) {
        return -1;
    }

    for (;;) {
        int rc = mya_proc_wait_poll(pid, &exit_code);
        if (rc < 0) {
            return -1;
        }
        if (rc > 0) {
            if (out_exit_code) {
                *out_exit_code = exit_code;
            }
            return 0;
        }
        mya_proc_yield();
    }
}

static int build_download_temp_path(const char* suffix, char* out_path, uint32_t out_size) {
    uint32_t pos = 0u;
    int32_t pid = mya_proc_getpid();
    char pid_text[16];
    char tick_text[32];

    if (!suffix || !out_path || out_size == 0u) {
        return -1;
    }
    if (pid < 0) {
        pid = 0;
    }

    mya_u32_to_dec((uint32_t)pid, pid_text, sizeof(pid_text));
    mya_u64_to_dec(mya_time_ticks(), tick_text, sizeof(tick_text));

    out_path[0] = '\0';
    if (append_text(out_path, out_size, &pos, PKG_DB_DIR "/.dl-") != 0 ||
        append_text(out_path, out_size, &pos, pid_text) != 0 ||
        append_text(out_path, out_size, &pos, "-") != 0 ||
        append_text(out_path, out_size, &pos, tick_text) != 0 ||
        append_text(out_path, out_size, &pos, suffix) != 0) {
        return -1;
    }

    return 0;
}

static int download_url_to_temp_file(const char* url, const char* suffix, char* out_path, uint32_t out_size) {
    const char* curl_argv[5];
    int32_t pid = -1;
    int32_t exit_code = 0;

    if (!is_remote_url(url) || !suffix || !out_path || out_size == 0u) {
        return -1;
    }

    ensure_dir_tree("/var");
    ensure_dir_tree(PKG_DB_DIR);
    if (build_download_temp_path(suffix, out_path, out_size) != 0) {
        return -1;
    }

    curl_argv[0] = "curl";
    curl_argv[1] = "-o";
    curl_argv[2] = out_path;
    curl_argv[3] = url;
    curl_argv[4] = NULL;

    if (mya_proc_spawn(PKG_CURL_EXEC, 4, curl_argv, 0u, &pid) != 0 || pid <= 0) {
        mya_putln("pkg: failed to start curl downloader");
        return -1;
    }
    if (wait_child_exit(pid, &exit_code) != 0 || exit_code != 0) {
        (void)mya_fs_remove(out_path);
        mya_putln("pkg: download failed");
        return -1;
    }

    return 0;
}

static int parse_repo_line(char* line, pkg_repo_entry_t* out) {
    char* p;
    char* name;
    char* version;
    char* path;
    char* abi_text;

    if (!line || !out || !line[0] || line[0] == '#') {
        return -1;
    }

    p = line;
    name = p;
    while (*p && *p != '|') {
        p++;
    }
    if (*p != '|') {
        return -1;
    }
    *p++ = '\0';

    version = p;
    while (*p && *p != '|') {
        p++;
    }
    if (*p != '|') {
        return -1;
    }
    *p++ = '\0';

    path = p;
    while (*p && *p != '|') {
        p++;
    }

    abi_text = NULL;
    if (*p == '|') {
        *p++ = '\0';
        abi_text = p;
    }

    if (!is_valid_package_name(name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0) {
        return -1;
    }
    if (path[0] != '/' && !is_remote_url(path)) {
        return -1;
    }

    out->abi = MYAOS_ABI_VERSION;
    if (abi_text && abi_text[0]) {
        if (parse_u32(abi_text, &out->abi) != 0) {
            return -1;
        }
    }

    str_copy(out->name, name, sizeof(out->name));
    str_copy(out->version, version, sizeof(out->version));
    str_copy(out->path, path, sizeof(out->path));
    return 0;
}

static int parse_installed_line(char* line, pkg_installed_entry_t* out) {
    char* p;
    char* name;
    char* version;
    char* source;

    if (!line || !out || !line[0]) {
        return -1;
    }

    p = line;
    name = p;
    while (*p && *p != '|') {
        p++;
    }
    if (*p != '|') {
        return -1;
    }
    *p++ = '\0';

    version = p;
    while (*p && *p != '|') {
        p++;
    }
    if (*p != '|') {
        return -1;
    }
    *p++ = '\0';

    source = p;

    if (!is_valid_package_name(name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0) {
        return -1;
    }

    str_copy(out->name, name, sizeof(out->name));
    str_copy(out->version, version, sizeof(out->version));
    str_copy(out->source, source, sizeof(out->source));
    return 0;
}

static int load_repo_entries(const char* index_path, pkg_repo_entry_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t size = 0;
    char* cursor;
    uint32_t count = 0;
    char temp_index_path[MYAOS_PATH_MAX];
    const char* source_path = index_path;
    uint8_t temp_used = 0u;

    if (is_remote_url(index_path)) {
        if (download_url_to_temp_file(index_path, ".idx", temp_index_path, sizeof(temp_index_path)) != 0) {
            return -1;
        }
        source_path = temp_index_path;
        temp_used = 1u;
    }

    if (read_text_file(source_path, g_text_buf, sizeof(g_text_buf), &size) != 0) {
        if (temp_used) {
            (void)mya_fs_remove(temp_index_path);
        }
        return -1;
    }

    cursor = (char*)g_text_buf;
    while (1) {
        char* line = next_line(&cursor);
        if (!line) {
            break;
        }
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (count >= max_entries) {
            break;
        }
        if (parse_repo_line(line, &out[count]) == 0) {
            count++;
        }
    }

    if (out_count) {
        *out_count = count;
    }
    if (temp_used) {
        (void)mya_fs_remove(temp_index_path);
    }
    return 0;
}

static int save_repo_entries(const char* index_path, const pkg_repo_entry_t* entries, uint32_t count) {
    char out[PKG_TEXT_MAX];
    uint32_t pos = 0;

    out[0] = '\0';
    if (append_text(out, sizeof(out), &pos, "# MYAOS_REPO1\n") != 0) {
        return -1;
    }
    if (append_text(out, sizeof(out), &pos, "# name|version|package_path|abi\n") != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        char abi_text[16];
        mya_u32_to_dec(entries[i].abi, abi_text, sizeof(abi_text));
        if (append_text(out, sizeof(out), &pos, entries[i].name) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].version) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].path) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, abi_text) != 0 ||
            append_text(out, sizeof(out), &pos, "\n") != 0) {
            return -1;
        }
    }

    ensure_dir_tree("/repo");
    return mya_fs_write(index_path, out, pos);
}

static int load_installed_entries(pkg_installed_entry_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t size = 0;
    uint32_t count = 0;
    char* cursor;

    if (read_text_file(PKG_DB_FILE, g_text_buf, sizeof(g_text_buf), &size) != 0) {
        *out_count = 0;
        return 0;
    }

    cursor = (char*)g_text_buf;
    while (1) {
        char* line = next_line(&cursor);
        if (!line) {
            break;
        }
        if (!line[0] || line[0] == '#') {
            continue;
        }
        if (count >= max_entries) {
            break;
        }
        if (parse_installed_line(line, &out[count]) == 0) {
            count++;
        }
    }

    if (out_count) {
        *out_count = count;
    }
    return 0;
}

static int save_installed_entries(const pkg_installed_entry_t* entries, uint32_t count) {
    char out[PKG_TEXT_MAX];
    uint32_t pos = 0;

    out[0] = '\0';
    if (append_text(out, sizeof(out), &pos, "# MYAOS_INSTALLED1\n") != 0) {
        return -1;
    }
    if (append_text(out, sizeof(out), &pos, "# name|version|source\n") != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (append_text(out, sizeof(out), &pos, entries[i].name) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].version) != 0 ||
            append_text(out, sizeof(out), &pos, "|") != 0 ||
            append_text(out, sizeof(out), &pos, entries[i].source) != 0 ||
            append_text(out, sizeof(out), &pos, "\n") != 0) {
            return -1;
        }
    }

    ensure_dir_tree("/var");
    ensure_dir_tree(PKG_DB_DIR);
    return mya_fs_write(PKG_DB_FILE, out, pos);
}

static int write_meta_file(
    const char* pkg_name,
    const char* version,
    uint32_t abi,
    const char* source,
    uint32_t file_count,
    const char* files_blob
) {
    char path[MYAOS_PATH_MAX];
    char out[PKG_META_MAX];
    char value[16];
    uint32_t pos = 0;

    if (build_meta_path(pkg_name, path, sizeof(path)) != 0) {
        return -1;
    }

    out[0] = '\0';
    if (append_text(out, sizeof(out), &pos, "name=") != 0 ||
        append_text(out, sizeof(out), &pos, pkg_name) != 0 ||
        append_text(out, sizeof(out), &pos, "\nversion=") != 0 ||
        append_text(out, sizeof(out), &pos, version) != 0 ||
        append_text(out, sizeof(out), &pos, "\n") != 0) {
        return -1;
    }

    mya_u32_to_dec(abi, value, sizeof(value));
    if (append_text(out, sizeof(out), &pos, "abi=") != 0 ||
        append_text(out, sizeof(out), &pos, value) != 0 ||
        append_text(out, sizeof(out), &pos, "\n") != 0) {
        return -1;
    }

    mya_u32_to_dec(file_count, value, sizeof(value));
    if (append_text(out, sizeof(out), &pos, "files=") != 0 ||
        append_text(out, sizeof(out), &pos, value) != 0 ||
        append_text(out, sizeof(out), &pos, "\nsource=") != 0 ||
        append_text(out, sizeof(out), &pos, source) != 0 ||
        append_text(out, sizeof(out), &pos, "\n") != 0) {
        return -1;
    }

    if (files_blob && files_blob[0]) {
        if (append_text(out, sizeof(out), &pos, files_blob) != 0) {
            return -1;
        }
    }

    return mya_fs_write(path, out, pos);
}

static int record_installed_package(const char* pkg_name, const char* version, const char* source) {
    pkg_installed_entry_t entries[PKG_INSTALLED_MAX];
    uint32_t count = 0;
    uint8_t replaced = 0;

    if (load_installed_entries(entries, PKG_INSTALLED_MAX, &count) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, pkg_name)) {
            str_copy(entries[i].version, version, sizeof(entries[i].version));
            str_copy(entries[i].source, source, sizeof(entries[i].source));
            replaced = 1u;
            break;
        }
    }

    if (!replaced) {
        if (count >= PKG_INSTALLED_MAX) {
            return -1;
        }
        str_copy(entries[count].name, pkg_name, sizeof(entries[count].name));
        str_copy(entries[count].version, version, sizeof(entries[count].version));
        str_copy(entries[count].source, source, sizeof(entries[count].source));
        count++;
    }

    return save_installed_entries(entries, count);
}

static int remove_installed_package(const char* pkg_name) {
    pkg_installed_entry_t entries[PKG_INSTALLED_MAX];
    pkg_installed_entry_t keep[PKG_INSTALLED_MAX];
    uint32_t count = 0;
    uint32_t keep_count = 0;
    uint8_t removed = 0;

    if (load_installed_entries(entries, PKG_INSTALLED_MAX, &count) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, pkg_name)) {
            removed = 1u;
            continue;
        }
        if (keep_count < PKG_INSTALLED_MAX) {
            str_copy(keep[keep_count].name, entries[i].name, sizeof(keep[keep_count].name));
            str_copy(keep[keep_count].version, entries[i].version, sizeof(keep[keep_count].version));
            str_copy(keep[keep_count].source, entries[i].source, sizeof(keep[keep_count].source));
            keep_count++;
        }
    }

    if (!removed) {
        return -1;
    }
    return save_installed_entries(keep, keep_count);
}

static int installed_package_exists(const char* pkg_name) {
    pkg_installed_entry_t entries[PKG_INSTALLED_MAX];
    uint32_t count = 0;

    if (!pkg_name || load_installed_entries(entries, PKG_INSTALLED_MAX, &count) != 0) {
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, pkg_name)) {
            return 1;
        }
    }

    return 0;
}

static int install_single_file_checked(
    const char* path,
    const char* hex_payload,
    const char* sha256_expected,
    uint8_t dry_run,
    uint32_t* out_bytes
) {
    uint32_t byte_len = (uint32_t)str_len(hex_payload) / 2u;
    uint8_t empty = 0u;

    if (out_bytes) {
        *out_bytes = 0u;
    }
    if (!path || path[0] != '/' || !hex_payload) {
        return -1;
    }

    if (byte_len == 0u) {
        if (sha256_expected && sha256_expected[0]) {
            char empty_hash[PKG_SHA256_HEX_LEN + 1u];
            sha256_hex(NULL, 0u, empty_hash);
            if (!str_eq_ci(empty_hash, sha256_expected)) {
                return -1;
            }
        }

        if (dry_run) {
            if (out_bytes) {
                *out_bytes = 0u;
            }
            return 0;
        }
        ensure_parent_dirs(path);
        (void)mya_fs_touch(path);
        if (out_bytes) {
            *out_bytes = 0u;
        }
        return mya_fs_write(path, &empty, 0u);
    }

    {
        uint8_t* decoded = (uint8_t*)mya_mem_map(byte_len, MYAOS_MEM_MAP_WRITABLE);
        int rc;

        if (!decoded) {
            return -1;
        }

        rc = decode_hex(hex_payload, decoded, byte_len, NULL);
        if (rc == 0 && sha256_expected && sha256_expected[0]) {
            char actual_hash[PKG_SHA256_HEX_LEN + 1u];
            sha256_hex(decoded, byte_len, actual_hash);
            if (!str_eq_ci(actual_hash, sha256_expected)) {
                rc = -1;
            }
        }
        if (rc == 0 && !dry_run) {
            ensure_parent_dirs(path);
            rc = mya_fs_write(path, decoded, byte_len);
            if (rc != 0) {
                (void)mya_fs_touch(path);
                rc = mya_fs_write(path, decoded, byte_len);
            }
        }

        (void)mya_mem_unmap(decoded);
        if (rc == 0 && out_bytes) {
            *out_bytes = byte_len;
        }
        return rc;
    }
}

static int install_single_file_blob(
    const char* path,
    const uint8_t* data,
    uint32_t size,
    uint8_t dry_run,
    uint32_t* out_bytes
) {
    uint8_t empty = 0u;

    if (out_bytes) {
        *out_bytes = 0u;
    }
    if (!path || path[0] != '/' || (size != 0u && !data)) {
        return -1;
    }
    if (dry_run) {
        if (out_bytes) {
            *out_bytes = size;
        }
        return 0;
    }

    ensure_parent_dirs(path);
    if (size == 0u) {
        (void)mya_fs_touch(path);
        if (out_bytes) {
            *out_bytes = 0u;
        }
        return mya_fs_write(path, &empty, 0u);
    }

    if (mya_fs_write(path, data, size) != 0) {
        (void)mya_fs_touch(path);
        if (mya_fs_write(path, data, size) != 0) {
            return -1;
        }
    }
    if (out_bytes) {
        *out_bytes = size;
    }
    return 0;
}

static int build_boot_bin_exec_path(const char* exec_path, char* out, uint32_t out_size) {
    uint32_t pos = 0u;

    if (!exec_path || !out || out_size == 0u || !str_starts_with(exec_path, "/bin/")) {
        return -1;
    }
    out[0] = '\0';
    if (append_text(out, out_size, &pos, "/boot/bin/") != 0 ||
        append_text(out, out_size, &pos, exec_path + 5) != 0) {
        return -1;
    }
    return 0;
}

static int spawn_program_with_bin_fallback(
    const char* exec_path,
    int argc,
    const char* const* argv,
    int32_t* out_exit_code
) {
    char fallback[MYAOS_PATH_MAX];
    int32_t pid = -1;
    int rc;

    if (!exec_path || argc <= 0 || !argv) {
        return -1;
    }

    rc = mya_proc_spawn(exec_path, argc, argv, 0u, &pid);
    if (rc != 0 && build_boot_bin_exec_path(exec_path, fallback, sizeof(fallback)) == 0) {
        rc = mya_proc_spawn(fallback, argc, argv, 0u, &pid);
    }
    if (rc != 0 || pid <= 0) {
        return -1;
    }
    return wait_child_exit(pid, out_exit_code);
}

static int run_pkg_script(const char* phase, const char* script_body, uint8_t dry_run) {
    char script_path[MYAOS_PATH_MAX];
    const char* argv[3];
    int32_t exit_code = 0;
    uint32_t script_len;
    const char* tag = phase ? phase : "script";

    if (!script_body || !script_body[0]) {
        return 0;
    }

    mya_puts("pkg: ");
    mya_puts(dry_run ? "would run " : "running ");
    mya_puts(tag);
    mya_puts(" script\n");

    if (dry_run) {
        return 0;
    }

    if (build_download_temp_path(".pkg-script.sh", script_path, sizeof(script_path)) != 0) {
        return -1;
    }
    script_len = (uint32_t)str_len(script_body);
    if (mya_fs_write(script_path, script_body, script_len) != 0) {
        return -1;
    }

    argv[0] = "msh";
    argv[1] = script_path;
    argv[2] = NULL;
    if (spawn_program_with_bin_fallback("/bin/msh.elf", 2, argv, &exit_code) != 0) {
        (void)mya_fs_remove(script_path);
        return -1;
    }
    (void)mya_fs_remove(script_path);

    if (exit_code != 0) {
        mya_puts("pkg: ");
        mya_puts(tag);
        mya_puts(" script failed, exit=");
        mya_put_u32((uint32_t)exit_code);
        mya_puts("\n");
        return -1;
    }
    return 0;
}

static void mpkg_meta_init(mpkg_meta_t* meta) {
    if (!meta) {
        return;
    }
    meta->name[0] = '\0';
    meta->version[0] = '\0';
    meta->arch[0] = '\0';
    meta->description[0] = '\0';
    meta->script_pre[0] = '\0';
    meta->script_post[0] = '\0';
    meta->depends_count = 0u;
    meta->provides_count = 0u;
    meta->abi = MYAOS_ABI_VERSION;
    meta->abi_set = 0u;
    for (uint32_t i = 0u; i < MPKG_LIST_MAX; i++) {
        meta->depends[i][0] = '\0';
        meta->provides[i][0] = '\0';
    }
}

static int mpkg_copy_string(const uint8_t* data, uint32_t len, char* out, uint32_t out_size) {
    uint32_t i = 0u;

    if (!data || !out || out_size == 0u) {
        return -1;
    }
    if (len == 0u) {
        out[0] = '\0';
        return 0;
    }
    while (i < len && i + 1u < out_size && data[i] != 0u) {
        out[i] = (char)data[i];
        i++;
    }
    out[i] = '\0';
    return (i > 0u) ? 0 : -1;
}

static int mpkg_push_list_item(char out_items[MPKG_LIST_MAX][MYAOS_NAME_MAX], uint32_t* io_count, const char* item) {
    if (!out_items || !io_count || !item || !item[0]) {
        return -1;
    }
    if (*io_count >= MPKG_LIST_MAX) {
        return -1;
    }
    str_copy(out_items[*io_count], item, MYAOS_NAME_MAX);
    (*io_count)++;
    return 0;
}

static int mpkg_parse_string_list(
    const uint8_t* data,
    uint32_t len,
    char out_items[MPKG_LIST_MAX][MYAOS_NAME_MAX],
    uint32_t* io_count
) {
    uint32_t start = 0u;
    uint32_t i = 0u;

    if (!data || !out_items || !io_count) {
        return -1;
    }

    for (i = 0u; i <= len; i++) {
        uint8_t sep = (i == len) ? 1u : (uint8_t)(data[i] == 0u || data[i] == ',' || data[i] == '\n' || data[i] == '\r');
        if (!sep) {
            continue;
        }
        if (i > start) {
            uint32_t item_len = i - start;
            char item[MYAOS_NAME_MAX];
            if (item_len >= MYAOS_NAME_MAX) {
                return -1;
            }
            for (uint32_t j = 0u; j < item_len; j++) {
                item[j] = (char)data[start + j];
            }
            item[item_len] = '\0';
            if (mpkg_push_list_item(out_items, io_count, item) != 0) {
                return -1;
            }
        }
        start = i + 1u;
    }

    return 0;
}

static int mpkg_dependency_name(const char* spec, char* out_name, uint32_t out_size) {
    uint32_t i = 0u;

    if (!spec || !spec[0] || !out_name || out_size == 0u) {
        return -1;
    }
    while (spec[i] != '\0' &&
           ((spec[i] >= 'a' && spec[i] <= 'z') ||
            (spec[i] >= 'A' && spec[i] <= 'Z') ||
            (spec[i] >= '0' && spec[i] <= '9') ||
            spec[i] == '_' || spec[i] == '-' || spec[i] == '.')) {
        if (i + 1u >= out_size) {
            return -1;
        }
        out_name[i] = spec[i];
        i++;
    }
    out_name[i] = '\0';
    return (i > 0u) ? 0 : -1;
}

static int installed_has_package(const char* name) {
    pkg_installed_entry_t installed[PKG_INSTALLED_MAX];
    uint32_t count = 0u;

    if (!name || !name[0]) {
        return 0;
    }
    if (load_installed_entries(installed, PKG_INSTALLED_MAX, &count) != 0) {
        return 0;
    }
    for (uint32_t i = 0u; i < count; i++) {
        if (str_eq(installed[i].name, name)) {
            return 1;
        }
    }
    return 0;
}

static int mpkg_check_dependencies(const mpkg_meta_t* meta) {
    if (!meta) {
        return -1;
    }
    for (uint32_t i = 0u; i < meta->depends_count; i++) {
        char dep_name[MYAOS_NAME_MAX];

        if (mpkg_dependency_name(meta->depends[i], dep_name, sizeof(dep_name)) != 0) {
            mya_puts("pkg: bad dependency spec: ");
            mya_putln(meta->depends[i]);
            return -1;
        }
        if (!installed_has_package(dep_name)) {
            mya_puts("pkg: missing dependency: ");
            mya_putln(dep_name);
            return -1;
        }
    }
    return 0;
}

static int mpkg_parse_metadata_block(const uint8_t* block, uint32_t len, mpkg_meta_t* out_meta) {
    uint32_t pos = 0u;

    if (!block || !out_meta) {
        return -1;
    }
    mpkg_meta_init(out_meta);

    while (pos + 8u <= len) {
        uint16_t type = read_u16_le(block + pos);
        uint32_t tlv_len = read_u32_le(block + pos + 4u);
        const uint8_t* data = block + pos + 8u;

        if ((uint64_t)pos + 8u + (uint64_t)tlv_len > (uint64_t)len) {
            return -1;
        }

        switch (type) {
            case MPKG_META_NAME:
                if (mpkg_copy_string(data, tlv_len, out_meta->name, sizeof(out_meta->name)) != 0) {
                    return -1;
                }
                break;
            case MPKG_META_VERSION:
                if (mpkg_copy_string(data, tlv_len, out_meta->version, sizeof(out_meta->version)) != 0) {
                    return -1;
                }
                break;
            case MPKG_META_ARCH:
                (void)mpkg_copy_string(data, tlv_len, out_meta->arch, sizeof(out_meta->arch));
                break;
            case MPKG_META_ABI:
                if (tlv_len == 4u) {
                    out_meta->abi = read_u32_le(data);
                    out_meta->abi_set = 1u;
                } else {
                    char abi_text[16];
                    if (mpkg_copy_string(data, tlv_len, abi_text, sizeof(abi_text)) != 0 ||
                        parse_u32(abi_text, &out_meta->abi) != 0) {
                        return -1;
                    }
                    out_meta->abi_set = 1u;
                }
                break;
            case MPKG_META_DEPENDS:
                if (mpkg_parse_string_list(data, tlv_len, out_meta->depends, &out_meta->depends_count) != 0) {
                    return -1;
                }
                break;
            case MPKG_META_PROVIDES:
                if (mpkg_parse_string_list(data, tlv_len, out_meta->provides, &out_meta->provides_count) != 0) {
                    return -1;
                }
                break;
            case MPKG_META_DESCRIPTION:
                (void)mpkg_copy_string(data, tlv_len, out_meta->description, sizeof(out_meta->description));
                break;
            case MPKG_META_SCRIPT_PRE:
                (void)mpkg_copy_string(data, tlv_len, out_meta->script_pre, sizeof(out_meta->script_pre));
                break;
            case MPKG_META_SCRIPT_POST:
                (void)mpkg_copy_string(data, tlv_len, out_meta->script_post, sizeof(out_meta->script_post));
                break;
            default:
                break;
        }

        pos += 8u + tlv_len;
    }

    return (pos == len) ? 0 : -1;
}

static int mpkg_path_from_strtab(
    const uint8_t* strtab,
    uint32_t strtab_size,
    uint64_t path_off,
    char* out_path,
    uint32_t out_size
) {
    uint32_t i = 0u;

    if (!strtab || !out_path || out_size == 0u || path_off >= strtab_size) {
        return -1;
    }
    while (path_off + i < strtab_size && i + 1u < out_size && strtab[path_off + i] != 0u) {
        out_path[i] = (char)strtab[path_off + i];
        i++;
    }
    out_path[i] = '\0';

    if (path_off + i >= strtab_size || out_path[0] != '/') {
        return -1;
    }
    return 0;
}

static int append_pkg_meta_line(char* blob, uint32_t blob_size, uint32_t* io_pos, const char* key, const char* value) {
    if (!blob || !io_pos || !key || !key[0] || !value || !value[0]) {
        return 0;
    }
    if (append_text(blob, blob_size, io_pos, key) != 0 ||
        append_text(blob, blob_size, io_pos, "=") != 0 ||
        append_text(blob, blob_size, io_pos, value) != 0 ||
        append_text(blob, blob_size, io_pos, "\n") != 0) {
        return -1;
    }
    return 0;
}

static int install_package_mpkg_buffer_ex(
    const uint8_t* pkg,
    uint32_t size,
    const char* package_path,
    const char* source_tag,
    uint8_t dry_run
) {
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint64_t metadata_off;
    uint64_t filetable_off;
    uint64_t data_off;
    uint64_t pkg_size;
    uint32_t crc_expected;
    uint32_t crc_actual;
    mpkg_meta_t meta;
    uint32_t entry_count;
    uint32_t strtab_size;
    uint64_t entries_off;
    uint64_t entries_bytes;
    uint64_t strtab_off;
    uint64_t data_size;
    char files_blob[PKG_FILES_META_MAX];
    uint32_t files_blob_pos = 0u;
    uint32_t file_count = 0u;
    uint32_t kmod_loaded_count = 0u;
    uint32_t kmod_failed_count = 0u;

    if (!pkg || size < MPKG_HEADER_SIZE) {
        return -1;
    }
    if (!(pkg[0] == 'M' && pkg[1] == 'P' && pkg[2] == 'K' && pkg[3] == 'G')) {
        return -1;
    }

    version = read_u16_le(pkg + 4u);
    header_size = read_u16_le(pkg + 6u);
    flags = read_u32_le(pkg + 8u);
    metadata_off = read_u64_le(pkg + 12u);
    filetable_off = read_u64_le(pkg + 20u);
    data_off = read_u64_le(pkg + 28u);
    pkg_size = read_u64_le(pkg + 36u);
    crc_expected = read_u32_le(pkg + 44u);

    if (version != MPKG_VERSION || header_size < MPKG_HEADER_SIZE || pkg_size != (uint64_t)size) {
        mya_putln("pkg: bad MPKG header");
        return -1;
    }
    if ((flags & (MPKG_FLAG_COMPRESSED | MPKG_FLAG_DELTA)) != 0u) {
        mya_putln("pkg: MPKG compression/delta not supported yet");
        return -1;
    }
    if (metadata_off < header_size || filetable_off < metadata_off || data_off < filetable_off || data_off > (uint64_t)size) {
        mya_putln("pkg: bad MPKG offsets");
        return -1;
    }

    crc_actual = mpkg_crc32_compute(pkg, size);
    if (crc_actual != crc_expected) {
        mya_putln("pkg: MPKG crc32 mismatch");
        return -1;
    }

    if (mpkg_parse_metadata_block(pkg + metadata_off, (uint32_t)(filetable_off - metadata_off), &meta) != 0) {
        mya_putln("pkg: bad MPKG metadata");
        return -1;
    }
    if (!is_valid_package_name(meta.name) || parse_version(meta.version, (uint32_t[3]){ 0, 0, 0 }) != 0) {
        mya_putln("pkg: MPKG metadata is incomplete");
        return -1;
    }
    if (!meta.abi_set) {
        meta.abi = MYAOS_ABI_VERSION;
    }
    if (meta.abi > MYAOS_ABI_VERSION) {
        mya_puts("pkg: abi too new for system: ");
        mya_put_u32(meta.abi);
        mya_puts(" > ");
        mya_put_u32(MYAOS_ABI_VERSION);
        mya_puts("\n");
        return -1;
    }
    if (mpkg_check_dependencies(&meta) != 0) {
        return -1;
    }

    if (run_pkg_script("pre-install", meta.script_pre, dry_run) != 0) {
        return -1;
    }

    if (filetable_off + MPKG_FILETABLE_HEAD_SIZE > data_off) {
        mya_putln("pkg: bad MPKG file table");
        return -1;
    }
    entry_count = read_u32_le(pkg + filetable_off);
    strtab_size = read_u32_le(pkg + filetable_off + 4u);
    entries_off = filetable_off + MPKG_FILETABLE_HEAD_SIZE;
    entries_bytes = (uint64_t)entry_count * (uint64_t)MPKG_FILE_ENTRY_SIZE;
    strtab_off = entries_off + entries_bytes;
    if (entries_off > data_off || strtab_off > data_off || strtab_off + (uint64_t)strtab_size > data_off) {
        mya_putln("pkg: bad MPKG file table bounds");
        return -1;
    }

    files_blob[0] = '\0';
    if (append_pkg_meta_line(files_blob, sizeof(files_blob), &files_blob_pos, "format", "MPKG") != 0 ||
        append_pkg_meta_line(files_blob, sizeof(files_blob), &files_blob_pos, "arch", meta.arch) != 0 ||
        append_pkg_meta_line(files_blob, sizeof(files_blob), &files_blob_pos, "description", meta.description) != 0) {
        mya_putln("pkg: package metadata overflow");
        return -1;
    }
    for (uint32_t i = 0u; i < meta.depends_count; i++) {
        if (append_pkg_meta_line(files_blob, sizeof(files_blob), &files_blob_pos, "depends", meta.depends[i]) != 0) {
            mya_putln("pkg: package metadata overflow");
            return -1;
        }
    }
    for (uint32_t i = 0u; i < meta.provides_count; i++) {
        if (append_pkg_meta_line(files_blob, sizeof(files_blob), &files_blob_pos, "provides", meta.provides[i]) != 0) {
            mya_putln("pkg: package metadata overflow");
            return -1;
        }
    }

    data_size = (uint64_t)size - data_off;
    for (uint32_t i = 0u; i < entry_count; i++) {
        uint64_t rec_off = entries_off + (uint64_t)i * (uint64_t)MPKG_FILE_ENTRY_SIZE;
        uint64_t path_off = read_u64_le(pkg + rec_off);
        uint64_t file_data_off = read_u64_le(pkg + rec_off + 8u);
        uint64_t file_size = read_u64_le(pkg + rec_off + 16u);
        uint32_t file_flags = read_u32_le(pkg + rec_off + 24u);
        uint32_t file_mode = read_u32_le(pkg + rec_off + 28u);
        char path[MYAOS_PATH_MAX];
        uint32_t written_bytes = 0u;

        if (mpkg_path_from_strtab(pkg + strtab_off, strtab_size, path_off, path, sizeof(path)) != 0) {
            mya_putln("pkg: bad MPKG path table entry");
            return -1;
        }
        if (append_text(files_blob, sizeof(files_blob), &files_blob_pos, "file=") != 0 ||
            append_text(files_blob, sizeof(files_blob), &files_blob_pos, path) != 0 ||
            append_text(files_blob, sizeof(files_blob), &files_blob_pos, "\n") != 0) {
            mya_putln("pkg: package metadata overflow");
            return -1;
        }

        if ((file_flags & MPKG_FILE_FLAG_DIR) != 0u) {
            if (dry_run) {
                mya_puts("would mkdir ");
                mya_puts(path);
                mya_puts("\n");
            } else {
                ensure_dir_tree(path);
            }
            file_count++;
            continue;
        }

        if (file_data_off > data_size || file_size > data_size - file_data_off || file_size > 0xFFFFFFFFull) {
            mya_putln("pkg: bad MPKG data entry");
            return -1;
        }
        if (install_single_file_blob(path, pkg + data_off + file_data_off, (uint32_t)file_size, dry_run, &written_bytes) != 0) {
            mya_puts("pkg: failed to install ");
            mya_putln(path);
            return -1;
        }
        if (dry_run) {
            mya_puts("would write ");
            mya_puts(path);
            mya_puts(" bytes=");
            mya_put_u32(written_bytes);
            mya_puts("\n");
        } else if (file_mode != 0u) {
            (void)mya_fs_chmod(path, file_mode & 0777u);
        } else if ((file_flags & MPKG_FILE_FLAG_EXEC) != 0u) {
            (void)mya_fs_chmod(path, 0755u);
        }

        if (is_kernel_module_path(path)) {
            if (dry_run) {
                mya_puts("would load module ");
                mya_puts(path);
                mya_puts("\n");
            } else {
                int load_rc = mya_mod_load_file(path);
                if (load_rc == 0) {
                    kmod_loaded_count++;
                } else {
                    kmod_failed_count++;
                    mya_puts("pkg: warning: failed to load module ");
                    mya_puts(path);
                    mya_puts("\n");
                }
            }
        }

        file_count++;
    }

    if (file_count == 0u) {
        mya_putln("pkg: MPKG does not contain installable entries");
        return -1;
    }

    if (run_pkg_script("post-install", meta.script_post, dry_run) != 0) {
        return -1;
    }

    ensure_dir_tree("/var");
    ensure_dir_tree(PKG_DB_DIR);

    if (!dry_run) {
        if (write_meta_file(meta.name, meta.version, meta.abi, source_tag ? source_tag : package_path, file_count, files_blob) != 0) {
            mya_putln("pkg: failed to write metadata");
            return -1;
        }
        if (record_installed_package(meta.name, meta.version, source_tag ? source_tag : package_path) != 0) {
            mya_putln("pkg: failed to update installed index");
            return -1;
        }
    }

    if (kmod_loaded_count > 0u) {
        mya_puts("pkg: loaded ");
        mya_put_u32(kmod_loaded_count);
        mya_puts(" kernel module(s)\n");
    }
    if (kmod_failed_count > 0u) {
        mya_puts("pkg: warning: could not load ");
        mya_put_u32(kmod_failed_count);
        mya_puts(" kernel module(s)\n");
    }

    mya_puts(dry_run ? "would install " : "installed ");
    mya_puts(meta.name);
    mya_puts(" ");
    mya_puts(meta.version);
    mya_puts("\n");
    return 0;
}

static int install_package_text_buffer_ex(const char* package_path, const char* source_tag, uint8_t dry_run, uint32_t size) {
    char* cursor;
    char pkg_name[MYAOS_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char pending_file[MYAOS_PATH_MAX];
    char pending_sha256[PKG_SHA256_HEX_LEN + 1u];
    char files_blob[PKG_FILES_META_MAX];
    uint32_t files_blob_pos = 0;
    uint32_t abi = MYAOS_ABI_VERSION;
    uint8_t abi_set = 0;
    uint32_t file_count = 0;
    uint32_t hashed_count = 0;
    uint32_t missing_hash_count = 0;
    uint32_t kmod_loaded_count = 0;
    uint32_t kmod_failed_count = 0;

    if (!package_path) {
        return -1;
    }

    (void)size;

    pkg_name[0] = '\0';
    version[0] = '\0';
    pending_file[0] = '\0';
    pending_sha256[0] = '\0';
    files_blob[0] = '\0';

    cursor = (char*)g_pkg_file_buf;
    {
        char* line0 = next_line(&cursor);
        if (!line0 || !str_eq(line0, "MYAPKG1")) {
            mya_putln("pkg: bad package format");
            return -1;
        }
    }

    while (1) {
        char* line = next_line(&cursor);
        if (!line) {
            break;
        }
        if (!line[0] || line[0] == '#') {
            continue;
        }

        if (str_starts_with(line, "name=")) {
            str_copy(pkg_name, line + 5, sizeof(pkg_name));
            continue;
        }
        if (str_starts_with(line, "version=")) {
            str_copy(version, line + 8, sizeof(version));
            continue;
        }
        if (str_starts_with(line, "abi=")) {
            if (parse_u32(line + 4, &abi) != 0) {
                mya_putln("pkg: bad abi field");
                return -1;
            }
            abi_set = 1u;
            continue;
        }
        if (str_starts_with(line, "file=")) {
            str_copy(pending_file, line + 5, sizeof(pending_file));
            pending_sha256[0] = '\0';
            continue;
        }
        if (str_starts_with(line, "sha256=")) {
            if (!pending_file[0] || !is_valid_sha256_hex(line + 7)) {
                mya_putln("pkg: bad sha256 field");
                return -1;
            }
            str_copy(pending_sha256, line + 7, sizeof(pending_sha256));
            continue;
        }
        if (str_starts_with(line, "hex=")) {
            uint32_t written_bytes = 0u;
            if (!pending_file[0]) {
                mya_putln("pkg: payload without file field");
                return -1;
            }
            if (!can_append_file_meta_entry(
                    files_blob_pos,
                    (uint32_t)sizeof(files_blob),
                    pending_file,
                    pending_sha256[0] ? pending_sha256 : NULL
                )) {
                mya_putln("pkg: package metadata overflow");
                return -1;
            }
            if (install_single_file_checked(
                    pending_file,
                    line + 4,
                    pending_sha256[0] ? pending_sha256 : NULL,
                    dry_run,
                    &written_bytes
                ) != 0) {
                mya_puts("pkg: failed to install ");
                mya_putln(pending_file);
                return -1;
            }

            if (dry_run) {
                mya_puts("would write ");
                mya_puts(pending_file);
                mya_puts(" bytes=");
                mya_put_u32(written_bytes);
                mya_puts("\n");
            }

            if (append_text(files_blob, sizeof(files_blob), &files_blob_pos, "file=") != 0 ||
                append_text(files_blob, sizeof(files_blob), &files_blob_pos, pending_file) != 0 ||
                append_text(files_blob, sizeof(files_blob), &files_blob_pos, "\n") != 0) {
                mya_putln("pkg: package metadata overflow");
                return -1;
            }
            if (pending_sha256[0]) {
                if (append_text(files_blob, sizeof(files_blob), &files_blob_pos, "sha256=") != 0 ||
                    append_text(files_blob, sizeof(files_blob), &files_blob_pos, pending_sha256) != 0 ||
                    append_text(files_blob, sizeof(files_blob), &files_blob_pos, "\n") != 0) {
                    mya_putln("pkg: package metadata overflow");
                    return -1;
                }
            }

            file_count++;
            if (pending_sha256[0]) {
                hashed_count++;
            } else {
                missing_hash_count++;
            }

            if (is_kernel_module_path(pending_file)) {
                if (dry_run) {
                    mya_puts("would load module ");
                    mya_puts(pending_file);
                    mya_puts("\n");
                } else {
                    int load_rc = mya_mod_load_file(pending_file);
                    if (load_rc == 0) {
                        kmod_loaded_count++;
                    } else {
                        kmod_failed_count++;
                        mya_puts("pkg: warning: failed to load module ");
                        mya_puts(pending_file);
                        mya_puts("\n");
                    }
                }
            }

            pending_file[0] = '\0';
            pending_sha256[0] = '\0';
            continue;
        }
    }

    if (pending_file[0]) {
        mya_putln("pkg: missing payload for declared file entry");
        return -1;
    }

    if (!is_valid_package_name(pkg_name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0 || file_count == 0u) {
        mya_putln("pkg: package metadata is incomplete");
        return -1;
    }

    if (!abi_set) {
        abi = MYAOS_ABI_VERSION;
    }
    if (abi > MYAOS_ABI_VERSION) {
        mya_puts("pkg: abi too new for system: ");
        mya_put_u32(abi);
        mya_puts(" > ");
        mya_put_u32(MYAOS_ABI_VERSION);
        mya_puts("\n");
        return -1;
    }

    ensure_dir_tree("/var");
    ensure_dir_tree(PKG_DB_DIR);

    if (!dry_run) {
        if (write_meta_file(pkg_name, version, abi, source_tag ? source_tag : package_path, file_count, files_blob) != 0) {
            mya_putln("pkg: failed to write metadata");
            return -1;
        }
        if (record_installed_package(pkg_name, version, source_tag ? source_tag : package_path) != 0) {
            mya_putln("pkg: failed to update installed index");
            return -1;
        }
    }

    if (missing_hash_count > 0u) {
        mya_puts("pkg: warning: missing sha256 for ");
        mya_put_u32(missing_hash_count);
        mya_puts(" file(s)\n");
    }
    if (hashed_count > 0u) {
        mya_puts("pkg: verified sha256 for ");
        mya_put_u32(hashed_count);
        mya_puts(" file(s)\n");
    }
    if (kmod_loaded_count > 0u) {
        mya_puts("pkg: loaded ");
        mya_put_u32(kmod_loaded_count);
        mya_puts(" kernel module(s)\n");
    }
    if (kmod_failed_count > 0u) {
        mya_puts("pkg: warning: could not load ");
        mya_put_u32(kmod_failed_count);
        mya_puts(" kernel module(s)\n");
    }

    mya_puts(dry_run ? "would install " : "installed ");
    mya_puts(pkg_name);
    mya_puts(" ");
    mya_puts(version);
    mya_puts("\n");
    return 0;
}

static int install_package_file_ex(const char* package_path, const char* source_tag, uint8_t dry_run) {
    uint32_t size = 0u;

    if (!package_path || !package_path[0]) {
        return -1;
    }
    if (mya_fs_read(package_path, g_pkg_file_buf, sizeof(g_pkg_file_buf), &size) != 0 || size == 0u) {
        mya_putln("pkg: failed to read package file");
        return -1;
    }

    if (size >= 4u &&
        g_pkg_file_buf[0] == 'M' &&
        g_pkg_file_buf[1] == 'P' &&
        g_pkg_file_buf[2] == 'K' &&
        g_pkg_file_buf[3] == 'G') {
        return install_package_mpkg_buffer_ex(g_pkg_file_buf, size, package_path, source_tag, dry_run);
    }

    if (size + 1u > sizeof(g_pkg_file_buf)) {
        mya_putln("pkg: package file is too large");
        return -1;
    }
    g_pkg_file_buf[size] = '\0';
    return install_package_text_buffer_ex(package_path, source_tag, dry_run, size);
}

static int install_package_url_ex(const char* package_url, uint8_t dry_run) {
    char temp_pkg_path[MYAOS_PATH_MAX];
    int rc;

    if (!is_remote_url(package_url)) {
        return -1;
    }

    mya_puts("pkg: downloading ");
    mya_putln(package_url);

    if (download_url_to_temp_file(package_url, ".pkg", temp_pkg_path, sizeof(temp_pkg_path)) != 0) {
        return -1;
    }

    rc = install_package_file_ex(temp_pkg_path, package_url, dry_run);
    (void)mya_fs_remove(temp_pkg_path);
    return rc;
}

static int install_package_source_ex(const char* source, uint8_t dry_run) {
    if (!source || !source[0]) {
        return -1;
    }
    if (is_remote_url(source)) {
        return install_package_url_ex(source, dry_run);
    }
    return install_package_file_ex(source, source, dry_run);
}

static int cmd_list(void) {
    pkg_installed_entry_t entries[PKG_INSTALLED_MAX];
    uint32_t count = 0;

    if (load_installed_entries(entries, PKG_INSTALLED_MAX, &count) != 0 || count == 0u) {
        mya_putln("pkg: no installed packages");
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts(entries[i].name);
        mya_puts(" ");
        mya_puts(entries[i].version);
        mya_puts(" ");
        mya_puts(entries[i].source);
        mya_puts("\n");
    }
    return 0;
}

static int cmd_info(const char* name) {
    char path[MYAOS_PATH_MAX];
    uint32_t size = 0;

    if (build_meta_path(name, path, sizeof(path)) != 0) {
        mya_putln("pkg: bad package name");
        return 1;
    }
    if (read_text_file(path, g_text_buf, sizeof(g_text_buf), &size) != 0) {
        mya_putln("pkg: package not found");
        return 1;
    }

    mya_puts((const char*)g_text_buf);
    if (size > 0u && g_text_buf[size - 1u] != '\n') {
        mya_puts("\n");
    }
    return 0;
}

static int cmd_avail(const char* index_path) {
    pkg_repo_entry_t entries[PKG_REPO_MAX];
    uint32_t count = 0;

    if (load_repo_entries(index_path, entries, PKG_REPO_MAX, &count) != 0) {
        mya_putln("pkg: repo index read failed");
        return 1;
    }

    if (count == 0u) {
        mya_putln("pkg: repo is empty");
        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts(entries[i].name);
        mya_puts(" ");
        mya_puts(entries[i].version);
        mya_puts(" ");
        mya_puts(entries[i].path);
        mya_puts(" abi=");
        mya_put_u32(entries[i].abi);
        mya_puts("\n");
    }

    return 0;
}

static int cmd_install_from_ex(const char* name, const char* index_path, uint8_t dry_run) {
    pkg_repo_entry_t entries[PKG_REPO_MAX];
    uint32_t count = 0;
    int32_t best_idx = -1;

    if (!is_valid_package_name(name)) {
        mya_putln("pkg: bad package name");
        return 1;
    }
    if (load_repo_entries(index_path, entries, PKG_REPO_MAX, &count) != 0) {
        mya_putln("pkg: repo index read failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!str_eq(entries[i].name, name)) {
            continue;
        }
        if (entries[i].abi > MYAOS_ABI_VERSION) {
            continue;
        }
        if (best_idx < 0 || compare_version(entries[best_idx].version, entries[i].version) < 0) {
            best_idx = (int32_t)i;
        }
    }

    if (best_idx < 0) {
        mya_putln("pkg: package not found in repo");
        return 1;
    }

    return install_package_source_ex(entries[best_idx].path, dry_run) == 0 ? 0 : 1;
}

static int cmd_install_from(const char* name, const char* index_path) {
    return cmd_install_from_ex(name, index_path, 0u);
}

static int cmd_upgrade_ex(const char* index_path, uint8_t dry_run) {
    pkg_installed_entry_t installed[PKG_INSTALLED_MAX];
    pkg_repo_entry_t repo[PKG_REPO_MAX];
    uint32_t installed_count = 0;
    uint32_t repo_count = 0;
    uint32_t updated = 0;
    uint32_t failed = 0;

    if (load_installed_entries(installed, PKG_INSTALLED_MAX, &installed_count) != 0 || installed_count == 0u) {
        mya_putln("pkg: no installed packages");
        return 0;
    }
    if (load_repo_entries(index_path, repo, PKG_REPO_MAX, &repo_count) != 0) {
        mya_putln("pkg: repo index read failed");
        return 1;
    }

    for (uint32_t i = 0; i < installed_count; i++) {
        int32_t best_idx = -1;

        for (uint32_t j = 0; j < repo_count; j++) {
            if (!str_eq(repo[j].name, installed[i].name)) {
                continue;
            }
            if (repo[j].abi > MYAOS_ABI_VERSION) {
                continue;
            }
            if (compare_version(repo[j].version, installed[i].version) <= 0) {
                continue;
            }
            if (best_idx < 0 || compare_version(repo[best_idx].version, repo[j].version) < 0) {
                best_idx = (int32_t)j;
            }
        }

        if (best_idx < 0) {
            continue;
        }

        mya_puts("upgrading ");
        mya_puts(installed[i].name);
        mya_puts(" ");
        mya_puts(installed[i].version);
        mya_puts(" -> ");
        mya_puts(repo[best_idx].version);
        mya_puts("\n");

        if (install_package_source_ex(repo[best_idx].path, dry_run) == 0) {
            updated++;
        } else {
            failed++;
        }
    }

    mya_puts(dry_run ? "pkg: would upgrade " : "pkg: upgraded ");
    mya_put_u32(updated);
    mya_puts(" package(s)");
    if (failed) {
        mya_puts(", failed ");
        mya_put_u32(failed);
    }
    mya_puts("\n");

    return failed ? 1 : 0;
}

static int cmd_upgrade(const char* index_path) {
    return cmd_upgrade_ex(index_path, 0u);
}

static int cmd_downgrade(const char* name, const char* version, const char* index_path, uint8_t dry_run) {
    pkg_installed_entry_t installed[PKG_INSTALLED_MAX];
    pkg_repo_entry_t repo[PKG_REPO_MAX];
    uint32_t installed_count = 0;
    uint32_t repo_count = 0;
    int32_t installed_idx = -1;
    int32_t repo_idx = -1;

    if (!is_valid_package_name(name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0) {
        mya_putln("pkg: bad downgrade arguments");
        return 1;
    }
    if (load_installed_entries(installed, PKG_INSTALLED_MAX, &installed_count) != 0 || installed_count == 0u) {
        mya_putln("pkg: no installed packages");
        return 1;
    }
    for (uint32_t i = 0; i < installed_count; i++) {
        if (str_eq(installed[i].name, name)) {
            installed_idx = (int32_t)i;
            break;
        }
    }
    if (installed_idx < 0) {
        mya_putln("pkg: package is not installed");
        return 1;
    }
    if (compare_version(version, installed[installed_idx].version) >= 0) {
        mya_putln("pkg: target version is not older than installed");
        return 1;
    }

    if (load_repo_entries(index_path, repo, PKG_REPO_MAX, &repo_count) != 0) {
        mya_putln("pkg: repo index read failed");
        return 1;
    }
    for (uint32_t i = 0; i < repo_count; i++) {
        if (repo[i].abi > MYAOS_ABI_VERSION) {
            continue;
        }
        if (str_eq(repo[i].name, name) && str_eq(repo[i].version, version)) {
            repo_idx = (int32_t)i;
            break;
        }
    }
    if (repo_idx < 0) {
        mya_putln("pkg: requested version is not in repo");
        return 1;
    }

    mya_puts(dry_run ? "would downgrade " : "downgrading ");
    mya_puts(name);
    mya_puts(" ");
    mya_puts(installed[installed_idx].version);
    mya_puts(" -> ");
    mya_puts(version);
    mya_puts("\n");

    return install_package_source_ex(repo[repo_idx].path, dry_run) == 0 ? 0 : 1;
}

static int cmd_remove_ex(const char* name, uint8_t dry_run) {
    char meta_path[MYAOS_PATH_MAX];
    uint32_t size = 0;
    uint8_t found_meta = 0u;
    uint8_t found_file = 0u;
    uint8_t found_installed = 0u;
    char* cursor;

    if (!is_valid_package_name(name)) {
        mya_putln("pkg: bad package name");
        return 1;
    }

    if (build_meta_path(name, meta_path, sizeof(meta_path)) == 0 &&
        read_text_file(meta_path, g_text_buf, sizeof(g_text_buf), &size) == 0) {
        found_meta = 1u;
    }

    found_installed = (uint8_t)installed_package_exists(name);
    if (!found_meta && !found_installed) {
        mya_putln("pkg: package not installed");
        return 1;
    }

    if (dry_run) {
        mya_puts("would remove ");
        mya_putln(name);
        if (found_meta) {
            mya_puts("meta ");
            mya_putln(meta_path);
            cursor = (char*)g_text_buf;
            while (1) {
                char* line = next_line(&cursor);
                if (!line) {
                    break;
                }
                if (str_starts_with(line, "file=")) {
                    found_file = 1u;
                    mya_puts("file ");
                    mya_putln(line + 5);
                }
            }
        }
        if (found_installed) {
            mya_putln("installed-db update");
        }
        if (!found_file) {
            mya_putln("note: no payload file entries recorded");
        }
        return 0;
    }

    if (found_meta) {
        cursor = (char*)g_text_buf;
        while (1) {
            char* line = next_line(&cursor);
            if (!line) {
                break;
            }
            if (str_starts_with(line, "file=")) {
                found_file = 1u;
                (void)mya_fs_remove(line + 5);
            }
        }
        (void)mya_fs_remove(meta_path);
    }

    if (found_installed) {
        (void)remove_installed_package(name);
    }

    mya_puts("removed ");
    mya_puts(name);
    mya_puts("\n");
    return 0;
}

static int cmd_remove(const char* name) {
    return cmd_remove_ex(name, 0u);
}

static int cmd_search(const char* pattern, const char* index_path) {
    pkg_installed_entry_t installed[PKG_INSTALLED_MAX];
    pkg_repo_entry_t repo[PKG_REPO_MAX];
    uint32_t installed_count = 0;
    uint32_t repo_count = 0;
    uint8_t found = 0u;

    if (!pattern || !pattern[0]) {
        mya_putln("pkg: search pattern is required");
        return 1;
    }

    if (load_installed_entries(installed, PKG_INSTALLED_MAX, &installed_count) == 0) {
        for (uint32_t i = 0; i < installed_count; i++) {
            if (!str_contains_ci(installed[i].name, pattern) &&
                !str_contains_ci(installed[i].source, pattern)) {
                continue;
            }
            mya_puts("installed ");
            mya_puts(installed[i].name);
            mya_puts(" ");
            mya_puts(installed[i].version);
            mya_puts(" ");
            mya_puts(installed[i].source);
            mya_puts("\n");
            found = 1u;
        }
    }

    if (load_repo_entries(index_path, repo, PKG_REPO_MAX, &repo_count) == 0) {
        for (uint32_t i = 0; i < repo_count; i++) {
            if (!str_contains_ci(repo[i].name, pattern) &&
                !str_contains_ci(repo[i].path, pattern)) {
                continue;
            }
            mya_puts("repo ");
            mya_puts(repo[i].name);
            mya_puts(" ");
            mya_puts(repo[i].version);
            mya_puts(" ");
            mya_puts(repo[i].path);
            mya_puts(" abi=");
            mya_put_u32(repo[i].abi);
            mya_puts("\n");
            found = 1u;
        }
    }

    if (!found) {
        mya_putln("pkg: no matches");
    }
    return 0;
}

static int cmd_repo_add(
    const char* index_path,
    const char* name,
    const char* version,
    const char* package_path,
    const char* abi_text
) {
    pkg_repo_entry_t entries[PKG_REPO_MAX];
    uint32_t count = 0;
    uint32_t abi = MYAOS_ABI_VERSION;
    int32_t existing_idx = -1;

    if (!is_valid_package_name(name) || parse_version(version, (uint32_t[3]){ 0, 0, 0 }) != 0 ||
        !package_path || (package_path[0] != '/' && !is_remote_url(package_path))) {
        mya_putln("pkg: invalid repo-add args");
        return 1;
    }

    if (abi_text && abi_text[0] && parse_u32(abi_text, &abi) != 0) {
        mya_putln("pkg: invalid abi for repo-add");
        return 1;
    }

    if (load_repo_entries(index_path, entries, PKG_REPO_MAX, &count) != 0) {
        count = 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (str_eq(entries[i].name, name)) {
            existing_idx = (int32_t)i;
            break;
        }
    }

    if (existing_idx >= 0) {
        str_copy(entries[existing_idx].version, version, sizeof(entries[existing_idx].version));
        str_copy(entries[existing_idx].path, package_path, sizeof(entries[existing_idx].path));
        entries[existing_idx].abi = abi;
    } else {
        if (count >= PKG_REPO_MAX) {
            mya_putln("pkg: repo full");
            return 1;
        }
        str_copy(entries[count].name, name, sizeof(entries[count].name));
        str_copy(entries[count].version, version, sizeof(entries[count].version));
        str_copy(entries[count].path, package_path, sizeof(entries[count].path));
        entries[count].abi = abi;
        count++;
    }

    if (save_repo_entries(index_path, entries, count) != 0) {
        mya_putln("pkg: failed to write repo index");
        return 1;
    }

    mya_putln("pkg: repo updated");
    return 0;
}

static int cmd_repo_init(const char* index_path) {
    pkg_repo_entry_t none[1];
    if (save_repo_entries(index_path, none, 0) != 0) {
        mya_putln("pkg: failed to initialize repo index");
        return 1;
    }
    mya_putln("pkg: repo initialized");
    return 0;
}

static void print_usage(void) {
    mya_putln("usage:");
    mya_putln("  pkg install <package_path_or_url>");
    mya_putln("  pkg install --dry-run <package_path_or_url>");
    mya_putln("  pkg remove <name>");
    mya_putln("  pkg search <pattern> [repo_index]");
    mya_putln("  pkg list");
    mya_putln("  pkg info <name>");
    mya_putln("  pkg avail [repo_index]");
    mya_putln("  pkg install-from <name> [repo_index]");
    mya_putln("  pkg upgrade [repo_index]");
    mya_putln("  pkg downgrade <name> <version> [repo_index]");
    mya_putln("  pkg dry-run <install|install-from|upgrade|downgrade|remove> ...");
    mya_putln("  pkg repo-init [repo_index]");
    mya_putln("  pkg repo-add <repo_index> <name> <version> <package_path_or_url> [abi]");
}

int program_main(int argc, char** argv) {
    const char* cmd;
    const char* index_path = PKG_INDEX_DEFAULT;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    cmd = argv[1];

    if (str_eq(cmd, "install")) {
        const char* source = NULL;
        uint8_t dry_run = 0u;

        if (argc < 3) {
            mya_putln("pkg: install requires package path or URL");
            return 1;
        }
        if (str_eq(argv[2], "--dry-run")) {
            if (argc < 4) {
                mya_putln("pkg: install --dry-run requires package path or URL");
                return 1;
            }
            source = argv[3];
            dry_run = 1u;
        } else {
            source = argv[2];
        }
        return install_package_source_ex(source, dry_run) == 0 ? 0 : 1;
    }

    if (str_eq(cmd, "remove")) {
        if (argc < 3) {
            mya_putln("pkg: remove requires package name");
            return 1;
        }
        return cmd_remove(argv[2]);
    }

    if (str_eq(cmd, "search")) {
        if (argc < 3) {
            mya_putln("pkg: search requires pattern");
            return 1;
        }
        if (argc >= 4) {
            index_path = argv[3];
        }
        return cmd_search(argv[2], index_path);
    }

    if (str_eq(cmd, "list")) {
        return cmd_list();
    }

    if (str_eq(cmd, "info")) {
        if (argc < 3) {
            mya_putln("pkg: info requires package name");
            return 1;
        }
        return cmd_info(argv[2]);
    }

    if (str_eq(cmd, "avail")) {
        if (argc >= 3) {
            index_path = argv[2];
        }
        return cmd_avail(index_path);
    }

    if (str_eq(cmd, "install-from")) {
        if (argc < 3) {
            mya_putln("pkg: install-from requires package name");
            return 1;
        }
        if (argc >= 4) {
            index_path = argv[3];
        }
        return cmd_install_from(argv[2], index_path);
    }

    if (str_eq(cmd, "upgrade")) {
        if (argc >= 3) {
            index_path = argv[2];
        }
        return cmd_upgrade(index_path);
    }

    if (str_eq(cmd, "downgrade")) {
        if (argc < 4) {
            mya_putln("pkg: downgrade requires <name> <version> [repo_index]");
            return 1;
        }
        if (argc >= 5) {
            index_path = argv[4];
        }
        return cmd_downgrade(argv[2], argv[3], index_path, 0u);
    }

    if (str_eq(cmd, "dry-run")) {
        if (argc < 3) {
            mya_putln("pkg: dry-run requires subcommand");
            return 1;
        }
        if (str_eq(argv[2], "install")) {
            if (argc < 4) {
                mya_putln("pkg: dry-run install requires package path or URL");
                return 1;
            }
            return install_package_source_ex(argv[3], 1u) == 0 ? 0 : 1;
        }
        if (str_eq(argv[2], "install-from")) {
            if (argc < 4) {
                mya_putln("pkg: dry-run install-from requires package name");
                return 1;
            }
            if (argc >= 5) {
                index_path = argv[4];
            }
            return cmd_install_from_ex(argv[3], index_path, 1u);
        }
        if (str_eq(argv[2], "upgrade")) {
            if (argc >= 4) {
                index_path = argv[3];
            }
            return cmd_upgrade_ex(index_path, 1u);
        }
        if (str_eq(argv[2], "downgrade")) {
            if (argc < 5) {
                mya_putln("pkg: dry-run downgrade requires <name> <version> [repo_index]");
                return 1;
            }
            if (argc >= 6) {
                index_path = argv[5];
            }
            return cmd_downgrade(argv[3], argv[4], index_path, 1u);
        }
        if (str_eq(argv[2], "remove")) {
            if (argc < 4) {
                mya_putln("pkg: dry-run remove requires package name");
                return 1;
            }
            return cmd_remove_ex(argv[3], 1u);
        }
        mya_putln("pkg: unsupported dry-run subcommand");
        return 1;
    }

    if (str_eq(cmd, "repo-init")) {
        if (argc >= 3) {
            index_path = argv[2];
        }
        return cmd_repo_init(index_path);
    }

    if (str_eq(cmd, "repo-add")) {
        if (argc < 6) {
            mya_putln("pkg: repo-add requires <repo_index> <name> <version> <package_path_or_url> [abi]");
            return 1;
        }
        return cmd_repo_add(argv[2], argv[3], argv[4], argv[5], argc >= 7 ? argv[6] : "");
    }

    print_usage();
    return 1;
}
