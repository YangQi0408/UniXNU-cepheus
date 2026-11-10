/* A640 HFI v2 SPSC queues. Caller serializes producers/consumers and owns
 * established GMU mappings. Cache callbacks are required even on coherent
 * memory (barriers still matter). No firmware execution in this module.
 * All lengths/indices are DWORDS. Keep one slot empty to distinguish full.
 */
#ifndef CEPHEUS_HFI_H
#define CEPHEUS_HFI_H
#include <stddef.h>
#include <stdint.h>
enum { CE_HFI_OK = 0, CE_HFI_EMPTY = 1, CE_HFI_FULL = 2,
       CE_HFI_INVALID = -1, CE_HFI_SMALL = -2, CE_HFI_IO = -3,
       CE_HFI_POISONED = -4 };
struct ceph_hfi_ops {
	void *ctx;
	int (*acquire)(void *); /* invalidate peer header+data, acquire barrier */
	/* Clean payload and complete its release barrier BEFORE updating index.
	 * Header-index cache lines must not share peer-owned dirty fields unless
	 * the platform provides coherent mappings; volatile is not cache coherency. */
	int (*publish_data)(void *);
	int (*publish_index)(void *); /* clean owned index, complete release barrier */
	int (*notify)(void *);  /* doorbell after publish */
	int (*validate)(void *); /* lifetime/ownership, before and after each access */
};
struct ceph_hfi_queue {
	volatile uint32_t *words;
	volatile uint32_t *read_index, *write_index;
	unsigned capacity, major;
	struct ceph_hfi_ops io;
	int poisoned; /* uncertain publication: no retries until reset/recreation */
};
/* Ordering around the index itself is distinct from a cache-maintenance
 * callback made BEFORE loading it. After observing peer publication, payload
 * loads must wait; before returning a slot, payload reads must be complete.
 * Full barrier also handles Normal-NC shared memory on arm64. */
