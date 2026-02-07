#pragma once

#include "dev/virtio.h"
#include "lib/common.h"

struct [[gnu::packed]] tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
    char data[]; // Array pointing to the data area following the header
                 // (flexible array member)
};

struct file {
    bool in_use;     // Indicates if this file entry is in use
    char name[100];  // File name
    char data[1024]; // File content
    size_t size;     // File size
};

constexpr size_t FILES_MAX = 3;
constexpr size_t DISK_MAX_SIZE = align_up(sizeof(file) * FILES_MAX, virtio::blk::SECTOR_SIZE);
constexpr char USTAR_MAGIC[] = "ustar";

namespace fs {
extern file files[FILES_MAX];

namespace ustar {
void init();
void flush();
} // namespace ustar

} // namespace fs
