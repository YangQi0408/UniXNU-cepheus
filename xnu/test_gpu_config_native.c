#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "cepheus_gpu_config_native.h"
#include "../tests/gpu_config_fixture.h"
#include "../tests/gpll0-scope-fixture.h"
static uint8_t *mapped;
static unsigned reads,guards,fail_guard,begins,ends,revoked,late_loss,change_db,changed,change_owner;
static uint32_t fuse;
static int missing_fuse,begin_error,finish_error;
static struct ceph_gpu_config_native_owner owner;
static int validate(void *);
static uint32_t rh_read(uintptr_t base,uint32_t off)
{
    assert(rh_lock && base==(uintptr_t)mapped && !(off&3u) && off<1024 && !revoked);
    ++reads;return config_read(mapped,off);
}
int cepheus_gpu_fuse_sample(uint32_t *out)
{if(missing_fuse)return -1;*out=fuse;return 0;}
#include "cepheus_gpu_config_native_private.h"
static int begin(void *ctx,const char *sha)
{
    assert(ctx==mapped && rh_lock && !strcmp(sha,CE_GPU_CONFIG_FDT_SHA256));++begins;
    return begin_error;
}
static void revoke(void)
{revoked=1;assert(!mprotect(mapped,4096,PROT_NONE));}
static int validate(void *ctx)
{
    assert(ctx==mapped && rh_lock);++guards;
    if(fail_guard && guards>=fail_guard)revoke();
    if(change_db && !changed && ceph_config_native.config.initial_freq==1) {
        mapped[548]=32;changed=1;
    }
    if(change_owner){change_owner=0;owner.validate=NULL;owner.finish=NULL;owner.rpmh_token=0;}
    return revoked?-1:0;
}
static void finish(void *ctx,int error)
{
    assert(ctx==mapped && rh_lock);++ends;finish_error=error;
    if(late_loss)revoke();
}
static void init(unsigned bin)
{
    assert(!mprotect(mapped,4096,PROT_READ|PROT_WRITE));config_fixture();memcpy(mapped,config_db,1024);
    gpll0_scope_reset();rh_db=(uintptr_t)mapped;rh_db_size=1024;
    memset(&ceph_config_native,0,sizeof(ceph_config_native));
    reads=guards=fail_guard=begins=ends=revoked=late_loss=change_db=changed=change_owner=0;
    missing_fuse=begin_error=finish_error=0;fuse=bin<<29;
    owner=(struct ceph_gpu_config_native_owner){mapped,begin,validate,finish,1};
}
static void unchanged_failure(void)
{
    struct ceph_gpu_config c,saved;memset(&c,0x5a,sizeof(c));saved=c;
    assert(cepheus_gpu_config_build_native(&owner,&c) && !memcmp(&c,&saved,sizeof(c)) && !rh_lock);
    unsigned r=reads,g=guards,b=begins,f=ends;
    assert(cepheus_gpu_config_build_native(&owner,&c) && reads==r && guards==g && begins==b && ends==f);
}
int main(void)
{
    mapped=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(mapped!=MAP_FAILED);
    struct ceph_gpu_config c;
    for(unsigned bin=0;bin<2;++bin) {
        init(bin);assert(!cepheus_gpu_config_build_native(&owner,&c));
        assert(begins==1 && ends==1 && !finish_error && c.policy.bin==bin && reads && !rh_lock);
        assert(!cepheus_gpu_config_check_native(&c));
        unsigned before=reads;struct ceph_gpu_config copy=c;
        copy.bw.ddr_data[3][1]^=1;assert(cepheus_gpu_config_check_native(&copy)==EINVAL && reads==before);
        copy=c;copy.cx.priority[1]^=1;assert(cepheus_gpu_config_check_native(&copy)==EINVAL);
        copy=c;copy.pdc.resources[1].levels[2]++;assert(cepheus_gpu_config_check_native(&copy)==EINVAL);
        copy=c;copy.policy.acd^=1;assert(cepheus_gpu_config_check_native(&copy)==EINVAL);
        assert(!cepheus_gpu_config_check_native(&c));
        fuse^=1u<<29;assert(cepheus_gpu_config_check_native(&c)==EBUSY && !ceph_config_native.ready);
    }
    init(0);assert(!cepheus_gpu_config_build_native(&owner,&c));unsigned boundaries=guards;
    for(unsigned n=1;n<=boundaries;++n) {
        init(0);fail_guard=n;unchanged_failure();
        assert(guards==n && ends==1 && revoked && !ceph_config_native.ready);
    }
    init(0);late_loss=1;unchanged_failure();assert(ends==1 && revoked && !finish_error);
    init(0);change_db=1;unchanged_failure();assert(changed && finish_error);
    init(0);change_owner=1;assert(!cepheus_gpu_config_build_native(&owner,&c) && ends==1 && !owner.validate);
    init(0);missing_fuse=1;unchanged_failure();assert(!reads && ends==1);
    init(0);fuse=UINT32_MAX;unchanged_failure();assert(!reads);
    init(0);fuse=2u<<29;unchanged_failure();assert(!reads);
    init(0);mapped[0]=0;unchanged_failure();assert(reads && ends==1);
    init(0);begin_error=EINVAL;unchanged_failure();assert(begins==1 && !ends && !reads);
    init(0);owner.rpmh_token=0;unchanged_failure();assert(!begins && !reads);
    init(0);rh_db=0;unchanged_failure();assert(!begins && !reads);
    init(0);rh_db_size=1023;unchanged_failure();assert(!begins && !reads);
    init(0);rh_poisoned=true;unchanged_failure();assert(!begins && !reads);
    init(0);scope_expire=5;unchanged_failure();assert(!ceph_config_native.ready);
    for(int context=1;context<=3;++context) {
        init(0);scope_context=context;assert(cepheus_gpu_config_build_native(&owner,&c)==EBUSY && !reads && !begins);
    }
    init(0);rh_lock=1;assert(cepheus_gpu_config_build_native(&owner,&c)==EBUSY && rh_lock && !reads);rh_lock=0;
    init(0);assert(cepheus_gpu_config_check_native(&c)==EBUSY && !reads);
    assert(cepheus_gpu_config_check_native(NULL)==EINVAL);
    assert(!munmap(mapped,4096));
    printf("Native config: actual parser/scope, two bins, %u exhaustive owner PROT_NONE boundaries, late loss/drift/refusal/copy identity PASS (synthetic DB/fuse/owner, no GPU)\n",boundaries);
}
