/* Included by the official SDK harness: exercise real surface drawing after LED input. */
static int harness_scenarios(const char *id) {
    if(strcmp(id,"deck-focus")) return 0;
    expect(mod->on_frame!=NULL,"native LED callback is registered");
    D.view=AZ_VIEW_DECK; D.count=4;
    unsigned char f[120]={0},copy[120];
    for(int l=0;l<2;l++)for(int r=0;r<2;r++) {
        f[12]=l?0x29:0x26;f[68]=r?0x29:0x26;memcpy(copy,f,sizeof f);
        mod->on_frame(AZ_BUS_ERP_LED,f,sizeof f);
        expect(!memcmp(copy,f,sizeof f),"LED frame unchanged");
        ticks(1);paint_surfaces(1);
        expect(surfs[0].shown&&surfs[2].shown,"both selected numbers visible");
        expect(surfs[0].r.y==561+l*120&&surfs[2].r.y==561+r*120,"highlights follow selected panels");
        char left[2]={(char)('1'+l*2),0},right[2]={(char)('2'+r*2),0};
        expect(frame_has(left)&&frame_has(right),"selected numbers are drawn within their badges");
        expect(!surfs[0].d.on_input&&!surfs[2].d.on_input,"highlights pass touches through");
    }
    D.count=2;ticks(1);expect(!surfs[0].shown&&!surfs[2].shown,"hidden outside four-deck layout");
    D.count=4;mod->on_frame(AZ_BUS_ERP_LED,f,sizeof f);ticks(1);
    advance(2*NS);call_tick();expect(!surfs[0].shown&&!surfs[2].shown,"stale selection hides");
    f[12]=f[68]=0;mod->on_frame(AZ_BUS_ERP_LED,f,sizeof f);ticks(1);
    expect(!surfs[0].shown&&!surfs[2].shown,"unknown selection hides");
    return 1;
}
