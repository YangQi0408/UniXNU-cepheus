# PAS13 查询差异的后续定位（2026-11-08）

## 2026-11-11：普通入口、dispatcher与返回字段

同一固定TZ原件的入口/返回续查已保存，未执行手机或SMC。旧原生证据已含
`0x1c102f80..1c103003`；本次复核其缺少Ghidra function归属的直接调用边，
不把它误报为新发现的函数。普通路径保存x0..x13为0x70B输入，经
`0x1c03b8b4`未命中resume后，由`0x1c03b68c`取得空闲调用槽；
`0x1c102fb4..bc`读取slot首项工作栈顶并切SP，**不等于切调度线程**，
随后`0x1c102fc0`直接调用`0x1c03ae6c`→`0x1c0574d0`→dispatcher。
调用槽表`0x1c161aa8`有9项、步长0x160，状态/CPU字段和工作栈由
`0x1c03b5b0`初始化；这是另一结构，不能混同scheduler current thread
`[0x1c161828]`或把slot状态当优先级。

入口`1c0532d4(9/1)`及返回`1c0532c0(4)`经`1c050770`转换为DAIF位，
分别清除/设置中断屏蔽，**不是线程优先级设置**。初始化
`1c04c9dc`→`1c0562e8`先bootstrap、注册3个service worker、后降idle/yield；
worker29/bootstrap31→idle0仍仅静态成立。普通入口实际任务/优先级、其他
修改者、resume与interrupt影响尚未建立，不能据此认定live caller一定为0。

新增私有`check-tz-smc-entry-return-20261111.py`在有界主机RAM执行原始入口、
slot取得/状态/释放、wrapper、完整dispatcher原字节的PAS7正常分支、参数
校验、trampoline和lookup，再执行响应帧组织/复制/恢复；在`smc #0`前停止。
dispatcher从固定注册表选中原PAS7项（ID`0x02000207`、arginfo1、flags1、
handler`0x1c04f444`），将ID13送到调度边界。调度调用是**明确替身**，不执行
allocation/enqueue/wait/context-switch，直接转交原trampoline；结果指针
`frame+0xf8`由**原dispatcher**写入，替身只验证它，不补写。结果scratch/
初始CPU/栈/调用槽/锁/current pointer均合成，无bootstrap或运行身份观察。

wrapper status0时保留结果字段；service构建
`[1,0x83,status,result0,result1,result2,0..0]`，复制到
`1c103004()`返回的CPU帧顶减0x70。原restore恢复x0..x13，仍是交给monitor
的内部响应，不直接等同正常世界寄存器。随后另一次有界调用原monitor
helper`0x1c01c5e0`，把envelope+0x10/+0x18/+0x20/+0x28复制到合成context
的x0..x3存储字段；未执行完整monitor路由、context restore或EL3返回。

| 合成初始状态/结果 | 内部响应开头 | monitor context x0..x3 | 原指令执行条数 |
|---|---|---|---|
| 固定count15、CPU0 | `[1,0x83,0,1,0,0]` | `[0,1,0,0]` | 2224 |
| 固定count15、MT置位CPU7 | 同上 | 同上 | 2224 |
| 假设count0 | `[1,0x83,0,0,0,0]` | `[0,0,0,0]` | 1969 |
| 三个不同64位哨兵结果（handler未执行） | status0、三哨兵原样 | status0、三哨兵原样 | 1845 |

四场景PASS，1870条静态原指令由Capstone及LLVM解码核对，原byte records
绑定固定原件；预算20000条/3秒，禁exception/范围外执行访存/代码写。
CPU帧、当前FNID及调用槽清理匹配，非结果monitor字段未变，current pointer
未被这些执行路径改写；**调度器是替身，因此不证明调度前后live线程不变**。
这降低所覆盖正常路径的结果错位/清零解释，不定位真机false；count0和哨兵
不是live证据。首跑fixture把`1c180bc8`清理槽误声明4B，原`str xzr`为8B而
被访存门禁拒绝；修正fixture后通过，不追认首跑。v1替身曾冗余补写结果
指针，仅作中间材料；v2移除该写入、要求原dispatcher绑定，不追认v1范围。

