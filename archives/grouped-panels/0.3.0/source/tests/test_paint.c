/* SPDX-License-Identifier: GPL-3.0-only
 * Calls the real renderer with an instrumented SDK. This checks documented line
 * boxes and field bounds, not native font pixels; emulator screenshots remain
 * the visual check. Workers are registered but deliberately not run here.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../grouped-panels.c"

struct az_surface { int index; };
struct az_draw { int token; };
static struct az_surface test_surfaces[4];
static az_surface_v1 descriptors[4];
static az_rect placed[4], area;
static struct { az_rect r; char value[128]; } text_calls[8];
static int theme, nsurfaces, ntexts, operations, paint_cases, checks, workers;
static struct az_draw draw;

#define CHECK(ok) do { checks++; if (!(ok)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#ok); assert(ok); } } while(0)

static int codepoint_bytes(const unsigned char *s)
{
    if (*s < 0x80) return 1;
    int n = (*s >= 0xc2 && *s <= 0xdf) ? 2 :
            (*s >= 0xe0 && *s <= 0xef) ? 3 :
            (*s >= 0xf0 && *s <= 0xf4) ? 4 : 0;
    CHECK(n != 0);
    for (int i=1;i<n;i++) CHECK(s[i] && (s[i]&0xc0)==0x80);
    if (n==3) CHECK(!(*s==0xe0 && s[1]<0xa0) && !(*s==0xed && s[1]>=0xa0));
    if (n==4) CHECK(!(*s==0xf0 && s[1]<0x90) && !(*s==0xf4 && s[1]>=0x90));
    return n;
}
static int width(const char *s,int px)
{
    int tenths=0;
    for (const unsigned char *p=(const unsigned char *)s;*p;) {
        int n=codepoint_bytes(p);
        tenths += n>1 ? px*10 : *p=='W' ? px*9 : px*7;
        p+=n;
    }
    return (tenths+9)/10;
}
static int intersects(az_rect a,az_rect b)
{
    return a.x < b.x+b.w && b.x < a.x+a.w && a.y < b.y+b.h && b.y < a.y+a.h;
}
static void bounds(az_rect r)
{
    CHECK(r.w>=0 && r.h>=0);
    CHECK(r.x>=area.x && r.y>=area.y);
    CHECK(r.x+r.w<=area.x+area.w && r.y+r.h<=area.y+area.h);
    /* Native BPM remains outside (2 DECK's title row has none; its panel ends at +620). */
    CHECK(r.x+r.w<=(area.w==620?area.x+620:530+640*((area.x-10)/640)));
    CHECK(r.y+r.h<=612+120*((area.y-561)/120)); /* Remain and overview remain outside. */
}
static void fill(az_draw *d,int x,int y,int w,int h,uint32_t rgb)
{
    (void)rgb;CHECK(d==&draw);bounds((az_rect){x,y,w,h});operations++;
}
static void text(az_draw *d,int x,int y,const char *s,int px,uint32_t rgb)
{
    (void)rgb;CHECK(d==&draw);CHECK(ntexts<8);CHECK(px>=8 && px<=96);
    az_rect r={x,y,width(s,px),px*3/2};bounds(r);
    for(int i=0;i<ntexts;i++) CHECK(!intersects(r,text_calls[i].r));
    text_calls[ntexts].r=r;
    snprintf(text_calls[ntexts].value,sizeof text_calls[ntexts].value,"%s",s);
    ntexts++;operations++;
}
static uint32_t color(int role) { return role==AZ_COL_BG ? (theme?0xffffff:0x101018) : (theme?0x101018:0xffffff); }
static int ui_theme(void) { return theme; }
static int deck_count(void) { return 4; }
static int deck_info(int d,az_deck_info *out) { (void)d;out->loaded=0;return 0; }
static int deck_grid(int d,double *beat,double *bar) { (void)d;*beat=*bar=-1;return -1; }
static az_surface *create(const char *id,const az_surface_v1 *s)
{
    CHECK(!strcmp(id,"grouped-panels"));CHECK(nsurfaces<4);
    descriptors[nsurfaces]=*s;test_surfaces[nsurfaces].index=nsurfaces;
    return &test_surfaces[nsurfaces++];
}
static void place(az_surface *s,az_rect r) { CHECK(s->index>=0 && s->index<4);placed[s->index]=r; }
static void show(az_surface *s,int on) { (void)s;(void)on; }
static void redraw(az_surface *s) { (void)s; }
static int start(const char *id,const char *name,void *(*fn)(void *),void *arg)
{
    (void)name;(void)arg;CHECK(!strcmp(id,"grouped-panels"));CHECK(fn!=NULL);workers++;return 0;
}
static int events(const char *id,void (*fn)(int,int,void *),void *user)
{
    (void)user;CHECK(!strcmp(id,"grouped-panels"));CHECK(fn==gp_deck_event);return 0;
}
static int has_text(const char *s)
{
    for(int i=0;i<ntexts;i++) if(!strcmp(text_calls[i].value,s)) return 1;
    return 0;
}
static void render(int deck)
{
    gp_layout=GP_LAYOUT_4;gp_slot_shows[deck]=deck;
    area=gp_header_rect(deck);ntexts=0;
    descriptors[deck].on_paint(&draw,area,descriptors[deck].user);paint_cases++;
    char number[2]={(char)('1'+deck),0};CHECK(has_text(number));
    CHECK(!has_text("LEFT") && !has_text("RIGHT")); /* the cyan border shows selection */
    CHECK(ntexts==4+(gp_display[deck].loaded && gp_hints[deck].has_gap));
}
static void setup_readout(int deck,int state)
{
    gp_readout *v=&gp_display[deck];memset(v,0,sizeof *v);
    if(!state) return;
    v->loaded=1;snprintf(v->title,sizeof v->title,"Access Denied");
    if(state!=1) snprintf(v->key,sizeof v->key,"12B");
    if(state>=2) {v->phrase_known=1;v->bar=state==2?1:8;v->beat=4;}
    if(state==4) {memset(v->title,'W',sizeof v->title-1);v->title[sizeof v->title-1]=0;}
    if(state==5) snprintf(v->title,sizeof v->title,"深夜のミックス 🎶 深夜のミックス 🎶 深夜のミックス 🎶");
}
int main(void)
{
    az_core_v1 core={.abi=AZ_MOD_ABI,.size=sizeof core,.deck_count=deck_count,
        .deck_info=deck_info,.deck_grid=deck_grid,.surface_create=create,.surface_place=place,
        .surface_show=show,.surface_redraw=redraw,.draw_text=text,.fill_rect=fill,
        .text_width=width,.color=color,.ui_theme=ui_theme,.thread_start=start,.on_deck_event=events};
    CHECK(gp_init(&core)==0);CHECK(nsurfaces==4);CHECK(workers==1);
    for(int deck=0;deck<4;deck++) {
        az_rect expected=gp_header_rect(deck);
        CHECK(!memcmp(&placed[deck],&expected,sizeof expected));
        CHECK(descriptors[deck].on_input==NULL); /* Core makes these windows input-transparent. */
        CHECK(descriptors[deck].views==AZ_VIEW_BIT(AZ_VIEW_DECK));
        CHECK(descriptors[deck].layer==AZ_LAYER_OVER_EP147);
    }
    for(theme=0;theme<=1;theme++) for(int deck=0;deck<4;deck++)
        for(int selected=0;selected<=1;selected++) for(int state=0;state<6;state++) {
            gp_selected[deck]=selected;setup_readout(deck,state);render(deck);
            CHECK(has_text(state==0?"KEY --":state==1?"KEY --":"KEY 12B"));
            CHECK(has_text(state==0?"PHRASE --":state==1?"PHRASE ?":state==2?"PHRASE 1/8":"PHRASE 8/8"));
        }
    for(theme=0;theme<=1;theme++) for(int left=1;left<=2;left++) for(int right=1;right<=2;right++) {
        unsigned char frame[128],before[128];memset(frame,0xa5,sizeof frame);
        frame[0x0c]=left==1?6:9;frame[0x44]=right==1?6:9;memcpy(before,frame,sizeof frame);
        CHECK(gp_led_frame(AZ_BUS_ERP_LED,frame,sizeof frame)==AZ_FRAME_OK);
        CHECK(!memcmp(frame,before,sizeof frame));
        unsigned bits=__atomic_load_n(&gp_select_bits,__ATOMIC_ACQUIRE);
        for(int deck=0;deck<4;deck++) {
            gp_selected[deck]=gp_selected_deck(bits,deck&1)==deck;setup_readout(deck,3);render(deck);
        }
    }
    for(theme=0;theme<=1;theme++) for(int deck=0;deck<4;deck++) for(int bar=1;bar<=8;bar++) for(int beat=1;beat<=4;beat++) {
        setup_readout(deck,2);gp_display[deck].bar=bar;gp_display[deck].beat=beat;render(deck);
    }
    /* Every mix hint at once: alerts, stems, longest gap text, coloured keys. No overlap,
     * nothing outside the header or into native BPM, in both layouts' widths. */
    double gaps[]={0,0.4,-7.9,8.0};
    for(theme=0;theme<=1;theme++) for(int deck=0;deck<4;deck++) for(int alert=0;alert<3;alert++)
        for(int match=0;match<4;match++) for(unsigned gi=0;gi<4;gi++) for(int stems=0;stems<3;stems++) {
            setup_readout(deck,4);gp_display[deck].has_stems=stems>0;gp_display[deck].stems_on=stems==2;
            gp_hints[deck]=(gp_hint){match,alert,1,gaps[gi]};gp_selected[deck]=deck&1;render(deck);
            CHECK(has_text(gaps[gi]==0?"±0.0":gi==1?"+0.4":gi==2?"-7.9":"+8.0"));
        }
    memset(gp_hints,0,sizeof gp_hints);
    /* 2 DECK: each side's slot paints the deck that side selected, without the
     * redundant highlight or LEFT/RIGHT; text stays clear of the stock BPM column. */
    for(theme=0;theme<=1;theme++) for(int slot=0;slot<2;slot++) for(int deck=slot;deck<4;deck+=2)
        for(int state=0;state<6;state++) {
            gp_layout=GP_LAYOUT_2;gp_slot_shows[slot]=deck;gp_selected[deck]=1;setup_readout(deck,state);
            area=gp_slot_rect(GP_LAYOUT_2,slot);ntexts=0;
            descriptors[slot].on_paint(&draw,area,descriptors[slot].user);paint_cases++;
            char number[2]={(char)('1'+deck),0};CHECK(has_text(number));
            CHECK(!has_text("LEFT") && !has_text("RIGHT"));CHECK(ntexts==4);
            CHECK(has_text(state<=1?"KEY --":"KEY 12B"));
        }
    /* A slot with no deck (unknown layout or stale selection) paints nothing. */
    {int ops=operations;gp_slot_shows[0]=-1;area=gp_slot_rect(GP_LAYOUT_2,0);
     descriptors[0].on_paint(&draw,area,descriptors[0].user);CHECK(operations==ops);gp_slot_shows[0]=0;}
    gp_layout=GP_LAYOUT_4;
    int before=operations;area=(az_rect){10,561,519,46};gp_paint(&draw,area,(void *)(intptr_t)0);CHECK(operations==before);
    area=gp_header_rect(0);gp_paint(&draw,area,(void *)(intptr_t)4);CHECK(operations==before);
    printf("paint: %d scenarios, %d draw calls, %d assertions; day/night, all decks and selection pairs, empty/unknown/long ASCII/UTF-8 titles, Camelot 12B, all 32 phrase beats; no text overlap or drawing outside headers; native BPM/overview bounds clear; input-transparent surfaces; LED bytes unchanged\n",paint_cases,operations,checks);
    return 0;
}
