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
#include "hud.h"
#define MOD_ID "grouped-panels"
static const az_core_v1 *C;
static az_surface *gp_surface[4];
static int gp_show_row=-1, gp_visible[4];
static gp_readout gp_display[4];
static int gp_selected[4];
static unsigned gp_select_bits,gp_serial,gp_seen_serial;
/* Slot -> deck it paints, and the layout it was placed for (UI thread). */
static int gp_slot_shows[4]={-1,-1,-1,-1}, gp_layout=-1;
static az_rect gp_slot_at[4];
/* Last native layout tab touched (touch thread writes, UI reads/saves). */
static int gp_tab_seen, gp_tab_saved;
/* Mix hints per deck (UI thread): key match, time alert, tempo gap to the reference. */
typedef struct { int match, alert, has_gap; double gap; } gp_hint;
static gp_hint gp_hints[4];
static long gp_started[4];
static int gp_hints_row=-1;
/* Mod Menu override for two-deck views: 0 Auto (tab watch), 1 2 DECK, 2 2/4 DECK. */
static int gp_view_row=-1;
static int gp_two_deck_tab(void) {
    int pick=gp_view_row>=0?C->row_value(gp_view_row):0;
    if(pick==1) return GP_LAYOUT_2;
    if(pick==2) return GP_LAYOUT_24;
    return __atomic_load_n(&gp_tab_seen,__ATOMIC_RELAXED);
}
static long gp_last_frame,gp_last_poll=-250;
#define GP_ACCENT 0x32D5FFu
static long gp_ui_now_ms(void) {
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1000L+ts.tv_nsec/1000000L;
}
/* Watches the native layout tabs only; never takes a touch. */
static int gp_touch(const az_touch *t,void *user) {
    (void)user;
    if(!t || !t->down || C->view()!=AZ_VIEW_DECK) return AZ_TOUCH_PASS;
    int tab=gp_layout_tab(t->x,t->y);
    if(tab) __atomic_store_n(&gp_tab_seen,tab,__ATOMIC_RELAXED);
    return AZ_TOUCH_PASS;
}
static int gp_led_frame(int bus,unsigned char *f,int len) {
    if(bus!=AZ_BUS_ERP_LED || !f || len<120) return AZ_FRAME_OK;
    unsigned bits=(unsigned)gp_selection(f[0x0c])|((unsigned)gp_selection(f[0x44])<<2);
    __atomic_store_n(&gp_select_bits,bits,__ATOMIC_RELEASE);
    __atomic_add_fetch(&gp_serial,1u,__ATOMIC_RELEASE);
    return AZ_FRAME_OK;
}
/* Surface text is drawn as Latin-1, not UTF-8 (a UTF-8 "·" shows as "Â·"). Convert in
 * place: Latin-1 characters keep their accents, typographic quotes/dashes become ASCII,
 * anything else becomes '?'. The result is never longer than the input. */
