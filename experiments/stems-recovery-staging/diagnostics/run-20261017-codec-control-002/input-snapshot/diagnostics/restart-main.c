/* SPDX-License-Identifier: GPL-3.0-only */
/* Appended to the original harness; original assertion is unchanged. */
int main(void) {
    char tmp[]="/tmp/az-staging-diagnostic-XXXXXX";
    char *made=mkdtemp(tmp);if(!made)abort();snprintf(folder,sizeof folder,"%s",made);
    for(int k=0;k<NSTEM;k++) {
        char path[1200];snprintf(path,sizeof path,"%s/%d.ogg",folder,k);
        FILE *f=fopen(path,"w");if(!f)abort();fclose(f);
    }
    unsigned completed=0;
    for(unsigned attempt=0;attempt<DIAGNOSTIC_ATTEMPTS;attempt++) {
        int d=(int)(attempt%NDECK),before=atomic_load(&failed);
        diag_count=0;diag_self=NULL;
        completion_boundary(d,1);
        CHECK("independent registry confirms every actual worker joined",diag_all_joined());
        CHECK("restart leaves no loader in flight",!atomic_load(&g_stem_loading[d]));
        completed++;
        if(atomic_load(&failed)!=before) {diag_dump(attempt,d);break;}
        if(attempt==0)diag_dump(attempt,d);
    }
    boundary_enabled=0;
    for(int d=0;d<NDECK;d++){recovery_free_buffers(d);free_stems(d);}
    for(int k=0;k<NSTEM;k++){char path[1200];snprintf(path,sizeof path,"%s/%d.ogg",folder,k);unlink(path);}
    char path[1200];snprintf(path,sizeof path,"%s/azstem.env",folder);unlink(path);rmdir(folder);
    printf("focused restart cases=%u; %d passed, %d failed\n",completed,atomic_load(&passed),atomic_load(&failed));
    return atomic_load(&failed)?1:0;
}