static inline void ceph_hfi_memory_order(void)
{
#if defined(__aarch64__) || defined(__arm64__)
	__asm__ volatile("dsb sy" ::: "memory");
#elif !defined(KERNEL)
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
#error "HFI shared memory requires arm64"
#endif
}
static inline uint32_t ceph_hfi_header(unsigned id, unsigned words, unsigned type, unsigned seq)
{
	if (id > 255 || !words || words > 255 || type > 15 || seq > 4095) { return 0; }
	return id | (words << 8) | (type << 16) | (seq << 20);
}
static inline unsigned ceph_hfi_padded(unsigned count, unsigned major)
{
	return major == 2 ? (count + 3u) & ~3u : count;
}
static inline int ceph_hfi_overlap(uintptr_t a,size_t n,uintptr_t b,size_t m)
{
	return n && m && (a>UINTPTR_MAX-n || b>UINTPTR_MAX-m || (a<b+m && b<a+n));
}
static inline int ceph_hfi_owned(struct ceph_hfi_queue *q,const void *p,size_t bytes)
{
	return q && (ceph_hfi_overlap((uintptr_t)p,bytes,(uintptr_t)q,sizeof(*q)) ||
	    ceph_hfi_overlap((uintptr_t)p,bytes,(uintptr_t)q->words,q->capacity*sizeof(uint32_t)) ||
	    ceph_hfi_overlap((uintptr_t)p,bytes,(uintptr_t)q->read_index,sizeof(uint32_t)) ||
	    ceph_hfi_overlap((uintptr_t)p,bytes,(uintptr_t)q->write_index,sizeof(uint32_t)));
}
static inline int ceph_hfi_same(const struct ceph_hfi_queue *a,const struct ceph_hfi_queue *b)
{
	return a->words==b->words && a->read_index==b->read_index && a->write_index==b->write_index &&
	    a->capacity==b->capacity && a->major==b->major && a->io.ctx==b->io.ctx &&
	    a->io.acquire==b->io.acquire && a->io.publish_data==b->io.publish_data &&
	    a->io.publish_index==b->io.publish_index && a->io.notify==b->io.notify &&
	    a->io.validate==b->io.validate;
}
static inline int ceph_hfi_valid(struct ceph_hfi_queue *q)
{
	if (!q || !q->words || !q->read_index || !q->write_index ||
	    (((uintptr_t)q->words|(uintptr_t)q->read_index|(uintptr_t)q->write_index)&3u) ||
	    !q->io.acquire || !q->io.publish_data || !q->io.publish_index || !q->io.notify || !q->io.validate ||
	    q->capacity < 8 || q->capacity > 1024 || (q->capacity & 3u) ||
	    (q->major != 1 && q->major != 2) ||
	    (uintptr_t)q->words>UINTPTR_MAX-q->capacity*sizeof(uint32_t) ||
	    (uintptr_t)q->read_index>UINTPTR_MAX-sizeof(uint32_t) ||
	    (uintptr_t)q->write_index>UINTPTR_MAX-sizeof(uint32_t) ||
	    q->read_index==q->write_index ||
	    ceph_hfi_overlap((uintptr_t)q->words,q->capacity*sizeof(uint32_t),(uintptr_t)q->read_index,4) ||
	    ceph_hfi_overlap((uintptr_t)q->words,q->capacity*sizeof(uint32_t),(uintptr_t)q->write_index,4)) { return CE_HFI_INVALID; }
	if (q->poisoned) { return CE_HFI_POISONED; }
	struct ceph_hfi_queue before=*q;
	if (q->io.validate(q->io.ctx) || q->poisoned || !ceph_hfi_same(q,&before)) {
		q->poisoned=1; return CE_HFI_IO;
	}
	return 0;
}
static inline int ceph_hfi_load(struct ceph_hfi_queue *q,volatile uint32_t *address,uint32_t *out)
{
	int e=ceph_hfi_valid(q);if(e)return e;
	uintptr_t p=(uintptr_t)address,b=(uintptr_t)q->words;
	if(!out || ((uintptr_t)out&3u) || ceph_hfi_owned(q,out,sizeof(*out)) ||
	    (address!=q->read_index && address!=q->write_index &&
	     ((p&3u) || p<b || p-b>=q->capacity*sizeof(uint32_t))))return CE_HFI_INVALID;
	uint32_t value=*address;ceph_hfi_memory_order();
	e=ceph_hfi_valid(q);if(e)return e;
	*out=value;return 0;
}
static inline int ceph_hfi_store(struct ceph_hfi_queue *q,volatile uint32_t *address,uint32_t value)
{
	int e=ceph_hfi_valid(q);if(e)return e;
	uintptr_t p=(uintptr_t)address,b=(uintptr_t)q->words;
	if(address!=q->read_index && address!=q->write_index &&
	   ((p&3u) || p<b || p-b>=q->capacity*sizeof(uint32_t)))return CE_HFI_INVALID;
	*address=value;ceph_hfi_memory_order();
	return ceph_hfi_valid(q);
}
static inline int ceph_hfi_callback(struct ceph_hfi_queue *q,int (*callback)(void *))
{
	int e=ceph_hfi_valid(q);if(e)return e;
	if(!callback || (callback!=q->io.acquire && callback!=q->io.publish_data &&
	    callback!=q->io.publish_index && callback!=q->io.notify))return CE_HFI_INVALID;
	struct ceph_hfi_queue before=*q;
	if(callback(q->io.ctx) || q->poisoned || !ceph_hfi_same(q,&before)){q->poisoned=1;return CE_HFI_IO;}
	return ceph_hfi_valid(q);
}
static inline int ceph_hfi_indices(struct ceph_hfi_queue *q, unsigned *r, unsigned *w)
{
	if(!r || !w || (((uintptr_t)r|(uintptr_t)w)&3u) ||
	    ceph_hfi_overlap((uintptr_t)r,sizeof(*r),(uintptr_t)w,sizeof(*w)) ||
	    ceph_hfi_owned(q,r,sizeof(*r)) || ceph_hfi_owned(q,w,sizeof(*w)))return CE_HFI_INVALID;
	int e=ceph_hfi_valid(q);if(e)return e;
	e=ceph_hfi_callback(q,q->io.acquire);if(e)return e;
	uint32_t read,write;
	e=ceph_hfi_load(q,q->read_index,&read);if(e)return e;
	e=ceph_hfi_load(q,q->write_index,&write);if(e)return e;
	ceph_hfi_memory_order();
	if (read >= q->capacity || write >= q->capacity ||
	    (q->major == 2 && ((read | write) & 3u))) { return CE_HFI_INVALID; }
	e=ceph_hfi_valid(q);if(e)return e;
	*r=read;*w=write;
	return 0;
}
static inline int ceph_hfi_write(struct ceph_hfi_queue *q, const uint32_t *msg, unsigned count)
{
	if (!msg || ((uintptr_t)msg&3u) || !count || count>255 ||
	    (uintptr_t)msg>UINTPTR_MAX-count*sizeof(*msg) || ceph_hfi_owned(q,msg,count*sizeof(*msg)))return CE_HFI_INVALID;
	unsigned r, w;
	int error = ceph_hfi_indices(q, &r, &w);
	if (error) { return error; }
	if (((msg[0] >> 8) & 255u) != count) { return CE_HFI_INVALID; }
	unsigned used = (w + q->capacity - r) % q->capacity;
	unsigned padded = ceph_hfi_padded(count, q->major);
	if (padded >= q->capacity - used) { return CE_HFI_FULL; }
	for (unsigned i = 0; i < padded; ++i) {
		error=ceph_hfi_store(q,q->words+(w+i)%q->capacity,i<count?msg[i]:0xfafafafau);
		if(error){q->poisoned=1;return error;}
	}
	error=ceph_hfi_callback(q,q->io.publish_data);if(error)return error;
	ceph_hfi_memory_order();
	error=ceph_hfi_store(q,q->write_index,(w+padded)%q->capacity);if(error)return error;
	error=ceph_hfi_callback(q,q->io.publish_index);if(error)return error;
	error=ceph_hfi_callback(q,q->io.notify);if(error)return error;
	return 0;
}
static inline int ceph_hfi_read(struct ceph_hfi_queue *q, uint32_t *out, unsigned capacity, unsigned *count)
{
	if (!count || ((uintptr_t)count&3u) || ceph_hfi_owned(q,count,sizeof(*count)) ||
	    ceph_hfi_overlap((uintptr_t)out,(capacity<255?capacity:255)*sizeof(*out),
	                     (uintptr_t)count,sizeof(*count))) { return CE_HFI_INVALID; }
	*count = 0;
	unsigned r, w;
	int error = ceph_hfi_indices(q, &r, &w);
	if (error) { return error; }
	if (r == w) { return CE_HFI_EMPTY; }
	uint32_t header;error=ceph_hfi_load(q,q->words+r,&header);if(error)return error;
	unsigned length = (header >> 8) & 255u;
	unsigned padded = ceph_hfi_padded(length, q->major);
	unsigned used = (w + q->capacity - r) % q->capacity;
	if (!length || padded > used || padded >= q->capacity) { return CE_HFI_INVALID; }
	if (!out || length > capacity) { return CE_HFI_SMALL; }
	if (((uintptr_t)out&3u) || (uintptr_t)out>UINTPTR_MAX-length*sizeof(*out) ||
	    ceph_hfi_owned(q,out,length*sizeof(*out))) { return CE_HFI_INVALID; }
	uint32_t copy[255];
	for (unsigned i = 0; i < length; ++i) {
		error=ceph_hfi_load(q,q->words+(r+i)%q->capacity,&copy[i]);if(error)return error;
	}
	if(copy[0]!=header){q->poisoned=1;return CE_HFI_IO;}
	ceph_hfi_memory_order(); /* copy finishes before firmware may reuse slots */
	error=ceph_hfi_store(q,q->read_index,(r+padded)%q->capacity);if(error)return error;
	error=ceph_hfi_callback(q,q->io.publish_index);if(error)return error;
	for(unsigned i=0;i<length;++i)out[i]=copy[i];
	*count = length;
	return 0;
}
/* v2 ACK carries the original command header in word1, status in word2.
 * Match the ENTIRE header (id/type/length/sequence), not just wrapping seq.
 * Caller must not reuse a sequence while an older command is outstanding.
 */
