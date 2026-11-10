/* Actual applied cpu_idle/machine_idle bodies are inserted by the test runner. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include "cepheus_gpu_idle.h"
#define ARM64_BOARD_CONFIG_SWEET 1
#define TRUE true
#define FALSE false
#define SIGPdisabled 1u
#define __improbable(x) (x)
#define KDBG(...) ((void)0)
#define DBG_FUNC_START 1
#define DBG_FUNC_END 2
#define CPUPM_IDLE_WFE 0
#define CPUPM_IDLE_TIMER_WFE 0
#define DSB_SY 0
#define DAIF_IRQF 1
#define DAIF_FIQF 2
#define DAIFSC_IRQF 1
#define DAIFSC_FIQF 2
typedef bool boolean_t;
typedef struct {
    unsigned cpu_number,cpu_signal,cpu_cluster_id,cpu_id,wfe_count;
    uint64_t rtcPop,idle_timer_deadline;
    void *cpu_active_thread;
    void (*cpu_idle_notify)(unsigned,bool,uint64_t *);
    void (*idle_timer_notify)(void);
} cpu_data_t;
static cpu_data_t cpus[2];
static unsigned current_cpu,wfes,wfis,notify_enter,notify_exit;
static unsigned mode;
static bool reset_return;
static bool idle_enable=true;
static unsigned wfi=1,idle_proximate_io_wfe_masked,idle_proximate_timer_wfe;
static struct ceph_idle_gate ceph_gpu_idle_gate;
static jmp_buf returned;
static cpu_data_t *getCpuDatap(void){return &cpus[current_cpu];}
static void *current_thread(void){return &cpus[0];}
static void __attribute__((noreturn)) Idle_load_context(void){longjmp(returned,1);}
static uint64_t mach_absolute_time(void){return 1000;}
static uint64_t ml_cluster_wfe_timeout(unsigned cid){(void)cid;return mode==1?100:0;}
static bool wfe_to_deadline_or_interrupt(unsigned cid,uint64_t deadline,cpu_data_t *p,bool unmask)
{
    (void)cid;(void)deadline;(void)unmask;
    assert(ceph_idle_state(&ceph_gpu_idle_gate)&(1u<<p->cpu_number));
    wfes++;return true;
}
static bool cpu_proximate_timer(void){return mode==2;}
static void clock_absolutetime_interval_to_deadline(uint64_t t,uint64_t *out){*out=t;}
static void timer_resync_deadlines(void){}
static void SetIdlePop(void){}
static void ClearIdlePop(bool v){(void)v;}
static void platform_cache_idle_enter(void){}
static uintptr_t exception_stack_pointer(void){return 1;}
static void configure_coresight_registers(cpu_data_t *p){(void)p;}
static void test_wfi(void)
{
    assert(ceph_idle_state(&ceph_gpu_idle_gate)&(1u<<current_cpu));
    wfis++;
    assert(!ceph_idle_block(&ceph_gpu_idle_gate));
    assert(!ceph_idle_drained(&ceph_gpu_idle_gate));
}
#define __builtin_arm_wfi() test_wfi()
#define __builtin_arm_dsb(x) ((void)(x))
#define __builtin_arm_rsr(x) ((void)(x),3u)
#define __builtin_arm_wsr(x,y) ((void)(x),(void)(y))
static void notify(unsigned id,bool entry,uint64_t *ticks)
{
    (void)id;(void)ticks;
    assert(reset_return || (ceph_idle_state(&ceph_gpu_idle_gate)&(1u<<current_cpu)));
    if(entry)notify_enter++;else notify_exit++;
}
int cepheus_gpu_idle_cpu_enter(unsigned cpu){return ceph_idle_enter(&ceph_gpu_idle_gate,cpu);}
void cepheus_gpu_idle_cpu_leave(unsigned cpu){ceph_idle_leave(&ceph_gpu_idle_gate,cpu);}
static bool wfe_process_recommendation(void)
{
    assert(ceph_idle_state(&ceph_gpu_idle_gate)&(1u<<current_cpu));
    wfes++;
    if(mode==3){current_cpu=1;return true;}
    return mode==1;
}
static void Idle_context(void);
void cpu_idle_exit(boolean_t) __attribute__((noreturn));
/* APPLIED_CPU_IDLE */
/* APPLIED_CPU_IDLE_EXIT */
/* APPLIED_MACHINE_IDLE */
static void Idle_context(void){cpu_idle();}
static void reset(void)
{
    cpus[0]=(cpu_data_t){.cpu_number=0,.cpu_idle_notify=notify};
    cpus[1]=(cpu_data_t){.cpu_number=1,.cpu_idle_notify=notify};
    current_cpu=wfes=wfis=notify_enter=notify_exit=mode=0;
    idle_enable=true;wfi=1;
    reset_return=false;
    idle_proximate_io_wfe_masked=idle_proximate_timer_wfe=0;
    ceph_gpu_idle_gate=(struct ceph_idle_gate){0};
}
static void run_idle(void)
{
    if(!setjmp(returned))cpu_idle();
}
int main(void)
{
    reset();run_idle();assert(wfis==1 && notify_enter==1 && notify_exit==1);
    assert(ceph_idle_drained(&ceph_gpu_idle_gate));
    reset();assert(!ceph_idle_block(&ceph_gpu_idle_gate));run_idle();
    assert(!wfis && !wfes && !notify_enter && ceph_idle_drained(&ceph_gpu_idle_gate));
    reset();idle_enable=false;run_idle();assert(!ceph_idle_state(&ceph_gpu_idle_gate));
    reset();cpus[0].cpu_signal=SIGPdisabled;run_idle();assert(!ceph_idle_state(&ceph_gpu_idle_gate));
    reset();mode=1;idle_proximate_io_wfe_masked=1;run_idle();
    assert(wfes==1 && !wfis && !ceph_idle_state(&ceph_gpu_idle_gate));
    reset();mode=2;idle_proximate_timer_wfe=1;run_idle();
    assert(wfes==1 && !wfis && !ceph_idle_state(&ceph_gpu_idle_gate));
    reset();mode=2;run_idle();assert(!wfes && !wfis && !ceph_idle_state(&ceph_gpu_idle_gate));
    reset();wfi=0;run_idle();assert(!wfis && !notify_enter && !ceph_idle_state(&ceph_gpu_idle_gate));
    reset();reset_return=true;if(!setjmp(returned))cpu_idle_exit(true);
    assert(!ceph_idle_state(&ceph_gpu_idle_gate));
    reset();mode=1;machine_idle();assert(wfes==1 && !wfis && !ceph_idle_state(&ceph_gpu_idle_gate));
    reset();mode=3;machine_idle();assert(current_cpu==1 && !ceph_idle_state(&ceph_gpu_idle_gate));
    reset();assert(!ceph_idle_block(&ceph_gpu_idle_gate));
    if(!setjmp(returned))machine_idle();
    assert(!wfes && !wfis && ceph_idle_drained(&ceph_gpu_idle_gate));
    reset();if(!setjmp(returned))machine_idle();
    assert(wfes==1 && wfis==1 && ceph_idle_drained(&ceph_gpu_idle_gate));
    puts("Actual applied idle bodies: early exits/WFE/WFI/reset/migration/block drainage PASS (host doubles)");
    return 0;
}