static void gp_disp(char *s)
{
    unsigned char *r = (unsigned char *)s, *w = r;
    while (*r) {
        unsigned c = *r, cp = 0; int n = 0;
        if (c < 0x80) { *w++ = *r++; continue; }
        if (c >= 0xc2 && c <= 0xdf) { n = 2; cp = c & 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { n = 3; cp = c & 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 4; cp = c & 0x07; }
        int ok = n > 0;
        for (int i = 1; ok && i < n; i++) { if ((r[i] & 0xc0) != 0x80) ok = 0; else cp = cp << 6 | (r[i] & 0x3f); }
        if (!ok) { *w++ = '?'; r++; continue; }
        r += n;
        if (cp <= 0xff) *w++ = (unsigned char)cp;
        else if (cp == 0x2018 || cp == 0x2019) *w++ = '\'';
        else if (cp == 0x201c || cp == 0x201d) *w++ = '"';
        else if (cp == 0x2013 || cp == 0x2014) *w++ = '-';
        else if (cp == 0x2026) { *w++ = '.'; *w++ = '.'; *w++ = '.'; }   /* 3 bytes in, 3 out */
        else *w++ = '?';
    }
    *w = 0;
}
/* Convert to the surface font's Latin-1 first, then trim to the SDK's actual width. */
static void gp_fit_text(char *s,int px,int width) {
    gp_disp(s);
    size_t n=strlen(s);
    while(n && C->text_width(s,px)>width) {
        do {--n;} while(n && (((unsigned char)s[n]&0xc0u)==0x80u));
        s[n]=0;
    }
}
static void gp_paint(az_draw *d,az_rect a,void *user) {
    int slot=(int)(intptr_t)user;
    if(slot<0 || slot>=4 || a.w<520 || a.h<46) return;
    int deck=gp_slot_shows[slot];
    if(deck<0 || deck>=4) return;
    /* In 2 DECK both visible decks are the selected ones: no highlight. */
    int marked=gp_selected[deck] && gp_layout!=GP_LAYOUT_2;
    const gp_readout *v=&gp_display[deck];
    uint32_t text=C->color(AZ_COL_TEXT),bg=C->color(AZ_COL_BG);
    int day=AZ_HAS(C,ui_theme) && C->ui_theme()==1;
    uint32_t muted=day?0x505967:0xA5A2B0, cell=day?0xD5D9DF:0x30303D;
    uint32_t accent=day?0x006482:GP_ACCENT;
    C->fill_rect(d,a.x,a.y,a.w,a.h,bg);
    const gp_hint *h=&gp_hints[deck];
    /* Time running out wins the number block; selection keeps its top border. */
    uint32_t block=h->alert==GP_ALERT_RED?0xE5484D:h->alert==GP_ALERT_AMBER?0xD49A21:marked?GP_ACCENT:cell;
    C->fill_rect(d,a.x,a.y,44,a.h,block);
    char number[2]={(char)('1'+deck),0};
    C->draw_text(d,a.x+12,a.y+2,number,27,(marked||h->alert)?0x07131A:text);
    if(marked) C->fill_rect(d,a.x,a.y,a.w,3,accent);
    /* Stems dot, bottom-right of the number block: filled = playing, hollow = found. */
    if(v->loaded && v->stems_on) C->fill_rect(d,a.x+33,a.y+35,7,7,(marked||h->alert)?0x07131A:accent);
    else if(v->loaded && v->has_stems && AZ_HAS(C,draw_rect)) C->draw_rect(d,a.x+33,a.y+35,7,7,1,(marked||h->alert)?0x07131A:muted);
    char title[128];
    snprintf(title,sizeof title,"%s",v->loaded?(v->title[0]?v->title:"Track loaded"):"No track loaded");
    gp_fit_text(title,16,360);
    C->draw_text(d,a.x+54,a.y+1,title,16,text);
    if(v->loaded && h->has_gap) {
        char gap[16];  /* converted below: the ± is UTF-8 in source */
        if(h->gap==0) snprintf(gap,sizeof gap,"±0.0");
        else snprintf(gap,sizeof gap,"%+.1f",h->gap);
        gp_disp(gap);
        C->draw_text(d,a.x+454,a.y+3,gap,14,h->gap==0?accent:muted);
    }
    char key[32],phrase[32];
    snprintf(key,sizeof key,"KEY %s",v->loaded && v->key[0]?v->key:"--");
    snprintf(phrase,sizeof phrase,"PHRASE %s",v->loaded?"?":"--");
    if(v->loaded && v->phrase_known) snprintf(phrase,sizeof phrase,"PHRASE %d/8",v->bar);
    uint32_t kc=!(v->loaded && v->key[0])?muted:h->match==GP_MATCH_GOOD?(day?0x167A3A:0x3CD070):
        h->match==GP_MATCH_RISKY?0xD49A21:h->match==GP_MATCH_CLASH?0xE5484D:text;
    gp_disp(key);
    C->draw_text(d,a.x+54,a.y+25,key,14,kc);
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
    int tab=__atomic_load_n(&gp_tab_seen,__ATOMIC_RELAXED);
    if(tab!=gp_tab_saved && AZ_HAS(C,cfg_set)) {gp_tab_saved=tab;C->cfg_set(MOD_ID,"layout",tab);}
    int layout=gp_layout_now(C->deck_count(),gp_two_deck_tab());
    int relayout=layout!=gp_layout;
    gp_layout=layout;
    int poll=now-gp_last_poll>=250;
    if(poll) gp_last_poll=now;
    int dirty[4]={0};
    for(int deck=0;deck<4;deck++) {
        int selected=fresh && gp_selected_deck(bits,deck&1)==deck;
        if(selected!=gp_selected[deck]) {gp_selected[deck]=selected;dirty[deck]=1;}
        if(poll) {
            gp_readout next={0};gp_data_read(deck,&next);
            if(memcmp(&next,&gp_display[deck],sizeof next)) {gp_display[deck]=next;dirty[deck]=1;}
        }
    }
    int hints_on=gp_hints_row<0 || C->row_value(gp_hints_row)==1;
    for(int deck=0;deck<4;deck++) {
        const gp_readout *v=&gp_display[deck];
        if(!(v->loaded && v->playing)) gp_started[deck]=0;
        else if(!gp_started[deck]) gp_started[deck]=now>0?now:1;
    }
    for(int deck=0;deck<4;deck++) {
        const gp_readout *v=&gp_display[deck];
        gp_hint next={0};
        if(hints_on && v->loaded) {
            int ref=gp_reference_deck(deck,gp_started);
            next.alert=gp_time_alert(v->playing,v->remaining_s);
            if(ref>=0) {
                next.match=gp_key_match(v->key,gp_display[ref].key);
                next.has_gap=gp_bpm_gap(v->bpm,gp_display[ref].bpm,&next.gap);
            }
        }
        if(memcmp(&next,&gp_hints[deck],sizeof next)) {gp_hints[deck]=next;dirty[deck]=1;}
    }
    for(int slot=0;slot<4;slot++) {
        int deck=enabled?gp_slot_deck(layout,slot,bits,fresh):-1;
        int show=deck>=0, changed=relayout || deck!=gp_slot_shows[slot] || (show && dirty[deck]);
        gp_slot_shows[slot]=deck;
        az_rect r=gp_slot_rect(layout,slot);
        if(show && memcmp(&r,&gp_slot_at[slot],sizeof r)) {
            gp_slot_at[slot]=r;C->surface_place(gp_surface[slot],r);changed=1;
        }
        if(show!=gp_visible[slot]) {
            gp_visible[slot]=show;C->surface_show(gp_surface[slot],show);changed=1;
        }
        if(show && changed) C->surface_redraw(gp_surface[slot]);
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
        gp_slot_at[deck]=gp_header_rect(deck);
        C->surface_place(gp_surface[deck],gp_slot_at[deck]);
    }
    /* Optional: without the touch service or a saved tab, two-deck views stay clear. */
    if(AZ_HAS(C,cfg_get)) gp_tab_seen=gp_tab_saved=C->cfg_get(MOD_ID,"layout",0);
    if(gp_tab_seen!=GP_LAYOUT_2 && gp_tab_seen!=GP_LAYOUT_24 && gp_tab_seen!=GP_LAYOUT_4)
        gp_tab_seen=gp_tab_saved=0;
    if(AZ_HAS(C,touch_filter) && AZ_HAS(C,view)) C->touch_filter(MOD_ID,gp_touch,NULL);
    if(AZ_HAS(C,register_row)&&AZ_HAS(C,row_value)) {
        az_row_v1 row={.size=sizeof row,.key="show",.label="Grouped deck panels",
            .kind=AZ_ROW_TOGGLE,.flags=AZ_ROW_DEFAULT_ON,
            .help="Deck headers with original Camelot key and relative phrase in 4 DECK, 2/4 DECK and 2 DECK. In a two-deck view, tap your layout tab once so the mod knows which one you're in. Native BPM and controls stay visible."};
        gp_show_row=C->register_row(MOD_ID,&row);
        az_row_v1 hints={.size=sizeof hints,.key="hints",.label="Mix hints",
            .kind=AZ_ROW_TOGGLE,.flags=AZ_ROW_DEFAULT_ON,.parent="show",
            .help="Key turns green, amber or red against the deck that's been playing longest. The deck number turns amber with 30 s left, red with 10 s. A dot shows stems: filled = playing, hollow = found but not playing. The small number is the BPM gap."};
        gp_hints_row=C->register_row(MOD_ID,&hints);
        static const char *const views[]={"Auto","2 DECK","2/4 DECK"};
        az_row_v1 view={.size=sizeof view,.key="two-deck",.label="Two-deck view",
            .kind=AZ_ROW_CHOICE,.parent="show",.choices=views,.n_choices=3,.default_choice=0,
            .help="Auto follows the layout tab you last tapped. Pick 2 DECK or 2/4 DECK if the headers don't match your screen. 4 DECK is always detected."};
        gp_view_row=C->register_row(MOD_ID,&view);
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
