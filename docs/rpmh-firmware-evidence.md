# Cepheus RpmhDxe静态取证

## 2026-11-10 16:29：NPA请求完成与PDC配置续查

手机保持已验收配对版，无重启/协议/SMC/MMIO执行；未构建新核或改动生产
后端。新增NPA与Clock完成路径纳入原字节核验，身份均与accepted boot一致。

NPA revision0x10002/table0x110b8的slot0x18→0x145c→0x391c→0x91a8，
**完成当前client请求，不是销毁client或全局清票**。汇编明确client在x0，
不能按伪代码误识别成额外参数。设置client+0x80 complete bit0；非type0x200
路径只清pending首32位，地址为client+0x38+((active_index XOR1)*0x18)，
并按old active首值非零与否调用client+0x88。type0x200另转0x31c8，未展开。
0x29b0首用/属性分支可强制更新再装0x8f64。0x8f7c调用plugin0x922c和条件
node driver0x9258，之后翻转active index与更新软件sequence；无变化等分支
可能跳过driver。独立destroy0x3a7c先complete，再unlink、plugin destroy、free。
注意protocol slot0x18与plugin slot0x18含义不同，不能按偏移名混为一谈。

Clock plugin0x432a0首指针0xc704、destroy0xc930。0xc704读取pending len>>3，
零时遍历**本client**配置route count，写零IB/AB/latency；0xd720比较新旧记录
并经0xd270去重入队master/slave节点，0xdb80进入已另验的聚合提交。
这保留其他client记录；全局BCM/ARC不必为零。独立0xc930才移除节点记录并
free client data。0xc704未被Ghidra定义成函数，使用完整556B raw/Capstone
解码，拒绝把伪代码不可用当成不可达。

私有logs新增npa-request-complete-v2（52 records）/clock-request-complete-v2
（148 records）bundle与snapshot；v1/final保留。NPA pending汇编
`ev_c588bf50dd6b001b806d1f1f1f81bcb919a9168072b299ab795b478df9c006bd`；
Clock完整update read
`ev_a3cc7f01211cd7134a7a55bf902298b2685ba143c0cfe5c3574cb794eedfeedb`。
核验新增NPA61/Clock47指令及范围/协议绑定；24测试普通/-O通过，含逐指令
拒绝、pending index/store宽度和slot突变。v5两模式receipt完全一致，SHA
`695440d362a8329d19082ca4ee34b50938496e6d22eaa0cadb0c3ec04bc30b31`。
crypto31/ICB24/NPA8回归通过。PASS_STATIC_BYTES_ONLY不代表活态ACK。

PdcDxe GUID B43C22DB-6333-490C-872D-0A73439059FD，36864B/SHA
`377e09d998ea8c538d77eb3ffca65194e7287d88b9e579dc6575dd2ff7f739eb`，
与accepted boot逐字节一致。0x1f58→0x2164经CmdDb slot0x10查询cx.lvl/
mx.lvl/xo.lvl/vrm.soc（初始地址0/0/0/4），检查0x0b291004字段后配置三组
TCS命令/使能。0x2450按chip版本选择display序列表，0x2418→0x2704设置
动态base+0x4500 bit0（保留bit31）。这是启动配置链，不是已证明的退出清票
或最终ARC状态。保存pdc-resource-config-v2（33 records）bundle/snapshot；
TCS汇编`ev_5cead7820611408a8b1e0e0ce5e691dc20cd9c46ea179191b670e5d6c8586730`。
未穷举PDC所有callback/其他writer，不能宣布全部退出路径已收敛。

下一是实际ARC registrar/最终writer及唯一XNU资源owner，另需解决配对
UEFI/XNU均PAS13=false的运行原因、secure PA/XPU/crypto clocks和SMMU。
本轮仅补静态链，无新GPU镜像/真机测试点；supported=false/CP halt保持，
GPU scratch/fence、Metal和PAS认证均未执行。

## 2026-11-10 16:08：显示退出的条件分支与ICB后续写入

离线继续，无手机/协议/SMC/MMIO执行，无内核或生产后端改动；Darwin/USB批
`714ca63`已推送，以下是后续增量，不覆盖v3结论或receipt。

accepted patched DisplayDxe GUID`4138022F-06C7-4F79-9C94-7E33B511A4E7`，
274432B/SHA`7a1799156845c0934164feb87831157f56dd8d5fa5a16356314bede5a64d3552`，
与device patched PE逐字节相同。全PE未找到EBS group GUID literal，**不代表
没有EBS callback**：init`0x1650`在`0x1708..0x1733`实际用BS+0x50注册
type0x201/TPL8/callback`0x14dc`。callback关闭UI/refresh事件，`0x2d64(0)`
经动态protocol slot+8取消callback，然后`0x15a8`尾跳`0x13c28`。

`0x13c28`循环三个display item，supported表0x35338/状态基址0x3ca50/
跨度0x628。**keep latch是跨item累积的x25，不是每panel局部布尔值**：初值1，
某supported item的state+0x5a3 bit0非零后CSEL置0；后续item不会重新置1。
latch为0且state+0x18等于1时才调用panel deinit及两次common/specific power
helper。三个item遍历结束后，latch为0才调用`0x13b58`的clock groups0..4。
free对象及`0x1a3dc`的最终display-state copy另行执行；后者不是ARC/client
全局快照。动态supported/flags/活动state未采集，不能声称本轮停掉了显示。

Clock关闭链`0x1261c`定位动态protocol，按动态平台表选clock group，跳过
entry+0x30 bit0置位项；slot+8查clock handle，slot+0x18调用关闭。core group
还调用`0x12598(0)`的条件domain关闭和`0x124fc(0)` bandwidth helper。
`0x124fc`若handle为空可新建`/icb/arbiter`的display client，type0x400/
create数据16B；arg1经`0x1ea84`发请求，否则`0x1eb08`定位NPA后调用slot+0x18。
此slot在accepted NPA的真正后端/效果尚未绑定，不能写成“全局清票”。部分
clock调用结果被忽略；`0x13c28`最终返回0也不是成功硬件ACK/停机凭据。

私有logs保存`display-resource-handoff-final-evidence.json`32 records及
snapshot，v1/v2中间bundle也保留。条件分支assembly Evidence
`ev_4ecb7aabc8f25d03357a7647a2bc161bd68bcfdd169110a98272349aeead2cc9`；
完整函数read `ev_aa381858fcbe54c6a28b4aee12b138d16e06a0a725e6296645d6b0c0abcd12c5`；
NPA包装read `ev_620018f6dc1a66eb90adba366dbfdf85dafed43ecc839c663bfbee15067b1955`。