最终REA bundle（312 records）
`../logs/pas13-instruction-replay-20261110/tz-smc-entry-return-20261111-v3-evidence.json`
SHA256`4e1ec9a3ba36804b9fa983284f72ffa5bf097b328a9c0ddd184e3b0af110c863`；
匹配snapshot SHA256`bcc3c356daf52bf7decfaeaa6361d050ea9a524c4a15a6018b4746a00e7387fe`。
关键Evidence：工作栈切换
`ev_a980b5a807bf9d12d40306879545bd31df13bc80d53455c70a104539af887078`，
直接call `ev_8a363abb4524c17b488071f458afe406b274c0202d9b594c47a8918f78a751a7`，
普通slot获取 `ev_1be9f20db3122bdedce68811f62c06ae9af6652a8fdb7c994a4bdae3203c83b7`，
monitor字段 `ev_1062c9f34ca7d02ebdb1136260134658a9adb2aad385f7854352fe7ddb88f6c5`。
最终receipt `tz-smc-entry-return-validation-20261111-v2.json` SHA256
`a552f17cd4afb9a7c0457ade318d77c936689a85c945a4f4788abc67a650c3f8`，
绑定script SHA256`3e1d195036c0df6bf7d8cc17e513f3d911865f5935e81ea5ebb04faf322fc1ad`。
普通/python -O的cases及bindings一致；完整receipt的LLVM临时listing SHA不同，
不声称逐字节一致。v1/v2 bundle及中间receipts保留；session已保存并关闭。

下一只追scheduler current pointer与priority修改者/secure返回再进入约束，
确认普通caller是否可能压过worker；完整monitor路由和实际运行TZ身份仍未知。
没有独立live区分手段时不追加等价合成场景，也不重复已完成的UEFI/XNU配对。
真实owner/secure PA/crypto仍独立缺口，supported=false、CP halt不变；
无生产源码改动/新CI/phone/SMC/PAS/GPU，GPU scratch/fence/Metal未执行。

## 2026-11-11：有界调度整段回放

继续同一固定TZ原件，新增wait/wake/context-switch与初始化证据。三个service
worker的native初始化优先级为29（`0x1c0d9cfc`在context为NULL时传0x1d），
bootstrap初始为31（`0x1c0d9330`），随后`0x1c0d93fc`将其降到idle0并yield。
普通SMC入口实际使用哪一任务、进入前/运行中优先级及全部修改者尚未建立，
不能把bootstrap31当真机查询caller优先级。

新增私有`check-tz-scheduler-20261111.py`在主机RAM回放enqueue→signal→yield→
worker→原PAS7 lookup→wait→caller恢复→取完成status，包含真实原指令的
`0x1c104480`寄存器/栈保存恢复与单参数trampoline。固定lookup12段原指令
仍与两份原生Ghidra及LLVM临时解码一致；调度/转发另33段和两个只读范围
逐字节匹配固定原件与完整REA byte records。共1132条静态指令，禁SMC/
HVC/SVC/ERET/范围外执行/访存/代码写，预算20000条及3秒。

| 合成初始状态 | 主机RAM结果 | 执行条数 |
|---|---|---|
| caller28、worker29、固定count15/空cache | lookup执行，true，2次上下文切换，完成记录取回 | 1149 |
| caller0、worker29、固定count15/空cache | 同上 | 1089 |
| caller29、worker29、固定count15/空cache | 同上 | 1144 |
| caller28、worker29、假设count0 | lookup执行，false，2次切换，完成记录取回 | 894 |
| 假设caller30、worker29、固定count15 | lookup未执行，调度返回0，结果仍0，pending保留 | 307 |

后两项是人为反例，**不是手机count或优先级的观察**。这里只证明所声明的
两线程/一任务初始状态下的行为；不证明live调度不变量、运行TZ身份、实际
false原因或硬件认证。registry、线程、队列、event、stack和canary view均
合成；worker从合成的prologue后saved context恢复，不执行bootstrap；allocation
省略、free为替身，仅native-thread路径，不执行secure-user切换、外部interrupt
或完整dispatcher。它比上一节模拟callback的单迭代回放范围更大，但仍非真机。

