/**
 * path_remap.c - LD_PRELOAD library for remapping com.termux paths to com.anroot
 *
 * This library intercepts filesystem system calls and remaps any path
 * starting with /data/data/com.termux to /data/data/com.anroot.
 * This allows the Anroot app to install packages from the Termux
 * repository which have hardcoded /data/data/com.termux paths.
 *
 * Build for each architecture:
 *   aarch64: aarch64-linux-android21-clang -shared -fPIC -o libpath_remap.so path_remap.c
 *   arm: armv7a-linux-androideabi21-clang -shared -fPIC -o libpath_remap.so path_remap.c
 *   x86_64: x86_64-linux-android21-clang -shared -fPIC -o libpath_remap.so path_remap.c
 *   i686: i686-linux-android21-clang -shared -fPIC -o libpath_remap.so path_remap.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define OLD_PREFIX "/data/data/com.termux"
#define NEW_PREFIX "/data/data/com.anroot"
#define OLD_PREFIX_LEN (sizeof(OLD_PREFIX) - 1)  /* 21 */
#define NEW_PREFIX_LEN (sizeof(NEW_PREFIX) - 1)  /* 20 */

/* Thread-local remapped path buffer */
static __thread char remapped_path[PATH_MAX];

/**
 * Remap a path from /data/data/com.termux to /data/data/com.anroot
 * Returns the remapped path (may be the original if no remapping needed)
 */
static const char *remap_path(const char *path) {
    if (path == NULL) return NULL;

    /* Only remap paths that start with /data/data/com.termux */
    if (strncmp(path, OLD_PREFIX, OLD_PREFIX_LEN) != 0) {
        return path;
    }

    /* Build remapped path */
    memcpy(remapped_path, NEW_PREFIX, NEW_PREFIX_LEN);
    /* Copy the rest of the path after OLD_PREFIX */
    strncpy(remapped_path + NEW_PREFIX_LEN, path + OLD_PREFIX_LEN,
            PATH_MAX - NEW_PREFIX_LEN - 1);
    remapped_path[PATH_MAX - 1] = '\0';

    return remapped_path;
}

/**
 * Remap a path, but also handle the case where path starts with
 * /data/data/com.termux but is actually accessed as a relative path
 * component (like ./data/data/com.termux inside dpkg extraction)
 */
static const char *remap_path_at(const char *path) {
    if (path == NULL) return NULL;

    /* Check for absolute path starting with /data/data/com.termux */
    if (path[0] == '/') {
        return remap_path(path);
    }

    /* Check for relative path containing data/data/com.termux */
    const char *termux_pos = strstr(path, "data/data/com.termux");
    if (termux_pos != NULL) {
        /* Remap by replacing the component */
        size_t prefix_len = termux_pos - path;
        if (prefix_len + NEW_PREFIX_LEN + strlen(termux_pos + OLD_PREFIX_LEN) < PATH_MAX) {
            memcpy(remapped_path, path, prefix_len);
            memcpy(remapped_path + prefix_len, NEW_PREFIX, NEW_PREFIX_LEN);
            strcpy(remapped_path + prefix_len + NEW_PREFIX_LEN,
                   termux_pos + OLD_PREFIX_LEN);
            return remapped_path;
        }
    }

    return path;
}

