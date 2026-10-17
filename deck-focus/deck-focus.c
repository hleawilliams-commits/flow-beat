/* SPDX-License-Identifier: GPL-3.0-only
 * Native deck selection, read from ErP LED output, never inferred from playback.
 * Byte 0x0c / 0x44: primary select level in bits 0..1, secondary in bits 2..3.
 * Observed for both sides: primary=2 secondary=1 means 1/2; reverse means 3/4.
 * Do not infer a selection from unknown combinations. Native selected=2, dim=1.
 * Four small, click-through SDK surfaces; no overlay over waveform pixels.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "azmod-mod.h"
#define MOD_ID "deck-focus"
static const az_core_v1 *C;
static az_surface *badge[2], *edge[2];
static int show_row=-1, visible[2], drawn_deck[2]={-1,-1};
static unsigned selected_bits, frame_serial; /* atomic bus -> UI handoff */
static unsigned seen_serial;
static long last_frame_ms;
#define ACCENT 0x32D5FFu

static long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1000L+ts.tv_nsec/1000000L;
}
static int selection(unsigned char v) {
    unsigned primary=v&3u, secondary=(v>>2)&3u;
    if(primary==2 && secondary==1) return 1;
    if(primary==1 && secondary==2) return 2;
    return 0;
}
static int led_frame(int bus, unsigned char *f, int len) {
    if(bus!=AZ_BUS_ERP_LED || !f || len<120) return AZ_FRAME_OK;
    unsigned bits=(unsigned)selection(f[0x0c]) | ((unsigned)selection(f[0x44])<<2);
    __atomic_store_n(&selected_bits,bits,__ATOMIC_RELEASE);
    __atomic_add_fetch(&frame_serial,1u,__ATOMIC_RELEASE);
    return AZ_FRAME_OK; /* Every byte remains unchanged. */
}
static int selected_deck(unsigned bits,int side) {
    unsigned s=(bits>>(side*2))&3u;
    return s==1 ? side : s==2 ? side+2 : -1;
}
static az_rect badge_rect(int deck) {
    return (az_rect){10+(deck&1)*640,561+(deck/2)*120,44,46};
}
static az_rect edge_rect(int deck) {
    return (az_rect){54+(deck&1)*640,561+(deck/2)*120,574,3};
}
static void paint_badge(az_draw *d,az_rect a,void *user) {
    int side=(int)(intptr_t)user, deck=drawn_deck[side];
    if(deck<0 || a.w<44 || a.h<46) return;
    C->fill_rect(d,a.x,a.y,a.w,a.h,ACCENT);
    C->draw_rect(d,a.x,a.y,a.w,a.h,2,0xFFFFFF);
    char num[2]={(char)('1'+deck),0};
    C->draw_text(d,a.x+12,a.y+2,num,28,0x07131A);
}
static void paint_edge(az_draw *d,az_rect a,void *user) {
    (void)user; C->fill_rect(d,a.x,a.y,a.w,a.h,ACCENT);
}
static void update(long now,int count,int enabled) {
    unsigned serial=__atomic_load_n(&frame_serial,__ATOMIC_ACQUIRE);
    if(serial!=seen_serial) {seen_serial=serial;last_frame_ms=now;}
    unsigned bits=__atomic_load_n(&selected_bits,__ATOMIC_ACQUIRE);
    int fresh=serial && now>=last_frame_ms && now-last_frame_ms<1000;
    for(int side=0;side<2;side++) {
        int deck=selected_deck(bits,side);
        int want=enabled && count==4 && fresh && deck>=0;
        if(want && deck!=drawn_deck[side]) {
            drawn_deck[side]=deck;
            C->surface_place(badge[side],badge_rect(deck));
            C->surface_place(edge[side],edge_rect(deck));
            C->surface_redraw(badge[side]); C->surface_redraw(edge[side]);
        }
        if(want!=visible[side]) {
            C->surface_show(badge[side],want);C->surface_show(edge[side],want);
            visible[side]=want;
        }
    }
}
static void tick(void) {
    update(now_ms(),C->deck_count(),show_row<0 || C->row_value(show_row)==1);
}
static int init(const az_core_v1 *core) {
    if(!core || core->abi!=AZ_MOD_ABI) return 1;
    C=core;
    if(!AZ_HAS(C,deck_count)||!AZ_HAS(C,surface_create)||
       !AZ_HAS(C,surface_place)||!AZ_HAS(C,surface_show)||!AZ_HAS(C,surface_redraw)||
       !AZ_HAS(C,fill_rect)||!AZ_HAS(C,draw_rect)||!AZ_HAS(C,draw_text)) return 2;
    for(int side=0;side<2;side++) {
        az_surface_v1 s={.size=sizeof s,.layer=AZ_LAYER_OVER_EP147,
            .views=AZ_VIEW_BIT(AZ_VIEW_DECK),.on_paint=paint_badge,.user=(void*)(intptr_t)side};
        badge[side]=C->surface_create(MOD_ID,&s);
        s.on_paint=paint_edge;edge[side]=C->surface_create(MOD_ID,&s);
        if(!badge[side] || !edge[side]) return 3;
    }
    if(AZ_HAS(C,register_row)&&AZ_HAS(C,row_value)) {
        const az_row_v1 row={.size=sizeof row,.key="show",.label="Selected deck highlight",
            .kind=AZ_ROW_TOGGLE,.flags=AZ_ROW_DEFAULT_ON,
            .help="Cyan number and panel edge = selected for the jog controls. 4 DECK view only. Orange MASTER is separate."};
        show_row=C->register_row(MOD_ID,&row);
    }
    return 0;
}
static const char *const TAGS[]={"any",NULL};
static const az_mod_v1 MOD={.abi=AZ_MOD_ABI,.size=sizeof MOD,.id=MOD_ID,.fw_tags=TAGS,.init=init,.tick=tick,.on_frame=led_frame};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) {return &MOD;}
