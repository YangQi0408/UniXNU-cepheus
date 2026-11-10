/* Included before the actual staged Pool.c; only its external services are mocked. */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef void VOID;
typedef char CHAR8;
typedef uint8_t BOOLEAN;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
typedef intptr_t INTN;
typedef uint64_t EFI_STATUS;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef unsigned EFI_TPL;
typedef void *EFI_HANDLE;
typedef struct {UINT32 a;UINT16 b,c;uint8_t d[8];} EFI_GUID;
typedef enum {
  EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData, EfiBootServicesCode,
  EfiBootServicesData, EfiRuntimeServicesCode, EfiRuntimeServicesData,
  EfiConventionalMemory, EfiUnusableMemory, EfiACPIReclaimMemory,
  EfiACPIMemoryNVS, EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace,
  EfiPalCode, EfiPersistentMemory, EfiUnacceptedMemoryType, EfiMaxMemoryType
} EFI_MEMORY_TYPE;
typedef struct LIST_ENTRY LIST_ENTRY;
struct LIST_ENTRY {LIST_ENTRY *ForwardLink,*BackLink;};
typedef struct {EFI_TPL Tpl,OwnerTpl;unsigned Lock;} EFI_LOCK;

#define STATIC static
#define CONST const
#define EFIAPI
#define IN
#define OUT
#define OPTIONAL
#define TRUE 1
#define FALSE 0
#define MAX_UINT64 UINT64_MAX
#define MAX_ADDRESS UINTPTR_MAX
#define TPL_APPLICATION 4
#define TPL_NOTIFY 16
#define EfiLockAcquired 2
#define EFI_INITIALIZE_LOCK_VARIABLE(t) {t,0,1}
#define EFI_SUCCESS 0
#define EFI_INVALID_PARAMETER (UINT64_C(1)<<63|2)
#define EFI_OUT_OF_RESOURCES (UINT64_C(1)<<63|9)
#define EFI_ACCESS_DENIED (UINT64_C(1)<<63|15)
#define EFI_NOT_FOUND (UINT64_C(1)<<63|14)
#define EFI_ERROR(s) ((int64_t)(s)<0)
#define EFI_NATIVE_INTERFACE 0
#define EFI_PAGE_SIZE 4096
#define EFI_PAGE_SHIFT 12
#define EFI_PAGE_MASK 4095
#define DEFAULT_PAGE_ALLOCATION_GRANULARITY 4096
#define RUNTIME_PAGE_ALLOCATION_GRANULARITY 4096
#define EFI_SIZE_TO_PAGES(s) (((s)+4095)/4096)
#define EFI_PAGES_TO_SIZE(n) ((n)*4096)
#define MEMORY_TYPE_OEM_RESERVED_MIN 0x70000000u
#define GUARD_HEAP_TYPE_FREED 4
#define HEAP_GUARD_ALIGNED_TO_TAIL 1
#define SIGNATURE_32(a,b,c,d) ((UINT32)(a)|(UINT32)(b)<<8|(UINT32)(c)<<16|(UINT32)(d)<<24)
#define OFFSET_OF(t,m) offsetof(t,m)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ALIGN_VARIABLE(s) (((s)+7)&~(UINTN)7)
#define CR(p,t,m,s) ((t *)((char *)(p)-offsetof(t,m)))
#define BASE_CR(p,t,m) CR(p,t,m,0)
#define INITIALIZE_LIST_HEAD_VARIABLE(n) {&(n),&(n)}
#define ASSERT(p) assert((p)||expected_corruption)
#define ASSERT_LOCKED(l) assert((l)->Lock==EfiLockAcquired)
#define DEBUG(a) ((void)0)
#define DEBUG_CLEAR_MEMORY(p,n) memset(p,0xa5,n)
#define RETURN_ADDRESS(n) NULL
#define MemoryProfileActionAllocatePool 1
#define MemoryProfileActionFreePool 2