最终bundle（202 records）为
`../logs/pas13-instruction-replay-20261110/tz-dispatch-waitwake-20261111-v3-evidence.json`，
SHA256`16db157772078a2de4e7a0d4ef838d8b03e6f1fd3a2e5083e8ca4fe72b571fc9`；
匹配snapshot SHA256`307bb217ffb26c209e763f9e884f7c004b31bdb4062db7c143bffce5df31aec8`。
关键Evidence：native context switch
`ev_89ae41d38e1be1e98c128be2592eaed9bce89c632d8452c2809dd269e72de489`，
wait queue `ev_23071b409a11f26284230eed93fd30beae148bad85c568bafa10b98bb5c7e437`，
worker优先级 `ev_8985e03f6949715afcbaff25dd651c7a2e92e615679dffbc5e21a0480da34211`，
bootstrap/idle `ev_d645420f1e12b07b0d8a413c1fbf7f87c571708460b743255762884253786def` /
`ev_f34fd67d3c55a3181593eab7c0f67182ed789471939addc52b10015349376acf`。

最终receipt `tz-scheduler-validation-20261111-v2.json` SHA256
`9f4547722caa5c08341f22ef9cd4c2e5096b777a81b40d02b7408c4543d0910b`，五场景PASS；
python -O同样PASS且cases逐项相同。完整receipt只在LLVM临时路径引起的
listing SHA不同，不声称逐字节相同。v1为先前四场景，保留不追认v2范围。

下一聚焦普通SMC wrapper/入口接到dispatcher时的任务上下文及优先级；
如没有独立live辨别手段，不把更多合成反例当根因。真实资源owner/secure
PA/crypto仍独立缺口，PAS13=false及CP halt不变；无手机操作/新CI/GPU执行。

## 2026-11-11：任务调度与完成返回

同一固定TZ原件的后续静态证据已保存，原件未修改，没有手机/SMC/PAS/GPU
操作。`0x1c0ce3cc..1c0ce593`将frame、handler、argument count和0交给
`0x1c0d58b0`，后者分配/清零0x68B任务记录并复制到+0x20/+0x28/+0x30/+0x38，
callback在+0x18、result指针在+0x60。调度wrapper有-12忙和-9分配失败路径；
汇编保留w0返回值，decompiler的void签名不能作为返回语义。

`0x1c0d5990..1c0d5a73`入队并检查thread handle在注册列表中，失败返回
0x80000001；随后signal、yield并读取context+0x20的完成队列。非空时读取
entry+0x38的完成status并释放记录；**空/NULL完成队列的代码分支返回0**。
这只是条件分支事实，不证明合法同步PAS7调度能在handler未运行时回到这里，
也不是实际false原因。signal经过`0x1c0d99bc`，yield经`0x1c0d9030`和
`0x1c0d919c`选择优先级就绪队列并进入`0x1c0d9bf4`上下文切换；这不是简单
同步直接调用。完整signal/wait/context-switch和live调度不变量尚未建立。

`0x1c0d56f4`注册三个service worker，入口常量为`0x1c0d5b28`。Ghidra没有
为该入口建立function，因此不声称有该函数的dossier/decompilation。REA
完整原字节及LLVM/Capstone解码的限定范围`0x1c0d5b28..1c0d5bf7`为208B：
从pending移到active，`0x1c0d5ba4`通过record callback间接调用并转交四项
参数，`0x1c0d5bac`写w0到record+0x40，之后移入completed并归还idle worker。
队列为空时进入`0x1c0d06a0`等待。dispatcher的56B返回复制helper
`0x1c040098`已解码为普通memcpy；它自身不能提供handler执行证明。

