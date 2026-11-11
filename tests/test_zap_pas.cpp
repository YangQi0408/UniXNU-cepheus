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
static ceph_owner_evidence_gate owner_gate={};
static ceph_owner_evidence_snapshot owner={};
static ceph_zap_resource_expectation expected={};
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
static int acquire(void *,const ceph_zap_claim *c,const ceph_owner_evidence_gate *gate,
    const ceph_owner_evidence_snapshot *evidence,uint64_t now,ceph_zap_resource_evidence *resource,
    uint64_t *epoch)
{
    ++acquired;claim=*c;inaccessible();
    assert(gate && evidence && gate->state==CE_OEG_ACCEPTED && now==1100);
    assert(evidence->checksum==gate->accepted.checksum);
    assert(ceph_zap_memory_authenticated(c)<0);
    assert(ceph_memory_cp_output_safe(const_cast<const uint8_t *>(c->memory.mdt),8)<0 &&
        ceph_memory_cp_output_safe(c->memory.image,8)<0);
    std::thread reader(inaccessible);reader.join();
    for(size_t i=0;i<mdt.size();++i)assert(c->memory.mdt[i]==mdt[i]);
    assert(!memcmp(c->memory.image,load.data(),load.size()));
    if(mode==2)return -4;
    resource->memory=(ceph_zap_resource_memory){c->handle,c->memory.mdt_pa,c->memory.image_pa,
        c->memory.mdt_capacity,c->memory.image_capacity,c->memory.mdt_bytes,
        c->memory.logical_bytes,1,16384,CE_ZRC_DEVICE_NGNRNE,CE_ZRC_NORMAL_NC,
        CE_ZRC_DEVICE_NGNRNE,1,0,0};
    resource->crypto=(ceph_zap_resource_crypto){CE_ZRS_INHERITED_UEFI,CE_ZR_COMPLETE,
        1,0,CE_ZAP_RESOURCE_BCM_COUNT,0,evidence->owner_id,evidence->lease_id,
        evidence->resource_epoch,CE_ZAP_RESOURCE_CRYPTO_MASTER,CE_ZAP_RESOURCE_CRYPTO_SLAVE,
        {CE_ZAP_RESOURCE_BCM_CE0,CE_ZAP_RESOURCE_BCM_SN11,CE_ZAP_RESOURCE_BCM_SN0,
            CE_ZAP_RESOURCE_BCM_SH0,CE_ZAP_RESOURCE_BCM_MC0,CE_ZAP_RESOURCE_BCM_ACV},
        expected.ib_bytes,expected.ab_bytes,expected.crypto_ledger_id,expected.request_count,
        expected.ledger_checksum,evidence->ack_owner_id,evidence->ack_epoch,evidence->ack_count};
    resource->magic=CE_ZAP_RESOURCE_MAGIC;resource->version=CE_ZAP_RESOURCE_VERSION;
    resource->bytes=sizeof(*resource);resource->complete=1;
    resource->owner_evidence_checksum=evidence->checksum;resource->captured_ticks=1050;
    if(mode==24)resource->crypto.ledger_checksum++;
    if(mode==25)resource->memory.image_pa=resource->memory.mdt_pa;
    if(mode==26)resource->crypto.owner_id++;
    if(mode==27)resource->checksum^=1;else ceph_zap_resource_seal(resource);
    *epoch=0x640001;return 0;
}
static int lease(void *,const ceph_zap_claim *c,uint64_t epoch,
    const ceph_zap_resource_evidence *evidence)
{
    assert(epoch==0x640001 && c->handle==claim.handle && c->memory.image_pa==claim.memory.image_pa &&
        evidence && evidence->crypto.resource_epoch==owner.resource_epoch &&
        evidence->memory.handle==c->handle);
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
static void owner_fixture()
{
    owner.magic=CE_OWNER_EVIDENCE_MAGIC;owner.version=CE_OWNER_EVIDENCE_VERSION;
    owner.bytes=sizeof(owner);owner.complete=1;owner.owner_domain=CE_OD_XNU;
    owner.writer_mask=CE_OW_ALL;owner.ack_state=CE_OA_COMPLETE;owner.ack_count=2;
    owner.retention=CE_ORT_BOOT;owner.owner_id=0x640001;owner.lease_id=0x640002;
    owner.resource_epoch=0x640003;owner.captured_ticks=1000;
    owner.ack_epoch=owner.resource_epoch;owner.ack_owner_id=owner.owner_id;
    owner.sid_mask[0]=CE_OWNER_TBU0_LOCAL_SID_MASK;
    owner.sid_mask[1]=CE_OWNER_TBU1_LOCAL_SID_MASK;
    owner.tbu[0]=(ceph_owner_tbu_evidence){CE_OWNER_TBU0_BASE,CE_OWNER_TBU_BYTES,
        0,0x400,CE_OTR_OWNER_CONFIRMED,0,owner.lease_id,CE_OWNER_TBU0_STATUS_REG,1,0};
    owner.tbu[1]=(ceph_owner_tbu_evidence){CE_OWNER_TBU1_BASE,CE_OWNER_TBU_BYTES,
        0x400,0x400,CE_OTR_OWNER_CONFIRMED,0,owner.lease_id,CE_OWNER_TBU1_STATUS_REG,1,0};
    owner.rpmh=(ceph_owner_rpmh_evidence){CE_ORS_INHERITED_UEFI,CE_OR_COMPLETE,1,0,
        owner.owner_id,0x640004,owner.resource_epoch,9,0x640005};
    ceph_owner_evidence_seal(&owner);
    owner_gate={};owner_gate.version=CE_OWNER_EVIDENCE_VERSION;
    owner_gate.state=CE_OEG_ACCEPTED;owner_gate.retained=1;owner_gate.accepted=owner;
    expected={};expected.magic=CE_ZAP_RESOURCE_EXPECTATION_MAGIC;
    expected.version=CE_ZAP_RESOURCE_VERSION;expected.bytes=sizeof(expected);
    expected.max_age_ticks=100;expected.crypto_ledger_id=0x640006;
    expected.request_count=1;expected.ledger_checksum=0x640007;
    expected.ib_bytes=1600000000;expected.ab_bytes=55250000;
}
int main(int argc,char **argv)
{
    encoding();
    owner_fixture();
    ceph_zap_pas_resources resources={nullptr,acquire,lease,quiesce,release};
    ceph_zap_pas_transport transport={nullptr,invoke};
    ceph_scm_discovery discovery={};
    if(argc!=3) {
        assert(ceph_zap_pas_start(&state,0,&discovery,&resources,&owner_gate,&owner,
            &expected,1100,&transport)<0);
        assert(!invocations && !acquired && !mock_io::alloc_calls && state.phase==CE_ZP_REFUSED);
        puts("PAS13 encoding/unsupported admission PASS; real firmware lifecycle requires CEPHEUS_ZAP_ELF");return 0;
    }
    mode=atoi(argv[2]);assert(mode>=0 && mode<=27);
    elf=read_file(argv[1]);std::string base=argv[1];base.resize(base.size()-4);
    mdt=read_file((base+".mdt").c_str());load=read_file((base+".b02").c_str());
    assert(!cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&snapshot));
    discovery.convention=mode==20?CE_SCM_ARMV8_32:CE_SCM_ARMV8_64;
    discovery.available_mask=0x1f;discovery.supported=mode==1?0:1;discovery.terminal=1;
    if(mode==17)mock_io::fail_alloc_at=mock_io::alloc_calls+2;
    int e=ceph_zap_pas_start(&state,snapshot,&discovery,&resources,&owner_gate,&owner,
        &expected,1100,&transport);
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
    } else if(mode==1 || mode==2 || mode==3 || mode==17 || mode==20 || mode>=24) {
        assert(e && state.phase==CE_ZP_REFUSED && !mock_io::live && !invocations);
        assert(releases==((mode==3 || mode>=24)?1u:0u) && !kept);
    } else {
        assert(e && state.phase==CE_ZP_RETAINED && mock_io::live==2 && releases==1 && kept);
        uint64_t h=0;assert(!cepheus_gpu_firmware_snapshot(CE_FW_A640_ZAP,elf.data(),elf.size(),&snapshot));
        assert(cepheus_zap_memory_prepare(snapshot,&h)<0 && !h);
        assert(!cepheus_gpu_memory_release_cpu(snapshot));
    }
    if(mode==12)assert(invocations==16 && commands.size()==1);
    if(mode==11)assert(invocations==5);
    unsigned before=invocations;
    assert(ceph_zap_pas_start(&state,snapshot,&discovery,&resources,&owner_gate,&owner,
        &expected,1100,&transport)<0);
    assert(ceph_zap_pas_stop(&state,&transport)<0 && invocations==before);
    printf("PAS13 real workspace/RAM transport mode=%d PASS; no native SMC executed\n",mode);
}
