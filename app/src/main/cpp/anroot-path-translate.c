/*
 * anroot-path-translate.c - LD_PRELOAD library for path translation
 *
 * This library intercepts filesystem calls and translates paths from
 * /data/data/com.termux/ to /data/data/com.anroot/ so that packages
 * from the official Termux repository can be installed on the Anroot fork.
 *
 * Since both paths have the same length (com.termux and com.anroot are
 * both 10 characters), we can do in-place path translation in a static
 * buffer without worrying about buffer overflows.
 *
 * Build for Android:
 *   aarch64:  ${NDK_CLANG} -shared -fPIC -O2 -o libanroot-path-translate.so anroot-path-translate.c
 *   arm:      ${NDK_CLANG} -shared -fPIC -O2 -o libanroot-path-translate.so anroot-path-translate.c
 *   x86_64:   ${NDK_CLANG} -shared -fPIC -O2 -o libanroot-path-translate.so anroot-path-translate.c
 *   i686:     ${NDK_CLANG} -shared -fPIC -O2 -o libanroot-path-translate.so anroot-path-translate.c
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <sys/statvfs.h>
#include <sys/time.h>      /* for struct timeval (utimes) */
#include <sys/statfs.h>    /* for struct statfs (statfs) */

/* Path constants - same length, so in-place replacement is safe */
#define OLD_PREFIX "/data/data/com.termux"
#define NEW_PREFIX "/data/data/com.anroot"
#define OLD_PREFIX_LEN (sizeof(OLD_PREFIX) - 1)  /* 21 */
#define NEW_PREFIX_LEN (sizeof(NEW_PREFIX) - 1)  /* 21 */

/* Thread-local buffer for translated paths */
static __thread char path_buf[PATH_MAX];

/* Debug logging - set ANROOT_PATH_DEBUG=1 to enable */
static int debug_enabled(void) {
    static int cached = -1;
    if (cached == -1) {
        cached = getenv("ANROOT_PATH_DEBUG") ? 1 : 0;
    }
    return cached;
}

#define LOGD(fmt, ...) do { \
    if (debug_enabled()) \
        fprintf(stderr, "anroot-path: " fmt "\n", ##__VA_ARGS__); \
} while(0)

/*
 * Translate a path: if it starts with /data/data/com.termux/,
 * replace the prefix with /data/data/com.anroot/
 *
 * Returns:
 *   - The original pointer if no translation is needed
 *   - A pointer to a thread-local static buffer if translation was done
 *
 * Thread safety: Uses thread-local storage, so each thread gets its own buffer.
 */
static const char *translate_path(const char *path) {
    if (!path) return NULL;

    /* Quick check: if path doesn't start with /data/data/com.termux, skip */
    if (path[0] != '/') return path;

    size_t len = strlen(path);
    if (len < OLD_PREFIX_LEN) return path;

    /* Check if path starts with /data/data/com.termux */
    if (memcmp(path, OLD_PREFIX, OLD_PREFIX_LEN) != 0) return path;

    /* Check that the next character is '/' or '\0' (exact match) */
    if (path[OLD_PREFIX_LEN] != '/' && path[OLD_PREFIX_LEN] != '\0') return path;

    /* Translate: copy new prefix + rest of path */
    memcpy(path_buf, NEW_PREFIX, NEW_PREFIX_LEN);
    if (len > OLD_PREFIX_LEN) {
        memcpy(path_buf + NEW_PREFIX_LEN, path + OLD_PREFIX_LEN, len - OLD_PREFIX_LEN + 1);
    } else {
        path_buf[NEW_PREFIX_LEN] = '\0';
    }

    LOGD("translate: %s -> %s", path, path_buf);
    return path_buf;
}

