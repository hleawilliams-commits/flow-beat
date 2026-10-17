/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdlib.h>
#include "../mix-bookmark.c"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while(0)
static az_deck_info test_decks[4];
static int info(int d,az_deck_info *v){*v=test_decks[d];return 0;}
int main(void)
{
    static const az_core_v1 core={.size=sizeof core,.abi=AZ_MOD_ABI,.deck_info=info};C=&core;
    struct bookmark b,got,last={0};int n,slot;
    CHECK(!make_book(&b));
    test_decks[0]=(az_deck_info){.loaded=1,.title="Test vocal",.artist="Fixture",.content_id=100,.bpm=126,.pos_s=12,.has_stems=1,.stems_on=1};
    test_decks[1]=(az_deck_info){.loaded=1,.title="Test backing",.artist="Fixture",.content_id=200,.bpm=126,.pos_s=24,.has_stems=1,.stems_on=0};
    poll_decks(2000);CHECK(!make_book(&b));poll_decks(2500);CHECK(!make_book(&b));poll_decks(3100);CHECK(make_book(&b));
    CHECK(b.d[0].recipe==1 && b.d[1].recipe==2);CHECK(b.d[0].pos_s==12 && b.d[1].stems_on==0);
    chosen[1]=0;CHECK(!make_book(&b));chosen[1]=1;CHECK(make_book(&b));
    test_decks[0].content_id=101;CHECK(!make_book(&got));test_decks[0].content_id=100;
    test_decks[0].bpm=NAN;CHECK(!make_book(&got));test_decks[0].bpm=126;
    char path[]="/tmp/mix-book-test-XXXXXX";CHECK(mkdtemp(path)!=NULL);
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);CHECK(fd>=0);
    CHECK(book_scan(fd,&last,&n,&slot)==0 && n==0 && slot==0);
    CHECK(book_write(fd,0,&b)==0);CHECK(book_read(fd,0,&got)==1 && !memcmp(&b,&got,sizeof b));
    CHECK(book_write(fd,0,&b)<0); /* never replace */
    close(fd);fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);CHECK(fd>=0);
    CHECK(book_scan(fd,&last,&n,&slot)==0 && n==1 && slot==1); /* reopen persistence */
    got=b;got.d[0].recipe=99;book_seal(&got);CHECK(!book_valid(&got));
    got=b;got.d[0].title[0]^=1;CHECK(!book_valid(&got));
    got=b;memset(got.d[0].title,'x',128);book_seal(&got);CHECK(!book_valid(&got));
    int bad=openat(fd,"mix-02.book",O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(bad>=0);CHECK(write(bad,"bad",3)==3);close(bad);
    CHECK(book_scan(fd,&last,&n,&slot)<0);CHECK(unlinkat(fd,"mix-02.book",0)==0);
    bad=openat(fd,"pending.book",O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(bad>=0);close(bad);
    CHECK(book_write(fd,1,&b)<0);CHECK(book_read(fd,0,&got)==1);CHECK(unlinkat(fd,"pending.book",0)==0);
    CHECK(symlinkat("mix-01.book",fd,"mix-02.book")==0);CHECK(book_read(fd,1,&got)<0);CHECK(unlinkat(fd,"mix-02.book",0)==0);
    for(int i=1;i<BOOK_LIMIT;i++) CHECK(book_write(fd,i,&b)==0);
    CHECK(book_scan(fd,&last,&n,&slot)==0 && n==BOOK_LIMIT && slot==-1);
    CHECK(book_write(fd,BOOK_LIMIT,&b)<0);CHECK(book_write(-1,0,&b)<0);CHECK(book_read(-1,0,&got)<0);
    for(int i=0;i<BOOK_LIMIT;i++){char name[32];snprintf(name,sizeof name,"mix-%02d.book",i+1);CHECK(unlinkat(fd,name,0)==0);}
    close(fd);CHECK(rmdir(path)==0);
    printf("%d bookmark checks passed\n",checks);return 0;
}
