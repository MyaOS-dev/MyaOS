#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    int32_t read_fd = -1;
    int32_t write_fd = -1;
    uint32_t written = 0;
    uint32_t read = 0;
    char text[] = "pipe-ok";
    char out[32];

    (void)argc;
    (void)argv;

    if (mya_pipe_create(&read_fd, &write_fd) != 0) {
        mya_putln("pipecheck: create failed");
        return 1;
    }
    if (mya_pipe_write(write_fd, text, 7u, &written) != 0 || written == 0u) {
        mya_putln("pipecheck: write failed");
        return 1;
    }
    if (mya_pipe_read(read_fd, out, sizeof(out) - 1u, &read) != 0) {
        mya_putln("pipecheck: read failed");
        return 1;
    }
    out[read] = '\0';

    mya_puts("pipecheck: ");
    mya_puts(out);
    mya_puts("\n");

    (void)mya_pipe_close(read_fd);
    (void)mya_pipe_close(write_fd);
    return 0;
}