static EFI_TPL gEfiCurrentTpl=TPL_APPLICATION;
static BOOLEAN gMemoryMapTerminated;
static EFI_LOCK gMemoryLock=EFI_INITIALIZE_LOCK_VARIABLE(TPL_NOTIFY);
static BOOLEAN mOnGuarding;
static struct {struct {struct {unsigned Direction;} Fields;} HeapGuardPolicy;} gDxeMps;
static unsigned mode, page_frees, profile_allocs, profile_frees, installs, pool_failure;
static unsigned expected_corruption;
static struct {void *base;UINTN bytes;int live,guard;} pages[256];

static void InitializeListHead(LIST_ENTRY *l) {l->ForwardLink=l->BackLink=l;}
static int IsListEmpty(const LIST_ENTRY *l) {return l->ForwardLink==l;}
static void RemoveEntryList(LIST_ENTRY *l)
{l->BackLink->ForwardLink=l->ForwardLink;l->ForwardLink->BackLink=l->BackLink;}
static void InsertHeadList(LIST_ENTRY *h,LIST_ENTRY *l)
{l->ForwardLink=h->ForwardLink;l->BackLink=h;h->ForwardLink->BackLink=l;h->ForwardLink=l;}
static void InsertTailList(LIST_ENTRY *h,LIST_ENTRY *l)
{l->ForwardLink=h;l->BackLink=h->BackLink;h->BackLink->ForwardLink=l;h->BackLink=l;}
static EFI_STATUS CoreAcquireLockOrFail(EFI_LOCK *l)
{
  if(l->Lock==EfiLockAcquired)return EFI_ACCESS_DENIED;
  assert(gEfiCurrentTpl<=l->Tpl);
  l->OwnerTpl=gEfiCurrentTpl;l->Lock=EfiLockAcquired;gEfiCurrentTpl=l->Tpl;return 0;
}
static void CoreAcquireLock(EFI_LOCK *l) {assert(!CoreAcquireLockOrFail(l));}
static void CoreReleaseLock(EFI_LOCK *l)
{ASSERT_LOCKED(l);l->Lock=1;gEfiCurrentTpl=l->OwnerTpl;}
static void CoreAcquireMemoryLock(void) {CoreAcquireLock(&gMemoryLock);}
static void CoreReleaseMemoryLock(void) {CoreReleaseLock(&gMemoryLock);}
static int IsPoolTypeToGuard(EFI_MEMORY_TYPE t) {(void)t;return mode==1;}
static int IsHeapGuardEnabled(unsigned t) {return mode==2&&t==GUARD_HEAP_TYPE_FREED;}
static void *CoreAllocatePoolPages(EFI_MEMORY_TYPE,UINTN,UINTN,BOOLEAN);
static void CoreFreePoolPages(EFI_PHYSICAL_ADDRESS,UINTN);
static void SetGuardForMemory(EFI_PHYSICAL_ADDRESS,UINTN);
static int IsMemoryGuarded(EFI_PHYSICAL_ADDRESS);
static void *AdjustPoolHeadA(EFI_PHYSICAL_ADDRESS,UINTN,UINTN);
static void *AdjustPoolHeadF(EFI_PHYSICAL_ADDRESS,UINTN,UINTN);
static void AdjustMemoryF(EFI_PHYSICAL_ADDRESS *,UINTN *);
static void UnsetGuardForMemory(EFI_PHYSICAL_ADDRESS,UINTN);
static void GuardFreedPagesChecked(EFI_PHYSICAL_ADDRESS,UINTN);
static void ApplyMemoryProtectionPolicy(EFI_MEMORY_TYPE,EFI_MEMORY_TYPE,EFI_PHYSICAL_ADDRESS,UINT64);
static void CoreUpdateProfile(EFI_PHYSICAL_ADDRESS,unsigned,EFI_MEMORY_TYPE,UINTN,VOID *,VOID *);
static void InstallMemoryAttributesTableOnMemoryAllocation(EFI_MEMORY_TYPE);
static EFI_STATUS CoreInstallProtocolInterface(EFI_HANDLE *,EFI_GUID *,unsigned,VOID *);
VOID *CoreAllocatePoolI(EFI_MEMORY_TYPE,UINTN,BOOLEAN);
EFI_STATUS CoreFreePoolI(VOID *,EFI_MEMORY_TYPE *);

