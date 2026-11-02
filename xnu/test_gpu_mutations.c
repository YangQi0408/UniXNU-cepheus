/* Deterministic malformed-input corpus; ASan/UBSan catches decoder overruns.
 * This is not exhaustive fuzz coverage and never maps hardware. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_gmu_firmware.h"
#include "cepheus_arc.h"
#include "cepheus_bcm.h"
#include "cepheus_hfi.h"
static uint32_t rng=0x640a55;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
struct bytes {uint8_t *data;size_t size;};
static uint32_t rd(void *p,uint32_t off){struct bytes *b=p;assert(!(off&3) && off+4<=b->size);return ceph_gmu_le32(b->data+off);}
static int okay(void *p){(void)p;return 0;}
int main(void)
{
    uint8_t input[512];struct ceph_gmu_block blocks[32];
    for(unsigned run=0;run<20000;++run) {
        for(unsigned i=0;i<sizeof(input);++i)input[i]=(uint8_t)next();
        size_t length=next()%sizeof(input);unsigned count=99;
        int e=ceph_gmu_plan(input,length,blocks,32,&count);
        if(e)assert(!count);else for(unsigned i=0;i<count;++i)assert(blocks[i].file_offset+blocks[i].size<=length);
        /* Every other record has a valid DB prefix to exercise slave bounds. */
        if(run&1){memset(input,0,8);input[0]=1;input[4]=0xdb;input[5]=0x30;input[6]=3;input[7]=0x0c;input[140]&=0xfe;}
        struct bytes data={input,sizeof(input)};struct ceph_arc_db db={&data,sizeof(input),rd};
        struct ceph_arc_resource arc;struct ceph_bcm_resource bcm;char name[9];
        (void)ceph_arc_lookup(&db,"cx.lvl",&arc);
        (void)ceph_arc_lookup_address(&db,0x30000,name,&arc);
        (void)ceph_bcm_lookup(&db,"SH0",&bcm);
        uint32_t words[16],r=next()%20,w=next()%20,out[16];unsigned n;
        for(unsigned i=0;i<16;++i)words[i]=next();
        struct ceph_hfi_queue q={words,&r,&w,16,2,{NULL,okay,okay,okay,okay},0};
        e=ceph_hfi_read(&q,out,16,&n);if(e)assert(!n);else assert(n<=16 && r<16);
    }
    puts("GPU deterministic mutation corpus: 20000 GMU/ARC/BCM/HFI cases PASS (bounded negative coverage)");
}
