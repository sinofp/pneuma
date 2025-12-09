#include "file.h"
#include "kernel.h"

uint8_t disk[DISK_MAX_SIZE];

// TODO span
int oct2int(char *oct, int len) {
    int dec = 0;
    for (int i = 0; i < len; i++) {
        if (oct[i] < '0' || oct[i] > '7')
            break;

        dec = dec * 8 + (oct[i] - '0');
    }
    return dec;
}

void fs_init() {
    for (uint64_t sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, false);

    auto offset = 0uz;
    for (auto i = 0uz; i < FILES_MAX; ++i) {
        auto *header = reinterpret_cast<tar_header *>(&disk[offset]);
        if (header->name[0] == '\0')
            break;

        if (!strcmp(header->magic, USTAR_MAGIC))
            PANIC("invalid tar header: magic=\"%s\"", header->magic);

        auto filesz = oct2int(header->size, sizeof(header->size));
        auto *f = &files[i];
        f->in_use = true;
        strcpy(f->name, header->name);
        memcpy(f->data, header->data, filesz);
        f->size = filesz;
        printf("file (%d): %s, size=%d\n", i, f->name, f->size);

        offset += align_up(sizeof(tar_header) + filesz, SECTOR_SIZE);
    }
}

void fs_flush() {
    // Copy all file contents into `disk` buffer.
    memset(disk, 0, sizeof(disk));
    auto offset = 0uz;
    for (auto file_i = 0uz; file_i < FILES_MAX; ++file_i) {
        file *f = &files[file_i];
        if (!f->in_use)
            continue;

        auto *header = reinterpret_cast<tar_header *>(&disk[offset]);
        memset(header, 0, sizeof(*header));
        strcpy(header->name, f->name);
        strcpy(header->mode, "000644");
        strcpy(header->magic, USTAR_MAGIC);
        strcpy(header->version, "00");
        header->type = '0';

        // Turn the f size into an octal string.
        // TODO int2oct
        int filesz = f->size;
        for (int i = sizeof(header->size); i > 0; --i) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        // Calculate the checksum.
        int checksum = ' ' * sizeof(header->checksum);
        for (auto i = 0uz; i < sizeof(tar_header); ++i)
            checksum += disk[offset + i];

        for (int i = 5; i >= 0; --i) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        // Copy f data.
        memcpy(header->data, f->data, f->size);
        offset += align_up(sizeof(tar_header) + f->size, SECTOR_SIZE);
    }

    // Write `disk` buffer into the virtio-blk.
    for (uint64_t sector = 0; sector < sizeof(disk) / SECTOR_SIZE; ++sector)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

    printf("wrote %d bytes to disk\n", sizeof(disk));
}