#include "PoolUnderTest.c"

typedef struct {UINTN Signature;LIST_ENTRY Link;BOOLEAN FromPages;EFI_MEMORY_TYPE Type;
                UINT64 Start,End,VirtualStart,Attribute;} MEMORY_MAP;
#define MEMORY_MAP_SIGNATURE 1234
#define MAX_MAP_DEPTH 6
#define LShiftU64(n,s) ((UINT64)(n)<<(s))
#define UpdateMemoryStatistics(...) ((void)0)
static LIST_ENTRY gMemoryMap=INITIALIZE_LIST_HEAD_VARIABLE(gMemoryMap);
static MEMORY_MAP mMapStack[6],added_map;
static UINTN mMapDepth;
static void RemoveMemoryMapEntry(MEMORY_MAP *e) {RemoveEntryList(&e->Link);}
static void CoreAddRange(EFI_MEMORY_TYPE t,UINT64 start,UINT64 end,UINT64 attr)
{
  added_map=(MEMORY_MAP){.Type=t,.Start=start,.End=end,.Attribute=attr};
  InsertTailList(&gMemoryMap,&added_map.Link);
}
static void CoreFreeMemoryMapStack(void) {}
#include "PageUnderTest.c"

static CE_POOL_ALLOCATION get(void *p,UINTN n,UINT32 type,UINT64 generation,int good)
{
  CE_POOL_ALLOCATION a;
  memset(&a,0xff,sizeof(a));
  EFI_STATUS status=CoreCepheusPoolLookup((UINTN)p,n,type,generation,&a);
  if(good) {assert(!status);assert(a.Address==(UINTN)p&&a.Bytes>=n&&a.Type==type&&a.Generation);}
  else {assert(EFI_ERROR(status));CE_POOL_ALLOCATION zero={0};assert(!memcmp(&a,&zero,sizeof(a)));}
  return a;
}

static void *CoreAllocatePoolPages(EFI_MEMORY_TYPE t,UINTN n,UINTN alignment,BOOLEAN guard)
{
  (void)t;ASSERT_LOCKED(&gMemoryLock);
  if(pool_failure)return NULL;
  for(unsigned i=0;i<ARRAY_SIZE(pages);i++)if(!pages[i].live) {
    void *p=NULL;assert(!posix_memalign(&p,alignment,n*4096));
    pages[i].base=p;pages[i].bytes=n*4096;pages[i].live=1;pages[i].guard=guard;
    return p;
  }
  assert(0);return NULL;
}
static void CoreFreePoolPages(EFI_PHYSICAL_ADDRESS p,UINTN n)
{
  ASSERT_LOCKED(&gMemoryLock);
  for(unsigned i=0;i<ARRAY_SIZE(pages);i++)if(pages[i].live&&(UINTN)pages[i].base==p) {
    assert(pages[i].bytes==n*4096);
    /* No surviving table entry may point into pages the real allocator frees. */
    for(unsigned j=0;j<CE_POOL_SLOTS;j++)if(mCepheusPoolLifetime.Slot[j].State==1)
      assert(mCepheusPoolLifetime.Slot[j].Address<p||mCepheusPoolLifetime.Slot[j].Address>=p+n*4096);
    free(pages[i].base);pages[i].live=0;page_frees++;return;
  }
  assert(0);
}
static int IsMemoryGuarded(EFI_PHYSICAL_ADDRESS p)
{for(unsigned i=0;i<ARRAY_SIZE(pages);i++)if(pages[i].live&&(UINTN)pages[i].base==p)return pages[i].guard;return 0;}
static void SetGuardForMemory(EFI_PHYSICAL_ADDRESS p,UINTN n) {(void)n;assert(IsMemoryGuarded(p));}
static void *AdjustPoolHeadA(EFI_PHYSICAL_ADDRESS p,UINTN n,UINTN size)
{assert(size<=n*4096);return (void *)(UINTN)p;}
static void *AdjustPoolHeadF(EFI_PHYSICAL_ADDRESS p,UINTN n,UINTN size)
{assert(size<=n*4096);return (void *)(UINTN)p;}
static void AdjustMemoryF(EFI_PHYSICAL_ADDRESS *p,UINTN *n) {(void)p;(void)n;}
static void UnsetGuardForMemory(EFI_PHYSICAL_ADDRESS p,UINTN n) {(void)p;(void)n;}
static void GuardFreedPagesChecked(EFI_PHYSICAL_ADDRESS p,UINTN n) {(void)p;(void)n;}
static void ApplyMemoryProtectionPolicy(EFI_MEMORY_TYPE a,EFI_MEMORY_TYPE b,EFI_PHYSICAL_ADDRESS p,UINT64 n)
{(void)a;(void)b;(void)p;(void)n;}
static void CoreUpdateProfile(EFI_PHYSICAL_ADDRESS pc,unsigned action,EFI_MEMORY_TYPE t,UINTN size,VOID *p,VOID *name)
{(void)pc;(void)t;(void)size;(void)p;(void)name;if(action==1)profile_allocs++;else profile_frees++;}
static void InstallMemoryAttributesTableOnMemoryAllocation(EFI_MEMORY_TYPE t) {(void)t;}
static EFI_STATUS CoreInstallProtocolInterface(EFI_HANDLE *h,EFI_GUID *g,unsigned kind,VOID *p)
{assert(h&&g&&kind==0&&p==&mCepheusPoolQuery);*h=p;installs++;return 0;}

