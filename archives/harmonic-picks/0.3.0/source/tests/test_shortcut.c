#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../harmonic-picks.c"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static void idle(unsigned char *f) {
    for (int i=0;i<152;i++) f[i]=(unsigned char)(i*17+3);
    f[6]=8; f[31]=0; f[32]=f[33]=0;
}
static void reset(void) { native_request=0;native_phase=0;native_until=0;native_requested_at=0; }
int main(void) {
    unsigned char f[152], before[152]; reset(); idle(f); memcpy(before,f,152);
    CHECK(native_frame_at(AZ_BUS_MIXER_IN,f,152,100)==AZ_FRAME_OK);
    CHECK(!memcmp(f,before,152));
    CHECK(native_request_browse(100)); CHECK(!native_request_browse(101));
    CHECK(native_frame_at(AZ_BUS_DECK_IN,f,152,110)==AZ_FRAME_OK); CHECK(!memcmp(f,before,152));
    native_frame_at(AZ_BUS_MIXER_IN,f,96,110); CHECK(!memcmp(f,before,152));
    native_frame_at(AZ_BUS_MIXER_IN,f,98,120);
    CHECK(f[32]==0x40 && f[6]==0);
    for(int i=0;i<152;i++) if(i!=6 && i!=32) CHECK(f[i]==before[i]);
    idle(f); native_frame_at(AZ_BUS_MIXER_IN,f,152,319); CHECK(f[32]==0x40);
    idle(f); native_frame_at(AZ_BUS_MIXER_IN,f,152,320); CHECK(!memcmp(f,before,152)); CHECK(!native_request);
    idle(f); native_frame_at(AZ_BUS_MIXER_IN,f,152,321); CHECK(!memcmp(f,before,152));
    for(int bit=0;bit<16;bit++) {
        reset();native_request_browse(100); idle(f); f[32+bit/8]=(unsigned char)(1<<(bit%8));memcpy(before,f,152);
        native_frame_at(AZ_BUS_MIXER_IN,f,152,101);CHECK(!memcmp(f,before,152));CHECK(!native_request);
    }
    reset();native_request_browse(100);idle(f);native_frame_at(AZ_BUS_MIXER_IN,f,152,101);
    idle(f);f[33]=1;memcpy(before,f,152);native_frame_at(AZ_BUS_MIXER_IN,f,152,102);
    CHECK(!memcmp(f,before,152));CHECK(!native_request); /* physical LOAD 4 preserved */
    reset();native_request_browse(100);idle(f);f[31]=1;memcpy(before,f,152);
    native_frame_at(AZ_BUS_MIXER_IN,f,152,101);CHECK(!memcmp(f,before,152));CHECK(!native_request);
    reset();native_request_browse(100);idle(f);memcpy(before,f,152);
    native_frame_at(AZ_BUS_MIXER_IN,f,152,1101);CHECK(!memcmp(f,before,152));CHECK(!native_request);
    reset();native_request_browse(100);native_frame_at(AZ_BUS_MIXER_IN,f,152,99);
    CHECK(!memcmp(f,before,152));CHECK(!native_request);
    reset();native_request_browse(100);idle(f);native_frame_at(AZ_BUS_MIXER_IN,f,152,101);
    idle(f);f[10]=211;f[90]=99;native_frame_at(AZ_BUS_MIXER_IN,f,152,201);
    CHECK(f[10]==211 && f[90]==99);CHECK(f[33]==0);
    printf("PASS: %d shortcut assertions\n",checks);
}