/* Function pointer types */
typedef int (*orig_open_t)(const char *, int, ...);
typedef int (*orig_openat_t)(int, const char *, int, ...);
typedef int (*orig_access_t)(const char *, int);
typedef int (*orig_faccessat_t)(int, const char *, int, int);
typedef int (*orig_stat_t)(const char *, struct stat *);
typedef int (*orig_lstat_t)(const char *, struct stat *);
typedef int (*orig_fstatat_t)(int, const char *, struct stat *, int);
typedef int (*orig_mkdir_t)(const char *, mode_t);
typedef int (*orig_mkdirat_t)(int, const char *, mode_t);
typedef int (*orig_unlink_t)(const char *);
typedef int (*orig_unlinkat_t)(int, const char *, int);
typedef int (*orig_rmdir_t)(const char *);
typedef int (*orig_symlink_t)(const char *, const char *);
typedef int (*orig_symlinkat_t)(const char *, int, const char *);
typedef ssize_t (*orig_readlink_t)(const char *, char *, size_t);
typedef ssize_t (*orig_readlinkat_t)(int, const char *, char *, size_t);
typedef int (*orig_rename_t)(const char *, const char *);
typedef int (*orig_renameat_t)(int, const char *, int, const char *);
typedef int (*orig_chmod_t)(const char *, mode_t);
typedef int (*orig_fchmodat_t)(int, const char *, mode_t, int);
typedef int (*orig_chown_t)(const char *, uid_t, gid_t);
typedef int (*orig_lchown_t)(const char *, uid_t, gid_t);
typedef int (*orig_fchownat_t)(int, const char *, uid_t, gid_t, int);
typedef DIR *(*orig_opendir_t)(const char *);
typedef int (*orig_creat_t)(const char *, mode_t);
typedef int (*orig_chdir_t)(const char *);
typedef char *(*orig_getcwd_t)(char *, size_t);
typedef int (*orig_truncate_t)(const char *, off_t);
typedef int (*orig_execve_t)(const char *, char *const[], char *const[]);

/* Helper to get real function */
static void *get_real_func(const char *name) {
    return dlsym(RTLD_NEXT, name);
}

/* Intercepted functions */

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    const char *rpath = remap_path(path);
    orig_open_t orig = (orig_open_t)get_real_func("open");
    return orig(rpath, flags, mode);
}

int open64(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    const char *rpath = remap_path(path);
    orig_open_t orig = (orig_open_t)get_real_func("open64");
    return orig(rpath, flags, mode);
}

int openat(int dirfd, const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    const char *rpath = remap_path(path);
    orig_openat_t orig = (orig_openat_t)get_real_func("openat");
    return orig(dirfd, rpath, flags, mode);
}

int creat(const char *path, mode_t mode) {
    const char *rpath = remap_path(path);
    orig_creat_t orig = (orig_creat_t)get_real_func("creat");
    return orig(rpath, mode);
}

int access(const char *path, int mode) {
    const char *rpath = remap_path(path);
    orig_access_t orig = (orig_access_t)get_real_func("access");
    return orig(rpath, mode);
}

int faccessat(int dirfd, const char *path, int mode, int flags) {
    const char *rpath = remap_path(path);
    orig_faccessat_t orig = (orig_faccessat_t)get_real_func("faccessat");
    return orig(dirfd, rpath, mode, flags);
}

int stat(const char *path, struct stat *buf) {
    const char *rpath = remap_path(path);
    orig_stat_t orig = (orig_stat_t)get_real_func("stat");
    return orig(rpath, buf);
}

int stat64(const char *path, struct stat64 *buf) {
    const char *rpath = remap_path(path);
    int (*orig)(const char *, struct stat64 *) = (int (*)(const char *, struct stat64 *))get_real_func("stat64");
    return orig(rpath, buf);
}

int lstat(const char *path, struct stat *buf) {
    const char *rpath = remap_path(path);
    orig_lstat_t orig = (orig_lstat_t)get_real_func("lstat");
    return orig(rpath, buf);
}

int lstat64(const char *path, struct stat64 *buf) {
    const char *rpath = remap_path(path);
    int (*orig)(const char *, struct stat64 *) = (int (*)(const char *, struct stat64 *))get_real_func("lstat64");
    return orig(rpath, buf);
}

int fstatat(int dirfd, const char *path, struct stat *buf, int flags) {
    const char *rpath = remap_path(path);
    orig_fstatat_t orig = (orig_fstatat_t)get_real_func("fstatat");
    return orig(dirfd, rpath, buf, flags);
}

int mkdir(const char *path, mode_t mode) {
    const char *rpath = remap_path_at(path);
    orig_mkdir_t orig = (orig_mkdir_t)get_real_func("mkdir");
    return orig(rpath, mode);
}