static void reset(void)
{
  for(unsigned i=0;i<ARRAY_SIZE(pages);i++)assert(!pages[i].live);
  memset(&mCepheusPoolLifetime,0,sizeof(mCepheusPoolLifetime));
  CoreInitializePool();mode=pool_failure=0;gMemoryMapTerminated=0;
  gEfiCurrentTpl=TPL_APPLICATION;assert(mPoolMemoryLock.Lock==1);
}

static void real_allocator(void)
{
  reset();CoreCepheusPoolQueryInstall();assert(installs==1&&mCepheusPoolQuery.Version==1);
  for(unsigned m=0;m<3;m++)for(unsigned n=1;n<13000;n=n*3+1) {
    mode=m;void *p=NULL;unsigned before=page_frees;
    assert(!CoreAllocatePool(EfiBootServicesData,n,&p));
    CE_POOL_ALLOCATION a=get(p,n,4,0,1);assert(a.Bytes==n);
    get(p,n+1,4,a.Generation,0);get(p,n,3,a.Generation,0);
    get((char *)p+8,n>8?n-8:1,4,0,0);
    assert(!CoreFreePool(p));get(p,n,4,a.Generation,0);assert(page_frees==before+1);
  }
  mode=0;void *anchor=NULL,*p=NULL;
  assert(!CoreAllocatePool(4,72,&anchor));assert(!CoreAllocatePool(4,72,&p));
  CE_POOL_ALLOCATION old=get(p,72,4,0,1);
  assert(!CoreFreePool(p));void *next=NULL;assert(!CoreAllocatePool(4,72,&next));assert(p==next);
  CE_POOL_ALLOCATION fresh=get(next,72,4,0,1);assert(fresh.Generation>old.Generation);
  get(next,72,4,old.Generation,0);assert(!CoreFreePool(next));assert(!CoreFreePool(anchor));
  /* OEM allocation recursively allocates/frees its actual POOL descriptor. */
  assert(!CoreAllocatePool((EFI_MEMORY_TYPE)0x80000000u,72,&p));
  get(p,72,0x80000000u,0,1);assert(!CoreFreePool(p));assert(IsListEmpty(&mPoolHeadList));
  void *v[200];
  for(unsigned i=0;i<ARRAY_SIZE(v);i++)assert(!CoreAllocatePool(4,72,&v[i]));
  for(unsigned i=0;i<ARRAY_SIZE(v);i+=2)assert(!CoreFreePool(v[i]));
  for(unsigned i=1;i<ARRAY_SIZE(v);i+=2) {get(v[i],72,4,0,1);assert(!CoreFreePool(v[i]));}
  assert(!mCepheusPoolLifetime.Failed);reset();
}