/* Also handle the at() variants where dirfd might be AT_FDCWD */
static const char *translate_pathat(int dirfd, const char *path) {
    /* If dirfd is AT_FDCWD, the path is relative to cwd, treat as-is
     * unless it's absolute. For absolute paths, translate. */
    if (path[0] == '/') {
        return translate_path(path);
    }
    /* For relative paths with AT_FDCWD, check if the relative path
     * starts with data/data/com.termux/ (without leading /) */
    if (dirfd == AT_FDCWD) {
        const char *rel_prefix = "data/data/com.termux";
        size_t rel_len = 19; /* strlen("data/data/com.termux") */
        size_t len = strlen(path);
        if (len >= rel_len && memcmp(path, rel_prefix, rel_len) == 0 &&
            (path[rel_len] == '/' || path[rel_len] == '\0')) {
            memcpy(path_buf, "data/data/com.anroot", 20);
            if (len > rel_len) {
                memcpy(path_buf + 20, path + rel_len, len - rel_len + 1);
            } else {
                path_buf[20] = '\0';
            }
            LOGD("translate_rel: %s -> %s", path, path_buf);
            return path_buf;
        }
    }
    return path;
}


/* ===== Function pointer typedefs ===== */
typedef int (*orig_open_t)(const char *, int, ...);
typedef int (*orig_openat_t)(int, const char *, int, ...);
typedef int (*orig_creat_t)(const char *, mode_t);
typedef int (*orig_mkdir_t)(const char *, mode_t);
typedef int (*orig_mkdirat_t)(int, const char *, mode_t);
typedef int (*orig_stat_t)(const char *, struct stat *);
typedef int (*orig_lstat_t)(const char *, struct stat *);
typedef int (*orig_fstatat_t)(int, const char *, struct stat *, int);
typedef int (*orig_access_t)(const char *, int);
typedef int (*orig_faccessat_t)(int, const char *, int, int);
typedef int (*orig_chmod_t)(const char *, mode_t);
typedef int (*orig_fchmodat_t)(int, const char *, mode_t, int);
typedef int (*orig_chown_t)(const char *, uid_t, gid_t);
typedef int (*orig_lchown_t)(const char *, uid_t, gid_t);
typedef int (*orig_fchownat_t)(int, const char *, uid_t, gid_t, int);
typedef int (*orig_link_t)(const char *, const char *);
typedef int (*orig_linkat_t)(int, const char *, int, const char *, int);
typedef int (*orig_symlink_t)(const char *, const char *);
typedef int (*orig_symlinkat_t)(const char *, int, const char *);
typedef int (*orig_unlink_t)(const char *);
typedef int (*orig_unlinkat_t)(int, const char *, int);
typedef int (*orig_rmdir_t)(const char *);
typedef int (*orig_rename_t)(const char *, const char *);
typedef int (*orig_renameat_t)(int, const char *, int, const char *);
typedef int (*orig_renameat2_t)(int, const char *, int, const char *, unsigned int);
typedef DIR *(*orig_opendir_t)(const char *);
typedef ssize_t (*orig_readlink_t)(const char *, char *, size_t);
typedef ssize_t (*orig_readlinkat_t)(int, const char *, char *, size_t);
typedef char *(*orig_realpath_t)(const char *, char *);
typedef int (*orig_scandir_t)(const char *, struct dirent ***, int (*)(const struct dirent *), int (*)(const struct dirent **, const struct dirent **));
typedef int (*orig_statvfs_t)(const char *, struct statvfs *);
typedef int (*orig_truncate_t)(const char *, off_t);
typedef int (*orig_chdir_t)(const char *);
typedef char *(*orig_getcwd_t)(char *, size_t);
typedef long (*orig_pathconf_t)(const char *, int);
typedef int (*orig_utimes_t)(const char *, const struct timeval *);
typedef int (*orig_utimensat_t)(int, const char *, const struct timespec *, int);
typedef int (*orig_mkfifo_t)(const char *, mode_t);
typedef int (*orig_mknod_t)(const char *, mode_t, dev_t);
typedef int (*orig_statfs_t)(const char *, struct statfs *);
typedef int (*orig_fstatfs_t)(int, struct statfs *);
typedef int (*orig_euidaccess_t)(const char *, int);


/* ===== Intercepted functions ===== */

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    const char *tpath = translate_path(path);
    orig_open_t orig = (orig_open_t)dlsym(RTLD_NEXT, "open");
    return orig(tpath, flags, mode);
}

