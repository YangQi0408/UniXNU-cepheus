#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "cepheus_gpu_firmware.h"
extern "C" {
#include "cepheus_zap_pas.h"
#include "cepheus_cp_memory_api.h"
}
static int mode;
static uint64_t snapshot;
static ceph_zap_pas state={};
static ceph_zap_claim claim={};
static unsigned acquired,releases,kept,invocations,quiesced,resuming;
static std::vector<unsigned> commands;
static std::vector<uint8_t> elf,mdt,load;
static std::vector<uint8_t> read_file(const char *path)
{
    std::ifstream f(path,std::ios::binary);assert(f);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});
}
static void inaccessible()
{
    ceph_zap_memory_view view={};uint64_t other=0;
    assert(cepheus_zap_memory_get(claim.handle,&view)<0 && !view.mdt);
    assert(cepheus_zap_memory_release_cpu(claim.handle)<0);
    assert(cepheus_zap_memory_prepare(snapshot,&other)<0 && !other);
    assert(cepheus_gpu_memory_release_cpu(snapshot)<0);
}
static int acquire(void *,const ceph_zap_claim *c,uint64_t *epoch)
{
    ++acquired;claim=*c;inaccessible();
    assert(ceph_zap_memory_authenticated(c)<0);
    assert(ceph_memory_cp_output_safe(const_cast<const uint8_t *>(c->memory.mdt),8)<0 &&
        ceph_memory_cp_output_safe(c->memory.image,8)<0);
    std::thread reader(inaccessible);reader.join();
    for(size_t i=0;i<mdt.size();++i)assert(c->memory.mdt[i]==mdt[i]);
    assert(!memcmp(c->memory.image,load.data(),load.size()));
    if(mode==2)return -4;
    *epoch=0x640001;return 0;
}
static int lease(void *,const ceph_zap_claim *c,uint64_t epoch)
{
    assert(epoch==0x640001 && c->handle==claim.handle && c->memory.image_pa==claim.memory.image_pa);
    if(mode==3 || (mode==18 && state.phase==CE_ZP_STOP) ||
        (mode==19 && state.phase==CE_ZP_AUTH && commands.size()==3) ||
        (mode==21 && state.phase==CE_ZP_MEM) ||
        (mode==22 && state.phase==CE_ZP_IMAGE) ||
        (mode==23 && state.phase==CE_ZP_AUTH))return -1;
    return 0;
}
static int quiesce(void *,uint64_t epoch)
{assert(epoch==0x640001);++quiesced;return mode==15?-1:0;}
static void release(void *,uint64_t epoch,int keep)
{assert(epoch==0x640001);++releases;kept=keep;}
static int invoke(void *,ceph_scm_regs *r)
{
    ++invocations;
    if(resuming) {
        assert(r->x[0]==1 && r->x[1]==0x17 && r->x[6]==0xc001 && r->x[7]==0xc002);
        if(mode==12)return 0;
        resuming=0;r->x[0]=r->x[1]=0;return 0;
    }
    assert(r->x[2]==13 && !r->x[5] && !r->x[6] && !r->x[7]);
    unsigned cmd=(unsigned)r->x[0]&0xff;assert(r->x[0]==(0x42000200u|cmd));
    commands.push_back(cmd);
    ceph_zap_memory_view view={};
    assert(cepheus_zap_memory_get(claim.handle,&view)<0 && cepheus_zap_memory_release_cpu(claim.handle)<0);
    assert(ceph_zap_memory_finish(claim.handle,CE_ZAP_CPU_ABORT)<0);
    if(cmd==1) {
        assert(commands.size()==1 && r->x[1]==0x82 && r->x[3]==claim.memory.mdt_pa && !r->x[4]);
        if(mode==4)return -1;
        if(mode==5){r->x[0]=0;r->x[1]=7;return 0;}
        if(mode==6){r->x[0]=(uint64_t)-12;return 0;}
        if(mode==11 || mode==12){resuming=1;r->x[0]=1;r->x[1]=0x17;r->x[6]=0xc001;r->x[7]=0xc002;return 0;}
    } else if(cmd==2) {
        assert(commands.size()==2 && r->x[1]==3 && r->x[3]==claim.memory.image_pa && r->x[4]==4096);
        /* Model secure mem_setup destroying the old image. The production
         * CPU loader must restore it after the call and leave MDT untouched. */
        memset(const_cast<uint8_t *>(claim.memory.image),0xcc,4096);
        const_cast<volatile uint8_t *>(claim.memory.mdt)[claim.memory.mdt_capacity-1]=0x7a;
        if(mode==7)return -1;
        if(mode==8){r->x[0]=0;r->x[1]=9;return 0;}
    } else if(cmd==5) {
        assert(commands.size()==3 && r->x[1]==1 && !r->x[3] && !r->x[4]);
        assert(!memcmp(claim.memory.image,load.data(),load.size()));
        assert(std::all_of(claim.memory.image+load.size(),claim.memory.image+claim.memory.image_capacity,
            [](uint8_t b){return !b;}));
        assert(claim.memory.mdt[claim.memory.mdt_capacity-1]==0x7a);
        for(size_t i=0;i<mdt.size();++i)assert(claim.memory.mdt[i]==mdt[i]);
        if(mode==9)return -1;
        if(mode==10){r->x[0]=0;r->x[1]=11;return 0;}
    } else {
        assert(cmd==6 && commands.size()==4 && quiesced==1 && r->x[1]==1 && !r->x[3] && !r->x[4]);
        if(mode==13)return -1;
        if(mode==14){r->x[0]=0;r->x[1]=13;return 0;}
    }
    r->x[0]=r->x[1]=0;return 0;
}
static void encoding()
{
    ceph_zap_memory_view v={};v.mdt_pa=0x180000000;v.image_pa=0x181000000;
    v.mdt_capacity=16384;v.image_capacity=1048576;v.mdt_bytes=6860;v.logical_bytes=4096;
    ceph_scm_regs r={};
    assert(!ceph_zap_pas_regs(CE_SCM_ARMV8_64,CE_ZP_CMD_INIT,&v,&r));
    assert(r.x[0]==0x42000201 && r.x[1]==0x82 && r.x[2]==13 && r.x[3]==v.mdt_pa);
    assert(ceph_zap_pas_regs(CE_SCM_ARMV8_32,CE_ZP_CMD_INIT,&v,&r)<0);
    v.image_pa=v.mdt_pa;assert(ceph_zap_pas_regs(CE_SCM_ARMV8_64,CE_ZP_CMD_MEM,&v,&r)<0);
    v.image_pa=0x181000000;v.logical_bytes=1048576;
    assert(ceph_zap_pas_regs(CE_SCM_ARMV8_64,CE_ZP_CMD_MEM,&v,&r)<0);
    v.logical_bytes=4096;v.mdt_pa=(1ULL<<40)-8192;
    assert(ceph_zap_pas_regs(CE_SCM_ARMV8_64,CE_ZP_CMD_INIT,&v,&r)<0);
    v.mdt_pa=0x180000000;
    assert(ceph_zap_pas_regs(CE_SCM_ARMV8_64,(ceph_zap_pas_command)7,&v,&r)<0);
}
int main(int argc,char **argv)
{
    encoding();
    ceph_zap_pas_resources resources={nullptr,acquire,lease,quiesce,release};
    ceph_zap_pas_transport transport={nullptr,invoke};
    ceph_scm_discovery discovery={};
    if(argc!=3) {
        assert(ceph_zap_pas_start(&state,0,&discovery,&resources,&transport)<0);
        assert(!invocations && !acquired && !mock_io::alloc_calls && state.phase==CE_ZP_REFUSED);
        puts("PAS13 encoding/unsupported admission PASS; real firmware lifecycle requires CEPHEUS_ZAP_ELF");return 0;
    }
    mode=atoi(argv[2]);assert(mode>=0 && mode<=23);
    elf=read_file(argv[1]);std::string base=argv[1];base.resize(base.size()-4);
    mdt=read_file((base+".mdt").c_str());load=read_file((base+".b02").c_str());
    assert(!cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&snapshot));
    discovery.convention=mode==20?CE_SCM_ARMV8_32:CE_SCM_ARMV8_64;
    discovery.available_mask=0x1f;discovery.supported=mode==1?0:1;discovery.terminal=1;
    if(mode==17)mock_io::fail_alloc_at=mock_io::alloc_calls+2;
    int e=ceph_zap_pas_start(&state,snapshot,&discovery,&resources,&transport);
    if(!e) {
        assert(state.phase==CE_ZP_LIVE && state.exposed && state.epoch==0x640001 && !releases);
        assert(!cepheus_gpu_memory_release_cpu(snapshot));snapshot=0;
        assert(cepheus_zap_memory_release_cpu(claim.handle)<0);
        assert(ceph_zap_memory_reload(claim.handle)<0);
        assert(!ceph_zap_memory_authenticated(&claim));
        auto changed=claim;changed.handle^=256;assert(ceph_zap_memory_authenticated(&changed)<0);
        changed=claim;changed.memory.mdt_pa+=4096;assert(ceph_zap_memory_authenticated(&changed)<0);
        changed=claim;changed.memory.image++;assert(ceph_zap_memory_authenticated(&changed)<0);
        changed=claim;changed.memory.logical_bytes=1048576;assert(ceph_zap_memory_authenticated(&changed)<0);
        std::thread check([&]{for(unsigned n=0;n<100;++n)assert(!ceph_zap_memory_authenticated(&claim));});
        check.join();
        if(mode==16)mock_io::fail_complete=1;
        e=ceph_zap_pas_stop(&state,&transport);
        mock_io::fail_complete=0;
    }
    if(snapshot)assert(!cepheus_gpu_memory_release_cpu(snapshot));
    assert(ceph_zap_memory_authenticated(&claim)<0);
    if(mode==0 || mode==11) {
        assert(!e && state.phase==CE_ZP_STOPPED && !mock_io::live && releases==1 && !kept);
        assert(commands==std::vector<unsigned>({1,2,5,6}));
    } else if(mode==1 || mode==2 || mode==3 || mode==17 || mode==20) {
        assert(e && state.phase==CE_ZP_REFUSED && !mock_io::live && !invocations);
        assert(releases==(mode==3?1u:0u) && !kept);
    } else {
        assert(e && state.phase==CE_ZP_RETAINED && mock_io::live==2 && releases==1 && kept);
        uint64_t h=0;assert(!cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&snapshot));
        assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h);
        assert(!cepheus_gpu_memory_release_cpu(snapshot));
    }
    if(mode==12)assert(invocations==16 && commands.size()==1);
    if(mode==11)assert(invocations==5);
    unsigned before=invocations;
    assert(ceph_zap_pas_start(&state,snapshot,&discovery,&resources,&transport)<0);
    assert(ceph_zap_pas_stop(&state,&transport)<0 && invocations==before);
    printf("PAS13 real workspace/RAM transport mode=%d PASS; no native SMC executed\n",mode);
}
