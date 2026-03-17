#include "../lib/myaos.h"

#define SHM_NAME "shmcheck"
#define SHM_SIZE 128u

static int child_mode(void) {
    int32_t id = -1;
    char buf[SHM_SIZE];
    uint32_t read = 0;
    const char* reply = "child-ok";
    uint32_t written = 0;

    if (mya_shm_open(SHM_NAME, &id) != 0) {
        mya_putln("shmcheck: child open failed");
        return 1;
    }
    if (mya_shm_read(id, 0u, buf, sizeof(buf) - 1u, &read) != 0) {
        mya_putln("shmcheck: child read failed");
        return 1;
    }
    buf[read] = '\0';

    mya_puts("child read: ");
    mya_puts(buf);
    mya_puts("\n");

    if (mya_shm_write(id, 0u, reply, (uint32_t)mya_strlen(reply), &written) != 0 || written == 0u) {
        mya_putln("shmcheck: child write failed");
        return 1;
    }
    (void)mya_shm_close(id);
    return 0;
}

int program_main(int argc, char** argv) {
    int32_t id = -1;
    int32_t pid = -1;
    int32_t exit_code = 0;
    char buf[SHM_SIZE];
    uint32_t read = 0;
    uint32_t written = 0;
    const char* msg = "parent->child";
    const char* child_argv[] = {"/bin/shmcheck.elf", "child", NULL};

    if (argc > 1 && mya_streq(argv[1], "child")) {
        return child_mode();
    }

    if (mya_shm_create(SHM_NAME, SHM_SIZE, &id) != 0) {
        mya_putln("shmcheck: create failed");
        return 1;
    }
    if (mya_shm_write(id, 0u, msg, (uint32_t)mya_strlen(msg), &written) != 0 || written == 0u) {
        mya_putln("shmcheck: write failed");
        return 1;
    }

    if (mya_proc_spawn("/bin/shmcheck.elf", 2, child_argv, 0u, &pid) != 0) {
        mya_putln("shmcheck: spawn child failed");
        return 1;
    }
    if (mya_proc_wait(pid, &exit_code) != 0) {
        mya_putln("shmcheck: wait failed");
        return 1;
    }

    if (mya_shm_read(id, 0u, buf, sizeof(buf) - 1u, &read) != 0) {
        mya_putln("shmcheck: read-back failed");
        return 1;
    }
    buf[read] = '\0';

    mya_puts("parent read: ");
    mya_puts(buf);
    mya_puts("\n");
    (void)mya_shm_close(id);
    return exit_code == 0 ? 0 : 1;
}
