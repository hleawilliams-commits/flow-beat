/* SPDX-License-Identifier: GPL-3.0-only
 * Host test with a fake core: a USB whose library can't be read is read again after 10 s
 * and says which library files the MODS USB has; a big track list reaches a phone that
 * stops reading for a few seconds.
 *   cc -I<sdk>/include -o /tmp/t tests/test_library.c -lpthread && /tmp/t */
#define _GNU_SOURCE
#include <assert.h>
#include <sys/wait.h>
#include "../crate-remote.c"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)

static int fails_left, ntracks = 6683;
static char data[256];
static int fake_tracks(const char *id, int stick, int pl, int (*fn)(const az_track *, void *), void *user)
{
    (void)id; (void)stick; (void)pl;
    if (fails_left > 0) { fails_left--; return -1; }
    for (int i = 0; i < ntracks; i++) {
        char title[64]; snprintf(title, sizeof title, "Track number %d (Extended Mix)", i);
        az_track t = {.size = sizeof t, .id = (uint32_t)i + 1, .title = title, .artist = "Some Artist",
                      .album = "", .key = "8A", .path = "", .bpm = 124.0, .len_s = 300};
        if (fn(&t, user)) break;
    }
    return 0;
}
static int fake_playlists(const char *id, int stick, int (*fn)(int, int, const char *, int, void *), void *user)
{
    (void)id; (void)stick; fn(1, 0, "Gig", 0, user); return 0;
}
static const char *fake_data_dir(const char *id) { (void)id; return data; }
static void fake_log(const char *id, const char *fmt, ...) { (void)id; (void)fmt; }

static void test_library(void)
{
    static az_core_v1 core;
    core.size = sizeof core; core.abi = AZ_MOD_ABI;
    core.lib_tracks = fake_tracks; core.lib_playlists = fake_playlists;
    core.data_dir = fake_data_dir; core.log = fake_log;
    C = &core;
    char root[] = "/tmp/crtestXXXXXX";
    assert(mkdtemp(root));
    char cmd[400];
    snprintf(cmd, sizeof cmd, "mkdir -p %s/PIONEER/rekordbox && touch %s/PIONEER/rekordbox/export.pdb", root, root);
    assert(system(cmd) == 0);
    snprintf(data, sizeof data, "%s/MODS/community-mods/crate-remote/data", root);

    fails_left = 1;
    cr_sb b = {0};
    cr_api_tracks(&b, 0, 0);
    CHECK(strstr(b.s, "\"ok\":0") && strstr(b.s, "no exportLibrary.db and export.pdb"));
    CHECK(cr_tracks_seen[0] == -2);
    cr_sb_free(&b);
    cr_secs += 5;                                   /* still within 10 s: the failure is kept */
    cr_api_tracks(&b, 0, 0);
    CHECK(strstr(b.s, "\"ok\":0")); cr_sb_free(&b);
    cr_secs += 6;                                   /* past 10 s: read again, now it works */
    cr_api_tracks(&b, 0, 0);
    CHECK(strstr(b.s, "\"ok\":1") && strstr(b.s, "Track number 6682") && cr_tracks_seen[0] == 6683);
    CHECK(b.n > 400000);
    cr_sb_free(&b);
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    assert(system(cmd) == 0);
}

/* A reader that pauses 2.5 s mid-transfer, with 1 s send timeouts: the whole body arrives. */
static void test_slow_reader(void)
{
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int small = 4096;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small);
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &small, sizeof small);
    struct timeval tv = {1, 0};
    setsockopt(sv[0], SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    size_t n = 700000;
    char *body = malloc(n); memset(body, 'x', n);
    pid_t pid = fork();
    if (pid == 0) {
        close(sv[0]);
        size_t got = 0; char buf[8192]; int paused = 0;
        for (;;) {
            if (!paused && got > 100000) { usleep(2500000); paused = 1; }
            ssize_t r = read(sv[1], buf, sizeof buf);
            if (r <= 0) break;
            got += (size_t)r;
        }
        _exit(got == 700000 ? 0 : 1);
    }
    close(sv[1]);
    cr_send_all(sv[0], body, n);
    close(sv[0]);
    int st; waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    free(body);
}

int main(void)
{
    test_library();
    test_slow_reader();
    printf("test_library: %d checks passed\n", checks);
    return 0;
}