私有bundle为
`../logs/pas13-instruction-replay-20261110/tz-dispatch-scheduler-20261111-v1-evidence.json`，
137 records，SHA256`937b5ea2d53fef39bd81b9042c0463f6e0efbf2a177c605726a671290f854bde`；
匹配snapshot SHA256`d277addcddfb0f4d528449e83313ca40cf3dc8e1ab7b1f66a526994492197e8a`。
关键Evidence：

- enqueue/completion：`ev_701d42d375ba24f9b0bd94669c03a944348651e8a600712ec9845faf2d0a7787`
- worker原字节：`ev_6122c5ba57c5ab28e6ed397bac27a79d6f608e2a417a57c9b2d5ffefc9a975c8`
- workers初始化：`ev_e8567fb2be5ca792529de70643de804d7431f8565bd6c449a92805735062975e`
- yield：`ev_302747f7118600d818b03e311e152dde67d6d5d971728e094c06fd07a2b87e96`
- scheduler：`ev_9c4be1c9a27048197f9ee6283f815e252811ea5c51f5e3be68c2038e5a638f85`

同目录`check-tz-worker-20261111.py`仅在主机RAM执行这52条原指令的一次任务
迭代：callback替换为显式模拟true/false/error，在空队列wait入口前停止。
三个场景均转交frame/handler/argc1/0、写匹配status、发布完成队列、保持
frame及模拟result不变；普通及python -O结果逐字节相同。receipt
`tz-worker-validation-20261111-v1.json` SHA256
`f59f4e6cc411bd2f5e86eb8d3934053d0d1b326c00acf83cb52976496f785b6a`。
**未执行PAS handler、signal/yield/wait、上下文切换或完整dispatcher**；该回放
不能解释真机PAS13=false，也不能证明真实owner/认证或解除CP halt。

下一具体问题是signal唤醒worker及yield恢复caller之间是否必然发布该任务
完成记录；只在这个问题要求的范围补齐wait/wake/context-switch证据。运行TZ
身份/上游环境与真实资源owner仍独立未知，不重复已完成的UEFI/XNU配对查询。

2026-11-11参数校验续查：同一固定TZ原件与已有snapshot通过REA复用，新增
`0x1c056c60..0x1c056d73`完整69条指令及`0x1c05724c..0x1c0573ab`
单参数转发证据。前者的arginfo1值参数路径不访问输入帧，也不调用地址权限
helper；6个有界主机RAM场景验证返回和不改写帧。后者在PAS7 handler入口前
停止，确认ID13/result指针及SP/LR原样转发。普通及python -O均PASS。
这降低参数校验/单参数转发自身解释false的可能性，但未回放完整dispatcher、
上游frame、返回路径、EL2/EL3或实际运行TZ身份，仍不能解除supported=false。

私有bundle：`../logs/pas13-instruction-replay-20261110/tz-dispatch-args-20261111-v2-evidence.json`，
115 records，SHA256`2fac537cd1bf982aee6376dc3518e42eb6dd65689d2b1881f0181b90108e73c8`。
参数校验REA Evidence
`ev_50add8933d41859e15ebe2051e60aaba42b51d325af453ca5b910948a98edbb8`；
原字节`ev_4fd4b7bb428bae960d21de0b640ae2f3d8af2fb81c37767a6fe5b7d1906bd6b6`；
转发`ev_cedfd2fb6398bcb0de517930faae8aab1e327e815b263dd86e3e926841b057f4`。
本地脚本`check-tz-dispatch-args-20261111.py`及匹配snapshot在同目录；
`tz-dispatch-args-20261111-validation-v2.json` SHA256
`c60e970dea9b9a0636540c51b1d4f245ffa835662af9e0e46cc0914b11dd7c26`。
没有手机/SMC/PAS认证/GPU操作，原件未修改。

## 2026-11-10：受限原指令回放

### 2026-11-11：权限转换续查