int open64(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    const char *tpath = translate_path(path);
    orig_open_t orig = (orig_open_t)dlsym(RTLD_NEXT, "open64");
    return orig(tpath, flags, mode);
}

int openat(int dirfd, const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    const char *tpath = translate_pathat(dirfd, path);
    orig_openat_t orig = (orig_openat_t)dlsym(RTLD_NEXT, "openat");
    return orig(dirfd, tpath, flags, mode);
}

int creat(const char *path, mode_t mode) {
    const char *tpath = translate_path(path);
    orig_creat_t orig = (orig_creat_t)dlsym(RTLD_NEXT, "creat");
    return orig(tpath, mode);
}

int mkdir(const char *path, mode_t mode) {
    const char *tpath = translate_path(path);
    LOGD("mkdir(%s) -> %s", path, tpath);
    orig_mkdir_t orig = (orig_mkdir_t)dlsym(RTLD_NEXT, "mkdir");
    return orig(tpath, mode);
}

int mkdirat(int dirfd, const char *path, mode_t mode) {
    const char *tpath = translate_pathat(dirfd, path);
    LOGD("mkdirat(%d, %s) -> %s", dirfd, path, tpath);
    orig_mkdirat_t orig = (orig_mkdirat_t)dlsym(RTLD_NEXT, "mkdirat");
    return orig(dirfd, tpath, mode);
}

int stat(const char *path, struct stat *buf) {
    const char *tpath = translate_path(path);
    orig_stat_t orig = (orig_stat_t)dlsym(RTLD_NEXT, "stat");
    return orig(tpath, buf);
}

int lstat(const char *path, struct stat *buf) {
    const char *tpath = translate_path(path);
    orig_lstat_t orig = (orig_lstat_t)dlsym(RTLD_NEXT, "lstat");
    return orig(tpath, buf);
}

int fstatat(int dirfd, const char *path, struct stat *buf, int flags) {
    const char *tpath = translate_pathat(dirfd, path);
    orig_fstatat_t orig = (orig_fstatat_t)dlsym(RTLD_NEXT, "fstatat");
    return orig(dirfd, tpath, buf, flags);
}

int access(const char *path, int mode) {
    const char *tpath = translate_path(path);
    orig_access_t orig = (orig_access_t)dlsym(RTLD_NEXT, "access");
    return orig(tpath, mode);
}

int faccessat(int dirfd, const char *path, int mode, int flags) {
    const char *tpath = translate_pathat(dirfd, path);
    orig_faccessat_t orig = (orig_faccessat_t)dlsym(RTLD_NEXT, "faccessat");
    return orig(dirfd, tpath, mode, flags);
}

int chmod(const char *path, mode_t mode) {
    const char *tpath = translate_path(path);
    orig_chmod_t orig = (orig_chmod_t)dlsym(RTLD_NEXT, "chmod");
    return orig(tpath, mode);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags) {
    const char *tpath = translate_pathat(dirfd, path);
    orig_fchmodat_t orig = (orig_fchmodat_t)dlsym(RTLD_NEXT, "fchmodat");
    return orig(dirfd, tpath, mode, flags);
}

int chown(const char *path, uid_t uid, gid_t gid) {
    const char *tpath = translate_path(path);
    orig_chown_t orig = (orig_chown_t)dlsym(RTLD_NEXT, "chown");
    return orig(tpath, uid, gid);
}

int lchown(const char *path, uid_t uid, gid_t gid) {
    const char *tpath = translate_path(path);
    orig_lchown_t orig = (orig_lchown_t)dlsym(RTLD_NEXT, "lchown");
    return orig(tpath, uid, gid);
}

int fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flags) {
    const char *tpath = translate_pathat(dirfd, path);
    orig_fchownat_t orig = (orig_fchownat_t)dlsym(RTLD_NEXT, "fchownat");
    return orig(dirfd, tpath, uid, gid, flags);
}