int mkdirat(int dirfd, const char *path, mode_t mode) {
    const char *rpath = remap_path_at(path);
    orig_mkdirat_t orig = (orig_mkdirat_t)get_real_func("mkdirat");
    return orig(dirfd, rpath, mode);
}

int unlink(const char *path) {
    const char *rpath = remap_path(path);
    orig_unlink_t orig = (orig_unlink_t)get_real_func("unlink");
    return orig(rpath);
}

int unlinkat(int dirfd, const char *path, int flags) {
    const char *rpath = remap_path(path);
    orig_unlinkat_t orig = (orig_unlinkat_t)get_real_func("unlinkat");
    return orig(dirfd, rpath, flags);
}

int rmdir(const char *path) {
    const char *rpath = remap_path(path);
    orig_rmdir_t orig = (orig_rmdir_t)get_real_func("rmdir");
    return orig(rpath);
}

int symlink(const char *target, const char *linkpath) {
    const char *rtarget = remap_path(target);
    const char *rlinkpath = remap_path(linkpath);
    orig_symlink_t orig = (orig_symlink_t)get_real_func("symlink");
    return orig(rtarget, rlinkpath);
}

int symlinkat(const char *target, int newdirfd, const char *linkpath) {
    const char *rtarget = remap_path(target);
    const char *rlinkpath = remap_path(linkpath);
    orig_symlinkat_t orig = (orig_symlinkat_t)get_real_func("symlinkat");
    return orig(rtarget, newdirfd, rlinkpath);
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    const char *rpath = remap_path(path);
    orig_readlink_t orig = (orig_readlink_t)get_real_func("readlink");
    return orig(rpath, buf, bufsiz);
}

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz) {
    const char *rpath = remap_path(path);
    orig_readlinkat_t orig = (orig_readlinkat_t)get_real_func("readlinkat");
    return orig(dirfd, rpath, buf, bufsiz);
}

int rename(const char *oldpath, const char *newpath) {
    const char *rold = remap_path(oldpath);
    const char *rnew = remap_path(newpath);
    orig_rename_t orig = (orig_rename_t)get_real_func("rename");
    return orig(rold, rnew);
}

int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    const char *rold = remap_path(oldpath);
    const char *rnew = remap_path(newpath);
    orig_renameat_t orig = (orig_renameat_t)get_real_func("renameat");
    return orig(olddirfd, rold, newdirfd, rnew);
}

int chmod(const char *path, mode_t mode) {
    const char *rpath = remap_path(path);
    orig_chmod_t orig = (orig_chmod_t)get_real_func("chmod");
    return orig(rpath, mode);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags) {
    const char *rpath = remap_path(path);
    orig_fchmodat_t orig = (orig_fchmodat_t)get_real_func("fchmodat");
    return orig(dirfd, rpath, mode, flags);
}

int chown(const char *path, uid_t owner, gid_t group) {
    const char *rpath = remap_path(path);
    orig_chown_t orig = (orig_chown_t)get_real_func("chown");
    return orig(rpath, owner, group);
}

int lchown(const char *path, uid_t owner, gid_t group) {
    const char *rpath = remap_path(path);
    orig_lchown_t orig = (orig_lchown_t)get_real_func("lchown");
    return orig(rpath, owner, group);
}

int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags) {
    const char *rpath = remap_path(path);
    orig_fchownat_t orig = (orig_fchownat_t)get_real_func("fchownat");
    return orig(dirfd, rpath, owner, group, flags);
}

DIR *opendir(const char *path) {
    const char *rpath = remap_path(path);
    orig_opendir_t orig = (orig_opendir_t)get_real_func("opendir");
    return orig(rpath);
}

int chdir(const char *path) {
    const char *rpath = remap_path(path);
    orig_chdir_t orig = (orig_chdir_t)get_real_func("chdir");
    return orig(rpath);
}

int truncate(const char *path, off_t length) {
    const char *rpath = remap_path(path);
    orig_truncate_t orig = (orig_truncate_t)get_real_func("truncate");
    return orig(rpath, length);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    const char *rpath = remap_path(path);
    orig_execve_t orig = (orig_execve_t)get_real_func("execve");
    return orig(rpath, argv, envp);
}