固定TZ原件的新增有界静态证据已保存，未执行这些代码或任何PAS操作。
`0x1c04e2a8` 经descriptor+`0x228`回调或`0x1c04ef04`后更新state+`0x234`。
后者按+`0x20c`条件，使用+`0x200/+0x208`旧/新VM列表和permissions6的
24B目标记录，把region start/end交给`0x1c0a74d0`；该函数校验范围/权限，
再调用`0x1c0a930c`。新解码的`0x1c0a930c..0x1c0aac1b`（6416B）涉及
region ownership/VM permission masks、`DAT_1c17fbd8`逐页metadata和
`PTR_FUN_1c159348`的XPU/权限回调表。在这个已解码本体内未见直接写
PAS count/cache/ID；间接回调与callee的全部副作用仍未建立。
这些事实**没有解释真机PAS13=false**，不得据此绕过supported=false/CP halt。

私有原证据（27 records）为
`../logs/pas13-instruction-replay-20261110/tz-permission-transition-v1-evidence.json`，
SHA256 `9828717f73ff78276d8cb8c1175e0fa7bbeb9c1f76a824a56434cbd14ca04374`；
匹配snapshot SHA256
`fb9057e11b72869a7732d28f0fccddfd474463199ba1ed5ecf06f04641e9a85a`。
range-helper Evidence为
`ev_68af1916c2532ca34d3992c74c1c6445035f3899e5761d66f07834c108a06b41`。
没有新SMC、phone操作、运行secure RAM读取或真实认证结论。

配对真机已确认 UEFI/XNU 均 false 后，新增
`scripts/replay-tz-pas13.py`。只把固定原件的12段lookup代码（219条静态指令）、
count/15项descriptor/30项cache/锁复制到Unicorn 2.1.4的主机RAM；合成栈与
结果区独立，禁止范围外执行/读写、SMC/HVC/SVC/异常返回及代码写入。
代码全部与两份原生Ghidra导出及LLVM临时`.inst`解码一致。未运行startup、
dispatcher、EL2/EL3、认证、MMIO或手机命令，原件未修改。

| 合成RAM状态 | lookup结果 |
|---|---|
| 原始表、空cache、MPIDR0x81000000 | wrapper0/result[1,0,0]；363条，缓存PAS13；第二次89条仍true |
| 原始表、空cache、MPIDR0x81000200 | 同上 |
| count0或count5（止于PAS13项前） | false，分别108/161条，cache仍0 |
| PAS13项ID改0 | false，261条，cache仍0 |
| cache13已非零，再将count0或ID改0 | true，89条；未重新检查descriptor |
| lock13持有1 | 达100000条预算未返回；无verdict，不归为false |

结论只在这些受限输入成立：空cache本身不能解释固定代码/表的false；
两次已记录MPIDR在此局部模型中也不改变结果；非零cache绕过重新查表；
锁持有导致等待而非快速false。count/ID修改是**假设反例**，不是手机发生
修改的证据。运行TZ身份、其他初始化/写入者、EL2/dispatch与启动历史仍未知。
保持supported=false及CP halt门禁，不尝试PAS认证。

17项不含私有材料的合成回放测试通过，包含越界/代码写/异常指令/无返回/
错误wrapper/空调用拒绝及访问日志上限。最初原件回放8场景通过。v1/v2保留，
最初8场景receipt为`../logs/pas13-instruction-replay-20261110/replay-v3.json`，
SHA256 `7982a916f81387f83ae3cc48ccdaeb0795779304e6e9bd5e93751a6dccf326b1`。
逐访存日志最多2048项，完整read/write/omitted计数另存，边界检查不因截断关闭。

### 2026-11-10：缓存生命周期续查

固定原件的有界已分析引用中，count仅见`0x1c04d36c`；descriptor表起点见
`0x1c04d380/0x1c04d3a4`；PAS13 ID槽`0x1c13fb08`没有已分析直接引用。
cache引用落在lookup、init、mem_setup、auth及shutdown中，lazy指针写入在
`0x1c04db34`。这些不是完整间接写入者搜索，也不是运行RAM观察。

