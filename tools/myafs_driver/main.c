#include "myafs_driver.h"

#include <stdio.h>
#include <string.h>

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

static void print_usage(const char* argv0) {
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s init IMAGE [LABEL]\n", argv0);
    fprintf(stderr, "  %s list IMAGE [PATH]\n", argv0);
    fprintf(stderr, "  %s mkdir IMAGE PATH\n", argv0);
    fprintf(stderr, "  %s touch IMAGE PATH\n", argv0);
    fprintf(stderr, "  %s write IMAGE PATH TEXT...\n", argv0);
    fprintf(stderr, "  %s cat IMAGE PATH\n", argv0);
    fprintf(stderr, "  %s del IMAGE PATH\n", argv0);
}

static int load_fs(myafs_driver_t* fs, const char* image_path) {
    if (myafs_driver_load(fs, image_path) != 0) {
        fprintf(stderr, "failed to load image: %s\n", image_path);
        return -1;
    }
    return 0;
}

static int save_fs(const myafs_driver_t* fs, const char* image_path) {
    if (myafs_driver_save(fs, image_path) != 0) {
        fprintf(stderr, "failed to save image: %s\n", image_path);
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    const char* cmd;
    const char* image;
    myafs_driver_t fs;

    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    cmd = argv[1];
    image = argv[2];

    if (str_eq(cmd, "init")) {
        const char* label = (argc >= 4) ? argv[3] : "myafs";
        if (myafs_driver_init(&fs, label) != 0) {
            fprintf(stderr, "init failed\n");
            return 1;
        }
        return save_fs(&fs, image) == 0 ? 0 : 1;
    }

    if (load_fs(&fs, image) != 0) {
        return 1;
    }

    if (str_eq(cmd, "list") || str_eq(cmd, "ls")) {
        const char* path = (argc >= 4) ? argv[3] : "/";
        myafs_driver_dirent_t entries[MYAFS_DRIVER_MAX_NODES];
        uint32_t count = 0;

        if (myafs_driver_list(&fs, path, entries, MYAFS_DRIVER_MAX_NODES, &count) != 0) {
            fprintf(stderr, "list failed for %s\n", path);
            return 1;
        }

        for (uint32_t i = 0; i < count; i++) {
            if (entries[i].is_dir) {
                printf("%s/\n", entries[i].name);
            } else {
                printf("%s %u\n", entries[i].name, entries[i].size);
            }
        }
        return 0;
    }

    if (str_eq(cmd, "mkdir")) {
        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }
        if (myafs_driver_mkdir(&fs, argv[3]) != 0) {
            fprintf(stderr, "mkdir failed\n");
            return 1;
        }
        return save_fs(&fs, image) == 0 ? 0 : 1;
    }

    if (str_eq(cmd, "touch")) {
        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }
        if (myafs_driver_touch(&fs, argv[3]) != 0) {
            fprintf(stderr, "touch failed\n");
            return 1;
        }
        return save_fs(&fs, image) == 0 ? 0 : 1;
    }

    if (str_eq(cmd, "write")) {
        uint8_t buffer[MYAFS_DRIVER_FILE_MAX];
        uint32_t pos = 0;

        if (argc < 5) {
            print_usage(argv[0]);
            return 1;
        }

        for (int i = 4; i < argc; i++) {
            const char* text = argv[i];
            for (size_t j = 0; text[j] && pos + 1 < MYAFS_DRIVER_FILE_MAX; j++) {
                buffer[pos++] = (uint8_t)text[j];
            }
            if (i + 1 < argc && pos + 1 < MYAFS_DRIVER_FILE_MAX) {
                buffer[pos++] = ' ';
            }
        }

        if (myafs_driver_write(&fs, argv[3], buffer, pos) != 0) {
            fprintf(stderr, "write failed\n");
            return 1;
        }
        return save_fs(&fs, image) == 0 ? 0 : 1;
    }

    if (str_eq(cmd, "cat")) {
        uint8_t buffer[MYAFS_DRIVER_FILE_MAX + 1u];
        uint32_t size = 0;

        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }

        if (myafs_driver_read(&fs, argv[3], buffer, MYAFS_DRIVER_FILE_MAX, &size) != 0) {
            fprintf(stderr, "cat failed\n");
            return 1;
        }

        buffer[size] = 0;
        printf("%s", (const char*)buffer);
        if (size == 0 || buffer[size - 1] != '\n') {
            printf("\n");
        }
        return 0;
    }

    if (str_eq(cmd, "del") || str_eq(cmd, "rm") || str_eq(cmd, "remove")) {
        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }
        if (myafs_driver_remove(&fs, argv[3]) != 0) {
            fprintf(stderr, "remove failed\n");
            return 1;
        }
        return save_fs(&fs, image) == 0 ? 0 : 1;
    }

    fprintf(stderr, "unknown command: %s\n", cmd);
    print_usage(argv[0]);
    return 1;
}