交接核验新增Display PE身份、七段coverage/65关键指令（累计DXE core61+
Darwin68+USB42+Display65）。17项测试普通/-O通过，含每段缺失/每条拒绝/
真实CSEL条件翻转；v4两模式receipt PASS_STATIC_BYTES_ONLY且完全相同，SHA
`c3156f2106ab179dd65d214b9ea3b4befa8b3beed46782694d06fed19282ab0c`。
已排除“没GUID就没退出callback”和“每panel独立keep flag”两种错误推断，
未证明硬件最终状态或所有writer。下一绑定NPA slot+0x18、PDC/其余相关退出
写入和最终完整client/ARC/XNU owner；PAS13=false、secure PA/XPU/crypto clocks/
SMMU仍缺。无新构建/手机操作/真机点，不绕过supported=false/CP halt；
GPU scratch/fence、Metal与PAS认证未执行。

## 2026-11-10 15:56：实际DarwinBoot与USB退出路径

本机离线补证，无固件执行、协议调用、手机/SMC/MMIO操作或生产后端改动。
上一批`91f34eb`保持；旧bundle/snapshot/receipt不覆盖。

accepted boot内DarwinBoot GUID`1B64AF87-8415-4B57-9B16-680D70913B9F`，
27136B/SHA`8ce496a6621101c3354dc96cf031fb7ac1dc2ca89775cccddf19a07c41f4051b`，
与配对版staged RELEASE PE逐字节相同。map仅定位；实际字节经REA和
Capstone核验。entry`0x1050`用BS+0x50注册type0x201/TPL16/callback0x130c；
callback只写module+0x78b0为1再返回，不执行资源quiesce。

Boot entry`0x133c`的`0x27b8..0x290b`最多三次GetMemoryMap→EBS，BS槽为
0x38/0xe8。x27在`0x1360..0x136b`构造EFI_INVALID_PARAMETER
`0x8000000000000002`；只有该EBS错误重取map重试，其他错误进断言路径。
成功后按诊断flag封存/复制0xa60B SCM记录，再BL`0xb000` DarwinEnter。
后者屏蔽DAIF、画framebuffer条、DC CIVAC/IC IALLU、禁EL1 MMU/cache、
TLBI VMALLE1、BRx1。**CPU维护不是GPU DMA/SMMU停机；SCM记录不是资源lease。**
这将上一节staged源码观察提升为accepted PE的有界指令观察，仍非运行实测。

accepted patched UsbConfigDxe GUID`CD823A4D-7DEC-4531-AE5D-4134FA4127B8`，
90112B/SHA`3dd36eef1cbe67eb65acb5f1825d8ce461d62ab7122d87e7a0a5475ea7636371`，
与device PatchedBinaries原件逐字节相同。init`0x1790`用CreateEventEx注册
type0x200/TPL8/EBS GUID14028/callback16e4；实际BL清理位于`0x16f0→0x4670`。
清理关闭poll事件，循环两个core，调用vtable+0x60(arg1)，释放core/port，
条件调用domain协议+8并清domain指针。init`0x7c2c`把该core槽分别设为
`0x935c`/`0x9e98`；两setter对非零arg1均跳过PHY调用。文件flagf2cc/f2cd
为0/1，但不是手机活态flag。domain调用ABI/完成未定，不能推广成GPU或
全局SMMU teardown，也不能从返回值存入stack后忽略推导停机成功。

USB有界ICB消费链：`0x9fd4`创建`/icb/arbiter`上usb_ss1_bus/usb_ss2_bus，
type0x400/create数据16B；`0x80c0`/`0x9498`初始化和`0x8cd8`/`0x9b50`低功耗
通过`0xe498→NPA slot0x90`发送16 words向量请求。退出清理已定位的core
setter不是这两个LPM函数，free helpers仅释放对象。**不能声称EBS清空
这些ICB票或掌握完整client/最后writer。**路由/单位/运行成功仍待验证。

证据在既有私有logs目录：`darwin-resource-handoff-final-evidence.json`
9 records及snapshot（含错误常量read）；`usb-resource-handoff-v2-evidence.json`
42 records及snapshot，USB v1/final和Darwin v1保留；PDC entry初查5 records
另存`pdc-resource-entry-v1`，未发现GUID只为有界检索，不是全模块缺席证明。
Darwin错误常量read Evidence
`ev_62e250833aa6330a52356a28502ce25bd85b3db666a4db0cc081f2dcf500939c`；
USB callback read
`ev_dc4284110ebc2b59bd0c2218aa138e23bc5db446b81e8bd7dd307ae16c6e688b`。

`verify-resource-handoff-evidence.py`扩展两PE身份/embedded字节、五段Darwin/
八段USB coverage，68+42条指令（另保留DXE core61条）。15项测试普通/-O
通过，含每段缺失、每条关键指令拒绝、真实合成CMP/BL篡改、GUID/flag和
两PE身份/嵌入拒绝。v3两模式receipt PASS_STATIC_BYTES_ONLY完全相同，SHA
`ba1d401253da3ef887f5b0769770702b7b69f2f94ecc7db6da18cb810c0909cb`。
crypto31/ICB24/NPA8回归通过，无新核/构建/真机测试点。下一收敛Display/PDC
退出写入与完整资源状态/唯一XNU owner；PAS13=false、secure PA/XPU、crypto
clocks/SMMU仍缺，supported=false/CP halt保持，GPU/Metal/PAS认证未执行。

## 2026-11-10 15:35：两层聚合与已验收DXE派发顺序

本机离线原件核验；没有协议执行、SMC/MMIO、手机操作、生产后端或内核改动。
上一批已提交推送`b23e4ec`，本节是后续新增，不覆盖旧receipt或证据。

**Clock实际两层聚合已连通，不能只继承crypto单客户端请求：**

- `0xd294`遍历节点request list（count+0/table+8），每项IB+0、AB+8、
  NPA client pointer+16；读取client+0x30的类型。`type==0x1400`和其他
  客户端分成两桶，各自取最大IB、累加AB。只记录原类型，未凭参考命名为
  suppressible/required。节点state为total+0、两桶IB+8/+16、AB+24/+32。
- `0xd138`在非溢出域取`max(两桶IB, ceil(两桶AB之和/max(ports,1)))`。
  原循环的AB相加是64位模加，没有溢出拒绝；不能直接用于任意外部输入。
- `0xd450`再遍历BCM关联的master kind7/slave kind8节点；kind0按width/
  aggregation ports向上换算，每桶IB/AB分别取跨节点最大值，BCM total为
  `max(两桶IB, 两桶AB之和)`，不是再把所有路径AB求和。mode3跳过重算；
  `0xd9d4`已查初始化只设置mode1、随后mode2，不能据此穷举其他模式写入。
- `0xe894`形成active bandwidth：IB取两桶最大、AB取两桶之和，各按unit
  ceil后交`0xe190`编码；`0xe964`对vector kind2按total是否非零选择
  `node+0x50 | valid29`或零。`+0x50`动态应用mask的初始化仍未建立。
- `0xdb80..0xdc63`先排空software queue0x451f0，再排空hardware queue
  0x451e8，分别进入节点/BCM聚合后尾跳`0xe9dc`。Ghidra同时给该canonical
  procedure附属`0xe200..0xe273`，两段均保存，但不能用附属清理段代替提交段。
  `0xe8fc`按动态clock_id+0x18排序RPMh请求，byte+0x14避免重复排队。
  **软件队列排空不是活态ACK，也不是完整client/最终交接快照。**

