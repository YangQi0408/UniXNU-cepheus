/* SM8150 CPU/GPU bring-up diagnostics. Explicit boot opt-in, thread context.
 * CPU frequency table/requests and cycle counters are observed, not changed.
 * GPUCC is gated by GCC CFG_AHB; GMU/GX are NOT poked if powered down.
 * No frequency requests, voltage writes, GPU resets, DMA or firmware upload.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include "cepheus_soc.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
void cepheus_soc_init(void);
static int soc_wait_chan;
static uint32_t sr(vm_offset_t base, unsigned off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}
static void swait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&soc_wait_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}
struct osm_bank { const char *name; uint64_t pa; unsigned cores; vm_offset_t va; bool enabled; };
static struct osm_bank banks[] = {
	{"l3", 0x18321000, 1, 0, false}, {"silver", 0x18323000, 4, 0, false},
	{"gold", 0x18325800, 3, 0, false}, {"prime", 0x18327800, 1, 0, false}
};
static void osm_dump(struct osm_bank *b)
{
	b->va = ml_io_map((vm_offset_t)b->pa, 0x1400);
	if (!b->va) { printf("CEPHEUS OSM: %s map failed\n", b->name); return; }
	uint32_t enable = sr(b->va, 0), dcvs = sr(b->va, 0xbc);
	printf("CEPHEUS OSM: %s pa=%#llx enable=%#x per_core=%#x\n", b->name, b->pa, enable, dcvs);
	b->enabled = (enable != 0xffffffffu && (enable & 1u));
	if (!b->enabled) { return; }
	uint32_t prev = 0;
	for (unsigned i = 0; i < CEPHEUS_OSM_ROWS; ++i) {
		uint32_t raw = sr(b->va, 0x110 + i * 32), v = sr(b->va, 0x114 + i * 32);
		uint32_t khz = cepheus_osm_khz(raw);
		printf("CEPHEUS OSM: %s LUT%u raw=%#x khz=%u cores=%u volt_raw=%#x\n",
		    b->name, i, raw, khz, cepheus_osm_core_count(raw), v);
		if (raw == 0xffffffffu || (i && khz == prev)) { break; }
		prev = khz;
	}
}
static void osm_sample(unsigned sample)
{
	uint32_t before[4][4] = {{0}};
	uint64_t start = mach_absolute_time();
	for (unsigned n = 0; n < 4; ++n) {
		if (!banks[n].enabled) { continue; }
		for (unsigned c = 0; c < banks[n].cores; ++c) { before[n][c] = sr(banks[n].va, 0x9c0 + c * 4); }
	}
	swait(100);
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time() - start, &ns);
	for (unsigned n = 0; n < 4; ++n) {
		struct osm_bank *b = &banks[n];
		if (!b->enabled) { continue; }
		for (unsigned c = 0; c < b->cores; ++c) {
			uint32_t desired = sr(b->va, 0x920 + c * 4);
			uint32_t after = sr(b->va, 0x9c0 + c * 4);
			uint32_t delta = cepheus_counter_delta(before[n][c], after);
			uint32_t khz = desired < CEPHEUS_OSM_ROWS ? cepheus_osm_khz(sr(b->va, 0x110 + desired * 32)) : 0;
			printf("CEPHEUS OSM: sample%u %s core%u desired=%u target_khz=%u cycles=%u ns=%llu rate_khz=%llu\n",
			    sample, b->name, c, desired, khz, delta, ns,
			    ns ? (uint64_t)delta * 1000000u / ns : 0);
		}
	}
}
static void gpu_probe(void)
{
	vm_offset_t gcc = ml_io_map(0x00100000, 0x90000);
	if (!gcc) { return; }
	uint32_t ahb = sr(gcc, 0x71004);
	printf("CEPHEUS GPU: GCC CFG_AHB=%#x MEMNOC=%#x SNOC_DVM=%#x\n", ahb, sr(gcc, 0x7100c), sr(gcc, 0x71018));
	if (!cepheus_gpu_clock_on(ahb, 1)) {
		printf("CEPHEUS GPU: CFG_AHB unavailable; GPUCC/GMU/GX not read\n"); return;
	}
	vm_offset_t cc = ml_io_map(0x02c90000, 0x9000);
	if (!cc) { return; }
	uint32_t cx = sr(cc, 0x106c), gx = sr(cc, 0x100c), gmuclk = sr(cc, 0x1098);
	printf("CEPHEUS GPU: CX_GDSCR=%#x GX_GDSCR=%#x AHB=%#x GMU_CLK=%#x CXO=%#x SLEEP=%#x RCG=%#x CFG=%#x\n",
	    cx, gx, sr(cc, 0x1078), gmuclk, sr(cc, 0x109c), sr(cc, 0x1090), sr(cc, 0x1120), sr(cc, 0x1124));
	if (cx != 0xffffffffu && (cx & 0x80000000u) && cepheus_gpu_clock_on(gmuclk, 0)) {
		// GMU offsets are word offsets from GPU base, not byte offsets from GMU base.
		vm_offset_t gmu = ml_io_map(0x02c6a000, 0x30000);
		if (gmu) {
			printf("CEPHEUS GPU: GMU SYSRESET=%#x FW_BUSY=%#x INIT_RESULT=%#x POWER=%#x HFI=%#x\n",
			    sr(gmu, 0x1f800 * 4 - 0x6a000), sr(gmu, 0x1f81a * 4 - 0x6a000),
			    sr(gmu, 0x1f81c * 4 - 0x6a000), sr(gmu, 0x1f8d0 * 4 - 0x6a000),
			    sr(gmu, 0x1f980 * 4 - 0x6a000));
		}
	} else { printf("CEPHEUS GPU: GMU powered/clocked state not confirmed; skipped\n"); }
	// Do not access GX: GMU AHB fence must also be validated before GX reads.
	printf("CEPHEUS GPU: probe only; no GPU submission, firmware upload or Metal support\n");
}
static void soc_thread(void *arg, wait_result_t wr)
{
	(void)arg; (void)wr;
	swait(45000); // keep boot/USB alive before the hardware probe
	for (unsigned n = 0; n < 4; ++n) { osm_dump(&banks[n]); }
	gpu_probe();
	for (unsigned sample = 0; sample < 12; ++sample) { osm_sample(sample); swait(4900); }
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}
void cepheus_soc_init(void)
{
	static bool started;
	int enabled = 0;
	thread_t thread;
	PE_parse_boot_argn("cephsoc", &enabled, sizeof(enabled));
	if (!enabled || started) { return; }
	started = true;
	if (kernel_thread_start(soc_thread, NULL, &thread) == KERN_SUCCESS) { thread_deallocate(thread); }
}
#endif
