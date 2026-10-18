#include <assert.h>
#include <stdio.h>
#include "../layout.h"
int main(void) {
    for(unsigned v=0;v<256;v++) {
        int want=((v&15)==6)?1:((v&15)==9)?2:0;
        assert(gp_selection((unsigned char)v)==want);
    }
    for(int l=0;l<4;l++)for(int r=0;r<4;r++) {
        unsigned bits=(unsigned)l|((unsigned)r<<2);
        assert(gp_selected_deck(bits,0)==(l==1?0:l==2?2:-1));
        assert(gp_selected_deck(bits,1)==(r==1?1:r==2?3:-1));
    }
    assert(!gp_selection_fresh(0,10,10));assert(gp_selection_fresh(1,999,0));
    assert(!gp_selection_fresh(1,1000,0));assert(!gp_selection_fresh(1,1,2));
    for(int d=0;d<4;d++) {
        az_rect a=gp_header_rect(d);assert(a.x>=10 && a.y>=561 && a.x+a.w<=1270 && a.y+a.h<=727);
        assert(a.x+a.w==530+640*(d&1)); /* Native BPM begins here. */
        assert(a.y+a.h<=612+120*(d/2)); /* Remain/time and overview below. */
        for(int j=0;j<d;j++) {az_rect b=gp_header_rect(j);assert(a.x+a.w<=b.x || b.x+b.w<=a.x || a.y+a.h<=b.y || b.y+b.h<=a.y);}
    }
    /* Native tab hits; anything else (including the tab-group gaps) is ignored. */
    assert(gp_layout_tab(212,33)==GP_LAYOUT_2);assert(gp_layout_tab(277,33)==GP_LAYOUT_24);
    assert(gp_layout_tab(343,33)==GP_LAYOUT_4);assert(!gp_layout_tab(243,33));assert(!gp_layout_tab(312,33));
    assert(!gp_layout_tab(212,15));assert(!gp_layout_tab(212,51));assert(!gp_layout_tab(100,33));assert(!gp_layout_tab(400,33));
    /* deck_count()==4 is authoritative; two-deck views need a matching tap. */
    for(int t=0;t<=4;t++) assert(gp_layout_now(4,t)==GP_LAYOUT_4);
    assert(gp_layout_now(2,GP_LAYOUT_2)==GP_LAYOUT_2);assert(gp_layout_now(2,GP_LAYOUT_24)==GP_LAYOUT_24);
    assert(gp_layout_now(2,GP_LAYOUT_4)==GP_LAYOUT_UNKNOWN);assert(gp_layout_now(2,0)==GP_LAYOUT_UNKNOWN);
    assert(gp_layout_now(0,GP_LAYOUT_2)==GP_LAYOUT_UNKNOWN);
    /* 2/4 = 4 DECK panels; unknown never maps a deck. */
    for(int s=0;s<4;s++) {
        az_rect a=gp_slot_rect(GP_LAYOUT_24,s),b=gp_header_rect(s);
        assert(a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h);
        assert(gp_slot_deck(GP_LAYOUT_24,s,0,0)==s);assert(gp_slot_deck(GP_LAYOUT_4,s,0,0)==s);
        assert(gp_slot_deck(GP_LAYOUT_UNKNOWN,s,5,1)==-1);
    }
    /* 2 DECK: title rows only, above TRACK/REMAIN (y>=612), side by side. */
    for(int s=0;s<2;s++) {
        az_rect a=gp_slot_rect(GP_LAYOUT_2,s);
        assert(a.y==561 && a.y+a.h<=607 && a.x>=10 && a.x+a.w<=1270 && a.w==620);
    }
    assert(gp_slot_rect(GP_LAYOUT_2,0).x+620<=gp_slot_rect(GP_LAYOUT_2,1).x);
    /* Sides follow selection: left 1 or 3, right 2 or 4; stale or none hides. */
    for(int l=0;l<4;l++)for(int r=0;r<4;r++) {
        unsigned bits=(unsigned)l|((unsigned)r<<2);
        assert(gp_slot_deck(GP_LAYOUT_2,0,bits,1)==gp_selected_deck(bits,0));
        assert(gp_slot_deck(GP_LAYOUT_2,1,bits,1)==gp_selected_deck(bits,1));
        assert(gp_slot_deck(GP_LAYOUT_2,0,bits,0)==-1);
        assert(gp_slot_deck(GP_LAYOUT_2,2,bits,1)==-1);assert(gp_slot_deck(GP_LAYOUT_2,3,bits,1)==-1);
    }
    puts("layout: selection decoding, stale signals, all headers, native control boundaries, layout tabs and 2 / 2-4 DECK slots passed");
}
