#include <assert.h>
#include "../deck-focus.c"
static int checks;
#define CHECK(x) do {assert(x);checks++;} while(0)
static int shown[4];static az_rect rects[4];
static int idx(az_surface *s){return (int)(intptr_t)s-1;}
static void place(az_surface*s,az_rect r){rects[idx(s)]=r;}
static void show(az_surface*s,int on){shown[idx(s)]=on;}
static void redraw(az_surface*s){(void)s;}
int main(void){
 static const az_core_v1 core={.surface_place=place,.surface_show=show,.surface_redraw=redraw};C=&core;
 badge[0]=(az_surface*)1;edge[0]=(az_surface*)2;badge[1]=(az_surface*)3;edge[1]=(az_surface*)4;
 for(unsigned v=0;v<256;v++) CHECK(selection(v)==((v&15)==6?1:(v&15)==9?2:0));
 unsigned char f[120]={0},old[120];
 update(0,4,1);CHECK(!shown[0]&&!shown[2]);
 for(int left=0;left<2;left++)for(int right=0;right<2;right++){
  f[12]=left?0x29:0x26;f[68]=right?0x29:0x26;memcpy(old,f,120);
  CHECK(led_frame(AZ_BUS_ERP_LED,f,120)==AZ_FRAME_OK);CHECK(!memcmp(f,old,120));
  update(100,4,1);CHECK(drawn_deck[0]==left*2);CHECK(drawn_deck[1]==1+right*2);
  CHECK(shown[0]&&shown[1]&&shown[2]&&shown[3]);
  CHECK(rects[0].y==561+left*120);CHECK(rects[2].y==561+right*120);
  CHECK(rects[0].x==10 && rects[2].x==650);
 }
 unsigned prior=frame_serial;led_frame(AZ_BUS_ERP_IN,f,120);led_frame(AZ_BUS_ERP_LED,f,119);
 CHECK(frame_serial==prior);CHECK(!memcmp(f,old,120));
 update(200,2,1);CHECK(!shown[0]&&!shown[2]);
 update(210,4,1);CHECK(shown[0]&&shown[2]);
 update(220,4,0);CHECK(!shown[0]&&!shown[2]);
 update(230,4,1);CHECK(shown[0]&&shown[2]);
 update(1100,4,1);CHECK(!shown[0]&&!shown[2]);
 led_frame(AZ_BUS_ERP_LED,f,120);update(1200,4,1);CHECK(shown[0]&&shown[2]);
 f[12]=0;led_frame(AZ_BUS_ERP_LED,f,120);update(1210,4,1);CHECK(!shown[0]&&shown[2]);
 f[68]=0x0a;led_frame(AZ_BUS_ERP_LED,f,120);update(1220,4,1);CHECK(!shown[0]&&!shown[2]);
 puts("PASS selection mapping, immutable frames, all four pairs, geometry, stale/unknown state, layout and enable gates");
 printf("%d assertions\n",checks);
}
