/* SPDX-License-Identifier: GPL-3.0-only */
#include "azmod-mod.h"
#include "storage.h"
#include <pthread.h>
#include <time.h>
#define MOD_ID "mix-bookmark"
static const az_core_v1 *C;
static az_surface *surface;
static int expanded, page_on, writer_ok, chosen[2] = {0, 1}, recipes[2] = {1, 2};
static az_rect button_where;

static az_rect bookmark_button_rect(void)
{
    int four = AZ_HAS(C, deck_count) && C->deck_count() == 4;
    return four ? (az_rect){950, 50, 120, 30} : (az_rect){1174, 50, 96, 30};
}
static void place_button(void)
{
    button_where = bookmark_button_rect();
    C->surface_place(surface, button_where);
}
static const char *const recipe_names[] = {"Full track", "Vocals", "Instrumental", "Drums", "Bass"};
static az_rect buttons[13];
static az_deck_info decks[4], prior[4];
static long stable_since[4], last_tick, page_seen;
static int stable[4];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct bookmark pending, saved[BOOK_LIMIT];
static int saved_index;
static dev_t shown_dev, pending_dev;
static ino_t shown_ino, pending_ino;
static int queued, busy, count, changed;
static char status[112] = "Reading tagged combos from USB...";
static long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec*1000L+t.tv_nsec/1000000; }
static void redraw(void) { if (surface) C->surface_redraw(surface); if (AZ_HAS(C, request_redraw)) C->request_redraw(); }
static void message(const char *s) { pthread_mutex_lock(&mu); snprintf(status,sizeof status,"%s",s); changed=1; pthread_mutex_unlock(&mu); }
static int same_track(const az_deck_info *a, const az_deck_info *b)
{
    return a->loaded == 1 && b->loaded == 1 && a->title[0] && a->content_id == b->content_id &&
        !strcmp(a->title,b->title) && !strcmp(a->artist,b->artist);
}
static void poll_decks(long now)
{
    for (int d=0;d<4;d++) {
        az_deck_info v = {.size=sizeof v,.has_stems=-1,.stems_on=-1,.pos_s=-1};
        if (C->deck_info(d,&v) != 0) v.loaded=0;
        if (!same_track(&v,&prior[d])) stable_since[d]=now;
        stable[d]=same_track(&v,&prior[d]) && now-stable_since[d]>=1000;
        prior[d]=decks[d]=v;
    }
}
static int make_book(struct bookmark *b)
{
    memset(b,0,sizeof *b);
    if (chosen[0]==chosen[1]) return 0;
    for (int i=0;i<2;i++) {
        int d=chosen[i]; az_deck_info fresh={.size=sizeof fresh};
        if (!stable[d] || C->deck_info(d,&fresh)!=0 || !same_track(&fresh,&decks[d])) return 0;
        struct book_deck *v=&b->d[i];
        v->deck=(uint32_t)d;v->content_id=fresh.content_id;v->recipe=(uint32_t)recipes[i];
        v->has_stems=fresh.has_stems;v->stems_on=fresh.stems_on;v->bpm=fresh.bpm;v->pos_s=fresh.pos_s;
        snprintf(v->title,sizeof v->title,"%s",fresh.title);snprintf(v->artist,sizeof v->artist,"%s",fresh.artist);
    }
    b->wall=(int64_t)time(NULL);book_seal(b);return book_valid(b);
}
static void save_pair(void)
{
    struct bookmark b;
    if (!writer_ok) { message("Storage worker unavailable; nothing saved."); return; }
    if (!make_book(&b)) { message("Choose two different loaded decks; wait a second after loading.");return; }
    pthread_mutex_lock(&mu);
    if (!busy) { pending=b; pending_dev=shown_dev; pending_ino=shown_ino; queued=busy=1;snprintf(status,sizeof status,"Saving to USB..."); }
    changed=1;pthread_mutex_unlock(&mu);redraw();
}
static void *writer(void *arg)
{
    (void)arg;
    dev_t olddev=0;ino_t oldino=0;int had_dir=0, probed=0;
    for (;;) {
        struct bookmark b={0}, last={0}, books[BOOK_LIMIT];int save,n=0,slot=-1;
        dev_t target_dev;ino_t target_ino;
        pthread_mutex_lock(&mu);save=queued;target_dev=pending_dev;target_ino=pending_ino;if(save){b=pending;queued=0;}pthread_mutex_unlock(&mu);
        const char *path=C->data_dir(MOD_ID);
        int dir=path?open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW):-1;
        struct stat st;int valid=dir>=0 && fstat(dir,&st)==0;
        int reload=valid && (!had_dir || st.st_dev!=olddev || st.st_ino!=oldino);
        if(reload){pthread_mutex_lock(&mu);count=0;shown_dev=0;shown_ino=0;pthread_mutex_unlock(&mu);}
        if (!valid) {
            if(!probed || had_dir || save) message("USB unavailable; nothing saved.");
            pthread_mutex_lock(&mu);count=0;shown_dev=0;shown_ino=0;pthread_mutex_unlock(&mu);
            had_dir=0;
        } else if (save && (st.st_dev!=target_dev || st.st_ino!=target_ino || !target_ino)) {
            message("USB changed or not ready; save refused. Review and try again.");
            had_dir=0;
        } else if (save || reload) {
            int ok=book_scan(dir,&last,&n,&slot)==0;
            if (!ok) message("Saved tag unreadable. Existing tags preserved; save refused.");
            else if(save && slot<0) message("64 combos tagged. Storage full; existing tags preserved.");
            else if(save && book_write(dir,slot,&b)!=0) message("Tag failed. Check USB / pending.book; existing tags preserved.");
            else {
                int nn=0;for(int k=BOOK_LIMIT-1;k>=0;k--){struct bookmark v;if(book_read(dir,k,&v)==1)books[nn++]=v;}
                n=nn;
                pthread_mutex_lock(&mu);memcpy(saved,books,(size_t)n*sizeof books[0]);count=n;saved_index=0;shown_dev=st.st_dev;shown_ino=st.st_ino;
                snprintf(status,sizeof status,save?"Combo #%d tagged for your next practice.":"%d tagged combos on this USB.",n);
                changed=1;pthread_mutex_unlock(&mu);
            }
            olddev=st.st_dev;oldino=st.st_ino;had_dir=1;
        }
        probed=1;
        if(dir>=0)close(dir);
        if(save){pthread_mutex_lock(&mu);busy=0;pthread_mutex_unlock(&mu);}
        struct timespec t={0,300000000};nanosleep(&t,NULL);
    }
    return NULL;
}
static void text(az_draw *d,az_rect a,int x,int y,const char *s,int px,uint32_t c)
{
    if(x<a.x || y<a.y || y+px*3/2>a.y+a.h)return;
    char buf[160];snprintf(buf,sizeof buf,"%s",s);size_t n=strlen(buf);
    while(n && (int)(n*px*66/100)>a.x+a.w-x-8)buf[--n]=0;
    if(n)C->draw_text(d,x,y,buf,px,c);
}
static void button(az_draw*d,az_rect a,int i,int x,int y,int w,const char*s,int active)
{
    az_rect r={x,y,w,42};if(x<a.x||x+w>a.x+a.w||y<a.y||y+42>a.y+a.h)return;
    buttons[i]=r;uint32_t col=C->color(active?AZ_COL_HI:AZ_COL_TEXT_DIM);
    C->draw_rect(d,x,y,w,42,active?2:1,col);text(d,r,x+10,y+7,s,18,col);
}
static void panel(az_draw*d,az_rect a,void*u)
{
    (void)u;page_seen=now_ms();memset(buttons,0,sizeof buttons);
    uint32_t hi=C->color(AZ_COL_HI),fg=C->color(AZ_COL_TEXT),dim=C->color(AZ_COL_TEXT_DIM);
    C->fill_rect(d,a.x,a.y,a.w,a.h,C->color(AZ_COL_BG));
    int x=a.x+20,y=a.y+14,half=(a.w-52)/2;
    text(d,a,x,y,"TAG MIX  /  good combinations from practice",24,hi);
    if(expanded)button(d,a,12,a.x+a.w-130,y,110,"Close",0);
    y+=46;
    text(d,a,x,y,"Good combination? Choose the two decks and tap TAG COMBO.",18,dim);y+=40;
    for(int i=0;i<2;i++) {
        int xx=x+i*(half+12);char s[160];
        int card_h=a.y+a.h-y; if(card_h>240)card_h=240;
        az_rect card={xx,y,half,card_h};
        for(int j=0;j<4;j++){snprintf(s,sizeof s,"Deck %d",j+1);button(d,a,i*4+j,xx+j*((half-18)/4+6),y,(half-18)/4,s,chosen[i]==j);}
        const az_deck_info*v=&decks[chosen[i]];
        snprintf(s,sizeof s,"%.60s",v->loaded==1?v->title:"Load a track first");text(d,card,xx,y+57,s,22,fg);
        text(d,card,xx,y+91,v->artist,18,dim);
        if(v->bpm>0)snprintf(s,sizeof s,"%.1f BPM  |  Position %.1fs",v->bpm,v->pos_s);else snprintf(s,sizeof s,"BPM / position unavailable");
        text(d,card,xx,y+123,s,18,fg);
        text(d,card,xx,y+155,v->has_stems==1?"Stems available":v->has_stems==0?"Stems unavailable":"Stem availability unknown",18,dim);
        snprintf(s,sizeof s,"Stem note: %s  >",recipe_names[recipes[i]]);button(d,a,8+i,xx,y+190,half,s,1);
    }
    y+=246;text(d,a,x,y,"Keep a reminder of what worked. Stem notes are manual; you control the pads.",17,dim);
    y+=34;button(d,a,10,x,y,250,"TAG COMBO",1);
    button(d,a,11,x+266,y,320,"Next combo  >",0);
    struct bookmark last;char msg[112];int n,index;
    pthread_mutex_lock(&mu);n=count;if(saved_index>=n)saved_index=0;index=saved_index;last=n?saved[index]:(struct bookmark){0};snprintf(msg,sizeof msg,"%s",status);pthread_mutex_unlock(&mu);
    y+=55;text(d,a,x,y,msg,18,hi);y+=42;
    if(n){
        char s[160];snprintf(s,sizeof s,"TAGGED COMBO %d / %d  /  newest first",index+1,n);text(d,a,x,y,s,19,dim);y+=32;
        for(int i=0;i<2;i++){snprintf(s,sizeof s,"D%u  %.54s  |  %.1f BPM  @ %.1fs  |  %s",last.d[i].deck+1,last.d[i].title,last.d[i].bpm,last.d[i].pos_s,recipe_names[last.d[i].recipe]);text(d,a,x,y,s,19,fg);y+=32;}
    }else text(d,a,x,y,"Nothing tagged yet. Tag a combination when it sounds good during practice.",18,dim);
}
static void collapse(void)
{
    expanded=0;if(surface){C->surface_focus(surface,0);place_button();}redraw();
}
static int input(const az_event*ev,void*u)
{
    (void)u;if(!ev)return 0;
    if(ev->kind==AZ_EV_BACK){page_on=0;collapse();return 0;}
    if(ev->kind!=AZ_EV_TAP)return 0;
    for(int i=0;i<13;i++){
        az_rect r=buttons[i];if(ev->x<r.x||ev->x>=r.x+r.w||ev->y<r.y||ev->y>=r.y+r.h)continue;
        if(i<8)chosen[i/4]=i%4;
        else if(i<10)recipes[i-8]=(recipes[i-8]+1)%RECIPE_COUNT;
        else if(i==10)save_pair();
        else if(i==11){pthread_mutex_lock(&mu);if(count)saved_index=(saved_index+1)%count;pthread_mutex_unlock(&mu);}
        else collapse();
        redraw();return 1;
    }
    return 0;
}
static void paint(az_draw*d,az_rect a,void*u)
{
    if(expanded){panel(d,a,u);return;}
    C->fill_rect(d,a.x,a.y,a.w,a.h,C->color(AZ_COL_BG));C->draw_rect(d,a.x,a.y,a.w,a.h,1,C->color(AZ_COL_HI));
    text(d,a,a.x+10,a.y+3,"TAG MIX",16,C->color(AZ_COL_HI));
}
static int surface_input(const az_event*ev,void*u)
{
    if(expanded)return input(ev,u);
    if(!ev||ev->kind!=AZ_EV_TAP)return 0;
    if(!C->surface_focus(surface,1))return 1;
    expanded=1;C->surface_place(surface,(az_rect){16,90,1248,680});redraw();return 1;
}
static void enter(void*u){(void)u;page_on=1;page_seen=now_ms();redraw();}
static void tick(void)
{
    long now=now_ms();if(now-last_tick<250)return;last_tick=now;poll_decks(now);
    if(expanded && C->view()!=AZ_VIEW_DECK)collapse();
    if(surface && !expanded){
        az_rect r=bookmark_button_rect();
        if(memcmp(&r,&button_where,sizeof r)!=0)place_button();
    }
    if(page_on && now-page_seen>2000)page_on=0;
    pthread_mutex_lock(&mu);int change=changed;changed=0;pthread_mutex_unlock(&mu);
    if(expanded || page_on || change)redraw();
}
static int init(const az_core_v1*core)
{
    if(!core||core->abi!=AZ_MOD_ABI)return 1;
    C=core;
    if(!AZ_HAS(C,deck_info)||!AZ_HAS(C,data_dir)||!AZ_HAS(C,thread_start)||!AZ_HAS(C,register_screen)||
       !AZ_HAS(C,draw_text)||!AZ_HAS(C,fill_rect)||!AZ_HAS(C,draw_rect)||!AZ_HAS(C,color))return 2;
    static const az_screen_v1 scr={sizeof(az_screen_v1),panel,input,enter,NULL};
    if(C->register_screen(MOD_ID,&scr)!=0)return 3;
    if(AZ_HAS(C,register_row)){
        static const az_row_v1 row={.size=sizeof row,.key="bookmarks",.label="Tag Mix",.kind=AZ_ROW_SCREEN,.screen=&scr,
            .help="Tag good track combinations during practice so you can find them again."};C->register_row(MOD_ID,&row);
    }
    writer_ok=C->thread_start(MOD_ID,"bookmark writer",writer,NULL)==0;
    if(!writer_ok)message("Storage worker unavailable; nothing will be saved.");
    if(AZ_HAS(C,surface_create)&&AZ_HAS(C,surface_place)&&AZ_HAS(C,surface_show)&&
       AZ_HAS(C,surface_focus)&&AZ_HAS(C,view)&&AZ_HAS(C,surface_redraw)){
        static const az_surface_v1 s={.size=sizeof s,.layer=AZ_LAYER_OVER_EP147,.views=AZ_VIEW_BIT(AZ_VIEW_DECK),.on_paint=paint,.on_input=surface_input};
        surface=C->surface_create(MOD_ID,&s);
        if(surface){place_button();C->surface_show(surface,1);}
    }
    return 0;
}
static const char*const FW[]={"any",NULL};
static const az_mod_v1 MOD={.abi=AZ_MOD_ABI,.size=sizeof MOD,.id=MOD_ID,.fw_tags=FW,.init=init,.tick=tick};
AZ_MOD_EXPORT const az_mod_v1*az_mod_v1_entry(void){return &MOD;}
