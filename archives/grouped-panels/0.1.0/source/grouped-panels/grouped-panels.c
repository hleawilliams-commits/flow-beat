/* SPDX-License-Identifier: GPL-3.0-only
 * Four compact, click-through headers. Stock BPM boxes, remain/time, overview
 * waveforms and tempo controls stay visible and receive their own touches.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "azmod-mod.h"
#include "layout.h"
#include "data.h"
#define MOD_ID "grouped-panels"
static const az_core_v1 *C;
static az_surface *gp_surface[4];
static int gp_show_row=-1, gp_visible[4];
static gp_readout gp_display[4];
static int gp_selected[4];
static unsigned gp_select_bits,gp_serial,gp_seen_serial;
static long gp_last_frame,gp_last_poll=-250;
#define GP_ACCENT 0x32D5FFu
static long gp_ui_now_ms(void) {
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1000L+ts.tv_nsec/1000000L;
}
static int gp_led_frame(int bus,unsigned char *f,int len) {
    if(bus!=AZ_BUS_ERP_LED || !f || len<120) return AZ_FRAME_OK;
    unsigned bits=(unsigned)gp_selection(f[0x0c])|((unsigned)gp_selection(f[0x44])<<2);
    __atomic_store_n(&gp_select_bits,bits,__ATOMIC_RELEASE);
    __atomic_add_fetch(&gp_serial,1u,__ATOMIC_RELEASE);
    return AZ_FRAME_OK;
}
/* Trim only whole UTF-8 codepoints; paint-time width is the SDK's actual font. */
static void gp_fit_text(char *s,int px,int width) {
    size_t n=strlen(s);
    while(n && C->text_width(s,px)>width) {
        do {--n;} while(n && (((unsigned char)s[n]&0xc0u)==0x80u));
        s[n]=0;
    }
}
static void gp_paint(az_draw *d,az_rect a,void *user) {
    int deck=(int)(intptr_t)user;
    if(deck<0 || deck>=4 || a.w<520 || a.h<46) return;
    const gp_readout *v=&gp_display[deck];
    uint32_t text=C->color(AZ_COL_TEXT),bg=C->color(AZ_COL_BG);
    int day=AZ_HAS(C,ui_theme) && C->ui_theme()==1;
    uint32_t muted=day?0x505967:0xA5A2B0, cell=day?0xD5D9DF:0x30303D;
    uint32_t accent=day?0x006482:GP_ACCENT;
    C->fill_rect(d,a.x,a.y,a.w,a.h,bg);
    C->fill_rect(d,a.x,a.y,44,a.h,gp_selected[deck]?GP_ACCENT:cell);
    char number[2]={(char)('1'+deck),0};
    C->draw_text(d,a.x+12,a.y+2,number,27,gp_selected[deck]?0x07131A:text);
    if(gp_selected[deck]) C->fill_rect(d,a.x,a.y,a.w,3,accent);
    char title[128];
    snprintf(title,sizeof title,"%s",v->loaded?(v->title[0]?v->title:"Track loaded"):"No track loaded");
    gp_fit_text(title,16,360);
    C->draw_text(d,a.x+54,a.y+1,title,16,text);
    if(gp_selected[deck]) C->draw_text(d,a.x+454,a.y+3,deck%2?"RIGHT":"LEFT",12,accent);
    char key[32],phrase[32];
    snprintf(key,sizeof key,"KEY %s",v->loaded && v->key[0]?v->key:"--");
    snprintf(phrase,sizeof phrase,"PHRASE %s",v->loaded?"?":"--");
    if(v->loaded && v->phrase_known) snprintf(phrase,sizeof phrase,"PHRASE %d/8",v->bar);
    C->draw_text(d,a.x+54,a.y+25,key,14,v->loaded && v->key[0]?text:muted);
    uint32_t pc=v->phrase_known?(v->bar==8?0xD49A21:accent):muted;
    C->draw_text(d,a.x+172,a.y+25,phrase,14,pc);
    for(int i=0;i<8;i++) {
        int x=a.x+320+i*24,y=a.y+30;
        C->fill_rect(d,x,y,21,10,cell);
        if(v->loaded && v->phrase_known) {
            int w=i<v->bar-1?21:i==v->bar-1?21*v->beat/4:0;
            if(w>0) C->fill_rect(d,x,y,w,10,pc);
        }
    }
}
static void gp_reset_phrase(void *user) {
    int deck=(int)(intptr_t)user;
    gp_data_reset(deck);
    if(AZ_HAS(C,banner)) {
        char text[80];snprintf(text,sizeof text,"Deck %d: phrase anchor requested",deck+1);
        C->banner(MOD_ID,text,1300);
    }
}
static void gp_tick(void) {
    long now=gp_ui_now_ms();
    unsigned serial=__atomic_load_n(&gp_serial,__ATOMIC_ACQUIRE);
    if(serial!=gp_seen_serial) {gp_seen_serial=serial;gp_last_frame=now;}
    unsigned bits=__atomic_load_n(&gp_select_bits,__ATOMIC_ACQUIRE);
    int fresh=gp_selection_fresh(serial,now,gp_last_frame);
    int enabled=gp_show_row<0 || C->row_value(gp_show_row)==1;
    gp_data_enable(enabled);
    int show=enabled && C->deck_count()==4;
    int poll=now-gp_last_poll>=250;
    if(poll) gp_last_poll=now;
    for(int deck=0;deck<4;deck++) {
        int changed=0, selected=fresh && gp_selected_deck(bits,deck&1)==deck;
        if(selected!=gp_selected[deck]) {gp_selected[deck]=selected;changed=1;}
        if(poll) {
            gp_readout next={0};gp_data_read(deck,&next);
            if(memcmp(&next,&gp_display[deck],sizeof next)) {gp_display[deck]=next;changed=1;}
        }
        if(show!=gp_visible[deck]) {
            gp_visible[deck]=show;C->surface_show(gp_surface[deck],show);changed=1;
        }
        if(show && changed) C->surface_redraw(gp_surface[deck]);
    }
}
static int gp_init(const az_core_v1 *core) {
    if(!core || core->abi!=AZ_MOD_ABI) return 1;
    C=core;
    if(!AZ_HAS(C,deck_count)||!AZ_HAS(C,deck_info)||!AZ_HAS(C,deck_grid)||
       !AZ_HAS(C,surface_create)||!AZ_HAS(C,surface_place)||!AZ_HAS(C,surface_show)||
       !AZ_HAS(C,surface_redraw)||!AZ_HAS(C,draw_text)||!AZ_HAS(C,fill_rect)||
       !AZ_HAS(C,text_width)||!AZ_HAS(C,color)||!AZ_HAS(C,thread_start)) return 2;
    for(int deck=0;deck<4;deck++) {
        az_surface_v1 s={.size=sizeof s,.layer=AZ_LAYER_OVER_EP147,
            .views=AZ_VIEW_BIT(AZ_VIEW_DECK),.on_paint=gp_paint,.user=(void*)(intptr_t)deck};
        gp_surface[deck]=C->surface_create(MOD_ID,&s);
        if(!gp_surface[deck]) return 3;
        C->surface_place(gp_surface[deck],gp_header_rect(deck));
    }
    if(AZ_HAS(C,register_row)&&AZ_HAS(C,row_value)) {
        az_row_v1 row={.size=sizeof row,.key="show",.label="Grouped deck panels",
            .kind=AZ_ROW_TOGGLE,.flags=AZ_ROW_DEFAULT_ON,
            .help="4 DECK headers: original Camelot key, relative phrase and left/right selection. Native BPM and controls stay visible."};
        gp_show_row=C->register_row(MOD_ID,&row);
        static const char *const keys[]={"reset1","reset2","reset3","reset4"};
        static const char *const labels[]={"Deck 1 phrase starts now","Deck 2 phrase starts now","Deck 3 phrase starts now","Deck 4 phrase starts now"};
        for(int deck=0;deck<4;deck++) {
            az_row_v1 action={.size=sizeof action,.key=keys[deck],.label=labels[deck],
                .kind=AZ_ROW_ACTION,.parent="show",.on_activate=gp_reset_phrase,.user=(void*)(intptr_t)deck,
                .help="Sets the relative eight-bar counter's anchor. Start on a downbeat; this is not song-structure detection."};
            C->register_row(MOD_ID,&action);
        }
    }
    return gp_data_init(C,MOD_ID);
}
static const char *const GP_TAGS[]={"any",NULL};
static const az_mod_v1 GP_MOD={.abi=AZ_MOD_ABI,.size=sizeof GP_MOD,.id=MOD_ID,
    .fw_tags=GP_TAGS,.init=gp_init,.tick=gp_tick,.on_frame=gp_led_frame};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) {return &GP_MOD;}