static void rejection(void)
{
  void *p=NULL;assert(!CoreAllocatePool(4,72,&p));
  CE_POOL_ALLOCATION a=get(p,72,4,0,1);
  gMemoryMapTerminated=1;get(p,72,4,0,0);gMemoryMapTerminated=0;
  gEfiCurrentTpl=TPL_NOTIFY;get(p,72,4,0,0);gEfiCurrentTpl=TPL_APPLICATION;
  CoreAcquireLock(&mPoolMemoryLock);gEfiCurrentTpl=TPL_APPLICATION;
  get(p,72,4,0,0);gEfiCurrentTpl=TPL_NOTIFY;CoreReleaseLock(&mPoolMemoryLock);
  assert(CoreCepheusPoolLookup((UINTN)p,72,4,0,NULL)==EFI_INVALID_PARAMETER);
  /* A rejected corrupt free still invalidates metadata conservatively. */
  POOL_TAIL *tail=HEAD_TO_TAIL(BASE_CR(p,POOL_HEAD,Data));UINT32 saved=tail->Signature;
  expected_corruption=1;tail->Signature=0;
  assert(CoreFreePool(p)==EFI_INVALID_PARAMETER);expected_corruption=0;get(p,72,4,a.Generation,0);
  tail->Signature=saved;assert(!CoreFreePool(p));
  pool_failure=1;p=(void *)1;assert(CoreAllocatePool(4,9000,&p)==EFI_OUT_OF_RESOURCES&&p==NULL);
  assert(!mCepheusPoolLifetime.Failed);reset();
  /* Unknown addresses and a protected stale object are never dereferenced by lookup. */
  void *protected=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  assert(protected!=MAP_FAILED);get(protected,72,4,0,0);
  CePoolAllocated(&mCepheusPoolLifetime,(UINTN)protected,72,4);get(protected,72,4,0,1);
  CePoolFreed(&mCepheusPoolLifetime,(UINTN)protected);get(protected,72,4,0,0);
  assert(!munmap(protected,4096));reset();
}