`verify-crypto-icb-evidence.py`增加上述76条关键指令与有界函数覆盖要求。
31项合成测试普通/优化均通过，包括原指令拒绝、每段缺失及不连续函数边界。
v3两模式原件receipt为PASS_STATIC_BYTES_ONLY且完全相同，SHA256
`c7e2af21cc19214027f3cd399756151ded5516ef208580e2e815824a7720cb90`。
新`clock-resource-handoff-v1-evidence.json`含142 records，另保留snapshot。

**首次绑定实际DXE core，而非把相邻源码当作手机行为：**

accepted boot内GUID`D6A2CB7F-6A18-4E2F-B43B-9920A733700A`的PE119808B，
与本机`uefi/Build/SurfaceDuo1Pkg/RELEASE_CLANGPDB/AARCH64/DxeCore.efi`
逐字节相同，SHA256
`311a20a50632dbabdfcdaa9bc2ed7d08e4712c2c70b8f3f18b66834803812be8`。
DEBUG产物不相同，未用于推断。link map仅帮助定位；结论另经REA/Ghidra及
Capstone原字节核验，不依赖map符号可信度。

`CoreExitBootServices`实际entry`0x1a80`：Before group只派发一次→禁timer→
校验map key及runtime map→失败派发Failed group返回，或设置terminated并
派发EBS group→内存保护/CPU中断处理→清system table BootServices并清表。
三个GUID原字节已匹配，EBS为`27abf055-b1b8-4c26-8048-748f37baa2df`。

`0x5f24`通过`0x133b0`把signal event插在group链表头；`0x6194`向前遍历，
`0x6120→0x133c4`将notify插在每TPL队列尾。释放event lock经
`0x5f18→0x13394→0x3dec`，选择高于调用者TPL的最高pending TPL先处理，
每TPL取队头、释放锁、直接BLR callback并继续排空。因此同TPL简单情形为
**注册逆序**，不同TPL为**高TPL先运行**。已排队/重排队、嵌套、事件增删及
实际caller TPL会影响顺序；没有手机最终event清单，不能断言RPMh回调是
全局最后写入者。同步callback返回也不代表其资源已ACK或外部writer消失。

当前`unixnu/.../DarwinBoot.c`与配对版staged源码SHA不同；真正构建源码为
`out/scm-pair-final-6ce4c7c/firmware/source/DarwinBootPkg/Driver/DarwinBoot.c`，
SHA`b3421f8fad312348ae26040d2f973408d320124c6d1f43456611b8ca100d6150`。
其661行capture在循环前；664行GetMemoryMap、669行EBS、671行成功后
SCM handoff、672行DarwinEnter。最多三次，只有EFI_INVALID_PARAMETER重取
map重试。这里是**staged源码观察**，本轮未反汇编该DarwinBoot入口；没有
资源quiesce/final snapshot的源码路径，既有SCM handoff不是资源交接凭据。

新增独立`scripts/verify-resource-handoff-evidence.py`，绑定accepted core并
核验61条指令/三GUID/有界读取，6项合成测试普通/优化通过。v2两模式receipt
PASS_STATIC_BYTES_ONLY且相同，SHA256
`8e283f1f3bd2cb2a3b5c25c7a861db9548f6ade79e40b143980cd0d51f4044f3`。
私有logs目录保留`dxecore-resource-handoff-final-evidence.json`30 records及
snapshot；v1/v2中间证据和旧receipt均保留。NPA8/ICB24回归通过。

下一需绑定staged DarwinBoot实际入口、收敛其他最后写入者/完整client及
ARC状态，才能设计preallocated最终交接与XNU唯一owner。新建一个EBS
callback、提高TPL或直接复制pre-EBS缓存都不保证“最后”，不能补造lease。
PAS13=false、secure PA/XPU、crypto clock生命周期及SMMU仍缺；本轮没有
生产后端、新核、构建或GPU真机点。手机保持正常配对版，supported=false/
CP halt不绕过，GPU scratch/fence、Metal和PAS认证仍未执行。

```sh
python3 scripts/test-resource-handoff-evidence.py
python3 -O scripts/test-resource-handoff-evidence.py
python3 scripts/verify-resource-handoff-evidence.py --root .. \
  --output /absolute/new/resource-handoff-receipt.json
```

## 2026-11-10 15:08：crypto BCM映射、CmdDb来源与换算

本机离线原件续查；没有执行固件/协议/SMC/MMIO，手机和生产后端均未改动。
accepted boot新增第11个模块身份核验：device CmdDbDxe，GUID
ABA01FF8-2CCB-4E12-8B2E-CD3F4A742993，32768B，SHA256
`f1fe60c53ca0eb2613cec20aa6c69d8375543c76bbc226ba3a8c41bfc5535849`，
与boot内PE逐字节相同。不能依据邻近Epsilon版本代替device版本。

沿上一节八个route节点的原结构和Clock实际访问指令，确认master的BCM count/
table为+0xd0/+0xd8，slave为+0x90/+0x98；每个BCM均在topology+0x48/+0x50
所指30项hardware table0x313f0中唯一出现。具体映射：

| Route Node | Configured BCM |
| --- | --- |
| qxm_crypto | CE0 (0x3ca30) |
| qns_a2noc_snoc | 空 |
| qnm_aggre2_noc | SN11 (0x3b160) |
| qns_gemnoc_sf | SN0 (0x3b260) |
| qnm_snoc_sf | 空 |
| qns_llcc | SH0 (0x321f8) |
| llcc_mc | 空 |
| ebi | MC0 (0x32350), ACV (0x32448) |

CE0/SN11/SN0/SH0/MC0的kind=0，ACV kind=2(vector)；每项文件里的command
address+0xc、width+0x10、bw_unit+0x14、clock_id+0x18均为0。**这些零是
待初始化字段，不是有效命令地址、有效单位或当前投票**。

Clock0xd9d4使用上述count/table建立hardware request list；master request
kind7，slave kind8。0xd350/0xd3d0再通过这些表排入hardware queue；前者有
不连续函数体0xd0e0..0xd137与0xd350..0xd3cf，已补读完整两段，不能按单段
边界误判末尾。这里核验指针/字段和排队指令，不宣称完整运行生命周期完成。

Clock0xe474依hardware table取每个节点名，0x129f8经CmdDb slot0x38查询8B
aux数据，0x12978经slot0x10查地址；两者使用GUID
1c34f691-d33d-4e14-b8f8-32c6c029e95b。aux的u32+0、u16+4、u8+6分别存入
节点unit/width/clock_id；address存入+0xc。这是**动态字段来源**，尚未读取
手机数据库或确认初始化成功。Clock查询helper存在失败/返回零路径，不放宽
未来后端的非零地址/单位、bounds与所有权门槛。

