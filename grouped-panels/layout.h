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
static int gp_selection_fresh(unsigned serial,long now,long last) {
    return serial && now>=last && now-last<1000;
}
#endif