shutdown helper `0x1c04de54`按state走条件清理，直接清descriptor+`0x230`
的8B，再尾调`0x1c04d70c`。后者释放部分descriptor内容并清prefix `0x1e0`；
两个已解码本体没有直接清cache或+`0x1f8` ID。`0x1c0d6dc4`间接回调及
其他cleanup callee全部副作用仍未建立，不能宣布停机路径无其他写入。
原始dossier/raw证据保存在私有
`../logs/pas13-instruction-replay-20261110/tz-cache-lifecycle-v2-evidence.json`
（23 records）及匹配snapshot，v1保留。关键Evidence：
shutdown `ev_976acfae73a1defba7654d8b95a82c92e85333eb9cccc249f7312f698090141a`，
cleanup `ev_c893114d153d0a4ccd2fed73a06d230c914b6310c7ee3e295c22285fbf039e57`。

回放仍仅执行前述12段lookup代码，**未执行实际shutdown或任何PAS操作**。
新增三个假设RAM输入：

| 合成RAM状态 | lookup结果及state |
|---|---|
| cache13有效、state0 | true，89条；state保持0，descriptor无写入 |
| cache13有效、state5 | true，89条；state保持5，descriptor无写入 |
| 空cache、state5 | true，363条；lazy初始化将state清0，ID保持13 |

在固定代码/表条件下，state字段本身也不能解释false。实际11场景普通及`-O`
通过；17守卫测试两模式通过。新增receipt为`replay-v4.json`及
`replay-v4-opt.json`，普通版SHA256
`bfce0da0e0b542b5c9984978328bb4ac0e7e3cffceedfa7418da4a39c5b633b3`。
两份case数据一致，但LLVM listing包含临时路径，listing哈希及完整receipt
字节不一致；不声称两份receipt完全相同。UEFI/XNU实测false仍未解，
supported=false及CP halt保持；手机无新操作或测试要求。

```sh
python3 scripts/test-tz-pas13-replay.py
python3 scripts/replay-tz-pas13.py \
  --tz ../logs/recovery-tz-acquisition-20261107-clean/tz-partition.img \
  --evidence ../logs/tz-native-ghidra-20261107/evidence-dispatch.json \
  --evidence ../logs/tz-native-ghidra-20261107/evidence-startup-init.json \
  --output ../logs/pas13-instruction-replay-20261110/replay-new.json
```

本轮核对实际已启动的 query7-basic-b2b99e7 内核与本机 TZ 原件。结果排除了
本地 native callback 在打印前后丢失参数/返回值这一具体假设，并收窄了
启动初始化失败的解释。PAS13=false 的运行原因仍未知；没有手机操作、
新 SMC 或加载门禁变更。

## 已启动内核中的实际指令

候选 boot SHA256 为
`a060bd435ad3581520f29c0bb6886516963cd4f0955b8f40ce2a4cf23e0b5f19`。
用 7z 从 FAT payload 读取的 `EFI/UniXNU/Payloads/kernel.macho` 与
`../inputs/ci-42450429814/kernel.macho` 逐字节一致，内核 SHA256 为
`e9f8f3f735e2ab6d3f52fe2817ec51b898502b6f79130687f3b63c214ebd4978`。

`_ceph_scm_info_invoke` 的 `0xfffffe00074f57a0..7ac` 连续四条 `ldp` 从
同一个 regs 对象装入 x0..x7，下一条 `0xfffffe00074f57b0` 即 `smc #0`。
之后 `0xfffffe00074f57b4..7c0` 连续四条 `stp` 将 x0..x7 存回 regs；
printf 在这些保存之后调用。因此这份构建的代码没有把 printf 返回值
当作 SMC 结果，也没有在打印输入后忘记装入 ID13。

`_ceph_scm_query_run` 在 `0xfffffe0007501058` 读取返回 regs 的 x0/x1，
在 `0xfffffe0007501070` 将真实 x1 写到 verdict；没有读取初始 arginfo。
既有真机 transcript 的最后输入是 `0x42000207:1:13:0:0:0:0:0`，返回
为八个零；前一项 PAS7 availability 为 x0=0/x1=1。上述静态指令不能
认证 UDP 日志来源，但已排除这份内核编译结果存在相应寄存器接线错误。