int link(const char *oldpath, const char *newpath) {
    const char *told = translate_path(oldpath);
    const char *tnew = translate_path(newpath);
    orig_link_t orig = (orig_link_t)dlsym(RTLD_NEXT, "link");
    return orig(told, tnew);
}

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags) {
    const char *told = translate_pathat(olddirfd, oldpath);
    const char *tnew = translate_pathat(newdirfd, newpath);
    orig_linkat_t orig = (orig_linkat_t)dlsym(RTLD_NEXT, "linkat");
    return orig(olddirfd, told, newdirfd, tnew, flags);
}

int symlink(const char *target, const char *linkpath) {
    /* Translate BOTH the target and linkpath.
     * When packages from official Termux repos create symlinks, the target
     * typically points to /data/data/com.termux/... paths which don't exist
     * in our filesystem. The kernel resolves symlinks internally, bypassing
     * LD_PRELOAD, so we must translate the target here. */
    const char *ttarget = translate_path(target);
    const char *tlink = translate_path(linkpath);
    orig_symlink_t orig = (orig_symlink_t)dlsym(RTLD_NEXT, "symlink");
    return orig(ttarget, tlink);
}

int symlinkat(const char *target, int newdirfd, const char *linkpath) {
    /* Translate BOTH target and linkpath for the same reason as symlink(). */
    const char *ttarget = translate_path(target);
    const char *tlink = translate_pathat(newdirfd, linkpath);
    orig_symlinkat_t orig = (orig_symlinkat_t)dlsym(RTLD_NEXT, "symlinkat");
    return orig(ttarget, newdirfd, tlink);
}

int unlink(const char *path) {
    const char *tpath = translate_path(path);
    orig_unlink_t orig = (orig_unlink_t)dlsym(RTLD_NEXT, "unlink");
    return orig(tpath);
}

int unlinkat(int dirfd, const char *path, int flags) {
    const char *tpath = translate_pathat(dirfd, path);
    orig_unlinkat_t orig = (orig_unlinkat_t)dlsym(RTLD_NEXT, "unlinkat");
    return orig(dirfd, tpath, flags);
}

int rmdir(const char *path) {
    const char *tpath = translate_path(path);
    orig_rmdir_t orig = (orig_rmdir_t)dlsym(RTLD_NEXT, "rmdir");
    return orig(tpath);
}

int rename(const char *oldpath, const char *newpath) {
    const char *told = translate_path(oldpath);
    const char *tnew = translate_path(newpath);
    orig_rename_t orig = (orig_rename_t)dlsym(RTLD_NEXT, "rename");
    return orig(told, tnew);
}

int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    const char *told = translate_pathat(olddirfd, oldpath);
    const char *tnew = translate_pathat(newdirfd, newpath);
    orig_renameat_t orig = (orig_renameat_t)dlsym(RTLD_NEXT, "renameat");
    return orig(olddirfd, told, newdirfd, tnew);
}

int renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags) {
    const char *told = translate_pathat(olddirfd, oldpath);
    const char *tnew = translate_pathat(newdirfd, newpath);
    orig_renameat2_t orig = (orig_renameat2_t)dlsym(RTLD_NEXT, "renameat2");
    return orig(olddirfd, told, newdirfd, tnew, flags);
}

DIR *opendir(const char *path) {
    const char *tpath = translate_path(path);
    orig_opendir_t orig = (orig_opendir_t)dlsym(RTLD_NEXT, "opendir");
    return orig(tpath);
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    const char *tpath = translate_path(path);
    orig_readlink_t orig = (orig_readlink_t)dlsym(RTLD_NEXT, "readlink");
    return orig(tpath, buf, bufsiz);
}

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz) {
    const char *tpath = translate_pathat(dirfd, path);
    orig_readlinkat_t orig = (orig_readlinkat_t)dlsym(RTLD_NEXT, "readlinkat");
    return orig(dirfd, tpath, buf, bufsiz);
}

char *realpath(const char *path, char *resolved) {
    const char *tpath = translate_path(path);
    orig_realpath_t orig = (orig_realpath_t)dlsym(RTLD_NEXT, "realpath");
    return orig(tpath, resolved);
}

