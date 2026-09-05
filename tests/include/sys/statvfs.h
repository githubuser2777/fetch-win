#ifndef _TEST_SYS_STATVFS_H
#define _TEST_SYS_STATVFS_H

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

static inline int statvfs(const char *path, struct statvfs *buf) {
    (void)path; (void)buf;
    return -1;
}

#endif
