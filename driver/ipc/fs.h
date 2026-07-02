#pragma once

// clang-format off
#define FILE_SERVER   1
#define FS_MAX_PATH   256
#define FS_MAX_DATA   4096

#define FS_OP_OPEN    1
#define FS_OP_CLOSE   2
#define FS_OP_READ    3
#define FS_OP_WRITE   4
#define FS_OP_SEEK    5
#define FS_OP_TELL    6
#define FS_OP_FLUSH   7
#define FS_OP_EOF     8
#define FS_OP_REMOVE  9
#define FS_OP_RENAME  10
#define FS_OP_MKDIR   11

/* Per-operation request structs — caller passes sizeof(specific type) to ssend */
typedef struct { int op; char path[FS_MAX_PATH]; char mode[16]; }             fs_req_open_t;
typedef struct { int op; int fd; }                                            fs_req_fd_t;
typedef struct { int op; int fd; unsigned size; }                             fs_req_read_t;
typedef struct { int op; int fd; unsigned size; char data[FS_MAX_DATA]; }     fs_req_write_t;
typedef struct { int op; int fd; long offset; int whence; }                   fs_req_seek_t;
typedef struct { int op; char path[FS_MAX_PATH]; }                            fs_req_path_t;
typedef struct { int op; char path[FS_MAX_PATH]; char path2[FS_MAX_PATH]; }   fs_req_rename_t;

/* Server receives into this union (sized to the largest member) */
typedef union {
    int              op;
    fs_req_open_t    open;
    fs_req_fd_t      fd;
    fs_req_read_t    read;
    fs_req_write_t   write;
    fs_req_seek_t    seek;
    fs_req_path_t    path;
    fs_req_rename_t  rename;
} fs_req_t;

/* Response types
 *
 * fs_resp_ok_t (int) result semantics per op:
 *   OPEN    : 1-based server FD, or < 0 on error
 *   WRITE   : bytes written, or < 0 on error
 *   TELL    : current byte offset
 *   EOF     : 1 at end-of-file, 0 otherwise
 *   others  : 0 OK / < 0 error
 *
 * fs_resp_read_t: result = 0 OK / < 0 error; size = bytes read; data = payload
 */
typedef int fs_resp_ok_t;
typedef struct { int result; unsigned size; char data[FS_MAX_DATA]; } fs_resp_read_t;

// clang-format on
