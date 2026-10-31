/* SPDX-License-Identifier: GPL-3.0-only
 * From Grouped Panels' layout.h: deck selection from the LED frame and the native layout tabs.
 * The header and slot placement is gone: layout.txt places the panels. */
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
static int gp_selection_fresh(unsigned serial,long now,long last) {
    return serial && now>=last && now-last<1000;
}
#endif
