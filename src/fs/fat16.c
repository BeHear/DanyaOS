#include "fat16.h"
#include "../drivers/ata.h"
#include "../memory/heap.h"
#include "../libc/string.h"
#include "../drivers/vga.h"

static fat16_fs_t fs;

static uint32_t fat16_get_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fs.fat_start + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t sector[512];
    if (ata_read_sectors(fat_sector, 1, sector) != 0) return 0xFFF8;

    uint16_t next = *(uint16_t*)&sector[ent_offset];
    return next;
}

static int fat16_read_cluster(uint32_t cluster, void* buf) {
    uint32_t sector = fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;
    return ata_read_sectors(sector, fs.bpb.sectors_per_cluster, buf);
}

static void fat16_parse_name(const char* input, uint8_t* output) {
    memset(output, ' ', 11);
    int i = 0;
    int j = 0;
    int dot_seen = 0;

    while (input[i] && j < 11) {
        if (input[i] == '.') {
            if (!dot_seen) {
                dot_seen = 1;
                j = 8;
            }
            i++;
            continue;
        }
        if (j < 11) {
            char c = input[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            output[j++] = c;
        }
        i++;
    }
}

static void fat16_unparse_name(const uint8_t* input, char* output) {
    int j = 0;
    for (int i = 0; i < 8; i++) {
        if (input[i] != ' ') output[j++] = input[i];
    }
    if (input[8] != ' ') {
        output[j++] = '.';
        for (int i = 8; i < 11; i++) {
            if (input[i] != ' ') output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

static void fat16_scan_root(void) {
    uint8_t sector[512];
    uint32_t root_size = fs.bpb.root_entries * 32;
    uint32_t root_sectors = (root_size + 511) / 512;

    fs.file_count = 0;

    for (uint32_t s = 0; s < root_sectors; s++) {
        if (ata_read_sectors(fs.root_start + s, 1, sector) != 0) continue;

        int end_found = 0;
        for (uint32_t i = 0; i < 512; i += 32) {
            fat16_dir_entry_t* entry = (fat16_dir_entry_t*)&sector[i];

            if (entry->name[0] == 0x00) { end_found = 1; break; }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attr == 0x0F) continue;
            if (entry->attr & 0x08) continue;

            if (fs.file_count >= FAT16_MAX_FILES) return;

            fat16_file_t* file = &fs.files[fs.file_count];
            fat16_unparse_name(entry->name, file->name);
            file->size = entry->file_size;
            file->first_cluster = entry->first_cluster;
            file->attr = entry->attr;
            file->used = 1;
            fs.file_count++;
        }
        if (end_found) break;
    }
}

int fat16_mount(void) {
    memset(&fs, 0, sizeof(fs));

    uint8_t sector[512];
    if (ata_read_sectors(0, 1, sector) != 0) {
        vga_puts("  [FAIL] FAT16: Cannot read boot sector\n");
        return -1;
    }

    memcpy(&fs.bpb, sector, sizeof(fat16_bpb_t));

    if (fs.bpb.bytes_per_sector != 512) {
        vga_puts("  [FAIL] FAT16: Unsupported sector size\n");
        return -1;
    }

    if (fs.bpb.fat_size_16 == 0) {
        vga_puts("  [FAIL] FAT16: Not a FAT16 filesystem\n");
        return -1;
    }

    if (fs.bpb.sectors_per_cluster == 0) {
        vga_puts("  [FAIL] FAT16: Invalid sectors per cluster\n");
        return -1;
    }

    fs.fat_start = fs.bpb.reserved_sectors;
    fs.root_start = fs.fat_start + fs.bpb.num_fats * fs.bpb.fat_size_16;
    fs.data_start = fs.root_start + (fs.bpb.root_entries * 32 + 511) / 512;

    uint32_t total_sectors = fs.bpb.total_sectors_32;
    if (total_sectors == 0) total_sectors = fs.bpb.total_sectors_16;
    if (total_sectors > fs.data_start)
        fs.total_clusters = (total_sectors - fs.data_start) / fs.bpb.sectors_per_cluster;
    else
        fs.total_clusters = 0;
    fs.mounted = 1;

    fat16_scan_root();

    // Count actual free clusters by scanning FAT
    uint32_t free_clusters = 0;
    {
        uint32_t total_fat_sectors = fs.bpb.fat_size_16;
        uint32_t entries_per_sector = 512 / 2;
        for (uint32_t si = 0; si < total_fat_sectors; si++) {
            uint8_t sector_buf[512];
            if (ata_read_sectors(fs.fat_start + si, 1, sector_buf) != 0) continue;
            for (uint32_t i = 0; i < entries_per_sector; i++) {
                uint32_t cl = si * entries_per_sector + i;
                if (cl < 2 || cl >= 2 + fs.total_clusters) continue;
                uint16_t val = *(uint16_t*)&sector_buf[i * 2];
                if (val == 0x0000) free_clusters++;
            }
        }
    }

    vga_printf("  [ OK ] FAT16: %d files, %u KB free\n",
               fs.file_count,
               (free_clusters * fs.bpb.sectors_per_cluster * 512) / 1024);

    return 0;
}

int fat16_read_file(const char* name, void* buf, uint32_t max_size) {
    if (!fs.mounted) return -1;

    fat16_file_t* file = NULL;
    for (int i = 0; i < fs.file_count; i++) {
        if (fs.files[i].used && strcasecmp(fs.files[i].name, name) == 0) {
            file = &fs.files[i];
            break;
        }
    }
    if (!file) return -1;

    uint32_t bytes_read = 0;
    uint32_t cluster = file->first_cluster;
    uint8_t* ptr = (uint8_t*)buf;
    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;

    while (cluster >= 2 && cluster < 0xFF8 && bytes_read < max_size) {
        uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
        if (!cluster_buf) break;

        if (fat16_read_cluster(cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            break;
        }

        uint32_t to_copy = file->size - bytes_read;
        if (to_copy > cluster_size) to_copy = cluster_size;
        if (bytes_read + to_copy > max_size) to_copy = max_size - bytes_read;

        memcpy(ptr + bytes_read, cluster_buf, to_copy);
        bytes_read += to_copy;
        kfree(cluster_buf);

        cluster = fat16_get_next_cluster(cluster);
    }

    return (int)bytes_read;
}

static int fat16_allocate_cluster(void) {
    // Scan FAT for a free cluster
    uint32_t total_fat_sectors = fs.bpb.fat_size_16;
    uint32_t entries_per_sector = 512 / 2;
    uint32_t total_clusters = fs.total_clusters;

    for (uint32_t sector_idx = 0; sector_idx < total_fat_sectors; sector_idx++) {
        uint8_t sector[512];
        if (ata_read_sectors(fs.fat_start + sector_idx, 1, sector) != 0) continue;

        for (uint32_t i = 0; i < entries_per_sector; i++) {
            uint32_t cluster = sector_idx * entries_per_sector + i;
            if (cluster < 2 || cluster >= 2 + total_clusters) continue;

            uint16_t val = *(uint16_t*)&sector[i * 2];
            if (val == 0x0000) {
                // Mark as EOF (0xFFFF)
                *(uint16_t*)&sector[i * 2] = 0xFFFF;
                if (ata_write_sectors(fs.fat_start + sector_idx, 1, sector) != 0) return 0;
                return (int)cluster;
            }
        }
    }
    return 0; // No free clusters
}

static int fat16_write_cluster(uint32_t cluster, const uint8_t* data, uint32_t size) {
    uint32_t sector = fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;
    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint32_t to_write = size < cluster_size ? size : cluster_size;

    uint8_t buf[512];
    uint32_t offset = 0;
    for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster && offset < to_write; s++) {
        uint32_t chunk = (to_write - offset) < 512 ? (to_write - offset) : 512;
        memcpy(buf, data + offset, chunk);
        if (chunk < 512) memset(buf + chunk, 0, 512 - chunk);
        if (ata_write_sectors(sector + s, 1, buf) != 0) return -1;
        offset += chunk;
    }
    return (int)to_write;
}

static int fat16_write_cluster(uint32_t cluster, const uint8_t* data, uint32_t size);
static void fat16_free_cluster_chain(uint32_t cluster);

int fat16_write_file(const char* name, const void* data, uint32_t size) {
    if (!fs.mounted) return -1;

    // Check if file exists — for simplicity, delete and recreate
    fat16_delete_file(name);

    // Allocate first cluster
    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint32_t needed_clusters = (size + cluster_size - 1) / cluster_size;
    if (needed_clusters == 0) needed_clusters = 1;

    int first_cluster = fat16_allocate_cluster();
    if (first_cluster < 2) {
        // Create empty entry if no clusters available
        first_cluster = 0;
    }

    // Write data to clusters
    int prev_cluster = first_cluster;
    uint32_t remaining = size;
    const uint8_t* ptr = (const uint8_t*)data;
    int write_failed = 0;

    for (uint32_t c = 0; c < needed_clusters; c++) {
        if (write_failed) break;

        int cur_cluster = (c == 0) ? first_cluster : fat16_allocate_cluster();
        if (cur_cluster < 2) { write_failed = 1; break; }

        if (c > 0 && prev_cluster >= 2) {
            // Link previous cluster to current
            uint32_t fat_offset = prev_cluster * 2;
            uint32_t fat_sector = fs.fat_start + (fat_offset / 512);
            uint32_t ent_offset = fat_offset % 512;
            uint8_t fat_sec[512];
            if (ata_read_sectors(fat_sector, 1, fat_sec) == 0) {
                *(uint16_t*)&fat_sec[ent_offset] = (uint16_t)cur_cluster;
                ata_write_sectors(fat_sector, 1, fat_sec);
            }
        }

        uint32_t to_write = remaining < cluster_size ? remaining : cluster_size;
        if (fat16_write_cluster((uint32_t)cur_cluster, ptr, to_write) != 0) {
            write_failed = 1;
        }
        ptr += to_write;
        remaining -= to_write;
        prev_cluster = cur_cluster;
    }

    if (write_failed && first_cluster >= 2) {
        fat16_free_cluster_chain(first_cluster);
        return -1;
    }

    // Create entry in root directory
    uint8_t sector[512];
    uint32_t root_size = fs.bpb.root_entries * 32;
    uint32_t root_sectors = (root_size + 511) / 512;

    uint8_t parsed_name[11];
    fat16_parse_name(name, parsed_name);

    for (uint32_t s = 0; s < root_sectors; s++) {
        if (ata_read_sectors(fs.root_start + s, 1, sector) != 0) continue;

        for (uint32_t i = 0; i < 512; i += 32) {
            fat16_dir_entry_t* entry = (fat16_dir_entry_t*)&sector[i];

            if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                memcpy(entry->name, parsed_name, 11);
                entry->attr = 0x20;
                entry->file_size = size;
                entry->first_cluster = (uint16_t)(first_cluster & 0xFFFF);
                entry->first_cluster_high = (uint16_t)((first_cluster >> 16) & 0xFFFF);

                if (ata_write_sectors(fs.root_start + s, 1, sector) != 0) return -1;

                if (fs.file_count < FAT16_MAX_FILES) {
                    fat16_file_t* file = &fs.files[fs.file_count];
                    strncpy(file->name, name, 12);
                    file->name[12] = '\0';
                    file->size = size;
                    file->first_cluster = (uint32_t)first_cluster;
                    file->attr = 0x20;
                    file->used = 1;
                    fs.file_count++;
                }

                return (int)size;
            }
        }
    }

    return -1;
}

static void fat16_free_cluster_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < 0xFF8) {
        uint32_t next = fat16_get_next_cluster(cluster);

        uint32_t fat_offset = cluster * 2;
        uint32_t fat_sector = fs.fat_start + (fat_offset / 512);
        uint32_t ent_offset = fat_offset % 512;

        uint8_t sector[512];
        if (ata_read_sectors(fat_sector, 1, sector) == 0) {
            *(uint16_t*)&sector[ent_offset] = 0x0000;
            ata_write_sectors(fat_sector, 1, sector);
        }

        cluster = next;
    }
}