int scandir(const char *path, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **)) {
    const char *tpath = translate_path(path);
    orig_scandir_t orig = (orig_scandir_t)dlsym(RTLD_NEXT, "scandir");
    return orig(tpath, namelist, filter, compar);
}

int statvfs(const char *path, struct statvfs *buf) {
    const char *tpath = translate_path(path);
    orig_statvfs_t orig = (orig_statvfs_t)dlsym(RTLD_NEXT, "statvfs");
    return orig(tpath, buf);
}

int truncate(const char *path, off_t length) {
    const char *tpath = translate_path(path);
    orig_truncate_t orig = (orig_truncate_t)dlsym(RTLD_NEXT, "truncate");
    return orig(tpath, length);
}

int chdir(const char *path) {
    const char *tpath = translate_path(path);
    orig_chdir_t orig = (orig_chdir_t)dlsym(RTLD_NEXT, "chdir");
    return orig(tpath);
}

long pathconf(const char *path, int name) {
    const char *tpath = translate_path(path);
    orig_pathconf_t orig = (orig_pathconf_t)dlsym(RTLD_NEXT, "pathconf");
    return orig(tpath, name);
}

/* ===== Missing syscall interceptions (critical for dpkg) ===== */

/*
 * utimes() - Used by dpkg to set file timestamps after extraction.
 * This is the DIRECT cause of "error setting timestamps: No such file or directory"
 * because dpkg creates files via open() (intercepted) but then calls utimes()
 * with the original com.termux path (NOT intercepted).
 *
 * Note: Android bionic implements utimes() by calling utimensat(AT_FDCWD, path, ts, 0)
 * internally within libc.so, which likely bypasses PLT/GOT. So we MUST intercept
 * utimes() directly, not just utimensat().
 */
int utimes(const char *path, const struct timeval tv[2]) {
    const char *tpath = translate_path(path);
    orig_utimes_t orig = (orig_utimes_t)dlsym(RTLD_NEXT, "utimes");
    return orig(tpath, tv);
}

/*
 * utimensat() - Used by coreutils, gzip, patch, tar, zstd for setting timestamps.
 * Also used internally by bionic's futimens() and utimes(), but those internal
 * calls within libc.so bypass LD_PRELOAD, so we must intercept each function
 * individually.
 */
int utimensat(int dirfd, const char *path, const struct timespec ts[2], int flags) {
    const char *tpath = translate_pathat(dirfd, path);
    orig_utimensat_t orig = (orig_utimensat_t)dlsym(RTLD_NEXT, "utimensat");
    return orig(dirfd, tpath, ts, flags);
}

/*
 * mkfifo() - Used by dpkg and coreutils for creating named pipes.
 */
int mkfifo(const char *path, mode_t mode) {
    const char *tpath = translate_path(path);
    orig_mkfifo_t orig = (orig_mkfifo_t)dlsym(RTLD_NEXT, "mkfifo");
    return orig(tpath, mode);
}

/*
 * mknod() - Used by dpkg, coreutils, and fsck.cramfs for creating device nodes.
 */
int mknod(const char *path, mode_t mode, dev_t dev) {
    const char *tpath = translate_path(path);
    orig_mknod_t orig = (orig_mknod_t)dlsym(RTLD_NEXT, "mknod");
    return orig(tpath, mode, dev);
}

/*
 * statfs() - Used by dpkg for filesystem statistics. Different from statvfs()
 * which we already intercept. Both exist as separate syscalls on Linux.
 */
int statfs(const char *path, struct statfs *buf) {
    const char *tpath = translate_path(path);
    orig_statfs_t orig = (orig_statfs_t)dlsym(RTLD_NEXT, "statfs");
    return orig(tpath, buf);
}

/*
 * euidaccess() - Used by some programs to check file access with effective UID.
 * Android bionic provides this as a wrapper around faccessat().
 */
int euidaccess(const char *path, int mode) {
    const char *tpath = translate_path(path);
    orig_euidaccess_t orig = (orig_euidaccess_t)dlsym(RTLD_NEXT, "euidaccess");
    return orig(tpath, mode);
}