CmdDb原表0x6078 revision0x10000，slot0x10→0x1444→0x2128(address查询)，
slot0x38→0x15b0→0x2410(aux查询)。初始化0x2540→0x45c0用32位load读取
`0x0c3f000c`的数据库指针并零扩展，随后核验version=1及magic=0x0c0330db，
不匹配则清数据库指针。这是已验收固件的**静态来源/头部校验**，本轮未读
该寄存器、未访问手机RAM，也不据此确认活态数据库边界或可被XNU继承。

Clock0xd450按BCM width/node width及aggregation ports缩放，IB/AB在有余数
时向上取整，分别取各类别最大值。0xe894随后按bw_unit再次向上取整；
0xe190饱和至14位，IB放低14位，AB放bits14..27，非零加valid bit29；
0xed58用动态地址构造command，每组末尾加commit bit30。这与现有
`ceph_bcm_div`的Android floor/minimum-one规则**不完全相同**，不能直接把
Android helper当作UEFI唯一聚合器的复刻。仍缺原请求/其他client的完整聚合
快照、运行模式/边界、ACV应用策略及最后EBS状态；不能生成“已确认”lease。

`verify-crypto-icb-evidence.py`扩展六节点、topology membership、动态零字段、
Clock指令和CmdDb原表/入口核验；参考identity追加EFICmdDb.h及cmd_db_bcm.h
固定git blob，异构建关联而非来源证明，未复制专有源码。新增10项合成测试，
共26项普通/优化通过；v2原件两模式PASS_STATIC_BYTES_ONLY且一致，SHA256
`df7c8a06837159c7d89fdeaafb97fffbc28d0d4003ddbacb3f481c2e1577a3c7`。
原NPA8/ICB24测试通过。v1和所有旧证据保留，没有内核构建。

新增私有bundle/snapshot在`../logs/pas13-instruction-replay-20261110/`：
`dal-crypto-bcm-v2-evidence.json`（77 records）、
`clock-crypto-bcm-final-evidence.json`（110 records）、
`cmddb-crypto-bcm-v2-evidence.json`（37 records）；v2两模式receipt同目录。
下一处理完整资源聚合/最终EBS交接及XNU唯一owner，另需PAS13支持条件、
secure PA/XPU、crypto时钟生命周期与SMMU。无新GPU真机测试点，不改
supported=false/CP halt，GPU scratch/fence、Metal和PAS认证未执行。

## 2026-11-10 14:48：具体crypto路由与请求ABI核验

仅本机离线取证，无固件执行、手机操作、SMC/MMIO或内核改动。复用已验收
boot的patched DALSys、device ClockDxe及**Epsilon HashDxe**原件身份。新增
`scripts/verify-crypto-icb-evidence.py`独立核验，未扩展生产资源后端。

DAL config0x2dfd0→fabric table0x2dfb8→topology0x30ed8，配置DRV=2，tree
root=0x30f30。Clock0xdc64组装`(master << 32) | slave`，经0xdd7c跳转到
0xdda0搜索；完整搜索体其实是0xdd80..0xddaf，不能只从canonical entry向后读。
原指令SUB后signed GT选择left/right。crypto key `0x1700000000`沿八节点：
0x30f30→0x314f0→0x31800→0x3aa10→0x3b370→0x3b568→0x3c410→0x3c888，
命中route0x3cb28（4 pairs，数组0x3cb78）。原件节点字段如下：

| Master (ID/width/aggregation ports) | Slave (ID/width/aggregation ports) |
| --- | --- |
| qxm_crypto (23/8/1) | qns_a2noc_snoc (143/16/1) |
| qnm_aggre2_noc (112/16/1) | qns_gemnoc_sf (104/16/1) |
| qnm_snoc_sf (76/16/1) | qns_llcc (243/16/4) |
| llcc_mc (180/4/4) | ebi (0/4/4) |

这是**配置路由**，不是手机活态路由或投票。master aggregation字段+0x28，
slave+0x1c；width+0x18。名称/ID/偏移与固定参考结构相关，不把Android的
125/512等其他命名空间ID直接等同。BCM节点/命令地址/单位和最终聚合仍待补。

Hash0x5138生成master23/slave0 pair；wrapper0x9e08以`/icb/arbiter`、client
名`/clk/ce1`、type0x400、pair字节数8调用NPA slot0x10。0x9d90校验pair
count后经0x9ee0调用NPA slot0x90，vector长度8个32位word，即单请求32B。
请求type为`0x10004`；union+8，IB=1600000000（+8）、AB=55250000（+16），
latency=0（+24）。常数来自0xb010，所有栈构造及consumer读取指令已核验。
padding未由该构造初始化，不作为语义字段。Clock0xcc24匹配低16位type4，
复制两个u64值；bit16存在则读取+24 latency，和Hash构造一致。

`docs/crypto-icb-reference.json`记录固定SDM670参考仓库/commit及三个git blob
身份，通过gh API重新核对size/blob。参考`icbarb.h`定义type3=4、latency flag
0x10000、IB/AB单位Bytes/second及latency nanoseconds；`icbid.h`中CRYPTO=23、
EBI1=0；`icb_soc_data.h`给出相关布局。**这是异构建参考关联，不是本机二进制
的构建来源证明**，单位不是现场测量。只提交元数据/核验代码，不复制专有源码。

私有证据目录`../logs/pas13-instruction-replay-20261110/`新增：
`dal-crypto-route-final-evidence.json`（50 records）、
`clock-crypto-route-final-evidence.json`（66 records）、
`hash-crypto-abi-final-evidence.json`（26 records）及各自snapshot；旧记录保留。
核验先绑定accepted boot模块并逐字节对照REA原读取，再检查有界树/环/指针/
证据覆盖/四pairs/原指令。v1普通与优化receipt完全相同、均PASS_STATIC_BYTES_ONLY，
SHA256 `57e76b3de4f396f101f472ad79fa1129a59211ba4bdb27c18de6709aebdf09d0`。
16项新增合成测试普通/优化均通过，原ICB24及NPA8项通过。不需要内核构建。

同时保存两个有界排查：NPA13dc→25a0初始化核心pool/log及/dev/null、core mutex、
/npa/in_stm等，0x9930 hook为RET，未在已查体中找到ARC registrar；仍存在
间接DAL初始化，不能作全局不存在证明。Pmic0x14398→0x1455c动态node路径的
0x149fc名称builder使用`/pm/`+type/chipletter/number+`/`+key（mV/on_off/mode/en），
这是已查PMIC路径，不是ARC。证据`npa-core-init-v2-evidence.json`（13 records）
及`pmic-resource-naming-evidence.json`（17 records）和snapshot；不反复重搜同体。

下一补crypto BCM映射/提交聚合/完成与最终EBS交接，再实现唯一XNU资源所有者。
本轮不证明current votes、live ACK或PAS资源lease，PAS13=false、secure PA/XPU、
crypto生命周期与SMMU未解；supported=false/CP halt保持，GPU scratch/fence、
Metal、PAS认证均未运行，无新真机测试点，手机保持正常配对版。

