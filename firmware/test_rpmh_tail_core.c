/* Included after actual staged Pool.c/ConvertPagesEx and the allocator fixtures. */
static VOID *tail_images[3];
static MEMORY_MAP *tail_map[259];
static UINT64 tail_array, tail_cx, tail_mx;
static struct ceph_rpmh_tail tail_good;

static void tw32(UINT64 p,UINT32 v) {memcpy((void *)(UINTN)p,&v,4);}
static void tw64(UINT64 p,UINT64 v) {memcpy((void *)(UINTN)p,&v,8);}
static void tail_map_setup(void)
{
  InitializeListHead(&gMemoryMap);unsigned n=0;
  CoreAcquireMemoryLock();
  for(unsigned i=0;i<3;i++) {
    tail_map[n]=AllocateMemoryMapEntry();assert(tail_map[n]);
    *tail_map[n]=(MEMORY_MAP){.Signature=MEMORY_MAP_SIGNATURE,.FromPages=1,.Type=EfiBootServicesCode,
      .Start=(UINTN)tail_images[i],.End=(UINTN)tail_images[i]+mCeTail.Image[i].Bytes-1,.Attribute=8};
    InsertTailList(&gMemoryMap,&tail_map[n++]->Link);
  }
  for(unsigned i=0;i<ARRAY_SIZE(pages);i++)if(pages[i].live) {
    assert(n<ARRAY_SIZE(tail_map));tail_map[n]=AllocateMemoryMapEntry();assert(tail_map[n]);
    *tail_map[n]=(MEMORY_MAP){.Signature=MEMORY_MAP_SIGNATURE,.FromPages=1,.Type=EfiBootServicesData,
      .Start=(UINTN)pages[i].base,.End=(UINTN)pages[i].base+pages[i].bytes-1,.Attribute=8};
    InsertTailList(&gMemoryMap,&tail_map[n++]->Link);
  }
  CoreReleaseMemoryLock();
}
static void tail_image(CE_TAIL_IMAGE *image,unsigned n,UINTN bytes,CONST UINT8 *header,UINTN headerbytes,
                       UINTN baseoff,UINTN rva,CONST UINT64 *words,UINTN count,CONST UINT32 *fixups)
{
  void *p=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);
  tail_images[n]=p;UINT64 base=(UINTN)p;
  *image=(CE_TAIL_IMAGE){base,bytes,header,headerbytes,baseoff,rva,count,words,fixups};
  memcpy(p,header,headerbytes);tw64(base+baseoff,base);unsigned fix=0;
  for(unsigned i=0;i<count;i++) {
    UINT64 value=words[i];if(fixups[fix]==i) {value+=base;fix++;}tw64(base+rva+8*i,value);
  }
}
static UINT64 tail_object(UINTN bytes)
{
  void *p=NULL;assert(!CoreAllocatePool(4,bytes+4,&p));memset(p,0,bytes+4);
  tw32((UINTN)p,bytes);return (UINTN)p+4;
}
static void tail_fixture(void)
{
  reset();ZeroMem(&mCeTail,sizeof(mCeTail));ZeroMem(&mCeTailRecord,sizeof(mCeTailRecord));
  ZeroMem(mCeTailMapSpan,sizeof(mCeTailMapSpan));mCeTailMapFailed=0;
  InitializeListHead(&mFreeMemoryMapEntryList);
  fake_ticks=1000;fake_pars=0;fake_fail_par=0;fake_masked=0;fake_revoke=0;
  fake_revoke_base=0;fake_bad_par=0;fake_bad_el=0;fake_frequency=19200000;
  fake_late_par=fake_late_kind=0;
  tail_image(&mCeTail.Image[0],0,CE_RPMH_IMAGE_BYTES,mCeRpmhHeader,sizeof(mCeRpmhHeader),
    CE_RPMH_BASE_OFFSET,CE_RPMH_TEXT_RVA,mCeRpmhText,ARRAY_SIZE(mCeRpmhText),mCeRpmhFixups);
  tail_image(&mCeTail.Image[1],1,CE_DAL_IMAGE_BYTES,mCeDalHeader,sizeof(mCeDalHeader),
    CE_DAL_BASE_OFFSET,CE_DAL_TEXT_RVA,mCeDalText,ARRAY_SIZE(mCeDalText),mCeDalFixups);
  tail_image(&mCeTail.Image[2],2,CE_CMDDB_IMAGE_BYTES,mCeCmdDbHeader,sizeof(mCeCmdDbHeader),
    CE_CMDDB_BASE_OFFSET,CE_CMDDB_TEXT_RVA,mCeCmdDbText,ARRAY_SIZE(mCeCmdDbText),mCeCmdDbFixups);
  tw64(mCeTail.Image[0].Base+0xe610,mCeTail.Image[1].Base+0x1f0d0);
  tw64(mCeTail.Image[1].Base+0x1f160,mCeTail.Image[1].Base+0x3390);
  tw64(mCeTail.Image[1].Base+0x1f168,mCeTail.Image[1].Base+0x33c8);
  tw64(mCeTail.Image[2].Base+0x6088,mCeTail.Image[2].Base+0x1444);
  tail_array=tail_object(48);tail_cx=tail_object(72);tail_mx=tail_object(72);
  tw32(tail_array,0x30000);tw64(tail_array+8,tail_cx);
  tw32(tail_array+24,0x30004);tw64(tail_array+32,tail_mx);
  tw32(tail_cx+24,5);tw32(tail_mx+24,6);
  *(UINT8 *)(UINTN)(tail_cx+28)=1;*(UINT8 *)(UINTN)(tail_mx+28)=1;
  tw32(mCeTail.Image[0].Base+0xe644,2);tw64(mCeTail.Image[0].Base+0xe648,tail_array);
  mCeTail.Address[0]=0x30000;mCeTail.Address[1]=0x30004;
  tail_map_setup();fake_runtime.AtRuntime=0;gBS=(void *)1;
}
static void tail_cleanup(void)
{
  mCeTail.Active=0;fake_masked=0;gMemoryMapTerminated=0;fake_revoke_base=0;
  for(unsigned i=0;i<3;i++)assert(!munmap(tail_images[i],mCeTail.Image[i].Bytes));
  for(unsigned i=0;i<ARRAY_SIZE(pages);i++)if(pages[i].live) {free(pages[i].base);pages[i].live=0;}
  reset();
}
static int tail_capture(void)
{
  mCeTail.Bound=1;mCeTail.Finished=0;mCeTail.Attempts=1;
  gMemoryMapTerminated=1;fake_runtime.AtRuntime=1;gBS=NULL;
  fake_pars=0;CoreCepheusRpmhTail();assert(!mCeTail.Active&&fake_masked==0);
  struct ceph_rpmh_tail copied;assert(!CeTailCopy(&copied));
  assert(!memcmp(&copied,&mCeTailRecord,sizeof(copied)));
  assert(copied.complete&&copied.requests.phase==2);
  if(copied.error)assert(!memcmp(copied.requests.resource,(UINT8[sizeof(copied.requests.resource)]){0},sizeof(copied.requests.resource)));
  return copied.error;
}
static void tail_bind_tests(void)
{
  tail_fixture();UINT64 bases[3];for(unsigned i=0;i<3;i++)bases[i]=(UINTN)tail_images[i];
  EFI_STATUS status=CeTailBind(bases);
  if(status)fprintf(stderr,"Bind status=%llx reads=%u failed=%u map=%zu elapsed=%llu words=%zu/%zu/%zu\n",
    (unsigned long long)status,mCeTail.Reads,mCeTail.Failed,mCeTail.MapCount,
    (unsigned long long)(fake_ticks-mCeTail.Begin),mCeTail.Image[0].TextWords,
    mCeTail.Image[1].TextWords,mCeTail.Image[2].TextWords);
  assert(!status);assert(mCeTail.Bound&&mCeTail.Address[0]==0x30000&&mCeTail.Address[1]==0x30004);
  assert(CeTailBind(bases)==EFI_ACCESS_DENIED);
  CoreCepheusTailAttempt();assert(mCeTail.Attempts==1);tail_cleanup();
  tail_fixture();for(unsigned i=0;i<3;i++)bases[i]=(UINTN)tail_images[i];
  bases[2]=bases[0];assert(CeTailBind(bases)==EFI_INVALID_PARAMETER);tail_cleanup();
  tail_fixture();for(unsigned i=0;i<3;i++)bases[i]=(UINTN)tail_images[i];
  *(UINT8 *)tail_images[0]^=1;assert(CeTailBind(bases)==EFI_COMPROMISED_DATA);tail_cleanup();
}
static void tail_tests(void)
{
  tail_bind_tests();tail_fixture();assert(!tail_capture());
  assert(!ceph_rpmh_tail_valid(&mCeTailRecord));tail_good=mCeTailRecord;
  assert(mCeTailRecord.requests.identity_mask==7&&mCeTailRecord.boundaries==15);
  assert(mCeTailRecord.requests.resource[0].drv[0].local[0].data==5);
  unsigned baseline=fake_pars;tail_cleanup();
  for(unsigned i=0;i<sizeof(tail_good);i++) {
    struct ceph_rpmh_tail corrupt=tail_good;((UINT8 *)&corrupt)[i]^=1;assert(ceph_rpmh_tail_valid(&corrupt));
  }
  tail_fixture();tw32(tail_cx+24,9);assert(!tail_capture());
  assert(mCeTailRecord.requests.resource[0].drv[0].local[0].data==9);tail_cleanup();
  tail_fixture();assert(!CoreFreePool((void *)(UINTN)(tail_cx-4)));assert(tail_capture());tail_cleanup();
  tail_fixture();UINT64 moved=tail_object(48);memcpy((void *)(UINTN)moved,(void *)(UINTN)tail_array,48);
  assert(!CoreFreePool((void *)(UINTN)(tail_array-4)));tw64(mCeTail.Image[0].Base+0xe648,moved);
  tail_map_setup();assert(!tail_capture());tail_cleanup();
  for(unsigned which=0;which<6;which++) {
    tail_fixture();
    if(which==0)tail_map[0]->Attribute=0x2000;
    if(which==1)tail_map[0]->Type=EfiMemoryMappedIO;
    if(which==2)tail_map[0]->Link.BackLink=NULL;
    if(which==3)tail_map[0]->End++;
    if(which==4)tail_map[1]->Start=tail_map[0]->Start;
    if(which==5)fake_bad_par=1;
    assert(tail_capture());tail_cleanup();
  }
  for(unsigned i=1;i<=baseline;i++) {
    tail_fixture();fake_fail_par=i;assert(tail_capture());
    unsigned halted=fake_pars;assert(CeTailRead(NULL,(UINTN)tail_images[0],(UINT8[1]){0},1));
    assert(fake_pars==halted);tail_cleanup();
  }
  /* Failed translation revokes a real mapped image page, no later load allowed. */
  tail_fixture();fake_fail_par=1;fake_revoke=1;fake_revoke_base=(UINTN)tail_images[0];
  assert(tail_capture());tail_cleanup();
  tail_fixture();CeTailMapRangeChanged((UINTN)tail_map[0]&~UINT64_C(4095),1);
  assert(tail_capture());tail_cleanup();
  tail_fixture();UINT64 map_page=(UINTN)tail_map[0]&~UINT64_C(4095);
  CoreAcquireMemoryLock();
  assert(!CoreConvertPagesEx(map_page,1,FALSE,EfiBootServicesData,TRUE,0x2000));
  CoreReleaseMemoryLock();assert(CeTailMapNode((UINTN)tail_map[0])<0);
  assert(tail_capture());tail_cleanup();
  /* Arbitrary map links must fail provenance before probing or dereferencing. */
  tail_fixture();void *unknown=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  assert(unknown!=MAP_FAILED);gMemoryMap.ForwardLink=(void *)((UINTN)unknown+OFFSET_OF(MEMORY_MAP,Link));
  assert(tail_capture());assert(fake_pars==2);assert(!munmap(unknown,4096));tail_cleanup();
  tail_fixture();tail_map[0]->FromPages=0;assert(tail_capture());tail_cleanup();
  tail_fixture();mCeTailMapFailed=1;assert(tail_capture());
  assert(!mCeTailRecord.reads&&!ceph_rpmh_tail_valid(&mCeTailRecord));tail_cleanup();
  tail_fixture();for(unsigned i=0;i<ARRAY_SIZE(mCeTailMapSpan);i++)
    mCeTailMapSpan[i]=(CE_TAIL_MAP_SPAN){0x100000+(UINT64)i*4096,0x101000+(UINT64)i*4096,1};
  CeTailMapAllocated(0x900000,4096);assert(mCeTailMapFailed&&tail_capture());tail_cleanup();
  for(unsigned kind=1;kind<=5;kind++) {
    tail_fixture();fake_late_par=baseline;fake_late_kind=kind;
    assert(tail_capture()==CE_RPMH_SNAP_CHANGED);
    assert(!ceph_rpmh_tail_valid(&mCeTailRecord));tail_cleanup();
  }
  tail_fixture();mCeTail.Active=1;mCeTail.Serial=mCepheusPoolLifetime.Serial;mCeTail.Begin=0;
  mCeTail.Frequency=fake_frequency;fake_ticks=1;
  assert(CoreCepheusTailRejectMutation());assert(mCeTail.Failed);mCeTail.Active=0;tail_cleanup();
  tail_fixture();mCeTail.Active=1;void *p=NULL;
  assert(CoreAllocatePool(4,72,&p)==EFI_OUT_OF_RESOURCES&&mCeTail.Failed);
  assert(CoreFreePool((void *)(UINTN)(tail_cx-4))==EFI_ACCESS_DENIED);mCeTail.Active=0;tail_cleanup();
  tail_fixture();mCeTail.Active=1;CoreAcquireMemoryLock();
  LIST_ENTRY before=gMemoryMap;assert(!AllocateMemoryMapEntry());
  assert(CoreConvertPagesEx((UINTN)tail_map[0]&~UINT64_C(4095),1,TRUE,EfiConventionalMemory,FALSE,0)==EFI_ACCESS_DENIED);
  assert(!memcmp(&before,&gMemoryMap,sizeof(before))&&mCeTail.Failed);
  CoreReleaseMemoryLock();mCeTail.Active=0;tail_cleanup();
  printf("Actual exit-tail core: %u translation boundaries, phase2/full-byte corruption/moved/freed/map/freeze/PROT_NONE PASS\n",baseline);
}
