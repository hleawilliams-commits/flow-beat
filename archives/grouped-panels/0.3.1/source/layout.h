/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef GP_LAYOUT_H
#define GP_LAYOUT_H
#include "azmod-mod.h"
static int gp_selection(unsigned char v) {
    unsigned primary=v&3u, secondary=(v>>2)&3u;
    if(primary==2 && secondary==1) return 1;
    if(primary==1 && secondary==2) return 2;
    return 0;
}
static int gp_selected_deck(unsigned bits,int side) {
    unsigned s=(bits>>(side*2))&3u;
    return s==1 ? side : s==2 ? side+2 : -1;
}
static az_rect gp_header_rect(int deck) {
    return (az_rect){10+(deck&1)*640,561+(deck/2)*120,520,46};
}
/* Layouts. The SDK's deck_count() says 2 for both 2 DECK and 2/4 DECK, so the mod
 * remembers which native tab the DJ last touched. 0 = not known: draw nothing in a
 * two-deck view rather than risk covering 2 DECK's remain/tempo readouts. */
enum { GP_LAYOUT_UNKNOWN=0, GP_LAYOUT_2=2, GP_LAYOUT_24=3, GP_LAYOUT_4=4 };
/* The native 2 DECK | 2/4 DECK | 4 DECK tab group, measured on the 1280x800 screen. */
static int gp_layout_tab(int x,int y) {
    if(y<16 || y>50) return GP_LAYOUT_UNKNOWN;
    if(x>=182 && x<=242) return GP_LAYOUT_2;
    if(x>=244 && x<=311) return GP_LAYOUT_24;
    if(x>=314 && x<=374) return GP_LAYOUT_4;
    return GP_LAYOUT_UNKNOWN;
}
static int gp_layout_now(int deck_count,int tab) {
    if(deck_count==4) return GP_LAYOUT_4;
    if(deck_count==2 && (tab==GP_LAYOUT_2 || tab==GP_LAYOUT_24)) return tab;
    return GP_LAYOUT_UNKNOWN; /* includes a stale 4 DECK tap while the core says 2 */
}
/* 2/4 DECK keeps 4 DECK's lower panels exactly. 2 DECK has one wide panel per side
 * whose title row (x..x+620, y561..607) carries the header; slots 2 and 3 are unused. */
static az_rect gp_slot_rect(int layout,int slot) {
    if(layout==GP_LAYOUT_2) return (az_rect){10+(slot&1)*640,561,620,46};
    return gp_header_rect(slot);
}
/* Which deck a slot shows: fixed in 4 and 2/4; in 2 DECK each side shows the deck
 * its physical side has selected (left 1 or 3, right 2 or 4), -1 when unknown. */
static int gp_slot_deck(int layout,int slot,unsigned bits,int fresh) {
    if(layout==GP_LAYOUT_4 || layout==GP_LAYOUT_24) return slot;
    if(layout==GP_LAYOUT_2 && slot<2 && fresh) return gp_selected_deck(bits,slot);
    return -1;
}
static int gp_selection_fresh(unsigned serial,long now,long last) {
    return serial && now>=last && now-last<1000;
}
#endif