## 2026-11-10 14:17：DAL默认电压配置与Clock消费者绑定

复用已验收patched DALSys/ClockDxe身份，只读原件与REA记录。DAL PropBin
0x1da18为packed属性`0x12800011,index=1`，名称`ClockRailConfig`；sized entry
0x1d258（size8）指向0x1f230。该16B记录表依次是`/arc/client/rail_cx`、
`/vcs/vdd_gx`、`/arc/client/rail_mmcx`、`/arc/client/rail_mx`，每项默认值
`0x100`，后有NULL/0终止记录。没有单位或当前硬件状态证明。

Clock0xaf5c经0x1b6c取`ClockRailConfig`，按16B步长比较资源名、读记录+8
并写输出。0x6bec调用该helper，将默认值放入本地rail结构+0xdc，再以
callback0x6a4c调用ResourceAvailable wrapper0xa6e0。这进一步绑定了
**默认配置的消费者路径**，没有找到ARC节点注册者，也不能据此把current_cx/
current_mx填成0x100。Pmic/DAL动态命名、外部协议或其他初始化仍待追查。
读取“没有xrefs”的工具结果不作为不可达证明。

私有证据`dal-clock-rail-config-evidence.json`（25 records）和
`clock-rail-consumer-final-evidence.json`（57 records）及对应snapshot。
`verify-icb-registrar-evidence.py`增加属性/结构/四默认值/终止边界、14条
Clock读取与可用回调指令核验；v6普通/优化receipt PASS且相同，SHA256
`d50ad73ff9aaea7bcb0d903e26b3544f65b1ca48977127800aac8503def56e15`。
24合成测试普通/优化均通过，v1-v5均保留。没有内核/UEFI代码改动或构建，
手机保持正常配对版，无新测试点；PAS13=false与资源/SMMU接管尚未解决，
GPU/Metal/认证未执行。14:08批已提交推送`a6a332e`，本节为后续追加。

## 2026-11-10 14:08：内部隐式等待与EBS清理补证

同一已验收RpmhDxe原件SHA03ccd266...，REA/Ghidra12.1.2与本机Capstone
核验；只读文件，不执行固件/协议/SMC/MMIO，也没有新内核或手机测试。

**修正13:49的过宽推论：**slot0xb8/0xc0返回EFI_UNSUPPORTED仍成立，外层
barrier本身不保证完成；但不能由此推断整条提交链缺少等待，或断言CPR请求
没有完成。实际slot0x78→wrapper0x17f4，0x181c调用issue_command_set0x2cdc。
其共同返回点0x2ec8设置kind=1/churn=1、handle=x19/request_id=w20，再在
0x2ed8调用0x2a1c→0x4014；后者查pending record0x3e8c，必要时循环/递归
等待0x3f54。0x3f68→0x561c→0x735c读HAL IRQ位图，type0取base+0xd04&0xf，
type2取base+0x20d04&0xff；0x3f70无IRQ则继续，0x3f80进入完成处理0x2b20。
这是**handle范围的隐式churn**，不是所有客户端/EBS的全局屏障。该轮询循环
没有超时；静态代码不证明手机观察到ACK、错误处理正确或已有完整所有权。

**首次定位实际EBS写入路径：**entry0x14dc以BootServices+0x170创建type0x200、
TPL8的event，GUID0xe028为`27abf055-b1b8-4c26-8048-748f37baa2df`
（本机MdePkg EventGroup.h的ExitBootServices GUID），callback0x149c。
callback先0x14b0卸载RPMh协议，再尾跳0x38f4遍历配置的DRV/TCS：

- 0x3938→0x7d2c将TCS控制寄存器写0，type0为base+tcs*0x2a0+0xd14，
  type2为base+tcs*0x2a0+0x20d14。
- 0x3944→0x7e14将TCS命令使能写0，相应offset为0xd1c/0x20d1c。
- 完整0x38f4的两次直接调用只有上述clear函数，无pending/completion检查。
  这只是已定位的一个EBS回调，不是全部回调及顺序的证明；HAL校验失败路径
  仍存在，静态event构造也不证明运行时注册成功。

不能把TCS寄存器清零扩展成“ARC电源投票被清除”。但pre-EBS缓存快照不能
直接冒充最终XNU交接状态，必须处理此回调、其他最后写入者与接管顺序。
DarwinBoot.c现有GetMemoryMap→ExitBootServices→DarwinEnter源码路径没有
资源quiesce；这里仅作源码观察，不把未核对的源码当已验收二进制行为。

完整私有证据：`../logs/pas13-instruction-replay-20261110/`
`rpmh-implicit-churn-final-evidence.json`（61 records）和
`rpmh-ebs-final-evidence.json`（81 records），分别保留snapshot。
EBS clear原字节Evidence为`ev_c2646efd12d8a4b9aa30e293414570a79a88574de6ccba45bb14d053e59aa49c`、
`ev_4cbb1fef0d38d7f4e662fb98de819faf8bd8354ec6fd5f16288f93222741e79c`、
`ev_105a5d4029cef478b7c8ad72ca8f58a5897d492ec799c8d9047bc734d2910589`。
核验脚本增加issue slot、20条隐式等待指令、43条event/uninstall/clear指令
以及完整clear loop调用约束。`icb-registrar-verification-v5.json`和优化模式
receipt均PASS且逐字节相同，SHA256
`363162d04711addb3047d1f4367505fe6e42089d5775037267bcbe5fab8b5e73`。
21项合成测试普通/优化模式通过，NPA8测试通过；旧v1-v4 receipt不覆盖。

下一查ARC节点/配置、其他EBS写入者和唯一XNU共享所有者。尚无资源生产后端
或新的GPU真机点；手机保持正常配对版，PAS13=false/CP halt不改，
GPU scratch/fence、Metal、PAS认证均未执行。

## 2026-11-10 13:49：ICB注册者已定位，完成屏障有不支持分支

本机只做原件静态分析与有界文件核验，无手机操作、协议调用、SMC或MMIO。
已验收boot内10个模块PE字节与原件一致，新增patched DALSys及Epsilon Limits；
129 plain DXE inventory新增`/icb/arbiter`字节匹配。不是运行态注册证明。

**首次连通实际配置与注册路径：**

- patched DALSys SHA97231a87...的device表0x1dd30中`/icb/arb`对应属性offset
  0x368。packed property0x1db00是`icb_info`、type0x12、结构索引20，不是
  SMMU的索引21。sized entry0x1d388→config0x2dfd0→node0x2df78→resource
  0x30e80，节点/资源同名`/icb/arbiter`。原件中driver/plugin均初始NULL；
  这不是可直接运行的后端，也不能把NULL当注册失败。
