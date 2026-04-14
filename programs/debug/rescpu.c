#include "../lib/myaos.h"

#define RESCPU_MAX_PROCS 64u

typedef struct {
    uint32_t pid;
    uint64_t ticks;
    char name[MYAOS_PROC_NAME_MAX];
} sample_t;

typedef struct {
    uint32_t pid;
    uint64_t delta;
    char name[MYAOS_PROC_NAME_MAX];
} row_t;

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

static int sample(sample_t* out, uint32_t max_count, uint32_t* out_count) {
    myaos_proc_info_t info[RESCPU_MAX_PROCS];
    uint32_t count = 0;

    if (!out || !out_count) {
        return -1;
    }

    if (mya_proc_list(info, RESCPU_MAX_PROCS, &count) != 0) {
        return -1;
    }

    if (count > max_count) {
        count = max_count;
    }

    for (uint32_t i = 0; i < count; i++) {
        out[i].pid = info[i].pid;
        out[i].ticks = info[i].cpu_ticks_used;
        str_copy(out[i].name, info[i].name, sizeof(out[i].name));
    }

    *out_count = count;
    return 0;
}

static uint64_t ticks_before(uint32_t pid, const sample_t* s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (s[i].pid == pid) {
            return s[i].ticks;
        }
    }
    return 0u;
}

static void sort_rows(row_t* rows, uint32_t n) {
    if (!rows) {
        return;
    }

    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1u; j < n; j++) {
            if (rows[j].delta > rows[i].delta) {
                row_t tmp;
                tmp.pid = rows[i].pid;
                tmp.delta = rows[i].delta;
                str_copy(tmp.name, rows[i].name, sizeof(tmp.name));

                rows[i].pid = rows[j].pid;
                rows[i].delta = rows[j].delta;
                str_copy(rows[i].name, rows[j].name, sizeof(rows[i].name));

                rows[j].pid = tmp.pid;
                rows[j].delta = tmp.delta;
                str_copy(rows[j].name, tmp.name, sizeof(rows[j].name));
            }
        }
    }
}

int program_main(int argc, char** argv) {
    sample_t s0[RESCPU_MAX_PROCS];
    sample_t s1[RESCPU_MAX_PROCS];
    row_t rows[RESCPU_MAX_PROCS];
    uint32_t n0 = 0;
    uint32_t n1 = 0;
    uint32_t row_count = 0;
    uint64_t total = 0;
    uint64_t wait_ticks = 250u;

    if (argc >= 2) {
        uint64_t parsed = 0;
        if (mya_strto_u64(argv[1], &parsed) == 0 && parsed > 0u) {
            wait_ticks = parsed;
        }
    }

    if (sample(s0, RESCPU_MAX_PROCS, &n0) != 0) {
        mya_putln("rescpu: sample0 failed");
        return 1;
    }

    mya_proc_sleep(wait_ticks);

    if (sample(s1, RESCPU_MAX_PROCS, &n1) != 0) {
        mya_putln("rescpu: sample1 failed");
        return 1;
    }

    for (uint32_t i = 0; i < n1 && row_count < RESCPU_MAX_PROCS; i++) {
        uint64_t before = ticks_before(s1[i].pid, s0, n0);
        uint64_t delta = (s1[i].ticks >= before) ? (s1[i].ticks - before) : 0u;

        rows[row_count].pid = s1[i].pid;
        rows[row_count].delta = delta;
        str_copy(rows[row_count].name, s1[i].name, sizeof(rows[row_count].name));
        total += delta;
        row_count++;
    }

    sort_rows(rows, row_count);

    mya_puts("pid cpu% ticks name\n");
    for (uint32_t i = 0; i < row_count; i++) {
        uint64_t pct10 = (total == 0u) ? 0u : (rows[i].delta * 1000u) / total;

        mya_put_u32(rows[i].pid);
        mya_puts(" ");
        mya_put_u32((uint32_t)(pct10 / 10u));
        mya_puts(".");
        mya_put_u32((uint32_t)(pct10 % 10u));
        mya_puts(" ");
        mya_put_u64(rows[i].delta);
        mya_puts(" ");
        mya_putln(rows[i].name);
    }

    return 0;
}
