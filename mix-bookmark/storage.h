/* SPDX-License-Identifier: GPL-3.0-only
 * Versioned local records. Separate files prevent a new save damaging older saves.
 * Worker-only I/O. The caller opens the current SDK data directory for each request.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define BOOK_LIMIT 64
#define RECIPE_COUNT 5
struct book_deck {
    uint32_t deck, content_id, recipe;
    int32_t has_stems, stems_on;
    float bpm, pos_s;
    char title[128], artist[128];
};
struct bookmark {
    uint32_t magic, version;
    int64_t wall;
    struct book_deck d[2];
    uint32_t checksum;
};
static uint32_t book_checksum(const struct bookmark *b)
{
    const unsigned char *p = (const unsigned char *)b;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(struct bookmark, checksum); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static void book_seal(struct bookmark *b)
{
    b->magic = 0x4d495842; b->version = 1; b->checksum = book_checksum(b);
}
static int book_valid(const struct bookmark *b)
{
    if (b->magic != 0x4d495842 || b->version != 1 || b->checksum != book_checksum(b) ||
        b->d[0].deck == b->d[1].deck) return 0;
    for (int i = 0; i < 2; i++) {
        const struct book_deck *d = &b->d[i];
        if (d->deck > 3 || d->recipe >= RECIPE_COUNT || !d->title[0] ||
            !memchr(d->title, 0, sizeof d->title) || !memchr(d->artist, 0, sizeof d->artist) ||
            !isfinite(d->bpm) || !isfinite(d->pos_s) || d->bpm < 0 || d->bpm > 400 || d->pos_s < -1 ||
            d->has_stems < -1 || d->has_stems > 1 || d->stems_on < -1 || d->stems_on > 1) return 0;
    }
    return 1;
}
/* Returns 1 loaded, 0 absent, -1 corrupt/unreadable. Never skips corruption. */
static int book_read(int dir, int slot, struct bookmark *b)
{
    char name[32]; snprintf(name, sizeof name, "mix-%02d.book", slot + 1);
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    unsigned char extra; struct stat st;
    int ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == (off_t)sizeof *b;
    ssize_t n = ok ? read(fd, b, sizeof *b) : -1;
    ok = n == (ssize_t)sizeof *b && read(fd, &extra, 1) == 0 && book_valid(b);
    close(fd); return ok ? 1 : -1;
}
/* A complete scan is required before writing: no deletion, replacement or wraparound. */
static int book_scan(int dir, struct bookmark *last, int *count, int *free_slot)
{
    *count = 0; *free_slot = -1;
    for (int s = 0; s < BOOK_LIMIT; s++) {
        struct bookmark b; int r = book_read(dir, s, &b);
        if (r < 0) return -1;
        if (!r) { if (*free_slot < 0) *free_slot = s; continue; }
        (*count)++;
        if (*count == 1 || b.wall >= last->wall) *last = b;
    }
    return 0;
}
static int book_write(int dir, int slot, const struct bookmark *b)
{
    if (slot < 0 || slot >= BOOK_LIMIT || !book_valid(b)) return -1;
    char name[32]; snprintf(name, sizeof name, "mix-%02d.book", slot + 1);
    struct stat st;
    if (fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) return -1;
    /* A stale temporary file is left for recovery, never silently discarded. */
    int fd = openat(dir, "pending.book", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    int ok = write(fd, b, sizeof *b) == (ssize_t)sizeof *b && fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (!ok) { unlinkat(dir, "pending.book", 0); return -1; }
    /* Only this mod's single writer owns these names. */
    if (renameat(dir, "pending.book", dir, name) != 0) return -1;
    if (fsync(dir) != 0 && errno != EINVAL && errno != ENOTSUP) return -1;
    return 0;
}