## TZ 返回结果与初始化

固定原件 SHA256 仍为
`989389bcf4527b2b8e445998aba49454e556ac554c18d80e9eb999a8cbbdb114`。
PAS7 调度项 `0x1c130458` 与 INFO 调度项 `0x1c130ed8` 均为 arginfo=1、
flags=1。既有 dispatcher 原指令 `0x1c0571ac..1dc` 在 flags bit0=1，
handler 返回值非负时保留结果区，再将其复制至 frame+8。
PAS7 wrapper 返回0，且 `0x1c04f470` 将布尔 verdict 存在结果区首项。
所以这条局部链不会把 true 因 wrapper 返回0而覆盖成 false。

旧报告未深入的 backend initialization 两个 helper 已定范围解码：

| 地址 | 本轮观察 |
|---|---|
| `0x1c0bc8cc..c90b` | 调用 `0x1c04086c`，把配置的 subsystem load-region 起点/长度写入 handoff 范围与 checksum，返回前者状态 |
| `0x1c04086c..4099f` | 从 `/tz/oem` 查询 `OEM_pil_secure_app_load_region_start/size` 与 `OEM_pil_subsys_load_region_start/size`，写入固定 globals |
| `0x1c0bccf0..ce53` | 初始化 `0x862fe200` 指向的区域登记结构，根据模式/已有数据选择复制、清零及登记 load regions；末尾 `0x1c054c98` 为返回0的 stub |
| `0x1c04dab4..dae7` | backend 初始化非零返回后，在 `0x1c04dadc` 原指令自旋；正常路径返回 |

这两个 helper 没有在所解码本体内直接清零 descriptor count、PAS13 ID 或
cache。其间接/未展开 callee 的全部副作用未建立，不能据此宣布所有写入者
均已排除。第一 helper 返回失败会阻止这条启动路径完成，不能直接解释
“系统继续运行且 supported7 正常返回 false”。

## 下一行动

2026-11-10 更新：下述配对已完成一次。UEFI 和 XNU 各七次查询完整且经
生产策略重放与 handoff checksum 核验，PAS13 均为 false。EL 均0x4，
MPIDR 为0x81000000/0x81000200；用户简短触摸/唤醒确认正常。证据
`../logs/scmpair-6ce4c7c-live-20261110-1226/scm-pair.json`，详见
[配对结果](scm-pair-preflight.md)。本轮表明 false 在 XNU 接管前已存在，
没有排除启动历史、运行 TZ/EL2 或 descriptor/cache 解释。下段保留原计划，
不要把它当作重复查询请求；下一应离线调查接管前环境和独立资源后端。

同一 XNU 阶段重复相同查询不会区分当前剩余假设。若后续确实需要真机
定位 supported=false，有辨别力的最小对照是在同一启动链中，于 UEFI
ExitBootServices 前取得固定 PAS13 查询结果、把原始返回和 CurrentEL /
CPU 身份放进可信 handoff，再与 XNU 的查询阶段对应。这个对照能区分
“XNU 接管前已为 false”与“接管后改变”，但不会证明运行 TZ 的完整身份
或 PAS 认证能力，也不能直接复用 DXE 函数作为运行时后端。

还需建立的事实是实际运行 secure image 身份、EL2 是否介入 SMC、以及
运行态 descriptor count/cache 是否被改变。仅有 normal-world 分区 SHA
不能替代运行映像身份；不能把静态 VA 用来扫描 live secure RAM。
GPU 电源/crypto/secure PA 接管依然是独立实现工作。

## 可复核材料

`../logs/zap-pas-backend-20261108/audit-pas13-continuation.py` 只读取本机
原件、既有 kernel/boot/log，使用 Capstone 定范围解码、LLVM 反汇编和
7z 读取 FAT 成员。运行成功生成独立
`../logs/zap-pas-backend-20261108/pas13-continuation.json`，包含材料完整
身份、逐条指令及所解码字符串。它验证静态字节和 boot payload 一致性，
不是完整 CFG、真实 firmware 执行或 GPU/PAS 成功证明。