- 实际ClockDxe SHA babe80a2...入口0x1a74调用0xc520。该初始化先调用0xe474
  准备fabric/RPMh handle，再0xd9d4取得配置；为主node写driver0xc650，
  为resource写plugin0x432a0，0xc5d4→0xa764→NPA slot0x20定义节点。
  standalone node也经0xc628定义。外部DAL提供数据、Clock携带ICB实现；此前
  “Clock只是消费者”仅适用于已查ClockRailConfig/MDSS路径，不覆盖本链。
- plugin0x432a0字段`[0xc704,0x1400,0,0xc930,0xca0c,0,0xcd50]`：0xc704
  更新，0xca0c create。创建参数每master/slave pair8B；更新len>>3，每请求
  32B。0xcc24原指令解低16位type1/2/4，bit16附加字段位于+0x18；具体单位、
  master/slave身份及crypto请求ABI还需独立关联，不能仅凭数字推断带宽。
- 主node driver0xc650仅`LDR w0,[x0,#0x30]; RET`，直接返回requested_state。
  真正工作在plugin→0xdb80→0xe9dc。节点状态/EFI同步成功仍不是hardware ACK。

**外层barrier分支不保证完成（内部等待见14:08修正）：**

0xe9dc批量提交active/sleep/wake；条件分支0xeb78调用0x128fc。该包装读取
RPMh协议slot0xb8（固定0x10001参考布局名`rpmh_barrier_all`），0x12964 BLR后
直接恢复栈返回，不检查或传播EFI结果。本机RpmhDxe协议表0xe0f8+0xb8→0x18dc，
实际`MOV x0,0x8000000000000003; RET`即EFI_UNSUPPORTED；slot0xc0同样不支持。
这是**已验证的函数字节/分支关联**，不是手机执行记录，不证明该分支本轮触发；
也未穷举替代churn/IRQ路径。该barrier分支本身不能证明请求已经硬件完成。
此前已查Cambria CPR0xafa0调用slot0xc0，同样不能把包装调用当成功churn。

关键Evidence：
`ev_b25bf89e4bee18760b4eb6a724949939a00309c9f682299fecf9e977c04a733b`
（Clock注册），`ev_a3cc7f01211cd7134a7a55bf902298b2685ba143c0cfe5c3574cb794eedfeedb`
（plugin原指令），`ev_1c536a902e0a0972d0b7a6ca39d5c25cc4605c64447eb2f7dadec6d8e81fd2b4`
（DAL node），`ev_ca77c40358cd6202e996a0b66e57ea3bf78034a5ada526d02a6ae1a0dc508a8d`
（Clock包装），`ev_0953fd40f4c70bb9e76a8c1e2dd53f9392a2d035245d674d0b4e641a9e2fec84`
（RPMh不支持指令）。bundle/snapshot位于同私有logs目录：dal-icb-config、
clock-icb-barrier、rpmh-barrier-slot。Limits0x4ae4为PMIC/GFX消费者且包含GPU/GDSC
写入，未执行；其证据limits-resource-evidence保留，不当ARC registrar。

新`verify-icb-registrar-evidence.py`连同原boot解析器核验上述原字节、DAL配置链、
plugin表及17条关键指令，`icb-registrar-verification-v3.json` PASS，SHA256
`024527a2771aa3726ac75c2b8f70db513c61681f096511505322466e9b36bb84`。
13项合成evidence/packed-DAL/边界拒绝测试通过，`python -O`同样通过；原NPA
8测试通过，10模块receipt为npa-state-verification-v5.json，旧receipt不覆盖。

下一继续查实际ARC rail registrar、ICB硬件完成记录与最后EBS写入者，再设计
唯一XNU共享所有者。尚无生产资源acquire/lease/quiesce后端、无新内核/构建/
真机测试点；PAS13=false、CP halt维持，GPU scratch/fence/Metal/认证未执行。

```sh
python3 scripts/test-icb-registrar-evidence.py
python3 -O scripts/test-icb-registrar-evidence.py
python3 scripts/verify-icb-registrar-evidence.py --root .. \
  --output ../logs/pas13-instruction-replay-20261110/icb-registrar-new.json
```

## 2026-11-10 13:32：crypto资源链与模块身份续查

模块身份核验扩为8项，新增Env/Pmic及Epsilon Hash/Cipher；最终receipt
`../logs/pas13-instruction-replay-20261110/npa-state-verification-v4.json`，SHA256
`47d35ae4ee23e26735337cf4a6f6e722c59431a7bb78aec4301c8adfb92403be`。
8项合成测试仍通过。这只更新离线脚本，无生产内核/固件/phone操作。

已验收HashDxe的SHA是`0fd065458824be63d61a851d039b5d3198d556a6bee3ecfec423b9a24e3be266`，
来自Epsilon/BOOT.XF.3.0-00527-SM8150LZB-1，不是cepheus设备目录的58afbd5e...。
首次误选设备目录版只有strings/xrefs，已停止，另存
hash-unselected-reference-evidence.json；不把其结果当本轮已打包行为。
Cipher也为Epsilon版SHA94ed2f29...。两者PE与已验收内层FFS逐字节一致，
这仍不是运行时dispatch或PAS对其资源需求的证明。

- EnvDxe0x2358在NPA协议可用后为`/icb/arbiter`注册ResourceAvailableCb；
  所展开路径只有client创建/向量请求/可用回调，不定义ARC/ICB节点。
  ev_6b183416d2e52345ff8de84c98bdc1dbfd1048598beb081173bb72d31f35f81f，
  证据env-npa-evidence.json。
- 实际Hash0x5138创建名为`/clk/ce1`的**向量客户端**，其resource是
  `/icb/arbiter`，type0x400、创建参数8、配置输入0x17；不是直接时钟API。
  准备的请求字中有0x10004、1600000000、0x34b0c50，0x9d90验证请求数后
  通过0x9ee0调用NPA IssueVectorRequest(slot0x90)，长度8。这些数值的
  ICB结构/单位/master-slave含义尚未核验，不能直接移植为PAS/GPU带宽票。
  ev_0d6febbc2af7c460bb2e428973c36c0af053ec06a26d5105edc74e0f8b969bd7、
  ev_8c0ccc8dcacd0d71313aadaa6afdad551f60cb1d1849c981704d85d57cd59809；
  hash-selected-vector-evidence.json（保留原字节读和Ghidra局限）。
- Clock目标helper0xaa64在展开本体内构造4组CPU频率表；0xad68只设置
  context+0x60字节1。它们未提供新的ARC DefineNode证据；不是穷举整个
  ClockDxe/间接callee。clock-target-followup-evidence.json。

结论：找到了本候选crypto带宽的具体消费者调用链，但还没有仲裁器driver/
完成屏障/最终EBS状态或XNU所有者。不能从NPA客户端调用返回推导hardware
ACK，不能把DXE函数指针当运行时接口，不能让PAS acquire返回成功。
下一定位实际`/arc/client/rail_cx`、rail_mx与`/icb/arbiter`注册者及driver，
连同最后EBS写入/停止与lease；supported=false和CP halt保持，GPU/Metal未执行。

## 2026-11-10：NPA查询/退出与已验收模块身份

