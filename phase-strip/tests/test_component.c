/* SPDX-License-Identifier: GPL-3.0-only
 * Exercise the real mod's registered callbacks against a recording SDK. These
 * checks protect classic geometry, saved-setting interpretation and deck mapping;
 * they do not substitute for native-font or hardware screenshots. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../phase-strip.c"

struct az_surface { int token; };
struct az_draw { int token; };
static struct az_surface fixture_surface;
static struct az_draw fixture_draw;
static az_surface_v1 descriptor;
static az_rect area;
static int values[3] = { 1, 0, 0 }, registrations, count = 4;
static int loaded[4] = {1,1,1,1}, playing[4] = {1,1,1,1};
static double phases[4];
static int grid_ok[4] = {1,1,1,1};
static int visible, placements, redraws, nlabels, nrulers, nticks[4], checks, cases;
static int label_decks[4], label_y[4];
static int ruler_y[4];
#define CHECK(ok) do { checks++; if (!(ok)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #ok); assert(ok); } } while (0)
static int register_row(const char *id, const az_row_v1 *row)
{
    CHECK(!strcmp(id, "phase-strip"));
    CHECK(registrations < 3);
    const char *keys[] = { "show", "when", "position" };
    CHECK(!strcmp(row->key, keys[registrations]));
    if (!registrations) CHECK(row->kind == AZ_ROW_TOGGLE && row->flags == AZ_ROW_DEFAULT_ON);
    else {
        CHECK(row->kind == AZ_ROW_CHOICE && !strcmp(row->parent, "show"));
        CHECK(row->default_choice == 0);
        if (registrations == 2) {
            CHECK(row->n_choices == 3);
            CHECK(!strcmp(row->choices[0], "Top"));
            CHECK(!strcmp(row->choices[1], "Band"));
            CHECK(!strcmp(row->choices[2], "Low"));
        }
    }
    return registrations++;
}
static int row_value(int handle) { CHECK(handle >= 0 && handle < 3); return values[handle]; }
static int deck_count(void) { return count; }
static int deck_info(int deck, az_deck_info *out)
{
    CHECK(deck >= 0 && deck < 4); CHECK(out->size == (int)sizeof *out);
    out->loaded=loaded[deck]; out->playing=playing[deck]; return 0;
}
static int deck_grid(int deck, double *beat, double *bar)
{
    CHECK(deck >= 0 && deck < 4); *beat=phases[deck]; *bar=-1;
    return grid_ok[deck] ? 0 : -1;
}
static az_surface *create(const char *id, const az_surface_v1 *desc)
{
    CHECK(!strcmp(id,"phase-strip")); descriptor=*desc; return &fixture_surface;
}
static void place(az_surface *s, az_rect rect)
{
    CHECK(s == &fixture_surface); CHECK(rect.x == 212 && rect.w == 838 && rect.h == 47);
    CHECK(rect.y >= 0 && rect.y + rect.h <= 800); area=rect; placements++;
}
static void show(az_surface *s, int on) { CHECK(s==&fixture_surface); visible=on; }
static void redraw(az_surface *s) { CHECK(s==&fixture_surface); redraws++; }
static uint32_t color(int slot) { return slot==AZ_COL_TEXT_DIM ? 0x777777 : 0xffffff; }
static void bounds(int x,int y,int w,int h)
{
    CHECK(w>=0 && h>=0);
    CHECK(x>=area.x && y>=area.y && x+w<=area.x+area.w && y+h<=area.y+area.h);
}
static void fill(az_draw *d,int x,int y,int w,int h,uint32_t rgb)
{
    CHECK(d==&fixture_draw); bounds(x,y,w,h);
    if (w==2 && h==47) CHECK(x==630 && y==area.y && rgb==0xffffff);
    else if (h==1) {
        CHECK(nrulers<4); ruler_y[nrulers++]=y;
        CHECK(x==226 && w==822 && rgb==0x777777);
    } else {
        CHECK(w==3 && h==10);
        int lane=(y-area.y-2)/11; CHECK(lane>=0 && lane<4);
        const int decks[]={2,0,1,3}; CHECK(rgb==DECK_RGB[decks[lane]]);
        CHECK(y==area.y+2+lane*11); nticks[lane]++;
    }
}
static void text(az_draw *d,int x,int y,const char *s,int px,uint32_t rgb)
{
    CHECK(d==&fixture_draw); CHECK(px==8 && strlen(s)==1 && s[0]>='1' && s[0]<='4');
    bounds(x,y,6,12); CHECK(x==214); CHECK(nlabels<4);
    int deck=s[0]-'1'; CHECK(rgb==(playing[deck]==1 ? DECK_RGB[deck] : 0x777777));
    label_decks[nlabels]=deck; label_y[nlabels++]=y;
}
static void log_message(const char *id,const char *msg,...) { (void)id;(void)msg; }
static az_core_v1 core={
    .abi=AZ_MOD_ABI,.size=sizeof(az_core_v1),.log=log_message,
    .register_row=register_row,.row_value=row_value,.deck_count=deck_count,
    .deck_info=deck_info,.deck_grid=deck_grid,.surface_create=create,.surface_place=place,
    .surface_show=show,.surface_redraw=redraw,.fill_rect=fill,.draw_text=text,.color=color
};
static void render(void)
{
    nlabels=nrulers=0; memset(nticks,0,sizeof nticks);
    descriptor.on_paint(&fixture_draw,area,descriptor.user); cases++;
    const int expected[]={2,0,1,3};
    int row=0;
    for(int lane=0;lane<4;lane++) {
        int deck=expected[lane];
        if(loaded[deck]!=1) { CHECK(nticks[lane]==0); continue; }
        CHECK(label_decks[row]==deck); CHECK(label_y[row]==area.y+lane*11);
        CHECK(ruler_y[row]==area.y+7+lane*11);
        int ticking=playing[deck]==1 && grid_ok[deck] && phases[deck]>=0 && phases[deck]<=1;
        CHECK((nticks[lane]>0)==ticking); row++;
    }
    CHECK(nlabels==row && nrulers==row);
}
int main(void)
{
    CHECK(init(&core)==0); CHECK(registrations==3);
    CHECK(descriptor.on_input==NULL); /* Native touch targets remain available. */
    CHECK(descriptor.views==AZ_VIEW_BIT(AZ_VIEW_DECK)); CHECK(descriptor.layer==AZ_LAYER_OVER_EP147);
    tick(); CHECK(visible && placements==1 && area.y==46);
    /* A saved 0.1.1 Band/Low choice is interpreted identically after 0.1.2 ignored it. */
    const int saved_positions[]={0,1,2,-1,3,99};
    const int expected_y[]={46,72,752,46,46,46};
    for(unsigned p=0;p<sizeof saved_positions/sizeof saved_positions[0];p++) {
        values[2]=saved_positions[p]; tick(); CHECK(area.y==expected_y[p]);
        for(int step=0;step<=16;step++) {
            for(int deck=0;deck<4;deck++) phases[deck]=(double)((step+deck)%17)/16;
            render();
        }
    }
    for(int deck=0;deck<4;deck++) {
        loaded[deck]=0;render();loaded[deck]=1;
        playing[deck]=0;render();playing[deck]=1;
        grid_ok[deck]=0;render();grid_ok[deck]=1;
        phases[deck]=-1;render();phases[deck]=1.01;render();phases[deck]=0;
    }
    values[0]=0;tick();CHECK(!visible);values[0]=1;tick();CHECK(visible);
    count=2;tick();CHECK(!visible);values[1]=1;tick();CHECK(visible);
    /* Moving the position while hidden is applied on the next visible tick. */
    values[0]=0;tick();values[2]=2;tick();CHECK(!visible);
    values[0]=1;tick();CHECK(visible && area.y==752);
    /* A core without optional row support still starts in classic Top/4-DECK mode. */
    core.register_row=NULL;core.row_value=NULL;r_show=r_when=r_pos=-1;shown=0;placed_y=-1;
    count=4;CHECK(init(&core)==0);tick();CHECK(visible && area.y==46);render();
    printf("phase-strip: %d paint scenarios, %d assertions; classic bounds, 3-1-2-4 mapping, saved position migration, visibility and input transparency\n",cases,checks);
    return 0;
}