static void boundaries(void)
{
  CE_POOL_ALLOCATION a;
  CePoolAllocated(&mCepheusPoolLifetime,0x1000,72,4);
  assert(!CePoolLookup(&mCepheusPoolLifetime,0x1000,72,4,0,&a));
  CePoolAllocated(&mCepheusPoolLifetime,0x1000,72,4);assert(mCepheusPoolLifetime.Failed);
  assert(CePoolLookup(&mCepheusPoolLifetime,0x1000,72,4,0,&a));reset();
  mCepheusPoolLifetime.Serial=MAX_UINT64;CePoolAllocated(&mCepheusPoolLifetime,0x1000,72,4);
  assert(mCepheusPoolLifetime.Failed);reset();
  CePoolAllocated(&mCepheusPoolLifetime,MAX_UINT64-7,72,4);assert(mCepheusPoolLifetime.Failed);reset();
  /* Construct exact collisions, exercise the bounded chain and sticky overflow. */
  UINT64 colliding[CE_POOL_PROBES+1];unsigned count=0;UINTN bucket=CePoolHash(0x1000);
  for(UINT64 p=0x1000;count<ARRAY_SIZE(colliding);p+=8)if(CePoolHash(p)==bucket)colliding[count++]=p;
  for(unsigned i=0;i<CE_POOL_PROBES;i++)CePoolAllocated(&mCepheusPoolLifetime,colliding[i],72,4);
  assert(!mCepheusPoolLifetime.Failed);
  for(unsigned i=0;i<CE_POOL_PROBES;i++)assert(!CePoolLookup(&mCepheusPoolLifetime,colliding[i],72,4,0,&a));
  CePoolFreed(&mCepheusPoolLifetime,colliding[0]);
  assert(!CePoolLookup(&mCepheusPoolLifetime,colliding[CE_POOL_PROBES-1],72,4,0,&a));
  CePoolAllocated(&mCepheusPoolLifetime,colliding[CE_POOL_PROBES],72,4);
  assert(!mCepheusPoolLifetime.Failed);
  CePoolAllocated(&mCepheusPoolLifetime,colliding[0],72,4);assert(mCepheusPoolLifetime.Failed);reset();
  CePoolAllocated(&mCepheusPoolLifetime,0x1018,72,4);
  CePoolAllocated(&mCepheusPoolLifetime,0x2018,72,4);
  CoreAcquireMemoryLock();CoreCepheusPoolRangeChanged(0x1000,1);CoreReleaseMemoryLock();
  assert(CePoolLookup(&mCepheusPoolLifetime,0x1018,72,4,0,&a));
  assert(!CePoolLookup(&mCepheusPoolLifetime,0x2018,72,4,0,&a));
  CoreAcquireMemoryLock();CoreCepheusPoolRangeChanged(0x1000,2);CoreReleaseMemoryLock();
  assert(CePoolLookup(&mCepheusPoolLifetime,0x2018,72,4,0,&a));reset();
  CePoolAllocated(&mCepheusPoolLifetime,0x2008,72,4);
  CePoolRangeChanged(&mCepheusPoolLifetime,0x1000,1);
  assert(CePoolLookup(&mCepheusPoolLifetime,0x2008,72,4,0,&a));reset();
  CePoolAllocated(&mCepheusPoolLifetime,0x1000,4090,4);
  CePoolRangeChanged(&mCepheusPoolLifetime,0x2000,1);
  assert(CePoolLookup(&mCepheusPoolLifetime,0x1000,4090,4,0,&a));reset();
  for(unsigned which=0;which<3;which++) {
    CePoolRangeChanged(&mCepheusPoolLifetime,which==1?1:which==2?MAX_UINT64-4095:0,which==0?0:1);
    assert(mCepheusPoolLifetime.Failed);reset();
  }
}

static void actual_page_conversion(void)
{
  for(unsigned attributes=0;attributes<2;attributes++) {
    void *p=NULL;assert(!CoreAllocatePool(4,9000,&p));
    CE_POOL_ALLOCATION a=get(p,9000,4,0,1);
    UINT64 start=(UINTN)p&~UINT64_C(4095);UINT64 count=3;
    MEMORY_MAP original={.Type=EfiBootServicesData,.Start=start,.End=start+count*4096-1,.Attribute=8};
    InitializeListHead(&gMemoryMap);InsertTailList(&gMemoryMap,&original.Link);mMapDepth=0;
    CoreAcquireMemoryLock();
    assert(!CoreConvertPagesEx(start,count,!attributes,EfiConventionalMemory,attributes,0x2000));
    CoreReleaseMemoryLock();get(p,9000,4,a.Generation,0);
    assert(added_map.Type==(attributes?EfiBootServicesData:EfiConventionalMemory));
    assert(added_map.Attribute==(attributes?0x2000u:8u));
    /* The direct page path bypasses FreePool, just as the actual core can. */
    if(attributes)assert(!CoreFreePool(p));
    else {
      CoreAcquireMemoryLock();CoreFreePoolPages(start,count);CoreReleaseMemoryLock();
      reset();
    }
  }
  reset();
}

int main(void)
{
  real_allocator();rejection();boundaries();actual_page_conversion();
  assert(profile_allocs>230&&profile_frees>230&&page_frees>30);
  printf("Actual staged DXE pool: %u allocations/%u frees/%u pages; ABA, guards, OEM, poison, PROT_NONE PASS\n",
         profile_allocs,profile_frees,page_frees);
  return 0;
}
