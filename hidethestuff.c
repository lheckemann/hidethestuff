#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <fcntl.h>

#define lengthof(x) (sizeof(x)/sizeof(*x))


static char * namespace_root;


static int write_to_file(const char const * filename, const char const * contents) {
    int fd = open(filename, O_WRONLY);
    if (!fd)
        return errno;

    int count = strlen(contents);
    int written = 0, written_total = 0;
    while ((written = write(fd, contents + written_total, count - written_total))) {
        if (written < 0) {
            close(fd);
            return errno;
        }
        written_total += written;
        if (written_total >= count)
            break;
    }
    close(fd);
    return 0;
}


static int make_dirs_recursive(const char const * path) {
    char * path_mod = strdup(path);
    char * cur = path_mod + 1;
    while (*cur) {
        if (*cur == '/') {
            *cur = '\0';
            if (mkdir(path_mod, 0777) != 0 && errno != EEXIST) {
                free(path_mod);
                return errno;
            }
            *cur = '/';
        }
        cur++;
    }
    if (mkdir(path_mod, 0777) != 0 && errno != EEXIST) {
        free(path_mod);
        return errno;
    }
    free(path_mod);
    return 0;
}


static int bind_into_namespace(const char const * path) {
    char * source = strdup(path);
    char * dest = calloc(PATH_MAX, 1);
    int root_len = strlen(namespace_root);
    strncpy(dest, namespace_root, PATH_MAX);
    dest[root_len] = '/';
    strncpy(dest + root_len, path, PATH_MAX - root_len);
    printf("bind %s -> %s\n", source, dest);
    int error;
    if (!(error = make_dirs_recursive(dest)) == 0) {
        free(source);
        free(dest);
        return error;
    }
    if (mount(source,  dest, "bind", MS_BIND, NULL) != 0) {
        free(source);
        free(dest);
        return errno;
    }
    free(source);
    free(dest);
    return 0;
}


static char * additional[] = {
    "/nix/store",
    "/run/current-system",
    "/etc",
    NULL
};


int main() {
    int err;

    // Create temporary directory
    char * xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    char * user_dir;
    if (xdg_runtime_dir && *xdg_runtime_dir) {
        user_dir = calloc(PATH_MAX, 2);
        strncat(user_dir, xdg_runtime_dir, PATH_MAX/2);
        strncat(user_dir, "/hidethestuff", PATH_MAX/2);
        if (mkdir(user_dir, 0700) != 0 && errno != EEXIST) {
            fprintf(stderr, "Could not create %s: %s\n", user_dir, strerror(errno));
            exit(-1);
        }
        strncat(user_dir, "/XXXXXX", PATH_MAX);
    }
    else {
        user_dir = calloc(strlen("/run/user/4294967296/hidethestuff/XXXXXX"), 1);
        if (!user_dir) {
            fputs("Could not allocate memory.\n", stderr);
            exit(-1);
        }
        sprintf(user_dir, "/run/user/%d/hidethestuff/XXXXXX", getuid());
    }
    printf("%s\n", user_dir);
    namespace_root = mkdtemp(user_dir);
    if (!namespace_root) {
        perror("mkdtemp");
        exit(-1);
    }

    // Create new user namespace
    unshare(CLONE_NEWUSER);

    // Map root
    if (write_to_file("/proc/self/setgroups", "deny") != 0) {
        perror("writing to setgroups failed");
        exit(-1);
    }

    if (write_to_file("/proc/self/uid_map", "0 1000 1") != 0) {
        perror("writing to setgroups failed");
        exit(-1);
    }

    if (write_to_file("/proc/self/gid_map", "0 100 1") != 0) {
        perror("writing to setgroups failed");
        exit(-1);
    }

    // Create new mount and PID namespaces
    unshare(CLONE_NEWNS | CLONE_NEWPID);

    char * shared_dir = get_current_dir_name();
    if (!shared_dir) {
        perror("get shared directory");
        return -1;
    }
    int result = bind_into_namespace(shared_dir);
    if (result != 0) {
        fprintf(stderr, "Could not mount %s: %s\n", shared_dir, strerror(result));
        return -1;
    }

    for (char** it = additional; *it != NULL; it++) {
        int result = bind_into_namespace(*it);
        if (result != 0) {
            fprintf(stderr, "Could not mount %s: %s\n", *it, strerror(result));
            return -1;
        }
    }

    int cpid = fork();
    if (cpid > 1) {
        wait(NULL);
    }
    else if (cpid == 0) {
        // Now in the new PID namespace
        if (chroot(namespace_root) != 0) {
            perror("chroot");
            return -1;
        }
        if (chdir(shared_dir) != 0) {
            perror("chdir");
            return -1;
        }
        err = make_dirs_recursive("/proc");
        if (err != 0) {
            fprintf(stderr, "create /proc: %s", strerror(err));
            return -1;
        }
        if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
            perror("mount /proc");
        }
        setuid(1000);
        execlp("bash", "", NULL);
    }
}