新增`scripts/verify-npa-state-evidence.py`，直接从已验收配对boot
SHA256 `2c93dc3333665bdbc8f1c835c0e59226f5b2a0cfda2678d4bb121a3fc86903d2`
解码压缩FD/FFS/PE32。NpaDxe、RpmhDxe、ClockDxe及**Cambria CPRDxe**逐字节
等于本机原件。设备目录CPRDxe（c216b483...）未被此候选采用，不能把它的
行为当作本轮实际模块；选用Cambria SHA256
`63a777c11fc8006ca540886e2ea3ebdafa56bea752d716ea09f7514756821fbb`。
包含不证明DXE已dispatch、节点注册或live状态。

REA/Ghidra12.1.2同一NpaDxe SHA。四段REA读字节与原PE file-backed映射一致：

- Query0x1670和QueryByName0x169c的BL分别到0x8b14/0x8bb4；wrapper把查询
  状态写到输出，EFI return固定0。不能仅用EFI_SUCCESS验收查询结果。
  read ev_a550a7cd6cf77bb8bcf897c8e29162edeed57e366462a1b6df7094565199aedd。
- 0x8b14先尝试definition+0x38的link query；未处理则到0x89c4，后者先尝试
  definition+0x30的resource query，再处理通用ID。默认ID0在0x8a58读取
  resource+0x34并写result+8/type0/status0。参考ABI称CURRENT_STATE，布局
  对应active_state；**默认路径是软件字段读取，没有硬件READ/ACK**。
  专属query可能不同，未证明实际CX/MX采用哪个路径。
  pseudo ev_009c365953d8a0f632e68bf502e6fe80d11ea579f3bac8bf7eba81a91b57f6e1、
  ev_a6d245a3ca875afe96a6b44b1c10c65389cfc7285d8422db837efc4a57a55a04；
  read ev_717428169ce79d4235d9d992c94a65c990d1ceea2d918d5c1affe8843a0164c5。
- NpaDeinit0x1a74..1a9c构造handle/GUID/table/null，读取BootServices+0x150，
  尾跳UninstallMultipleProtocolInterfaces。此完整短路径无join/churn/释放
  client/投票恢复；**不能当全局完成屏障或EBS资源交接**。
  read ev_20c43373c4b3663c55b64488d44891e1d649a5237f77db66d24e0e4c2823e552。

在已验收内层FV129个plain PE32 DXE文件中检索NPA GUID、/arc/client/及
cx.lvl/mx.lvl字节，14个匹配模块及file offsets已保存。不涉及其他类型FFS、
间接构造或压缩子载荷。默认协议标识不能证明运行资源；后续目标不靠猜目录。

同轮进一步追模块注册路径，仅静态观察：

| 模块/地址 | 观察与仍缺的事实 |
|---|---|
| ClockDxe0x6bec→0xa6e0/0x6a4c | 查ClockRailConfig引导默认值、注册可用回调；回调创建clock required/suppressible客户端，属于消费者，不证明CX/MX节点定义 |
| ClockDxe0x3150→0xa764 | 可定义配置中的clock节点，未证明其就是/arc/client/节点 |
| Cambria CPR0x4c4c | 本体只处理0x106/mmcx和0x103/gfx；创建CPR_AGING，发RPMh并churn single；不是通用CX/MX聚合器，其他caller未穷举 |
| PmicDxe0x1455c→0x162f0 | 动态构造节点/资源，driver候选0x1522c，DefineNodeCb；0x14b08命名on_off/mV/mode PMIC资源，尚不能绑定到/arc/client/rail_cx |

Pmic driver候选未被Ghidra定义成procedure；该拒绝不是成功反编译，不以缺
函数推断不可达。完整bundle保留no-return/间接调用等限制。上述证据不建立
current_cx/mx、crypto/GPU租约或停止协议，没有生产资源后端/新GPU执行版。
下一沿**已打包模块**定位实际CX/MX节点/driver/completion与最后EBS写入者。

全部材料位于`../logs/pas13-instruction-replay-20261110/`：npa-query、
cambria-cpr、clock-init、clock-registration、pmic-npa的evidence/snapshot；
npa-state-verification-v3.json核验PASS，8项合成PE/检索覆盖测试PASS。
本轮无手机/协议调用/SMC/MMIO写，无内核编辑或重新构建。旧receipt保留。

```sh
python3 scripts/test-npa-state-evidence.py
python3 scripts/verify-npa-state-evidence.py --root .. \
  --output ../logs/pas13-instruction-replay-20261110/npa-state-new.json
```

目标来自本机xiaomi-cepheus UEFI设备目录，PE/arm64，65536字节：
`RpmhDxe.efi` SHA256
`03ccd266ce0c9e268ac199c0f27903c9dbdac675c9374c797e4178673564a7fa`。
REA/Ghidra12.1.2分析，不执行/修改固件。完整证据在cepheus-port的
`logs/rpmhdxe-03ccd266-evidence-final.json`，快照
`logs/rpmhdxe-03ccd266.snapshot.json`。地址是此静态PE image-relative VA，
不是手机物理地址，不能直接在XNU读这些地址。

## 观察

- `0x14dc`通过UEFI BootServices安装表`0xe0f8`；表头为`0x10001`，包含
  `0x15b4..0x18e4`包装函数。ev_7c07ef033afd8ffd0bfb23bf1e0e72580d31c20f9cec5e00526ee3bfacddaefb，
  表字节ev_5bcfd2c50c3b7d9113298417de8b7fb3e9ff480e0375ba805e45fc3ebd5e7dc4。
- `0x2f80`按资源地址查找/插入24字节记录，计数位于image+0xe644，数组指针
  image+0xe648。ev_3d4d654200b59c2b29b2d6ce9c8e6988a9be1d763c2620e9eece939969ff6708。
- `0x48d4`在每个资源中查找DRV id对应的72字节链表记录，不存在则zero-init
  分配。记录+0为DRV，+0x40为next，资源+8为首指针。
  ev_0e359d2db24982f890ef9b7438bc23f978a076430ed712a613a04399e7ef7335。
- `0x4b38`写入set相关triplet：value在+0x18+12*set、valid字节在+0x1c+12*set、
  状态在+0x20+12*set，再经`0x4a3c`比较缓存数据。
  ev_10104ed4d0eb41791878e28b19561e627f66f40f37e8b02561c068174c1bbf87，
  ev_934c566b2948279469110b4ca8dd390a204fc146386b26d81769c959246e1fee。
- `0x4c34`选择缓存set值并写入+0xc和+0x10(valid)，可能将+4设为1。
  **它由`0x5ae4`的TCS配置路径在触发命令前调用**，因此这个影子值不是
  “硬件已经确认成功”的证明。
  ev_2b9de1a52a93df3363e8a5b71ef8cf0b83ad41672ebf84b69ded72a75fab5e5d，
  ev_6e8b164f46fb5682ca6f8c8ad31646e16c1e8907f5ee61dace20d15eb903f514。