static inline int ceph_hfi_ack(const uint32_t *msg, unsigned count, uint32_t sent)
{
	return msg && count >= 3 && ((msg[0] >> 8) & 255u) == count &&
	    ((msg[0] >> 16) & 15u) == 1 && msg[1] == sent && msg[2] == 0;
}
/* Queue table matches 4 headers (12 DWORDs), three enabled, DSP disabled.
 * table requires 54 DWORDs; rings start at 224 bytes, each 4KiB. */
static inline int ceph_hfi_table(uint32_t *table, unsigned words, uint64_t gmu_base, size_t bytes)
{
	if (!table || words < 54 || (gmu_base & 4095u) || bytes < 224 + 4 * 4096 ||
	    gmu_base > UINT32_MAX - (224 + 4 * 4096)) { return CE_HFI_INVALID; }
	for (unsigned i = 0; i < 54; ++i) { table[i] = 0; }
	table[1] = 54; table[2] = 6; table[3] = 12; table[4] = 4; table[5] = 4;
	static const unsigned priority[] = {10, 10, 40, 20};
	for (unsigned i = 0; i < 4; ++i) {
		uint32_t *h = table + 6 + i * 12;
		h[0] = i == 3 ? 0 : 1;
		h[1] = (uint32_t)gmu_base + 224 + i * 4096;
		h[2] = i | (priority[i] << 8); h[3] = 1024;
		h[6] = h[7] = h[8] = 1;
	}
	return 0;
}
#endif
