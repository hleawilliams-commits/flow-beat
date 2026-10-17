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
    puts("layout: selection decoding, stale signals, all headers and native control boundaries passed");
}