## 推论与限制

观察支持存在“资源/DRV/set”软件缓存，可作为未来UEFI受控handoff的研究方向；
**不支持**直接把+0xc影子值当作当前ARC投票，更不证明READ响应语义。
需先处理未完成请求、锁与ExitBootServices期间的最后写入、loaded-image身份、
内存映射可读边界、空链表/外部投票、跨内核时序。不能按固定位移扫描手机RAM。

本次没有找到已验证的公开只读“返回本DRV已完成投票”协议函数；没有穷举所有
函数，不能据此说不存在。字符串`holding_read_request`存在，但没有交叉引用
不代表不可达。Ghidra对部分包装函数存在函数边界限制，必要处以反汇编补证。
REA bundle中unknowns=0仅表示工具记录格式，不表示硬件语义没有未知项。

因此GPU电源写入仍需可靠当前投票/所有权证据，不能因发现缓存就取消门槛。

## 2026-11-06：READ response 路径补证（仍不是 current-vote 证据）

复用同SHA的REA snapshot；本轮导出
`../logs/rpmh-read-static-20261106/evidence.json`。没有读取活RAM或执行固件。

- 字符串RscHalReadResponseData交叉引用指向`0x7578`：按DRV/TCS/CMD做
  geometry验证，然后读MMIO `base+0xd40+tcs*0x2a0+cmd*0x14`，另一DRV
  模式为`base+0x20d40+...`。这是HAL response寄存器，不是软件投票缓存。
  `ev_5698e6b71c0bf22fdf73b7d7ce700f69da6a6154839aa083152784c857a7e8bf`。
- `0x590c`按DRV/TCS映射调用HAL，返回response值；`0x4784`在完成请求队列中
  针对类型2的命令读response，再由`0x3e48`写入每client请求记录（valid=1、
  value在+4）。其他类型才走`0x4d60`完成缓存。
  `ev_bdac4a1f967dc9cfa60f49a36438c8550654149a0b028943b1ab82fb9ddbfc85`、
  `ev_668dbfadee54cb109d6991d680d97d839bfe61e9d7420750846afebda7d645fd`、
  `ev_1f62b0890bc0600d886cdc7b9e7a077edb85d59c2691b0f14710caf5086e8728`。
- `0x3174`只是初始化全局单命令请求，`0x30fc`再走`0x2cdc`命令set机制。
  当前观察的协议wrapper `0x1850`设置回调，`0x18ac`进入sleep，
  `0x18c4`退出sleep；不是三种“返回当前投票”的接口。
  `ev_db8f2a65ed10c303853aa93dcbe2934ae8f7c451073888f96479df0f2a40b6b3`、
  `ev_7d2c529f56e8c7cab38319474515bb42298635625e321069cad3284a78bf8920`。

推论：READ的返回值来自硬件response，不可与resource/DRV/set影子投票混为一谈。
尚无RPMh硬件规范或本机证据证明ARC READ response是本DRV旧request，而非
聚合/当前输出。因此不使用它填写power.prepare的current_cx/current_mx。
这轮调查缩小了未知范围，**没有解除投票继承或共享所有权阻塞**。
协议多个wrapper仍未被Ghidra定义为函数，其原始字节已读取留证，不能据此宣称
穷举所有公开接口；没有按image-relative地址扫描活RAM。

## 2026-11-08：协议资源记录getter和完成标记补证

同一SHA原件、Ghidra12.1.2，只做静态分析；导出
../logs/gpu-power-backend-20261108/rpmh-protocol-evidence.json。没有执行固件、
调用协议或访问手机RAM。以下地址仍为PE分析VA，不是手机物理地址。

- 协议表0xe0f8中slot偏移0x50指向0x1758；wrapper在0x176c调用0x293c，
  将返回指针写入x1所指输出。0x293c按资源地址二分搜索已有24字节记录，
  未命中返回NULL，无分配/触发请求。这给出未来受控交接的入口候选，
  不是已核验UEFI ABI或可在XNU间接调用的接口。
  表ev_346506b31ed0a4be1a2ff380b3cc2340346d48298bf5cadf5498124513b4509c；
  call指令ev_e3c9473ec22acb72ee32e9e28b86277c2eb61d36a1fa34f251e69601c31d5100；
  getter ev_56424597c7151bcc5fa0246b3da4c0d4986763195bc15661d922f6138438fae6。
- 0x4d60在完成路径把DRV记录+4清零，再调用0x4a3c重新计算各set状态。
  0x4784区分类型2的READ response和其他完成命令，后者调用0x4d60。
  ev_39739129e0a83fb9d33a9078545068d6ff53ee97517a18f58abac661dbe7428c、
  ev_d709b3ad2109c5279c436d317409d90e9cb364a77bb788b4877b4e9deac21cec。
- 0x5ae4配置TCS时，无论是否AMC，非READ命令都先调用0x4c34写影子值；
  只有AMC分支随后实际触发。因此+4==0和+0x10有效仍不足以证明+0xc是
  已完成active请求。休眠/唤醒配置及最后一次ExitBootServices写入还需处理。
  ev_3c87926d1abcbec25c2afc33476f26648a90d3949b47ba4391a696b0d0fc8718。
- 协议尾部0x1868、0x18dc、0x18e4的实际指令直接返回
  0x8000000000000003；不能把未定义成Ghidra函数当成隐藏getter。
  0x18dc指令ev_37deb161915230939100468a83ba3a9720e920a727beb4ebe480846992905297。

结论：找到了无需扫描RAM的记录查找入口候选，但未建立完成请求快照或最终
交接。不能据此实现一个始终成功的power.prepare，也不将缓存状态冒充硬件
ACK。下一必须在有身份/边界验证的UEFI上下文处理请求完成及最终交接，再
由XNU唯一共享资源管理者接管；当前没有这段实现。没有取消GPU启动门禁。

同轮通过gh取得SDM670参考源码，固定仓库Rivko/android-firmware-qti-sdm670
commit 20bb8ae36c93fc16bbadda0e0a83f930c0c8a271；4个文件的git blob/SHA256
保存在同证据目录reference-sdm670/identity.json。这是异型号参考源码，
不声称它就是本机RpmhDxe的构建来源。EFIRpmh.h的0x10001协议布局在偏移
0x50命名为rpmh_find_resource_command(address, out)，与已查本机表和call相符；
rpmh_resource_commands.h中DRV记录的state/priority/vote_at_rpmh/local_votes/
next布局也与上述静态访问吻合，尚不构成运行态交接保证。

参考rpmh_resource_commands.c:457的update直接覆盖该DRV/set的data，不按
client handle求max。不能通过“新建RPMh handle并投最小GPU档”自动保留原有
消费者需求；handle用于请求跟踪，不是新的硬件投票槽。churn_all也只等待
对应handle的请求，不能冒充全局所有客户端完成屏障。需进一步接入已有的
上层资源聚合器，或建立已核验的最终请求交接，不能用裸RPMh新client绕过。