int fat16_delete_file(const char* name) {
    if (!fs.mounted) return -1;

    uint8_t sector[512];
    uint32_t root_size = fs.bpb.root_entries * 32;
    uint32_t root_sectors = (root_size + 511) / 512;

    uint8_t parsed_name[11];
    fat16_parse_name(name, parsed_name);

    for (uint32_t s = 0; s < root_sectors; s++) {
        if (ata_read_sectors(fs.root_start + s, 1, sector) != 0) continue;

        for (uint32_t i = 0; i < 512; i += 32) {
            fat16_dir_entry_t* entry = (fat16_dir_entry_t*)&sector[i];

            if (memcmp(entry->name, parsed_name, 11) == 0) {
                if (entry->first_cluster >= 2) {
                    fat16_free_cluster_chain(entry->first_cluster);
                }
                entry->name[0] = 0xE5;
                if (ata_write_sectors(fs.root_start + s, 1, sector) != 0) return -1;

                for (int j = 0; j < fs.file_count; j++) {
                    if (fs.files[j].used && strcasecmp(fs.files[j].name, name) == 0) {
                        fs.files[j].used = 0;
                        break;
                    }
                }

                return 0;
            }
        }
    }

    return -1;
}

int fat16_create_file(const char* name) {
    return fat16_write_file(name, "", 0);
}

void fat16_list_files(void) {
    if (!fs.mounted) {
        vga_puts("FAT16 not mounted\n");
        return;
    }

    if (fs.file_count == 0) {
        vga_puts("  (empty)\n");
        return;
    }

    for (int i = 0; i < fs.file_count; i++) {
        if (fs.files[i].used) {
            vga_printf("  %-15s %u bytes\n", fs.files[i].name, fs.files[i].size);
        }
    }
}

int fat16_file_exists(const char* name) {
    for (int i = 0; i < fs.file_count; i++) {
        if (fs.files[i].used && strcasecmp(fs.files[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

fat16_fs_t* fat16_get_fs(void) {
    return &fs;
}
