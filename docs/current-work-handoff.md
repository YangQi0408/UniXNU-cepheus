# 小米9 iOS移植：当前全部工作交接

2026-11-11 23:20 +08:00：普通SMC入口/dispatcher/返回字段有界回放完成，
最终312-record REA bundle/snapshot保存、session关闭。原入口切工作SP及9项
CPU调用槽不等于切scheduler thread；9/1/4参数为DAIF mask，不是priority。
执行原dispatcher PAS7正常分支、参数校验/trampoline/lookup及返回envelope，
再独立调用monitor四字段复制helper；SMC前停，调度task handoff明确模拟。
frame+0xf8结果指针由原dispatcher写，替身不补写。1870静态原指令，四场景
PASS：固定count15 CPU0/MT CPU7均monitor-context[0,1,0,0]；假设count0为
[0,0,0,0]；三不同哨兵完整保留（未执行handler），后两者非live。正常/-O
cases/bindings一致、LLVM临时listing SHA不同；最终receipt SHAa552f17c...，
脚本/新bundle hashes匹配。降低正常返回错位/清零嫌疑，不定位live false。
首跑fixture4B槽遇原8B store被拒，修正8B后通过；中间v1不追认为v2。
详见pas13-continuation-audit.md顶部。下一current-thread/priority修改者及
secure返回再进入约束，不追加等价合成/配对/触摸轮。无生产改动/CI/phone，
实际线程/TZ身份及真owner/secure PA/crypto仍缺；supported=false、CP halt，
GPU/Metal未执行，目标active不停工。

2026-11-11 22:52 +08:00：PAS13有界调度整段回放完成。新202-record REA
bundle/snapshot已保存，check-tz-scheduler-20261111.py共1132静态原指令，
包含真实native寄存器/栈切换、转发和lookup；线程/队列/栈初态与free模拟，
无EL3/SMC/认证/手机/GPU。五场景PASS：caller28/0/29≤worker29且固定表时
均true并取回完成记录；假设count0返回false；假设caller30>worker29时
lookup未执行而返回0。后两者非live证据，不能归因。初始化worker29，
bootstrap31后降idle0已静态确认；实际SMC任务上下文/优先级未知。
最终receipt SHA9f454772...，普通/-O cases一致但LLVM临时listing SHA不同。
详见pas13-continuation-audit.md顶部。下一追普通SMC入口→dispatcher任务
上下文，不重复配对/触摸轮；无生产改动/CI，supported=false、CP halt保持，
真实owner/secure PA/crypto仍缺，GPU/Metal未执行，目标active不停工。

2026-11-11 22:35 +08:00：PAS13调度/返回路径继续离线收窄，GPU仍未执行。
新增137-record REA bundle及snapshot已保存，详见pas13-continuation-audit.md
顶部。找到三个service worker入口0x1c0d5b28（Ghidra未建function，仅原字节
和LLVM/Capstone有界解码），确认间接callback的四参数与record+0x40完成状态
写入/队列发布。三个原指令主机RAM单迭代场景PASS，callback结果明确模拟，
不执行PAS handler或scheduler/wait/context-switch；普通/-O receipt逐字节相同，
SHA f59f4e6c...。enqueue外层空完成队列返回0的分支已观察，但合法调度是否
可走该分支/live不变量未知，不归因为实际false。下一查signal→worker→wait→
caller恢复的完成约束，不重复2026-11-10配对查询。无生产源码改动/CI/手机/
SMC/PAS/GPU操作；supported=false、CP halt保持，目标active不停工。

2026-11-11 22:14 +08:00：用户已明确继续不停工，目标为active。SCM-pair诊断
runner和FDT恒等映射修复完成本地全量验收：
`/tmp/gpu-offline-scm-pair-runner-20261111-v2/receipt.json`为PASS，42 PASS、
0 FAIL、0 SKIP，SHA256
`4c5c8ad00af6dfd4ba019fe43059a59e811eedd98e9809f506a65f2d084fe63a`。
原v1失败保留：四项源于预置cepheus.dtb的非空恒等`ranges`被误拒，实固件
测试exit124的上限实际是240秒，不是900秒。独立ASan/UBSan实固件整链
exit0、耗时290秒，log SHA`fe853b91431612c12ab442dc6c56abf50c8440becf8c4d6169ce0b1c241a1552`；
仅将memory-real-fixtures有界上限调到600秒，sanitizers仍900秒，其余240秒。
FDT接受空或唯一完整覆盖的恒等映射，拒绝重复/部分重叠/翻译/短覆盖及畸形；
固定FDT SHA和双TBU基址7项测试PASS。记录文件不是本次live拓扑/owner证明。
runner只允许精确415d4727镜像/唯一cepheus/一次fastboot boot，校验UDP三件
材料和分析SHA；false/true/unknown查询结果均不授权GPU。完整流程mock
验证错误设备无启动、启动/采集/分析失败无重试；实XNU reference集成14项PASS。
全量验收后增加了runner流程测试并单独复验，未重跑不受影响的重型检查。
本批无内核生产源码改动，无新完整CI/phone boot/刷写/重启。

PAS13继续调查取得新的REA参数校验证据及有界原指令回放：
`../logs/pas13-instruction-replay-20261110/tz-dispatch-args-20261111-v2-evidence.json`
115 records，SHA`2fac537cd1bf982aee6376dc3518e42eb6dd65689d2b1881f0181b90108e73c8`；
匹配snapshot已保存。`0x1c056c60..0x1c056d73`69条参数校验指令，6个主机
RAM场景PASS；arginfo1值参数不访问/改写帧，单参数trampoline原样传ID13及
result指针到PAS7入口，在handler入口前停止。普通及python -O结果一致，
receipt SHA`c60e970dea9b9a0636540c51b1d4f245ffa835662af9e0e46cc0914b11dd7c26`。
这不是完整dispatcher/EL2/EL3/上游frame/返回路径或live状态的证据，false
原因仍未解除。下一步聚焦未覆盖dispatcher/运行环境及真实owner后端；
已有2026-11-10 UEFI/XNU均false的配对结果，不默认重复同样查询/控制轮。
GPU scratch/fence/Metal均未执行，PAS13=false/supported=false、CP halt保持。

2026-11-11：resource-owner provider结果到private CP admission owner的桥接已提交
并完成完整XNU链接。commit为`1f72f90`（桥接`516f615`及analyzer padding修复），
run`43025405283`全成功，30m8s；artifact
`../inputs/ci-43025405283/`。`device-commit.txt`精确匹配，`SHA256SUMS`通过；
kernel17581176B SHA256
`5427e02c29c56d76c7ae47968ca77e3b03a856fc46c8eae070d34e5dc3c79e03`，
build.log SHA256
`ce66a55677c3eab5acc98606377909b1107270b9fb5a80ce4e320aa6693c0a7e`。
`llvm-nm -u`为空；从精确commit archive按0085/0086/0087顺序staging后
`verify-gpu-kernel.py` PASS 116 core/49 memory API/0 unresolved。独立
`verify-ci-scm-pair.py` receipt为`out/kernel-verification-ci-43025405283.json`，
SHA256`de26ec66f69712b05ba86c3b528c1f2fada63c0a3554690ca490b904967c9b86`，
并确认touch/pmap、CI staged source比较及CPU capture/SMC codegen。首次
run`43022482625`在test fixture `{0}` padding的Clang analyzer警告处失败，未进入
Build XNU、无kernel；该失败保留，修复`1f72f90`只改为显式`memset`。新header只接受既有provider gate逐字节命名的
accepted result，并把provider/request/claim/expectations/result/clock复制成
checksummed immutable contract；每次owner validate重新核对provider lease、
owner/resource证据年龄、RPMh/ACK、crypto/ZAP身份。CP finish使用同一provider
context/lease；重复finish、空记录、身份错配、lease撤销或证据过期都粘滞失败并
只retain一次。生产CP的`keep=1`与显式成功`keep=0`分别覆盖。
focused `/tmp/resource-owner-cp-gpll0-20261111-v3/receipt.json`为PASS，184步，
SHA `c14ffa1e9e5353d50f17ddffc24236438e54bfd64e4d035378d24ff9eee373c2`，
验收期间source hashes未变。ZAP live-boundary
`/tmp/resource-owner-cp-zap-live-20261111-v1/receipt.json`也为PASS，SHA
`9f70cb9040d7e226ef5681347b131a77c907c84f1e14faafcf902a3c7fa38fcf`。
host 86项、ASan/UBSan、production静态分析、AArch64/staged codegen及13项
integration（1项可选reference SKIP）均PASS。
没有默认backend/startup caller、MMIO/SMC/PAS/crypto/clock/WPTR/CP unhalt/GPU
submission；夹具不是真实owner或硬件证据。PAS13=false/supported=false、CP
halted不变。该精确kernel已重新打包SCM-pair诊断镜像并独立验证：
`out/scm-pair-ci-43025405283-1f72f90-v1/boot-scm-pair.img`，70688768B SHA256
`415d47275ca5e6f4db13015fd423f6c9ee29547f192895582bc5edc9bb07a30c`；
receipt SHA`4330e727d4ec11558766165723497c96af1d76882e78114c9b0bb5252e686bae`，
boot-verification SHA`b45690836b21ccf135231c1ced197263c32b94d51d3ac27f9dcb1ed46d2d1922`，
两者PASS且phone_operations=false/gpu_execution=false。镜像未boot；手机保持
当前现场，无USB/重启/刷写操作。

2026-11-11：GPU owner provider 接线边界已离线完成，等待全量本地验证与提交。
新增 `cepheus_resource_owner_provider.h` / focused test：真实后端必须自有
lease 并在采集前后验证，显式 EXCLUSIVE_REAL、非模拟，精确绑定 provider
context、boot/owner/lease/resource/ACK epoch、RPMh/crypto ledger；输出复制且
禁止与任何输入/context/gate alias。一次 boot 仅一次尝试，所有已尝试失败在
固定 provider context 上 retain 并永久保留 uncertain；错误 pre-attempt 合同
和已有非零 gate version 在尝试前拒绝。没有默认 backend、startup caller、
MMIO/SMC/PAS/crypto/clock/GPU 操作。该 fixture 不是真实 owner 证据；
PAS13=false/supported=false/CP halted 不变。手机当前不做硬件操作。

2026-11-11：secure resource evidence 批次已提交并完成完整 CI。commit
`6a4c732`，run `43002894731` 全成功，37m0s；artifact 为
`../inputs/ci-43002894731/cepheus-touch-kernel/`，kernel 17564792B，
SHA256 `870092bcf24fc00337e1ae8ea2bdbb538d69df8b50ffd3738e004fb079e88450`，
`device-commit.txt` 精确匹配。`llvm-nm -u` 为 0；从该 commit archive 独立
staging 后 verifier PASS：116 core entrypoints/49 memory/firmware APIs。
ZAP load/stop/status 与 owner admission 已链接，staged
`cepheus_zap_resource_evidence.h` 与源码逐字节一致。build log 仍有 XNU 的
`-Wdeclaration-after-statement` 警告，未作为错误或验收豁免。
该 artifact 仍未打包、未 boot；PAS13=false/supported=false、CP halted 不变。

2026-11-11：PAS secure resource evidence 合同离线完成。新增
`xnu/cepheus_zap_resource_evidence.h` 与 focused test，把 secure PA/XPU/cache、
crypto route/BCM/ledger/ACK、owner gate/epoch 和年龄窗口绑定；PAS start/lease
已接入，并修复 transport epoch 与 owner resource epoch 的错误混用。真实
`a640_zap.elf` 的 0..27 host transport modes 全PASS，其中 24..27 覆盖
crypto ledger、PA 重叠、owner 混源和自封记录，均在 expose 前失败；纯合同
ASan/UBSan PASS。该记录只验证格式和交叉绑定，不是真实 owner、PAS 认证或
GPU 执行授权。当前源码尚未提交/完整 CI；手机仍未被 USB 工具枚举，保持
PAS13=false/supported=false/CP halted，无 phone/SMC/PAS/GPU 操作。

2026-11-11 17:30 离线收紧RPMh尾诊断设备入口。`run-rpmh-tail-device.py`现除
唯一cepheus设备、精确镜像哈希和仅`fastboot boot`外，还会在分析前验证
`capture.json`/`packets.jsonl`/`boot.log`三者一致：UDP正常跑满请求时长、
原始字节数与SHA匹配、accepted包数和总字节一致、来源全为`192.168.7.2`、
首包晚于本次boot命令。空/截断/旧包/混源/信号提前结束均FAIL；`TAIL_COMPLETE`
但collector error非0也FAIL，分析若声称GPU执行或admission变化亦拒绝。
新增fixture覆盖上述路径，`test-run-rpmh-tail-device.py`、12项integration
（1项可选reference SKIP）及O0/O2 ASan/UBSan尾hook测试PASS。此改动只验证
本地采集轮次身份，UDP仍不能认证设备来源或证明无丢包。手机当前仍未被
`adb`/`fastboot`/`lsusb`枚举，因此没有boot、重启、刷写、PAS/CP/GPU操作；
真机下一步仍是手动进入fastboot后运行同一镜像。17:33完整离线preflight为
`PASS_WITH_SKIPS`：26 PASS、0 FAIL、6项可选输入SKIP；receipt
`/tmp/gpu-offline-tail-runner-20261111-v2/receipt.json` SHA
`ddba0ed160b1133ceda3457861c8ef327d989f4d50e87dd69523575a4e2e6524`。
先前v1 receipt
`/tmp/gpu-offline-tail-runner-20261111-v1/receipt.json` SHA
`c97c97505a627687cbe3ef0ec44aee3b65fd797591dbb51d139d79a689295fe3`
仍为FAIL：sanitizer在旧240秒限制退出124，log SHA
`ff642f64c4e60205d4989e4017324025e6a317f407fd38744d5d405cd16d55f9`；
该失败保留。v2只把sanitizer有界限制调到900秒，实测log SHA
`61a376a85648db4314d882d1da8b1a751d493a9aa8704203589e4940d68040c8`，
其余检查仍为240秒，未放宽行为断言。

2026-11-11 17:05 owner-evidence完整链接与退出尾诊断包验收PASS，真机未启动。
commit`c9306124818aab7b0e005fad1b52deda98703863`的唯一完整CI42987126144
SUCCESS（33m23s），artifact `cepheus-touch-kernel`及build log已下载。
`device-commit.txt`精确匹配、`SHA256SUMS`通过；kernel17564288B SHA
`4041b59f97587b398d0f19eb02b83e05c8b3aff0e132e2238087258e8e90e028`，
build.log SHA`3a5a5b0082b23a6551540bfae9510b82d4ad71c515de6ef4941f2b555d83679b`，
`llvm-nm -u`为空。从精确commit archive staging后`verify-gpu-kernel.py`
PASS 116 core/49 memory API/0 unresolved，owner-evidence及CP admission/
native ring符号已链接；`verify-ci-scm-pair.py --rpmh-tail` PASS并生成
out/kernel-verification-ci-42987126144-tail.json。
用该精确kernel重打包现有退出尾诊断，五步本地构建全PASS，另在离线容器
独立复验PASS：out/rpmh-tail-real-42987126144-c930612-v1/boot-rpmh-tail.img，
70696960B SHA`aa519aeaf36d3c87b0b516695b814aefbf1d44655a94bc926606eb9de8d1616b`；
receipt SHA`67a5126f...`，boot-verification SHA`96684262...`，独立
boot-verification-independent SHA`f80d39a7...`。镜像只做90秒RPMh软件请求
快照及gated只读GPU探测，不解除CP halt/写WPTR/提交GPU。17:02主机
`lsusb`/`adb`/`fastboot`均未枚举手机，因此没有启动、重启、刷写或切换系统。
真机下一步仍是手动fastboot后临时`fastboot boot`此镜像；即使快照成功也不
等于真实owner/ACK，PAS13=false/supported=false、secure PA/crypto及GPU/Metal
执行仍未完成。

真机执行入口为`scripts/run-rpmh-tail-device.py IMAGE NEW_OUTPUT_DIR`。它拒绝
非唯一设备、非cepheus产品、错误镜像哈希、已占用UDP5140及已有输出目录；
固定只执行`fastboot boot`，不包含flash/erase/reboot/set_active/format。
先启动360秒有界UDP采集，收到READY后才发送镜像，结束后自动生成
analysis.json和receipt.json。设备未出现时返回2且不创建输出；真机启动后
先观察90秒scheduled记录，再核对18段raw、core summary和end marker。

2026-11-11 17:42 CP admission与owner证据链离线绑定。外部owner现在必须同时
提供已接受的双TBU/RPMh/ACK evidence gate及同一不可变snapshot；自封记录、
REFUSED gate、owner/lease/resource epoch或ACK/RPMh ledger身份不一致均在
尝试前拒绝，取锁后再次核对门禁与本地副本。admission记录新增owner/lease/
resource epoch，`check`同时复核这些字段。owner-evidence还拒绝
`now+max_age` uint64回绕。focused receipt
`/tmp/owner-admission-binding-local-v2/receipt.json` SHA
`57c8f49f616d58e4c0b84364c5bbba5c8af5767c280300b5708203dbfddb8917`：
host、ASan/UBSan、实际SWEET AArch64装配、staged AArch64编译及静态分析PASS。
全量离线preflight
`/tmp/gpu-offline-owner-admission-20261111-v1/receipt.json`为
`PASS_WITH_SKIPS`（26 PASS/0 FAIL/6可选输入SKIP）SHA
`d24b73590b5bf2de6632c10951ba31533993d7e92551ce49d9dde4f63956ae7b`；
sanitizer log SHA`61a376a85648db4314d882d1da8b1a751d493a9aa8704203589e4940d68040c8`。
这仍只是记录合同，不是真实owner/ACK，不解除CP halt，也没有GPU执行。

2026-11-11 owner-evidence离线批次：新增pure fail-closed双TBU/RPMh/ACK复制记录
admission及link-only wrapper，无startup caller/MMIO/SMC/firmware/GPU提交。
精确要求TBU0`0x2cc5000/0x1000`、TBU1`0x2cc9000/0x1000`及SID
`[0,0x400)`/`[0x400,0x800)`，拒绝ECATS debug误认、缺TBU、SID重叠、
foreign/stale/uncertain RPMh、缺/错ACK、simulated、partial、过期及output
alias；uncertain永久retain。commit`8163eec`；fresh full local preflight
`/tmp/owner-evidence-preflight-v2/receipt.json`四步PASS SHA`3053c969...`；
focused `/tmp/gpll0-owner-evidence-final-v2/receipt.json`除900s CP-ring链
timeout外全部PASS，FAIL保留；单独CP-ring重跑完整PASS receipt
`/tmp/owner-evidence-cp-ring-pool-v1/receipt.json` SHA`0306e6ff...`。
真实owner仍未取得，故GPU/Metal未执行；下一步唯一完整kernel link，然后
只读inherited-resource快照真机点。PAS13=false/supported=false、CP halt、
无PAS mutation/刷写/phone操作保持。

2026-11-11 15:20：owner-finish admission快照批次已提交/push为`aa1947b`，唯一
完整CI42974023175 SUCCESS（35m23s），artifact已下载独立验收。device-commit精确
匹配，kernel17564192B SHA`9ddd8ffc89728b396a2e46fae2d2e282494c2a06dccf6bd6502ef86eaab15f7f`，
build.log SHA`ed89ed57...`，`llvm-nm -u`为空。从exact commit源码归档staging后
`verify-gpu-kernel.py` PASS：115 core/49 memory API/0 unresolved；CP admission、
authenticated ZAP、ARC/BCM、ring/submit符号均链接。该核仅验收为已链接构建，
未打包/boot/刷写；PAS13=false/supported=false、CP halt、真owner/继承ledger/
双TBU/secure PA+crypto/真实ACK仍缺，GPU/Metal未执行。无phone操作。

2026-11-11 14:40：ACTIVE sender固定槽租约批次已提交为`c190a09`，离线验收完成，
等待唯一替代完整链接。最终ARC/BCM focused receipt `/tmp/active-lease-local-v4/
receipt.json` SHA`f8cf24cf...`，绑定修复后的macOS测试源码。`c190a09`首次完整CI
42971631606在macOS host失败：`test_arc_active.c`在private完整类型定义前声明
lease对象；移到include后后ARC/BCM复验全PASS，失败run无kernel，不追认。CP
admission finish快照的最终focused receipt `/tmp/cp-admission-finish-local-v2/
receipt.json` SHA`3a056ad4...`，130源文件/164步全PASS；full preflight
`/tmp/gpll0-admission-finish-preflight-v1/receipt.json` SHA`0beab894...`，
四步全PASS且382源hash仍匹配。该批尚无包含它的完整XNU kernel。

2026-11-11 14:19：ACTIVE sender固定槽租约批次离线完成，等待提交/完整链接。
新增sender-owned只读lease快照（槽位、完整命令、WAIT/enable/control、命令状态、
IRQ before/masked/final、attempted stores、CLEAN/UNCERTAIN）及ARC/BCM finish
回调证据；IRQ mask后第二次全bank扫描必须仍选同一槽，禁止重定向。补齐mask写后
读回失败也向owner报告UNCERTAIN。`test-bcm-active.py` final-v3全部PASS（host、
ASan/UBSan、两静态分析、实际RPMh AArch64编译、integration、boot-no-call、
staged-native），receipt `/tmp/active-lease-local-v3/receipt.json` SHA
`fb57087c...`；full preflight四步/83host/七分析PASS，receipt
`/tmp/gpll0-active-lease-preflight-v2/receipt.json` SHA`bf4a372c...`，382源文件
验收后未变。该批尚无包含它的完整XNU kernel。

`db33eff`的替代完整CI42967566527已SUCCESS并下载独立验收：kernel17580568B
SHA`ee325d473d1a2c7418f0b722b32d67ef646d89b44571a78eddb73d5387cc8235`，
build.log SHA`2dab8726...`；device-commit精确匹配，archived `db33eff`源码
staging后`verify-gpu-kernel.py` PASS（115 core/49 memory API/0 unresolved），
CP admission、authenticated ZAP、ARC/BCM、ring/submit符号均在链接核中。此核
包含CP admission staging修复，不含随后fixed-slot lease改动。PAS13=false/
supported=false与CP halt保持，真owner/继承ledger/双TBU/secure PA+crypto/真实
ACK仍缺，GPU/Metal未执行；无phone操作/重启/刷写。

2026-11-11 13:10：CP execution admission边界实现并离线验收PASS。新增
private identity和native one-shot wrapper，精确绑定7-store halted ring、
CP45 writes/66 reads sealed publication、15-effect enabled GX SMMU、LIVE
ZAP epoch及authenticated allocator claim、RPMh token；锁序
SCM→GPLL0→pool，最终复核全部live identity。输出不得覆盖输入/内部状态，
one attempt/sticky failure，错误token不消费admission；owner在所有已尝试
路径保留。无production caller，不写WPTR/unhalt/submit/新context/PAS
mutation/GPU。identity及native wrapper host/ASanUBSan/static analysis PASS；
focused/full preflight四步、AArch64 codegen、staged codegen、七分析全PASS，
receipt /tmp/gpll0-admission-final-v2/receipt.json SHA17e29e90...，绑定父
a538e53；随后仅清理测试夹具未用字段和文档，生产源码未变，因此不冒充
f944723的新receipt。原厂GMU/SQE GX SMMU0..24和ring25..31 actual C/C++整链全PASS，
无ASanUBSan；MMIO/ACK/IOKit/owner模拟，不是GPU。日志在
/tmp/gx-smmu-admission-base.8YrhgT/out和/tmp/cp-ring-admission-base.C6bAVt/out。
CI42959844615 @a538e53完整成功；下载artifact为17563504B SHA0758ee75...，
链接ring/CP/ZAP但无admission。旧交接误写的17546448B/358faebd已纠正。
详见docs/cp-execution-admission.md。此批已commit/push为f944723，唯一完整
CI42964201439前置全PASS，Build XNU因workflow漏拷两个admission头在
0087 staging失败，无kernel/artifact；已补workflow并加staging清单回归，
本地164步receipt SHA d222669e... PASS，准备唯一替代CI。真owner/继承资源
ledger/双TBU/secure PA+crypto/PAS13仍缺，CP halt/GPU/Metal未执行。无
phone操作/重启/切换系统，目标active不停工。

2026-11-11 12:20：ZAP authenticated binding/alias批次本地验收PASS。
`ceph_zap_memory_authenticated`只认证allocator记录：必须处于PAS成功后的
AUTH相位，handle/claim一致，snapshot已释放、无CPU view，双buffer已发布且
PA/容量/布局完全匹配；不提供owner、PAS或GPU admission证明。CP output
checks拒绝ZAP ALLOCATING/COPYING的不稳定字段及MDT/image backing alias，
普通GPU/ZAP handle、预算和生命周期保持隔离。真实a640_zap的24 PAS transport
modes、5 memory modes及原厂a630_sqe 10 build-pool modes全PASS，无sanitizer
report；receipt out/zap-live-boundary-receipt-20261111-v1/receipt.json
SHA9825d5bc...，记录ca9b010并证明验收期间源码未变。范围仅为host IOKit
doubles和真实固件字节，无phone/SMC/PAS mutation/CP unhalt/GPU执行。
CI42939546189 @78d34a8完整成功，kernel17546448B SHA358faebd...，source.tar
SHA472b5af5...；用archived tree独立staged链接PASS，112 core/45 memory APIs、
无unresolved，touch ordered contacts及16KiB PA64 callback PASS。该核不含
ring/ZAP，不冒充HEAD。准备提交/push本批并派发唯一包含ring/ZAP的完整CI；
下一native CP execution admission，将ring、CP snapshot、live SMMU和
authenticated ZAP epoch绑定真实pin/lease。PAS13=false/supported=false、
真owner/继承ledger/双TBU/secure PA+crypto/真实ACK仍缺，CP halt/GPU/Metal
未执行，目标active不停工。

2026-11-11 10:00：78d34a8已推送，唯一CI42939546189前置全PASS/Build XNU
进行中，不含后续ring。native halted ring/SQE七fixed stores后端已完成，
实际enabled GX SMMU/CP provenance/installed pool绑定，逐访问owner/halt检查、
每store完整14context核对，零继承ring/RPTR/完整tail、all-pool output拒绝。
final focused155steps SHA55b8703f...；full四步83host/七分析 SHA096e87b7...，
两source hash集及155log独立匹配。原厂GMU/SQE实际七新增整链25..31 PASS，
原CP12pool PASS，无sanitizer（MMIO/ACK/IOKit/owner模拟），非GPU执行。
早期ring v1 exit143保留不追认；见cp-ring-native-backend.md完整log hashes。
准备此批提交，下一实际认证/执行前状态绑定。CP halt/PAS13=false保持，
真owner/继承票/双TBU/secure PA+crypto缺，无phone操作/不重复fastboot问题。
目标active，用户明确不停工，GPU/Metal未执行。

2026-11-11 09:29：native CP/atomic build-seal/output-alias修复 frozen final-v3
完整本地验收通过：focused153steps SHA42df3226...；full四步/83host/七分析
SHAe4e61c8c...。完成后两source hash集与153log hashes独立匹配；原厂GMU/
SQE旧68HFI+12CP、实际25CP/GX SMMU与10build pool全部PASS，无ASanUBSan。
MMIO/ACK/IOKit/owner模拟，不是GPU运行。详见cp-native-backend.md完整hash。
4cc8d2c CI42929469961为历史失败/无kernel；此批准备提交及单一替代完整CI。
下一native halted ring/SQE七固定stores及actual已安装context绑定，无phone
操作/不重复fastboot问题。真owner/继承/TBU/PAS13/auth缺，CP halt保持，
GPU/Metal未执行，用户明确不停工，目标active。

2026-11-11 08:57：不停工。4cc8d2c已推送，CI42929469961在host regression
失败/未到Build XNU/无artifact；O0本机复现output4B自然对齐违反8B合同，
explicit_Alignas(8)修复，不放宽native。失败log SHA67cce68e...。新增native
CP45保护stores/66restore reads、actual六buffer准备pin/同锁atomic seal及
private restore provenance已实现并绑定GX SMMU。interim focused150PASS，
最终full四步/83host/七生产分析PASS receipt SHA48acc817...；最终focused
原厂固件旧68HFI+12CP/全链PASS log SHAd6d6cc7e...，新增24CP/GX SMMU
actual chain仍运行，未完结不追认。前轮mode12错把未暴露固件snapshot当作
必须retain的DMA backing，修正夹具，production保留未放宽。详见cp-native-
backend.md。待此批完结提交/唯一替代完整CI，下一native halted ring/SQE。
无phone操作/USB无手机，不重复fastboot问题；CP halt/PAS13=false保持，
真owner/继承票/TBU/auth仍缺，GPU/Metal未执行，目标active。

2026-11-11 08:11：2d20d14 CI42925513101 FAIL，actual XNU IOKit缺CPprivate
头（build.log:41073），已归档/无kernel，不追认local为full link。后续native
GX SMMU SID0/mask401/39VA、actual CP pool pin+发布+失效隔离已实现；fresh
focused147steps PASS SHA692c2856...，full四步/84host PASS SHA70b8ebf3...，
两source hash集完结仍匹配。原厂GMU/SQE整链68HFI+12CP+10GX SMMU cases
PASS/无ASanUBSan，logs SHA4dc03d41.../23364c73...；owner/IOKit/MMIO/ACK模拟。
补IOKit完整CP dependency staging+仅staged目录编译回归PASS。旧fast编译
失败/v3timeout终止/sourcechanged不追认。见gx-smmu-native-backend.md。
此批待提交/单一替代CI，下一native CP protection+实际restore provenance。
无phone操作/新boot/USB手机；真owner/TBU/PAS13/secure缺，CP halt/GPU未执行，
目标active不停工。

2026-11-11：继续不停工。b3d0871 HWCG/GBIF后续计数器与CP六buffer封存/pin
fresh frozen focused141steps PASS SHA04c809ab...，完整四步/83host preflight
PASS SHA92b7c19c...，结束后两source hash集匹配。原厂GMU/SQE actual68HFI
及12CP modes与旧memory链PASS，无sanitizer报告，log SHA7ff983a4...；硬件、
owner、IOKit、ACK模拟，不是GPU运行。旧超时与source-changed FAIL不追认。
CI42915818943 @36c94c3已完整成功且archived源码独立验收107core/35memory；
不含setup/counters/CP。见gx-native-backend.md与cp-memory-publication.md。
下一提交此批/一次完整XNU link并继续native GX SMMU。无phone操作/USB手机；
真实owner/继承票/TBU/PAS13/auth仍缺，CP halt保持，GPU/Metal未执行，active。

2026-11-11 05:36：不停工。471e223的CI42900020339完整成功并archived-source
独立验收PASS，101core/35memory/无unresolved，17492488B，kernel SHA98b66afd...，
receipt SHA079cbecb...；actual linked RPMh/idle也PASS。该核含OOB/query/pin，
不含后续native DB/fuse config及same-session HFI boot/DCVS/OOB-held START。
后续批focused final-v2 135checks PASS SHA346a6447...，完整preflight final-v2
四步/82host PASS SHA3228c619...；结束后两source hash sets未改变。显式原厂
固件actual pool同进程24 modes/ASanUBSan无report，log SHAc0601d76...；硬件、
owner、IOKit模拟，不是真GPU。native config 1925 PROT_NONE points全PASS。
本批准备提交/一次完整link。bin1 fixture QMP在query后，须继续补ACD-before-CM3
顺序证明及full bin1 START，不以此批旧receipt追认后续。详见hfi-native-backend。
TZ权限转换静态证据与TBU ECATS debug-lock语义已记录，PAS13 false未解。
无phone/打包/启动，USB无手机；不重复已有fastboot问题。真实global owner/
继承票/TBU/secure PA+crypto/PAS13仍缺；GPU/Metal未执行，目标active。

2026-11-11 04:53：继续不停工。3b0db57已提交推送，以下04:26“待提交”为
历史。actual pool HFI pin/seal、原生fixed版本查询已实现；focused final-v4
PASS SHA4b7609a7...，完整production preflight final-v3四步/81host/七分析
PASS SHA8120f3a8...，完成时source hashes全匹配。随后仅加测试：同进程
actual SMMU/upload/CM3/HFI与actual pool，九场景ASanUBSan全PASS、无report，
log SHAbbb51ccb...；旧receipt不追认新增测试。详见hfi-native-backend.md。
本批待提交/唯一组合完整XNU link；CI42886831693不含OOB/HFI/pin/native。
无phone操作/新boot镜像，USB无手机；等已有fastboot确认，不重复请求。
真owner/继承票/TBU/PAS13/auth/crypto仍缺，GPU/Metal未执行，active。

2026-11-11 04:26：HFI逐word/实际link full identity与view/layout/pre-post
guard、alias/bounds/输出原子/sticky loss本地focused final-v3 PASS，receipt
SHA13379064...，显式real GMU/SQE actual C/C++ ASanUBSan整链PASS，log
SHA1866de98...。final-v2 invalid-enum UBSan虽然exit0仍判FAIL保留；bridge
改raw int/极值拒绝不消费pin，UBSan终止；旧SMMU/upload日志声明已更正。
完整preflight final-v2四步/80host/七分析PASS，receipt SHA01f9d667...，两
验收source hashes仍匹配，待提交。详见hfi-access-lifetime.md，下一actual
pool HFI pin/原生fixed版本查询。无新CI/phone操作，GPU/Metal未执行，active。

2026-11-11 04:10：CI42886831693 @8c539ca于04:03:13完整成功，archived
source独立验收PASS，17473904B/SHA b150954326355d39ab71da0461521103a44cdd53d
013a8b996985dbf34aecdfa，98core/30memory/no unresolved/touch/PA64/SCM/tail。
receipt out/kernel-verification-ci-42886831693.json SHA345c2f2b...；实际linked
RPMh SHA2516428b...及idle SHA5009b78d... PASS，非runtime。未打包/boot。
后续OOB ee105f6已提交推送，不在8c核中。HFI逐word pre/post validate、实际
link全publication/view/layout及回调后identity复核、alias/bounds拒绝、原子
输出/sticky loss改动进行final-source focused及完整preflight，尚未验收/提交。
focused使用显式原厂a640_gmu.bin/a630_sqe.fw运行actual C/C++ ASanUBSan整链。
109bind/24read/32write PROT_NONE首轮通过，synthetic pool不是GPU。
下一原生HFI门铃/actual pool lifetime、真owner/继承/PAS13；无phone操作。
GPU/Metal未执行，PAS13=false/supported=false/CP halt保持，目标active不停工。

2026-11-11 03:54：QMP8c539ca已提交推送，唯一组合CI42886831693于03:35:40
派发，全部前置已PASS/完整XNU构建中，不是完整link验收，勿重复派发。
后续native OOB0使用actual CM3/HFI_READY private record及maps/config/version，
逐GMU读前电源门禁/后复核、真实rpmh scope永久保留、raw/ACK继承拒绝，
fixed OOB0 request/clear/drained release，copied owner/sticky failure，无startup。
focused final-v3 PASS SHAafdeff4a...，完整preflight final-v3四步/79host/七分析
PASS SHA2cde8885...，完结source hashes仍匹配；8863/17728 acquire与2698/5396
release owner/scope PROT_NONE为actual CM3函数+synthetic hardware，不是真GPU。
final-v2 FAIL定位测试peer断电PROT_NONE误读，保留，不放宽production门禁。
详见oob-native-backend.md；此批不在8c539ca CI，待提交/后续link。
下一HFI逐word租约/实际门铃与真resource owner/继承/PAS13；无phone操作，
无USB手机，等已有手动fastboot确认。旧tail unbooted镜像不含后续native。
PAS13=false/supported=false/CP halt保持，GPU/Metal未执行，目标active不停工。

2026-11-11 03:30后更新：GPLL0 CI42881254237 @d5bf1dd于03:27:42全成功，
archived-source独立验收PASS，kernel17473248B/SHA3dd662d8...，97core/30memory，
receipt SHA3222babe...；linked RPMh/idle PASS，GPLL0 disassembly仅手动检查，
不是独立自动CFG证明。native power父时钟/private counter绑定6e24284已推送，
不在上述CI。QMP实际fixed maps/500us真实delay/真实scope/copied owner/逐word
guard及cold连接/固定ACD=1后端focused final-v3 PASS（SHA5bfe6809...），完整
preflight final-v3四步/78host/七分析PASS（SHAfa930323...），完结后source hashes
全部仍匹配。3786owner/7574scope PROT_NONE、13stores/4IRQ只是synthetic peer。
早期preflight夹具FAIL及500us改动前证据保留，详见qmp-native-backend.md。
下一本批提交/唯一组合完整XNU link与真owner/继承/GMU通信；不自动phone操作，
旧tail未boot镜像不含后来批次，等已有手动fastboot确认。PAS13=false/
supported=false/CP halt不绕，GPU/Metal未执行，用户明确不停工，目标active。

以下03:14记录是历史，已由以上完整CI/local验收取代。
2026-11-11续推进：d5bf1dd已提交推送，唯一GPLL0合并CI42881254237于
03:02派发，全部前置通过/03:14完整Build XNU中，待artifact独立验收。
后续native power接实际保留父时钟/真实counter private publication，
pre/post owner guard及prepare前后核对/失效sticky；nominal-XO量化一致
不是calibrated exact300MHz，真实owner仍必需。focused final-v1
SHA95d9b994...及完整四步preflight SHA208c8888...全PASS，77host/七分析，
403power/25856parent/25856scope PROT_NONE及实际counter->准入通过；
此批不在d5bf1dd/待提交及后续link。详见power-native-backend.md。
当前无USB手机/无操作，已发手动fastboot请求等确认；旧tail诊断不变。
下一AOP/QMP原生连接/ACD及真owner/继承/认证，GPU/Metal未执行，目标active。

2026-11-11更新：CI42874717231 @091c7ff于02:55:48全成功，本机archived
source独立核验PASS，17473184B/SHA677de398...，97core/30memory/无unresolved。
receipt SHA2c31a939...；actual linked RPMh普通与-O同receipt SHA0a1ff59f...，
旧idle-only核双模式拒绝不出receipt；idle linked SHA552cdf4d...也PASS。
GPLL0 scope实际接入已790e0e4提交推送/focused和preflight final-v2 PASS，
不在091核中/待后续完整link。当前新增linked verifier及回归/文档收尾，
无phone boot/打包，GPU/Metal未执行。继续真owner/继承/父时钟绑定，
不制造票/ACK，不绕PAS13=false/supported=false/CP halt；目标active。

2026-11-11 02:33续：091c7ff已推送，02:23:33只派发一次完整XNU
CI42874717231，当前前置回归进行中，未link/验收。勿重复派发。
后续GPLL0 real copied rpmh token/map前后及逐读检查/写前永久保留scope与
idle/无写adoption发布前保留，保留请求不确定也通知owner，sticky failure。
focused final-v1 PASS，receipt out/gpll0-scope-local-20261111-final-v1/
receipt.json SHA e8e4ec4b...；actual scope+PROT_NONE/host/ASanUBSan/native/
staged/analysis/原厂对照，非真owner。完整local preflight进行中；本批不在
091c7ff中，待独立link。详见gpll0-scope-coordination.md。
USB无手机/无操作，旧tail未boot镜像不变；GPU/Metal未执行，目标active。
02:40 final-v2 focused SHA88f25722...及完整preflight SHA5af17ec7...均PASS，
四步/77host/七生产分析；final-v1 preflight include路径FAIL保留/修正重验。
actual counter也覆盖scope全失效点PROT_NONE。CI42874717231 @091c7ff已完成
全部前置/进入Build XNU，仍非link验收；后续GPLL0独立批待commit/link。

2026-11-11 02:15：idle完整XNU CI42867280759 @91a5ec1全成功并本机独立
验收PASS，kernel17472904B/SHA3a77999f46e307ed4b2353989dd2c1ae339cc9c643
a2178c42a7c9bf2e8e59f1，94core/30memory/touch/PA64/SCM/tail/无unresolved。
out/kernel-verification-ci-42867280759.json SHAd299587a...；附加actual linked
五idle entry/control flow PASS，out/idle-linked-ci-42867280759-v1.json
SHAd1d0d990...，用准确91a5ec1 archive验收，未boot。
后续actual apps-DRV reservation与legacy touch/vreg map前拒绝/ARCBCM token/
写前永久idle retain已实现，无startup/新票/SMC/真实资源owner。focused实际
source/adapters final-v2 PASS，1000pthread/ASanUBSan/native/staged/analysis；
preflight v1四步/77host/七分析PASS但注释/禁用断言运行中更新，fresh final-v2
进行中。详见rpmh-writer-coordination.md；原厂masked双TBU source audit PASS，
不是录制FDT/运行owner。此批待提交/完整link，不混入91a5ec1核。02:20 fresh
final-v2全部PASS，四步/77host/七分析，receipt SHA f31e8502...，生产源码
检查期间无改动。
USB仍无手机/无操作；旧tail未boot镜像不变。PAS13=false/supported=false/CP halt
不绕，GPU/Metal未执行，目标active继续真owner/继承/GPLL0协调/诊断及启动。

2026-11-11 01:34：组合native完整XNU CI42860421403 @d22a054成功并本机
独立验收PASS，kernel17472504B/SHA801079f0e46560de5ee4c2cd0ff5d36bb5b564
02012cf42f078fec610c457656，94 core/30 memory/无unresolved/touch/PA64/SCM/
tail通过；receipt out/kernel-verification-ci-42860421403.json，SHA1fd028a9...。
用git archive d22a054版本验收器/生产树，不把后来idle改动追认该核。
新增真实SWEET cpu_idle和前置machine_idle WFE reader/block生命周期，one-shot
10ms真实排空/超时拒绝/不确定keep永久保留，无startup/资源或GPU写/全局idle
参数变化；focused accepted SHA203b2286...，10000 pthread races及实际applied
函数体host/ASanUBSan/native AArch64/analysis通过，详见gpu-idle-coordination.md。
后续全量preflight final-v2已PASS，四步/77 host/七分析，SHA35f198c6...；
先前final-v1撞到修正前reset夹具失败保留。
91a5ec1已推送，01:40:04仅派发一次完整idle XNU CI42867280759；01:48
回归/静态分析通过、codegen进行中，待完整link/独立验收/真机，不是GPU执行。
无新镜像/手机操作，勿重复派发；无USB手机，等待已有
手动fastboot回复。旧未boot tail镜像CI42835691416 @2bf48ab/SHA b7d88d435...
不含近期native/idle。下一真实resource owner/继承/foreign writers/启动绑定；
PAS13=false/supported=false/CP halt保持，GPU/Metal未执行，目标active不停工。

2026-11-11 01:00：SMMU b5c8f2a及native upload d22a054均提交推送。
完整合并XNU CI42860421403 @d22a0541c5f4afd17fcf5cef90625dc76801405f于
01:00:29启动，已确认in_progress，尚非link PASS。只跟踪此run，不重复派发。
upload final-v1 preflight receipt SHA88d4ee34...四步/75 host/七分析通过，
focused及real GMU/SQE host chain已terminal PASS；当前全部自有exec已退出。
下一跟踪CI/下载独立验收，同时继续真实owner/继承票/idle边界；无手机操作。
旧未boot tail诊断仍CI42835691416 @2bf48ab，不含近期native，不冒充HEAD。
目标active未完成，PAS13=false/supported=false/CP halt不绕，GPU/Metal未执行。

2026-11-11续推进upload：原生固定三map、copied owner/publication、实际pool
ICACHE/DCACHE pin及sealed firmware snapshot接通，无caller/startup。逐TCM word/
cache byte pre/post guard及store前计数，失效sticky，Normal-NC真实发布、final
reset/retention/fault/lease复核；安装/retire/abandon互斥，失败QUARANTINED不free。
focused final-v1 PASS，8433 native/51 shared PROT_NONE、ASanUBSan/native/staged/
analysis，receipt SHAc5528ec2...。real GMU/SQE mixed ASanUBSan实际upload
pool0..7/native+pool0..5/整链0..22退出0，SHA69a32365...，均模拟硬件非运行。
全量preflight final-v1四步/75 host/七分析全PASS；下一一次合并完整XNU
link/产物验收及真实owner/继承/idle。
无手机操作，旧未boot tail诊断CI42835691416 @2bf48ab/SHA b7d88d435...不变，
不含近期native；PAS13=false/supported=false/CP halt保持，GPU/Metal未执行。
详见upload-native-backend.md；目标active不停工。

2026-11-11续推进：原生SMMU/真实六buffer pool pin本地验收收尾通过。
final-v2 focused receipt SHA c4a53b18...；8122 PROT_NONE、四map及实际
GMU reset/MEMNOC/DVM gates、live geometry/全stream扫描、SID5 scoped
15effects与真实delay，cleanup失败QUARANTINED保留pin。preflight final-v2
四步/74 host/七分析全PASS，SHA3047e55c...；real GMU/SQE ASanUBSan actual
pool0..7/native+pool0..5/整链0..22退出0，SHA5b433646...，均模拟硬件。
详见smmu-native-backend.md；无owner/startup/完整XNU link/新CI/手机操作。
原未boot tail诊断身份不变，不含本批；PAS13=false/supported=false/CP halt
不绕，GPU/Metal未执行，目标active。下一native upload逐访问guard与绑定、
真实owner/继承票/idle协调；手机未识别，继续等此前手动fastboot确认。

2026-11-11后续：ce190d9原生CM3已提交推送，继续原生PDC/RSC实际固定五map，
copied owner/input/config、GPLL0同锁/逐map及访问guard；要求既有retention/
CM3 reset/GX off，拒绝继承PDC enable，不改apps DRV或释放reset。共享PDC
去裸cold读、VA溢出/UINT32_MAX拒绝、失败输出原子/post store/barrier及sticky
失败，distinct GMU/PDC callback后再查PDC lease。48 attempted effects保留资源。
final-v1 local PASS：34844 native/2807 MMIO PROT_NONE边界、五map失败/
copied/reenter/off-aperture/冷态、ASanUBSan/native/staged/analysis。四步
pdc-native-preflight-20261111-final-v1全PASS，73 host/七生产分析；real GMU/
SQE mixed ASanUBSan链模式0..22退出0，log SHA530b021bd...，explicit captured
FDT通过，详见pdc-native-backend.md。不是GPU真机执行，ZAP optional未提供。
全native批仍待完整XNU link，无新CI/镜像/手机操作；USB仍无手机。旧未boot
诊断保持CI42835691416 @2bf48ab/SHA b7d88d435...，不含后续代码、不追认HEAD。
下一真实继承资源owner/idle/foreign-writer与原生SMMU/upload发布整合；
PAS13=false/supported=false/CP halt不绕，GPU/Metal未执行，目标active继续。

2026-11-11更新：继续工作，原生CM3 cold-start接固定GCC/GPUCC/GMU映射和
真实时间/delay，共享GPLL0锁、复制owner/config，逐map及GMU前置读前后
guard/sticky失效；要求既有TCM retention，不暗含upload/资源配置。无启动
caller或实际owner；18 attempted effects后保留资源，含不确定reset释放。
共享GMU MMIO补post load/store、UINT32_MAX后guard、失败输出不改和VA溢出。
final-v1 local receipt PASS：3423 immediate/5463 delayed PROT_NONE边界、
3/5 pause与9999成功/10000拒绝、ASanUBSan/native/staged/analysis。四步
cm3-native-preflight-20261111-final-v1全PASS，72 host/七生产分析；真实GMU/
SQE mixed ASanUBSan模式0..22主机模拟链退出0，log SHA da5a1e045...，不是
真机执行，ZAP optional未提供。详见cm3-native-backend.md；16KiB GMU/
GPUCC physical footprint重叠已明确。全批仍待完整XNU link，无新CI/手机操作，
USB未识别手机；原未boot诊断保持CI42835691416 @2bf48ab/SHA b7d88d435...。
下一原生PDC/RSC、SMMU/upload及真实owner/继承/idle协调。PAS13=false/
supported=false/CP halt保持，GPU/Metal未执行，目标继续active未完成。

23:57更新：新增原生TSENS bind/sample/now，固定四TM/SROT请求、真实内核
时间、独立锁及copied owner；所有map/status/sample/time前后guard，失效
立即锁存、无retry。通用sampler补每enable/ready/fault读guard、logical VA
重叠/溢出、时间停滞/倒退拒绝，late health变化保留。actual owner转换/时钟
健康仍无实现，未启用startup，无传感器配置写。host16KiB映射扩大范围和
双SROT同physical page合同明确，详见tsens-native-backend.md。
final-v1 local receipt33 generic/48 native PROT_NONE边界、ASanUBSan/
native/staged/analysis PASS；四步preflight final-v1全PASS，71 host/七分析。
原厂宏/captured FDT独立对照PASS；真实GMU/SQE mixed ASanUBSan modes0..22
主机模拟整链退出0，log SHA aad50b028...，不是GPU/新native真机运行，ZAP
optional未提供。v1初轮owner撤销后再次回调FAIL保留，立即锁存已修。
本批及前ARC/power待完整XNU构建链接，无新CI/打包/手机操作。23:55无手机，
等之前手动fastboot确认；诊断镜像保持CI42835691416 @2bf48ab/SHA b7d88d435...。
资源owner/继承票/idle协调、PAS13/secure PA/crypto/SMMU仍缺，CP halt保持。
GPU scratch/fence/认证/Metal尚未执行，目标未完成，继续实际平台接入。

23:39补充：32da8e4已推送。继续补正null map返回也须post owner guard，
final-v2 native local receipt PASS，含失败map同时owner撤销；fresh preflight
final-v2四步/70 host/七分析全部PASS。owner源码边界已核对：SWEET suspend/
quiesce拒绝不等于普通idle停止，wfi默认1/idle_enable默认开启，NO_MONITOR
不关WFI；软件rh_lock不排除继承sleep/wake硬件与其他DRV/secure writer。
详见resource-owner-boundary.md，未改CPU/boot args，不伪造资源lease。
手机仍未检测到，无手机操作或新CI；GPU/Metal未执行，目标未完成。

23:32更新：用户明确不停工，继续GPU。新增实际SWEET原生CX/GMU电源入口，
固定GCC/GPUCC/GMU ml_io_map、原生delay及同GPLL0锁，复制owner/boot单次
终止；逐map/read/store/delay前后validate，GMU每项前置读也逐访问检查，
UINT32_MAX错误读仍做post guard。实际owner仍无实现，无启动调用/reset释放/
GX写/PAS/SMMU/CP提交。final-v1 local receipt403 native/582 MMIO PROT_NONE
边界及host/sanitizer/native/staged/analysis PASS，四步preflight final-v1
全部PASS，70 host/七生产分析。v2夹具off-GPUCC故障、preflight v1同故障及
v3测试helper空指针分析失败保留，不追认；详见power-native-backend.md。
本批及新ARC仍待完整XNU构建/链接，无重复CI/手机操作。已验收诊断镜像
保持CI42835691416 @2bf48ab/SHA b7d88d435...，不含新生产代码，不重包冒充。
23:25 lsusb仍无手机；等已发手动fastboot确认后仅一次退出尾/GPU只读诊断。
下一真实资源owner/继承票/foreign writer及deep-idle协调；PAS13=false/
supported=false/CP halt不绕，GPU scratch/fence与Metal均未执行，目标未完成。

23:15更新：设备仍未检测到，等待之前已发出的手动fastboot确认；无手机
操作。生产ARC接入补缺：删除guard前rh_map及裸DB读，要求现有映射，DB
每word前后validate_access及sticky失败，sender保留实际resource验证；固定
name/复制owner，所有attempted store结束一次finish通知，uncertainty保留。
final-v3本地receipt实际ARC153 resource/194 PROT_NONE access/14 writes、
BCM1977/29、ASanUBSan/native/staged/analysis/integration通过；GCC定长name
警告的final-v2失败保留，不追认。四步preflight final-v2已全部PASS，69 host
及七生产静态分析通过。无生产owner实现/完整新XNU构建/新CI。已验收诊断镜像保持
CI42835691416 @2bf48ab和SHAb7d88d435...不变，不冒充新ARC；后续用新代码
前须完整构建。目标未完成，PAS13=false/CP halt不绕，GPU/Metal未执行。

23:03更新：真实tail启动包本地五步验收全部PASS，
out/rpmh-tail-real-42835691416-v1/boot-rpmh-tail.img，70696960B，
SHAb7d88d435723fd317825050de0ed93d890fb540b79cc1bbf89c9c5209f287c44；
receipt.json/boot-verification.json均通过，真实kernel绑定CI42835691416
@2bf48ab，固件绑定loader v3/core平台v3；精确双EFI及其余FV/shim/DTB/
FAT/touch参数保持核验。1b8f738已推送，未重复CI。手机未检测到，已请求
用户连接并手动fastboot；无boot/flash/reboot/restore。下一仅一次集中采集
退出尾18 raw chunks及只读GPU状态，最后一次短触摸/唤醒确认。有效软件
请求不当owner/ACK，GPU/Metal仍未执行，准入与CP halt保持。

22:59更新：完整XNU CI42835691416 @2bf48ab成功，已下载并独立验收PASS；
kernel17467744B/SHA97df727b22a19ee58ac0a2e83156dc4bedb2477eb3a09b4c93f26786d05a1ca8，
receipt out/kernel-verification-ci-42835691416.json，86 core/22 memory/无
unresolved/source/touch/PA64/native SCM/tail helper caller及ABI通过。
core v2漏基线4KiB runtime宏，不真机用；仅tail DSC补全基线extra flags，
新v3 core369664B/SHA22c81124c3e8513b1205001900481d463e2043ab1ce114f1071596508ed88ccf；
accepted-v4.json实际core/EBS/loader/native/sanitizer及core+40库八组编译/
链接选项严格一致PASS，旧v2被验收器拒绝。首轮SLINK_FLAGS不存在是测试
夹具错误，已纠正，未追认通过。新FV平台v2 SHA f5b685a13a1cd1c939f512ed51204bdd35ccdd578e520af5af4bec45fb133da5，
独立合成打包/拒绝通过。无重复CI/手机操作；下一真核本地打包与集中一次
手动fastboot诊断。GPU/Metal尚未执行，PAS13=false/supported=false/CP halt不绕。

22:50更新：完整XNU CI42835691416 @2bf48ab已进入Build XNU，前置通过；
不要重复派发。最终四步preflight out/rpmh-tail-preflight-20261110-final/
receipt.json PASS。复核发现旧isolated core缺LZMA/CRC启动库且RELEASE PCD
不同，不能真机替换；新增仅tail的RpmhTailCore.dsc保留default，平台版
out/rpmh-tail-core-platform-20261110-v2/370176B/SHA c41e3e863f6762f48fcd
b2ab602b4499f54ebc67e8f6b91a7a590d6df5d3ef90；accepted-v2.json实际core/
loader/EBS及库集/typed PCD/constructor与accepted core一致PASS。新FD
out/rpmh-tail-fv-platform-20261110-v1/3145728B/SHA53467f210a7a3bf90a921
fc84062d4528b49e768bb1de28087e093d2fef82a77，独立合成打包/拒绝PASS。
旧422400B core/旧FD仅隔离测试证据，不用于手机。无新XNU改动/手机操作；
下一跟踪同run、下载独立验收及真核本地打包，再一次手动fastboot诊断。

最新2026-11-10：退出尾诊断已接入DarwinBoot/1128B独立ADT/XNU/raw日志和
双FFS打包。完整本地loader29696B/SHAe0563bc54f783e85...、shared core
422400B/SHAb7a97cd5239d8cbc...；实际loader/Core Bind/EBS/Copy及O0/O2
sanitizer通过，loader native保留基线一map warning，无新增warning。
FD3145728B/SHA51f56f1eedbf69be...，197 inner中仅DarwinBoot/Core变，UI/
其余sections/shim/DTB/FAT/touch参数保留；合成kernel打包及独立拒绝测试
通过，非启动候选。实际XNU helper/独立raw analyzer及四步preflight通过。
新XNU完整编译/链接待派发，旧CI42819762105无tail hook，不得混用。详见
rpmh-exit-tail.md及local-build.md。正常手机保持，无boot/重启/刷写。
下一个真机点为一次集中退出尾/只读GPU诊断，不再重复触摸/SCM/memory轮；
PAS13=false/supported=false/CP halt不绕，GPU scratch/fence/Metal仍未执行。

最新2026-11-10 21:35：BCM/ARC完整XNU CI42819762105 @02f4077成功，内核/
日志已下载并独立验收PASS：17451096B/SHA a0170217a28daa09a639185135a74bfb
5315d87560fa9c822289e99ce6d56611，86 core/22 memory、无unresolved及source/
touch/PA64/native SCM核验通过，receipt out/kernel-verification-ci-42819762105.json。
旧aa585f5不是本批。手机/配对版不动，无打包/boot/新测试点。
新增隔离实际DXE Core allocation/free/page-conversion生命周期记录，精确core
请求字节/type/generation、free读头前失效、ABA拒绝/溢出sticky fail，直接页
free/attr变更失效，不改分配器结果/保护语义。完整本地AArch64 core204288B/
SHA5e515f548219...；实际Pool.c/ConvertPagesEx、234 alloc/233 pool free/40页，
O0/O2 ASan/UBSan/PROT_NONE/guard/OEM/collision边界PASS；完整native Pool分析
无warning，Page与原基线同两dead-store，不冒称全无warning。receipt
out/pool-lifetime-core-test-20261110-accepted.json，详见pool-lifetime-core.md。
仍无final collector/全球writer/最终票/ACK/生产owner，pre-EBS metadata query
不是读后lease，旧phase1 snapshot限制不变；无固件源码树改动/FV/默认打包。
下一core-owned final collector的live-map/属性/冻结生命周期及writer边界，再
真实ledger/owner；PAS13=false/CP halt不变，GPU scratch/fence/Metal未执行。

最新2026-11-10 20:49：新增实际sweet_rpmh固定GPU路线BCM ACTIVE raise，
仅SH2/SN15/SN3/SH0/MC0/ACV，真实DB地址/单位/宽/VCD、14bit X/Y只升、
ACV0->8；稳定VCD/commit/response/wait、新IRQ+每cmd issued/所需complete。
与ARC复用真实锁/poison发送器，逐访问前后owner/DB失败锁存，八bank BCM
冲突及mask后二次拒绝；29写严格序列/1977 PROT_NONE owner/ARC153失效点、
ASan/UBSan/原生完整RPMh/staged-only/静态分析PASS。receipt
out/bcm-active-local-20261110-final-v2/；最终四步preflight
out/bcm-active-preflight-20261110-final-v2/状态以receipt为准。
生产owner/最终继承票/ACK仍无实现，不用空TCS/软件snapshot冒充；详见
bcm-active-backend.md。新生产代码尚无完整XNU编译/链接，不用aa585f5核
冒充本批。无手机操作/镜像/启动hook/新测试点，正常SCM配对版保持；
PAS13=false/supported=false/CP halt不改，GPU scratch/fence/Metal未执行。
下一真实ledger/全局writer及deep-idle协调，剩余clocks/GDSC/PAS13/PA/
crypto/SMMU与parent测量准入仍缺。

最新2026-11-10 20:19：完整XNU CI42810544983 @aa585f5于20:05:32成功，
产物/日志已下载并本机独立验收PASS，kernel17450712B，SHA256
dd7d7339da56279976d8ca881a26419d943ccb3b6e5a6c2a14ef37c9128b4a83；
85核心入口/22内存API、无unresolved、生产source/touch/PA64/SCM capture通过。
receipt out/kernel-verification-ci-42810544983.json，日志../logs/
clock-measure-ci-42810544983/。以下19:45待构建记录为历史，旧失败保留。
随后仅改测试/文档，六固定地址/固定计数/精确频率及一致性边界独立验证，
208 lease/15 store变化、ASan/UBSan/原生/staged/静态分析通过，receipt
out/clock-measure-independent-20261110-v2/；四步preflight/68 host/7生产
静态分析PASS，out/clock-measure-independent-preflight-20261110/。
初次独立测试FAIL为预期值差1Hz，修正测试常量，生产代码未变；接受核仍绑定
aa585f5，不重跑完整云构建。无手机/打包/boot/新测试点，正常配对版保持。
下一真实owner/最终ARC票及ACK/资源协调，再闭合其余clocks/GDSC/PAS13/PA/
crypto/SMMU；PAS13=false/supported=false/CP halt不绕过，GPU/Metal未执行。

最新2026-11-10 19:37：新增实际GPLL0分频源counter测量入口，与父时钟同映射/
锁，固定GCC selector167/短1000+全10000，fresh DONE/稳定读回/最短窗口/
25ms poll及100ms整体/独立次数界。干净完成恢复原控制/门/分频/mux字段，
未知/异常不恢复并保留；不写只读count/PLL/GPUCC。208 lease/15 store变化、
过早/旧/卡DONE、倒退/停计时、原值恢复、ASan/UBSan、原生/staged编译与
静态分析通过，最终receipt out/clock-measure-local-20261110-final-v2/。
四步preflight out/clock-measure-preflight-20261110-final-v2/验收以receipt为准。
返回相对nominal XO原始频率/量化界，不自动凑300MHz；无真机频率/owner，
未塞入禁止失败前写硬件的power.prepare合同。详见clock-measure-backend.md。
本轮无手机操作/镜像/新测试，PAS13=false/CP halt不变，GPU/Metal/认证未执行；
完整XNU编译链接待开始，正常SCM配对版保持。
19:45更新：6482292已提交推送，CI42809502486失败于macOS ASan不支持
detect_leaks=1，未进入XNU构建；不是完整build PASS。仅Darwin关闭leak检测、
保留ASan/UBSan与Linux leak，生产代码不变。新构建待派发，旧失败保留。

最新2026-11-10 19:22：新增实际GCC映射/原生delay的GPLL0父时钟保持接口，
仅已校准FSM/ACTIVE/LOCK/RUN稳定PLL，apps bit0及GPU分频门bit16只置位，
500us等待、逐访问owner、共享字变化拒绝、异常保留/终止不重试；无PLL配置/
校准/启动hook。configured_div_hz为寄存器推算，不能满足实测parent合同。
最终receipt out/gpll0-local-20261110-final-v2/receipt.json，四步preflight
out/gpll0-preflight-20261110-final/receipt.json；验收状态以receipt为准。
详见gpll0-backend.md。真实owner/频率测量仍缺，无手机操作/打包/新测试点，
正常SCM配对版保持。PAS13=false/supported=false/CP halt不变；GPU scratch/
fence、Metal与认证未执行。本批完整XNU编译/链接尚未开始，不能冒称新核。

最新2026-11-10 18:50：实际RPMh只读采集构件完成并本地验收。DAL slot90/98
确认为type4 UEFI pool/free，旧数组resize释放风险留证。因EBS callback后
池页存活/guard/unmap未闭合，最终实现收窄为final map后、EBS前采集，phase1
显式诊断，EBS后仅seal/copy；不能当最终投票/ACK/owner。四PE text重定位
绑定、当前数组/池头尾/环/变化/33失败读、实际staged ADT/adapter/完整复制
夹具、ASan/UBSan/静态分析PASS，本地完整固件336896B/SHAdb654314c91a...。
receipt out/rpmh-snapshot-local-20261110/accepted-receipt.json，build
out/rpmh-snapshot-pre-ebs-accepted-20261110/，详见rpmh-snapshot.md。
默认SCM stage不变，无XNU修改/打包/手机操作/新测试点。下一最终RAM生命周期/
唯一writer及生产owner，PAS13/PA/crypto/SMMU仍缺；GPU/Metal/认证未执行。
18:03计划中的post-EBS遍历已由本节限制取代，不boot此独立构件。

最新2026-11-10 18:03：ARC完整XNU CI42792610794 @bba0314于17:51:44成功，
本机独立验收PASS：kernel17433272B/SHA00abd9206e8b...，82核心入口/22内存API/
无unresolved、全部生产source及touch/PA64/capture核验。receipt在
out/kernel-verification-ci-42792610794.json，日志../logs/arc-active-ci-42792610794/。
无打包/boot/手机操作，不能作为GPU执行版。固件finish_amc会继续提交队列，
不是全局排空；资源数组resize/排序移动使pre-EBS getter指针不能直接继承。
109条会话bundle/snapshot已保存并关闭REA，详见rpmh-firmware-evidence.md。
下一绑定最终数组/有效RAM生命周期，再做真实只读handoff；不是补一个成功
lease。生产owner/继承ACK/PAS13/PA/crypto/SMMU仍缺；配对版保持，GPU scratch/
fence、Metal、PAS认证未执行。以下17:40的PENDING为历史，已由本节成功取代。

最新17:40：ARC完整CI42792076835失败于macOS RAM fixture的__unused重定义，
未进XNU编译。bba0314加宏保护并针对Linux/模拟Darwin/ASan+UBSan通过；
新CI42792610794 @bba0314已启动PENDING。旧失败不追认PASS，完整核/链接待
确认。真实owner/继承票/PAS13/PA/crypto/SMMU仍缺，手机不动，GPU未执行。

最新2026-11-10 17:32：新增实际sweet_rpmh翻译单元的ARC ACTIVE raise通道，
共用触摸rh_lock/rh_poisoned；只升CX/MX合法档位、八bank冲突拒绝、fresh IRQ/
issued+complete ACK、逐访问owner校验及异常保留。70 lease/14写入故障、
ASan/UBSan、66 host、原生AArch64编译/静态分析和本机四步preflight PASS，
final receipt在out/arc-active-local-20261110-final/。local/CI copy/stage/
kernel符号核验已接通，详见arc-active-backend.md。真实owner/继承票仍缺，
不是电源后端完成；完整XNU构建/链接尚未跑，无启动hook/镜像/手机操作或新
真机点。正常配对版保持，PAS13=false/supported=false/CP halt不变，GPU
scratch/fence、Metal和PAS认证未执行。下一真实资源交接/owner绑定。
17:37代码7c80907已提交推送；完整XNU CI42792076835同commit已启动，source
stage PASS，构建/链接PENDING。gpu-work干净，自有exec已关闭；手机不动。

最新2026-11-10 17:11：PAS13内存合同收窄。固定descriptor flags20c=0/20d=1，
默认mem_setup跳过一般load-region登记，auth不转换权限而检查已有页mask，
HLOS mask8为subset候选；撤回PAS13必需OEM carveout/无条件hyp_assign推断。
25 raw/634指令/17分支本机核验普通/-O通过且receipt一致，112条bundle及
snapshot私有保存。运行TZ/flags/DDR/权限与MDT reader/cache仍未知，不能
视为lease或PAS准入。详见tz-pas-memory-contract.md。c57c2d2生命周期批
已推送。无生产后端/新核/构建/手机操作/新GPU测试点，正常配对版保持；
PAS13=false/supported=false/CP halt不变，GPU/Metal/PAS认证均未执行。

最新2026-11-10 16:48：TZ缓存生命周期离线续查完成。shutdown/cleanup已分析
本体清state8B/prefix0x1e0，不直接清ID/cache；间接及未展开callee副作用仍
未知。受限lookup新增cache有效state0/5、空cache state5三假设，均true；
11场景两模式通过，17守卫及14映射测试普通/-O通过。两replay-v4 case一致，
LLVM listing临时路径导致完整receipt不一致，不冒称相同。23条REA bundle/
snapshot保存私有logs，详见PAS后续文档。无新核/构建/手机操作或测试点，
正常配对版保持，PAS13=false、supported=false/CP halt不变；GPU/Metal/认证
未执行。下一转PAS内存准入/XPU范围与实际资源接管，不重复查询/触摸轮。

最新2026-11-10 15:35：Clock两层client/BCM聚合与accepted DxeCore EBS
派发顺序离线原件核验。节点两桶type0x1400/其他各max IB+sum AB，BCM各
max归一节点而非再sum路径；active/vector及sorted clock_id队列已留证。
mode/溢出/完整client活态未知。accepted DXE core119808B/SHA311a20a5...
与本机RELEASE逐字节一致，非DEBUG；先map校验再EBS，高TPL先/同TPL通常
注册逆序，同步callback非硬件ACK/唯一owner。当前与staged DarwinBoot
源码不同，后者EBS成功后仅SCM handoff→DarwinEnter；其实际入口仍待绑定。
crypto31/交接6项测试普通/优化通过，两原件receipt各自两模式一致；NPA8/
ICB24通过，clock-resource-handoff-v1/dxecore-resource-handoff-final证据
与snapshot保存私有logs，详见RPMh文档。15:08批b23e4ec已推送。本轮无
生产后端/内核/构建/手机操作/新GPU测试点；下一最终writer/client/ARC/
XNU owner，PAS13=false/PA/XPU/crypto clocks/SMMU仍缺，正常配对版保持，
supported=false/CP halt不绕过，GPU/Metal/PAS认证未执行。

最新2026-11-10 15:08：crypto六BCM与动态参数来源已离线绑定原件：CE0/SN11/
SN0/SH0/MC0/ACV（vector kind2）；DAL初始command/width/unit/clock_id均0，
非活票。Clock通过CmdDb slot0x38(aux8B)/0x10(address)填参数；accepted boot
第11模块device CmdDbDxe逐字节相同，初始化从0x0c3f000c读32位指针并核验
DB version1/magic0x0c0330db。只读文件，未读手机寄存器/RAM。Clock换算是
ceil两级缩放、IB低14/AB高14/valid29/commit30，和Android floor/minimum1
不同，不混用。crypto核验v2两模式PASS一致，共26合成测试普通/优化通过，
NPA8/ICB24通过；dal-crypto-bcm-v2/clock-crypto-bcm-final/cmddb-crypto-bcm-v2
bundle/snapshot及v2 receipt保存在私有logs，详见RPMh文档。14:48批9088461
已推送。仍需完整client聚合/最终EBS/XNU owner、PAS13支持、PA/XPU/crypto
clock/SMMU，无生产后端/内核/构建/手机操作或新真机点，正常配对版保持；
supported=false/CP halt不绕过，GPU/Metal/PAS认证未执行。

最新2026-11-10 14:48：具体crypto ICB路由与Hash ABI已离线原件核验。
DAL topology DRV2/tree八节点→四pairs：crypto→a2noc_snoc、aggre2→gemnoc_sf、
snoc_sf→llcc、llcc_mc→ebi；Hash生成master23/slave0、type0x10004的32B请求，
IB1600000000/AB55250000/latency0，与Clock解析偏移一致。固定SDM670参考
结构/ID/单位只作关联，非构建来源或活票/ACK。新增独立crypto verifier及16项
普通/优化合成测试通过；v1两模式原件receipt完全相同，ICB24/NPA8通过。
三final REA bundle/snapshot及v1 receipt保存在既有私有logs目录，详见RPMh
文档。另存有界NPA core init/PMIC动态/pm/命名证据，不据负检索断言ARC不存在。
下一crypto BCM/提交聚合/完成/最终EBS与唯一XNU所有者；PAS13=false、secure
PA/XPU、crypto生命周期/SMMU仍缺，无生产后端、新核、构建或手机操作，无新
GPU测试点。正常配对版保持，supported=false/CP halt不绕过，GPU/Metal/认证
未执行。14:17批2ee84d6已推送；本节为后续追加。

最新2026-11-10 14:17：DAL ClockRailConfig属性index1→0x1f230四16B记录，
CX/GX/MMCX/MX默认均0x100；Clock0xaf5c读配置，0x6bec存rail+0xdc并注册
ResourceAvailable callback0x6a4c。这是消费者默认配置，不是当前票或ARC
注册证明，不填current_cx/mx。v6原字节核验两模式PASS且相同，24合成测试
两模式通过；dal-clock-rail-config/clock-rail-consumer-final bundle/snapshot
与v6 receipt保存既有私有logs目录，详见RPMh文档。14:08批a6a332e已推送。
仍需实际ARC注册/其他EBS写入顺序/最终快照和唯一XNU所有权，PAS13=false
根因未解；无生产后端、新核、构建或手机操作，无新GPU测试点，正常配对版
保持，supported=false/CP halt不绕过，GPU/Metal/认证未执行。

最新2026-11-10 14:08：原件补证RPMh issue slot0x78→0x2cdc共同返回内部
隐式churn（handle/request_id/1/1），pending轮询HAL IRQ并处理完成；外层
barrier不支持不能再推断整链缺少等待。找到一个EBS回调0x149c：卸载RPMh
后0x38f4清配置DRV/TCS的control与command enable（0xd14/0xd1c或+0x20000），
无完成调用；不是电源票清除证明，也未穷举其他EBS写入/顺序。原字节核验
v5普通/优化receipt相同且PASS，21合成测试两模式通过，NPA8通过。证据
rpmh-implicit-churn-final/rpmh-ebs-final bundle/snapshot及icb-registrar-
verification-v5位于既有私有logs目录，详见RPMh文档。无内核/boot/手机操作，
无新GPU测试点。实际ARC/最终交接/XNU所有权仍缺；正常配对版保持，
PAS13=false、supported=false/CP halt不绕过，GPU/Metal/认证未执行。

最新2026-11-10 13:49：本机离线连通ICB注册链。patched DAL /icb/arb属性
icb_info索引20→/icb/arbiter node/resource，实际Clock入口0x1a74→0xc520
设置driver/plugin并经NPA定义；plugin按32B请求解码并提交RPMh。node driver
仅返回requested_state；条件barrier_all调用的本机RPMh slot0xb8直接返回
EFI_UNSUPPORTED且Clock忽略结果，不能当hardware ACK/完成屏障。已保存
dal-icb-config/clock-icb-barrier/rpmh-barrier-slot/limits-resource证据和快照。
10 boot模块字节一致、ICB原件核验PASS，13合成测试正常/优化模式通过，NPA
8测试通过。receipt icb-registrar-verification-v3、npa-state-verification-v5。
详见RPMh文档。尚缺ARC注册者/真正完成/EBS/XNU所有权，无生产后端、新核/
构建/手机操作/测试点；保持正常配对版，PAS13=false/CP halt不变，GPU未执行。

最新2026-11-10 13:32：离线资源续查，8个boot模块PE身份核验通过（增加
Env/Pmic/Epsilon Hash/Cipher），receipt npa-state-verification-v4，8合成测试
通过。实际Hash为Epsilon SM8150版，不是cepheus目录版；/clk/ce1是
/icb/arbiter向量client名，不是直接时钟API。请求布局/单位、仲裁器driver/
完成/EBS/XNU所有权未建立；Env该路径为ICB消费者，Clock两helper为CPU表/
模式设置。新增env/hash-selected-vector/clock-target-followup证据已保存，
详见RPMh文档。仍无生产资源后端、无新内核/构建/手机操作或测试点。
手机保持可用配对版；PAS13=false根因和shared资源接管仍缺，GPU/Metal未执行。

最新 2026-11-10 13:24 +08:00：本机离线推进，无新手机操作/SMC/内核改动。
新增有界PAS13原指令回放，12段219条静态指令与原生Ghidra/LLVM一致，8场景
通过、16项无私有材料守卫测试通过。原表空cache两记录MPIDR均true，count/ID
假设缺项可false，非零cache不复查，持锁达10万条未返回不归false。
不能据此确认手机发生修改；运行TZ/EL2/dispatch未知，真机两段false未解。
证据../logs/pas13-instruction-replay-20261110/replay-v3.json，详见PAS后续文档。
NPA默认CURRENT_STATE确为软件resource+0x34读取；NpaDeinit仅卸载协议，非
全局完成/资源交接屏障。四已验收boot模块身份逐字节核验，CPR为Cambria版
不是cepheus目录版。129 plain DXE PE内字节检索及Clock消费者/Pmic动态节点
链已保存，实际CX/MX driver/完成/最终EBS仍未知。8项PE/检索合成测试通过。
证据npa-state-verification-v3.json及5组REA bundle/snapshot，详见RPMh文档。
保持可用配对版及supported=false/CP halt门禁，不追加控制/查询/内存轮；
GPU scratch/fence/Metal/PAS认证未运行，无可boot GPU执行候选。

最新 2026-11-10 12:33 +08:00：配对诊断真机完成。67eea163/cepheus 已解锁，
12:27:29 单次临时 boot 成功，无刷写/自动重启/恢复/重试。镜像仍为
out/scm-pair-final-6ce4c7c/boot-scm-pair.img（SHA2c93dc333366...）。
生产日志核验 PAIR_COMPLETE：UEFI/XNU 各7次，CPU/in/out/end齐全，handoff
checksum通过；两段availability0x1f，PAS13输入0x42000207:1:13均返回8个0，
即接管前已false、XNU仍false，不必以XNU接管后才变化解释本轮结果。
两段EL0x4，UEFI MPIDR0x81000000/XNU0x81000200；运行TZ身份、EL2介入、
descriptor/cache与启动历史仍UNKNOWN，不能据此绕过supported=false。
用户“正常”确认主屏幕/触摸/熄屏唤醒，观察于12:30:55之前且采集仍在；
查询后5条health events3→189/HID184/errors0，有blank/display-on。scheduled
已捕获，但UDP接收时戳不证明90s，logger_delay_verified=false保留。
12:30:56主动signal收尾245.090645s/300s上限、396包/143442B/拒绝0；
RAW128中30整批124事件重放0差异，末尾4项不足整批省略。host RX错误/丢弃0，
capture/watch已退出，手机保持本轮可用配对版。日志SHAd064993a1781...，
完整证据../logs/scmpair-6ce4c7c-live-20261110-1226/result.json及scm-pair.json。
下一离线查接管前secure/启动环境与独立资源后端，不重复同查询/控制/内存轮。
PAS13=false根因未解；GPU/Metal及PAS认证未执行，平台所有权后端仍缺。

最新 2026-11-09 22:51 +08:00：已到 UEFI/XNU PAS13 配对诊断的单次真机测试点。
CI42632917379 @6ce4c7c 完整成功；kernel17449368B/SHA256
a2a4837d80a17dfcc2dc8fa5847d03308079cc0c691f7045dfba1e86a7b66c2e。
81 核心入口、22 内存 API 已链接，无 unresolved，源码/触摸/PA64/实际 CPU
采集与 preemption/SMC callback 核验 PASS；receipt 为
out/kernel-verification-ci-42632917379.json，完整日志在
../logs/scm-pair-ci-42632917379/build.log。固件、FV、FAT/boot 在本机完成，
四步及独立封装核验全部 PASS；真实候选 out/scm-pair-final-6ce4c7c/boot-scm-pair.img，
70688768B/SHA256 2c93dc3333665bdbc8f1c835c0e59226f5b2a0cfda2678d4bb121a3fc86903d2。
197 内部文件仅 DarwinBoot 变化，实际 EFI 与编译件一致，shim/DTB/无关 FAT
文件不变；cephzapmem=1 仅换为 cephscmpair=1，新增 2656B 零 ADT 槽。
本地 v4 前置（65 host/7 静态分析）、9 组日志测试、打包 fixture/10 拒绝项通过。
未 boot 或操作手机；手机保持已验收 zapmem-b5006b5。下一仅一次临时 boot、
有界日志采集及简短点按/唤醒确认，不重复控制/触摸/内存轮。
PAS13=false 未解，真实资源/crypto/secure PA/XPU/共享电源/SMMU 后端仍缺；
GPU scratch/fence 和 Metal 未执行，此候选不启动 GPU。
详见 [本地构建](local-build.md)及[配对诊断](scm-pair-preflight.md)。
历史 e5e7104 的 CI42627950109 前置失败保留；最终真实候选不是合成 fixture。

更新：**2026-11-08 20:25 +08:00**。范围：原生PAS生命周期已接实际内存，完整XNU构建成功；尚无GPU执行候选。
这是当前状态总览；各专题文档保留按时间追加的历史记录。

本轮新增固定PAS13 load/stop/status，复用串行原生SMC和本次启动的查询缓存；
不会重复查询或覆盖supported=false。实际IOKit pair独占claim、暴露前保留、
mem_setup后重新复制image且不改MDT、auth双层返回检查、同epoch GPU quiesce及
PAS6成功后释放均已接入；错误/中断不确定时保留。24种真实固件+RAM transport
场景、5种CPU工作区相关回归、9项staging、image/查询门禁、native AArch64
编译、两修改单元静态分析PASS，证据../logs/zap-pas-backend-20261108/receipt.json。
该receipt的6项源码哈希属于93093d3，其构建PENDING字段保留为当时状态；
a2c14ba仅修复INFO/PAS固定日志literal及对应codegen断言，CI42476845818已
完整构建成功。内核17449096B，SHA256
af9729b1f8b3df8f13915d6e1a1f6bd79bfc1e60d6e6845ddbafe87611faa65e，位于
../inputs/ci-42476845818/kernel.macho；81核心入口+22内存API已链接、无
unresolved，staged source、INFO/PAS日志标记、触摸有序上报及PA64 cache
callback核验PASS。后续证据为同目录build-verification.json、run.json和
build-followup.json。无手机操作或新的SMC，未打包/boot；资源acquire/lease/
quiesce后端、crypto带宽、secure PA/XPU及电源/SMMU接入仍缺，PAS13=false未解，
无GPU启动hook。详见[zap-platform-contract](zap-platform-contract.md)。

以下19:14为上一轮已完成的上电适配层构建记录。

用户要求快速推进GPU实测，不重复控制/触摸/内存轮。目标为GPU写scratch并完成
fence；目前仍缺真实共享资源接管、PAS认证/停止及SMMU平台接入，不能称只差
打开启动开关。手机保持已验收zapmem-b5006b5，无新设备操作。

本轮修正ceph_power_start在CX/时钟关闭时读取GMU CM3寄存器的问题：改到CX和
全部访问时钟开启后再确认reset==1；失败保留已发生的资源副作用。新增
cepheus_gpu_power_mmio.h，固定GCC/GPUCC/GMU窗口、访问时钟门禁、具名寄存器
及受限写掩码，实际volatile访问和arm64屏障；无映射/电源投票/所有权获取或
启动hook。空lease不允许通过，不能当作完整平台后端。新增两个链接入口，
staging/CI/verifier已接入。0e9b3ea完整XNU构建CI42473371718成功，内核
17415800B/SHAfa0b719f...；78核心入口/18内存API、无unresolved、staged source、
触摸及PA64核验均通过。产物../inputs/ci-42473371718/kernel.macho，证据
../logs/gpu-power-backend-20261108/build-verification.json。未打包/boot，
手机旧内核未包含改动。

针对性ASan/UBSan覆盖断电PROT_NONE区域、完整寄存器事务、196个lease失效点、
超时/异常复位及非法写；真实A640 GMU材料的4种相关bringup场景、9项集成、
下游CM3偏移比对和两个生产单元静态分析通过。未重跑完整离线入口；证据
../logs/gpu-power-backend-20261108/receipt.json。固件静态分析找到资源记录getter，
但仍是缓存且可能在TCS触发前更新，不能当已生效投票；详见
[RPMh补证](rpmh-firmware-evidence.md)。后续要建立UEFI到XNU的可信资源交接，
不是再发同样只读查询或把current_cx/current_mx填零。

最新：用户“已进入”后核对67eea163/cepheus，候选zapmem-b5006b5临时boot成功。
18:05:13收到六条有序执行成功记录：MDT16384B/PA0x184398000/attr0，镜像
1MiB/PA0x185d00000/attr0x44；4+256页翻译、pattern PASS、MDT恢复attr0xff、
end result0/retained_pages0。scheduled初始标记缺失，原严格七条判定NOT_PASS，
未补造日志或放宽标准；90秒时序不能验收。用户回复“正常”确认主屏幕/触摸/
唤醒PASS，主机观察18:16:04，晚于采集结束，不冒称操作已同步采集。
预检后9条health末events3。用户要求尽快推进GPU实测，不再重复控制或内存轮。
采集18:07:46自然结束300.000947s/340包/91661B，SHA479b2f82...；主机RX错误/
丢弃0，自有capture/watch均退出。无自动重启/恢复/重试，手机保持候选运行。
证据../logs/zapmem-b5006b5-live-20261108-1802/result.json，详见
[ZAP真机记录](zap-memory-preflight.md)。GPU/Metal未执行，PAS13=false仍未解。
以下为离线准备及更早记录。

已到单次真机预检点：MDT强序映射、逐4KiB AT/PAR检查、Device字节复制、释放
前Copyback及新旧别名验证已接入，11种无固件预检和5种真实ZAP工作区场景、
共享内存回归及相关门禁/打包/codegen/日志检查通过。新增cephzapmem=1独立
90秒后单次CPU预检，不跑SoC/SCM/GPU。99c6dba补齐显式prefault后通过针对性
host与静态分析；b5006b5修复CI下载KDK错误页的问题，生产代码未再改动。
CI42467907788完整成功，kernel17415576B/SHAf1b5a674...，76核心入口/18内存API、
无unresolved、staged source及触摸/PA64核验PASS。候选
../dist/boot-cepheus-zapmem-b5006b5.img为70680576B/SHA1f986169...，相对同核
控制版严格只加cephzapmem=1；新控制版相对已验收b2b99e7只换内核，配置不变。
全部封装核验通过，两个新镜像均未boot，不追加控制轮；手机未操作，仍保留
上一已验收查询版。下一仅需一次预检后简短触摸/唤醒确认，GPU/Metal尚未执行，
PAS13=false仍未解。下面为此前阶段记录。
详见[zap-memory-preflight](zap-memory-preflight.md)，证据../logs/zap-cache-20261108/。

最新离线实现独立ZAP工作区：1MiB连续/PA对齐镜像+16KiB MDT预算，固定快照
持有到复制完成、全容量清零、只读lease、与GPU池分离的handle和预算、局部
失败清理及complete失败永久保留。5种真实材料场景ASan/UBSan、相关GPU内存
host套件、9项integration、PA mask及修改C++静态分析通过。CI42459697884
@1b34034完整成功，kernel17431888B/SHA4ef4961a...，76核心入口/17内存API、
无unresolved、staged source和触摸/PA64核验PASS；产物
../inputs/ci-42459697884/kernel.macho，未打包/boot。
证据../logs/zap-memory-20261108/；普通Normal-NC内存仍没有secure/XPU保证，
禁止送PAS。下一缺口为secure内存/cache合同、crypto资源及真实认证/停止后端，
同时GPU电源/SMMU所有权未完成。没有新SMC或手机操作，不追加触摸轮。
缓存源码核对发现kIOMapInhibitCache对DRAM未必给出原厂MDT的nGnRnE；
已记录VM_WIMG_RT的候选分支及后续需核对的配置/页表/别名/复制约束。

最新离线153561e已补齐ZAP ELF不可变快照及内核MDT/重定位镜像准备。
本机MDT6860B、加载段1968B与原件逐字节一致，逻辑镜像4096B、未来分配需求
1MiB分开保留；不提供PAS PA/XPU保证、不认证、不置zap_loaded。ASan/UBSan
相关内存生命周期及新路径、9项integration、修改生产单元静态分析通过；
未重跑完整离线入口或追加手机测试。CI42457537126 @153561e完整成功，kernel
SHA763d274e.../17414848B，76核心入口/13内存API链接、无unresolved、staged source/
触摸/PA64核验PASS；产物../inputs/ci-42457537126/kernel.macho，未打包/boot。
手机仍运行已验收b2b99e7查询版，证据../logs/zap-staging-20261108/。
详见[ZAP准备实现与缺口](zap-platform-contract.md)。

最新query7-basic-b2b99e7（SHAa060bd43...）临时boot后，sample25四组及七次
REGS in/out/summary/QUERY7/end均齐全。生产策略核验TRANSCRIPT_COMPLETE，
command7 availability=0/1，QUERY7 confirmed=1，PAS13 supported仍0/0=false。
convention2/available0x1f/calls7/transport0/secure0/e-1；不代表认证失败或成功。
用户“正常”确认查询后触摸/唤醒PASS；查询后9条health events78增至170/errors0，
电源、blank/display on及后续触摸事件收到。RAW128，48完整批123事件0差异。
420.000939s/495包/164342B，日志SHA12dda1d3...，采集14:43:41自然到期，
回复观察14:43:57，未实际提前停止。自己的capture/watch均退出，手机保持运行。
证据../logs/query7-basic-b2b99e7-live-20261108-1436/result.json及scm-query.json。
用户时间紧，本轮不追加真机测试，不重复控制/完整离线；下一离线重点为
GPU电源/共享资源、SMMU及PAS加载后端和false差异，不放宽门禁。
GPU/Metal尚未运行；本轮排除command7不可用，触摸间歇和PAS13=false根因未解。

前一14:27基础控制轮soccontrol-b2b99e7本轮PASS：用户“都正常”，420s648包/
186771B，sample25四组齐全且之后触摸events317增至890/errors0，HID69对。
日志SHA2a4a5873...，证据../logs/soccontrol-b2b99e7-live-20261108-1426/。
自有采集/watch均退出。用户明确时间紧、尽可能省测试：不重复控制/离线回归，
查询出完整结果再一次简短点按/唤醒确认即收尾。配对query7-basic随后已完成，
见上述结果，不再重复控制。

最新指示“直接下一阶段吧”：转入基础SoC采样后的SCM/PAS13固定查询准备，
不再以补跑240s时序对照阻塞下一阶段；历史缺证据仍不追认PASS。
复用CI42450429814/b2b99e7 kernel SHAe9f8f3f7...，两张同核镜像已打包、
严格delta核验：soccontrol-b2b99e7 SHAb239af44...（只加cephsoc1，之后本轮PASS），
query7-basic-b2b99e7 SHAa060bd43...（相对控制只加cephscm1）。
默认45s基础等待/sample25后查询，不带fuse/ARC/GX/boost/memory/240s延迟。
新增基于生产策略的RAM日志核验13项、参数15项、时间线6项测试通过；
完整离线38/0/0，receipt ../logs/gpu-offline-query7-basic-20261108/。
新基础控制及配对查询现均已完成，手机保持查询版iOS运行。
GPU/Metal尚未运行，细节见[SCM查询结果](scm-query7-preflight.md)。

最新14:02轮：同核延迟`socdelay240-b2b99e7` SHA7e082de6...临时boot成功。
用户“现在正常”及两次复测“正常”确认当前UI/中央触摸/电源唤醒触摸PASS；
确认正常控制iOS经物理电源+音量下进fastboot，初始化等待未碰屏幕。
但USB14:04:28及14:07:24重新枚举，末UDP14:04:28，随后0有效RX且错误
增长；用户未主动拔插、怀疑线材，原因UNKNOWN。执行环境重启后capture
signal提前结束469.53s/600s，314包103834B，日志SHAe89b06c2...；自有
capture/watch均退出。无scheduled/begin/baseline-end/sample25，**阶段
对照INCONCLUSIVE，实际SoC执行UNKNOWN**；初次回复观察晚于末UDP，
不确认探测前PASS。RAW128早达限，40完整批126事件重放0差异；相关16测试
通过，单条health早于中央动作。证据`../logs/socdelay240-b2b99e7-live-20261108-1401/`。
保留当前可用手机，不自动重启/恢复；当时建议稳定数据线后补跑，用户后续
改为下一阶段，见上。GPU/Metal仍未运行，根因UNKNOWN。

前一真机13:52轮：新核`touchcontrol-b2b99e7`临时boot，用户“都正常了”确认
主屏幕/中央拖点/边缘/双指/电源熄屏唤醒触摸PASS。前序为失败iOS经物理
电源+音量下进fastboot，未报告中途Android。420s589包/157985B，日志SHA
d0722828...；last health18000/events1179/hid_calls1172/errors0，26完整批/
120事件重放0差异（RAW预算128尾批省略）。采集与自有watcher已结束，
ECM仍在，未自动重启；当时延迟候选未启动，之后本轮结果如上。
证据`../logs/touchcontrol-b2b99e7-live-20261108-1347/`。间歇根因仍UNKNOWN。

最新离线推进：新增固定`cephsocdelay=240`基础探测门禁及scheduled/begin/
baseline-end内核uptime标记，默认45s不变；不新增硬件操作。生产线程host
测试、4项封装门禁、14项参数及原SCM测试通过，完整离线37 PASS/0 FAIL/
0 SKIP，提交b2b99e7干净树重跑同样通过，receipt
`../logs/gpu-offline-socdelay-b2b99e7-20261108/receipt.json`。
CI42450429814 @b2b99e7完整成功，kernel SHAe9f8f3f7...；新核touch-only
`boot-cepheus-touchcontrol-b2b99e7.img` SHAafdf72c6...，同核延迟版
`boot-cepheus-socdelay240-b2b99e7.img` SHA7e082de6...，严格配对核验通过。
当时两者未测；之后新核控制PASS、延迟版当前体验PASS但阶段对照无结论。
流程见[soc-delay-preflight](soc-delay-preflight.md)，根因仍UNKNOWN。

最新12:35轮：`soconly-ordered-60a2dc5`临时boot，触摸FAIL，用户确认中央
操作已做、主屏幕/电源正常，选择“保留当前现场，先离线排查”。420s452包/
123804B，19条health至19000保持events6/hid_calls2/errors0，仅1079,0边缘
enter/leave；sample0..25四组齐全、GMU跳过，无SCM/GX/fuse/ARC/boost/内存。
日志SHA `a929e459341d37375bd4f72452b5a9e98baa3f8a9452668b8f03713adf1ee566`，
证据`../logs/soconly-ordered-60a2dc5-live-20261108-1235/`。当前ECM仍在，代理
未重启/恢复；停止后续GPU探测。6事件生产重放0差异，批内有序上报没有
解释无中央FIFO。离线包时间线4项测试通过，生产kernel/镜像未修改。
**根因仍UNKNOWN，不根据单次跨启动对照认定cephsoc单项因果。**

先前01:10轮：`touchordered-60a2dc5`临时boot本轮用户确认“好了，都正常”，主屏幕/
触摸/电源熄屏唤醒后输入PASS。日志575包/161064B，最后health events1148/
hid_calls1101/errors0；完整身份见gpu-acceptance和
`../logs/touchordered-60a2dc5-live-20261108-0111/result.json`。间歇根因仍未知。
采集尾01:15:47 ECM断开后见Xiaomi MTP/ADB，原因待确认，代理未重启。

用户要求继续至下一测试点：同kernel的`soconly-ordered-60a2dc5`已打包并
严格核验仅新增cephsoc=1，11项参数回归通过；SHA
`5d7e64ae5740ad4ab82e2e6ef614e537bd344e483b77aa44f0470cc1f8be8a24`。
该候选随后已测试触摸FAIL，不作为可用基线。详见
[同核SoC基础预检与结果](soc-ordered-preflight.md)及
[触摸理论最新排查](touch-failure-theory.md)。下面保留各阶段历史上下文。

前一离线阶段（历史）：用户要求“继续推进，直到需要在真机测试”。已实现整批先验证、
按FIFO顺序上报的独立修正；11项重放测试及生产FIFO故障测试通过，10份
保存RAW与顺序模型0差异。完整离线回归34 PASS/0 FAIL/0 SKIP，
receipt为`../logs/gpu-offline-touch-ordered-20261108/receipt.json`；
XNU CI42391773972 @60a2dc5完整成功，内核/PA64/linked核验及新旧touch-only
镜像打包完成。新候选`boot-cepheus-touchordered-60a2dc5.img` SHA256
`a00d1e29087d0f28e695eb0ec91166c48a917fde45685b655c46162541c9f074`，未启动；
触摸间歇根因未解，不把离线短点修正称真机已恢复。候选首轮仅触摸，
SCM/CPU boost/内存/GPU探测关闭。详见touch-failure-theory最新推进段。
当时停在真机前；之后验收已在本文开头更新。完整镜像/证据及分轮验收见
[有序上报真机前检查](touch-ordered-preflight.md)。

2026-11-08用户已拿走手机，要求先做理论。10份已有RAW日志的host重放
确认生产驱动按整批最终状态上报会吞掉批内enter→leave；最新失败19事件
可复现只有2组down/up，顺序模型5组。**这是独立源码缺陷，不能解释后段
空FIFO，间歇根因仍UNKNOWN**。新增8项host测试及CI/离线入口，生产驱动
与镜像未改，无新XNU构建/远端CI/手机操作（该段为先前理论轮）。详见
[触摸失败理论与重放](touch-failure-theory.md)。下面硬件结论均为历史记录。

> **最重要的现状：iOS 16.7.16 曾多轮成功运行主屏幕、触摸和电源键；
> 有序touch-only及新核控制PASS；历史45s soc-only触摸FAIL保留。
> 历史240s延迟轮体验PASS，掉线/中断导致阶段对照INCONCLUSIVE。
> 最新基础控制及QUERY7查询后触摸/唤醒PASS；七次查询完整，command7可用，PAS13=false。
> GPU/Metal硬件加速没有完成，未尝试PAS认证。
> 触摸间歇失败根因和PAS13 supported=false原因都没有定位。**

## 1. 目标、设备和证据分级

目标是在小米9上临时启动iOS 16.7.16，保持可交互SpringBoard、物理输入和
熄屏功能，再依次打通Adreno640平台、受限命令执行及iOS图形接入。

| 项目 | 已固定信息 |
|---|---|
| 设备 | Xiaomi Mi9，`cepheus`，SM8150 / Snapdragon855 |
| 预期serial | `67eea163` |
| 内存／存储 | 实测8GB／256GB机型，不泛化到其他布局 |
| 面板 | Samsung EA8076，1080×2340 |
| 触摸 | ST-FTS，I²C `0x49`，chip ID `36:39` |
| GPU | Adreno640，FDT chipid `0x06040001` |
| iOS | iPhone X材料，16.7.16 / 20H392；研究用SWEET shim |
| 最近设备观测 | query7-basic七次查询完整，PAS13=false；查询后触摸/唤醒PASS，采集已结束，手机保持运行 |

证据分为四级，**不得互相替代**：

1. **离线证据**：源码、固定固件字节、布局解析、RAM mock及单测。
2. **构建证据**：实际XNU编译／链接、符号和产物核验。
3. **真机局部证据**：寄存器／返回值／内存pattern／事件日志，仅限相应场景。
4. **用户验收**：指定镜像、指定启动的主屏幕／触摸／电源体验，不等于长期稳定。

GPU识别、命令编码、编译成功、SMC返回或CPU内存自检都不是GPU加速成功。

## 2. 工作目录与提交边界

所有下面的绝对路径均位于 `/home/yangqi/cepheus-port/`。

| 路径 | 用途／注意事项 |
|---|---|
| `gpu-work/` | **当前实现和证据文档工作树**，分支 `feat/adreno640` |
| `cepheus/` | 原主树，仍为 `35561c0`，有未提交SoC草稿及未跟踪文件；不要reset/clean覆盖 |
| `reboot-work/` | 已取消USB重启工作的隔离树，回退到 `1b59abd`；不继续此功能 |
| `dist/` | 本地启动镜像、打包receipt、FD及根镜像链接，旧产物全部保留 |
| `inputs/ci-*/` | 按CI运行号保存内核、摘要及固定源码commit |
| `logs/` | 实测日志、失败记录、离线receipt、私有静态取证输出 |
| `root/` | HFSX根镜像构建／修复材料；`dist/ios16-root*.simg`是链接 |
| `uefi/`、`unixnu/` | 原启动链材料，不直接覆盖已有脏树 |
| `upstream-sync/` | 独立上游候选树，不能默认晋级为已验收固件 |
| `inputs/gpu-refs-20261106/` | 固定HackiOS、Pong及GPU子模块参考源 |
| `/home/yangqi/android/kernel/xiaomi/sm8150/` | 本机下游源码参考，固定HEAD `36f369fdb77319a2984a48b89f025ac3e5f97386` |

当前生产改动已推送为 **`153561e`**，新增ZAP内核准备路径，CI42457537126
完整构建及产物核验成功。手机已验收镜像仍对应 **`b2b99e7`** / CI42450429814；新的源码或
文档提交不改变已验收镜像身份。65f5b96记录此前完整QUERY7实测。

最近提交主线：

| 提交 | 工作 |
|---|---|
| `008b2cd` | GX诊断偏移等修正，不是触摸根因修复 |
| `a0fa7a5` | 显式boot opt-in、受限one-shot SCM/PAS信息查询 |
| `2b2ed98`、`bf58eb2` | 固定六次SMC真实返回、PAS13=false及查询后UI验收记录 |
| `a49d4e8` | 原厂PAS13加载路径审查，撤回过强不可加载推断 |
| `5d0d2d7` | PILDxe客户端静态证据与本机日志缺口 |
| `cbc4750` | 拒绝有formattable风险的Android取证boot |
| `41ec3b1` | 本机TZ原件的原生Ghidra PAS查询链证据 |
| `da8ffc1` | TZ入口／重定位核对、补查command7 availability策略及测试 |
| `0ffbf3c` | 历史SCM关闭基线触摸FAIL，停止当时QUERY7开启测试 |
| `60a2dc5` | 整批验证后按FIFO顺序上报触摸 |
| `b2b99e7` | 可选固定240s基础探测等待；本轮复用该内核的默认45s配置 |
| `9594ed7` | 基础SCM配对封装及生产查询策略日志核验工具 |
| `a688daf` | 新基础控制PASS，缩减后续真机检查 |
| `65f5b96` | QUERY7七次完整、PAS13=false及查询后触摸/唤醒PASS |
| `153561e` | 固定ZAP快照、MDT及重定位CPU镜像准备，未认证/加载 |

## 3. 启动链与iOS用户态：已经做成的部分

启动链为：Android boot容器 → Project Aloha UEFI → UniXNU FAT payload →
DarwinBoot → XNU → userdata上的HFSX根 → launchd → SpringBoard。

- 用本机FDT和ADT替换参考机板参数、内存／UFS布局及面板信息。
- UEFI FD严格3MiB。扩成4MiB会覆盖SEC heap、vectors、页表和stack区域；
  不通过扩大固件区来解决构建空间问题。
- CPU personality匹配修正，8/8 CPU初始化和SGI1有日志证据。
- 修复恢复期 `systembag.kb`、logd目录／owner／`.migrated`，离线fsck与
  实际dyld cache文件头／非空文件核验；真机MKB_INIT及SpringBoard已通过。
- 关闭覆盖画面的framebuffer console，打通一次Home事件进入主屏幕。
- 保留PON物理电源键，去掉参考机错误GPIO按键读取；音量键不能据此认为已完整支持。
- USB CDC-ECM UDP日志可用，研究用持久console环仍保留。

原安装阶段确实重建并写入过userdata；它现在不是Android的正常data文件系统。
`fastboot boot`只说明没有刷boot分区，**不表示运行中的iOS不写根FS或持久日志**。
后续测试无需重新刷root；不要格式化userdata来“修复”触摸或启动。

根镜像历史修正版SHA256：
`313d5ccd792febf65fed401a17a54ca67591d55125b28b6289ea62bcccb53dde`。
它是原修复／安装记录，不是本次重新计算15GB根镜像的结果。
根镜像和Apple专有材料不上传公共仓库／CI。

## 4. 显示、熄屏与CPU性能工作

### 显示与熄屏

- workloop使用固定提交记录，减少每帧动态分配。
- 绝对期限调度，迟到跳过时隙而非追赶忙循环；`sweetfb_hz=30`是请求值，
  不是实测面板FPS。
- 限量记录submitted／presented／overwritten／copy_us／missed／off。
- 去掉读取write-combined framebuffer的逐像素分层淡出；off时仅一次清黑，
  加锁避免过时清黑。历史实测约0.7–0.9ms且用户确认改善。
- 这不是完整DSI／PMIC断电实现；present耗时也不包含用户态软件光栅化全程。

### CPU

- OSM只读实测：L3约1536MHz、Silver约1633MHz、Gold约711MHz、Prime约827MHz。
- 120s试验在固件LUT内选相同open-loop mV候选，将Gold／Prime请求到1.4016GHz；
  真机周期计数约1.402GHz，随后恢复旧请求有日志证据。
- 实现触摸活动驱动的限时CPU floor及TSENS热门槛：65°C退出、低于55°C允许
  重入、活动3s窗口、温度样本过期／无效拒绝。已有策略／温度日志，仍不是
  完整生产governor或异构调度整合，也没有受控持续动画benchmark。
- 最新失败基线**没有开启CPU boost**，不得把它归因于本轮主动超频。

PosterBoard历史SIGTRAP、bluetoothd／fairplayd.H2重复exec等用户态缺陷仍保留
记录，不因主屏幕可见就声明所有iOS服务完整。Wi-Fi／BT／音频／相机／蜂窝、
Apple服务和App Store没有获得可用验收；研究shim不等同SEP或正常iPhone安全模型。

## 5. 触摸工作：有序touch-only通过与最新SoC轮失败

### 已实现及验证

- SE17真实 `FIFO_DISABLE=1`；不能强开FIFO。GPI/GSI DMA仍未实现。
- 独立GPIO开漏I²C：SDA55／SCL56、IRQ122，RESET12／VDDIO84，遵守
  总线busy／仲裁／stretch超时，失败不发恢复脉冲。
- 限定ldoc1 XOB enable-only请求、正确电源地址，不改电压或校准分区。
- 原厂ST-FTS enter／motion／leave、首事件保留、跨批次状态、530218
  continuation规则修正；后者有源码和测试依据，但未证明是间歇失败根因。
- 1080×2340触摸canvas映射到SweetHID 1080×2400。
- opt-in诊断限128个非空RAW及30条health；SYSINFO用A6/0000只读已加载内容，
  不发load-data、固件或校准命令。
- touch-power旧基线及后续touchonly → soconly → socfuse → socfusearc →
  新核GX关闭 → 新核GX开启多轮分别获得用户正常确认。

### 最近失败轮：不是新SCM查询造成的

2026-11-07临时启动 **`boot-cepheus-allprobes-da8ffc1.img`**：

- `cephscm`不存在，CPU boost和内存workspace自检不开。
- 芯片36:39／SYSINFO可读，scan发送err=0；不等于扫描及交互正常。
- 起初19个非空事件：ready1／status3／enter5／motion5／leave5。
- 五个enter原始x均1079（右边缘），不是HID缩放制造的坐标。
- 全部16条health从1000到16000轮询保持events19／hid_calls6；最后
  reads16000、empty15994、errors0、active0、skip_next0、IRQ/SDA/SCL=1。
- 有touch HID down2／up2，但用户明确“触摸是坏的” → **验收FAIL**。
- 43:05:01／43:05:30对应下游force-cal状态；43:05:30在正常旧轮也出现，
  不是充分因果证据、不是可以改校准的理由。
- 与旧touchdiag失败表征相似。触摸源码blob及基线参数与a0fa7a5相同，
  仍不能排除内核时序／布局、电气状态、继承状态或固件内部行为。

准确结论：驱动能读芯片和FIFO，但捕获后段没有新的非空事件；根因**UNKNOWN**。
没有同步中心触摸时间轴，不能单凭空FIFO断言控制器停扫。既往多轮通过不等于
间歇问题修好，也不证明这次所有触摸硬件损坏。

没有自动复位、重发scan、固件／校准修改或试写电源恢复。手机未由代理重启。
该历史轮之后曾有有序touch-only验收，45s soc-only再次失败，当时用户选择
保留现场、先离线排查。之后经明确授权新核控制PASS、240s延迟轮体验PASS
但阶段证据不足；当前状态与后续门禁以首页及soc-delay-preflight为准。

## 6. GPU源码成果与真正缺口

下表“已实现”主要是生产模块源码、host故障测试和实际XNU链接；**不是平台
全部接通**。当前镜像没有GPU执行startup钩子，未实际发起GPU供电／固件／CP。

| 子系统 | 主要源码（`xnu/`） | 已有证据／仍缺什么 |
|---|---|---|
| SoC／电源门控 | `cepheus_soc.c`、`cepheus_gpu_power.h`、`cepheus_gpu_power_txn.h` | GPUCC/CX gate真机拒绝分支；真实ARC／clock／GDSC所有权后端仍缺 |
| ARC／BCM／配置 | `cepheus_arc*.h`、`cepheus_bcm.h`、`cepheus_gpu_bw.h`、`cepheus_gpu_config.h` | cmd-db/TCS只读及FDT源绑定OPP/BW规划；NOT SENT，不是旧票继承证明 |
| GPU内存 | `cepheus_gpu_memory.cpp`、`cepheus_gpu_memory*.h` | 真实IOKit接入、NC属性／PA门槛、单缓冲pattern回归；不是GPU DMA一致性通过 |
| GMU工作区 | `cepheus_gmu_workspace.*`、`cepheus_gmu_firmware.h`、`cepheus_gmu_upload*.h` | 分块、权限、不可变固件snapshot、6设备页表等CPU自检；未真机上传TCM |
| SMMU／VA | `cepheus_smmu_*.h`、`cepheus_gpu_vm*.h`、`cepheus_gpu_va.h`、`cepheus_gx_smmu_plan.h` | 4KiB设备页表与16KiB主机页划分、role检查、SID/ASID计划；TBU/ACTLR所有权及TLB实测缺 |
| PDC／RSC／QMP | `cepheus_gpu_pdc.h`、`cepheus_pdc_mmio.h`、`cepheus_qmp*.h` | 限定MMIO／握手／租约契约和测试；真实连接／运行租约未成立 |
| HFI／GMU启动 | `cepheus_hfi*.h`、`cepheus_gmu_bringup.*`、`cepheus_gmu_hfi_link.*`、`cepheus_gmu_start.h` | 有界ring、发布顺序、v2消息、CM3/OOB启动和故障模型；未真机boot GMU/HFI |
| GX／CP | `cepheus_gx_*.h`、`cepheus_cp_*.h`、`cepheus_a640_pm4.h`、`cepheus_sqe.h` | halted-window gate、保护、LLC/HWCG/VA、ring／scratch／fence规划；未解除halt／执行 |
| 故障／热／退休 | `cepheus_gpu_fault*.h`、`cepheus_gpu_thermal.h`、`cepheus_gpu_tsens.h`、`cepheus_gpu_retire.*`、GMU park/slumber | 故障隔离／RAM模型／生命周期测试；真实独立热监督、静默DMA及可靠断电／退休后端缺 |
| SCM/PAS | `cepheus_scm_discovery.h`、`cepheus_gpu_compile.c` | 受限原生SMC信息查询已实测；PAS专用XPU内存／crypto租约／auth和shutdown未做 |
| iOS图形前端 | 尚无本机完成的实现 | IOAccelerator/Metal ABI、shader compiler、资源同步、CoreAnimation GPU合成均缺 |

**已修复的真实panic**：`pmap_sync_page_attributes_phys`将32位pp左移后才转宽，
导致>4GiB PA截断。`0088-cepheus-pmap-cache-pa.patch`改用先扩展的`ptoa(pp)`。
源测试、linked 64位UBFIZ核验及真机32KiB NC pattern／release回归支持修复。
保留PA bounds／managed page／有效NC cache属性检查；不把physicalMask曾经
的猜测继续当根因，也不为了通过自检删门槛或强开缓存。

GMU工作区CPU自检曾通过，但同轮触摸失败，因此不能推荐其镜像作为可用版。
GX诊断的word/byte及fault字段缺陷已修，但真机仍被GMU不就绪门禁拒绝，
**GX可读寄存器路径尚未实测**，不能归因其修正解决触摸。

### 仍然阻止真实GPU启动的硬条件

当前基础控制及查询后触摸已独立验收PASS，间歇根因仍未解；不要求重复本轮控制。

1. PAS13查询false的差异、可信SCM共享buffer、PAS专用XPU内存及crypto带宽租约。
2. ARC/DRV继承票、共享clock/GDSC、真实DDR bootstrap带宽租约。
3. SMMU TBU/ACTLR/CB/ASID所有权，cache/TLB/DMA真机一致性。
4. GMU PDC/RSC/QMP/HFI真实后端，GX完整初始化和热监督。
5. 真实stop/quarantine与DMA静默证明；失败后不能盲free或降共享票。
6. 受限scratch/fence通过后才谈绘制；再后才是iOS用户态GLES/Metal接入。

## 7. PAS13调查、TZ取证与QUERY7候选

### 六次SMC实测（a0fa7a5）

64-bit self availability及INFO查询PAS1/2/5/6 availability都返回x0=0/x1=1。
第六次 `0x42000207`，arginfo1／ID13返回 **x0=0/x1=0 → supported=false**。
`e=-1`是引擎拒绝继续，不是认证失败；没有尝试PAS init/mem/auth/stop。
查询后UI该轮用户验收正常。INFO availability of auth不等于执行auth。

原厂KGSL→SSR→PIL-TZ加载路径不把supported7当准入。撤回“false证明PAS13
必然不能认证／加载”的推断，但**本项目门禁不放宽**；认证能力仍UNKNOWN。
PILDxe retail allowlist是UEFI本地策略，不是TZ安全服务实现，不修改制造支持。

### 本机TZ原件

- 用户进入recovery后读取固定 `by-name/tz` → `/dev/block/sde37`，4MiB。
- 读取前／主机／读取后SHA一致，原件SHA见第9节。
- 首读多86B dd统计输出被长度门禁拒绝，保留失败材料；`status=none`另名重采。
- Recovery已有cache/metadata rw且自动解密／尝试mount data失败；只能声明
  代理取证命令无分区写入，不能声明recovery整个运行环境无写入。
- ELF64 AArch64，无section headers；`TZ.XF.5.2-00209`等字符串仅属原件身份。
  采集分区SHA不证明之前SMC时运行的secure image与它完全相同。

REA strings/xrefs超时、CLI无procedure后，经用户允许改用原生Ghidra12.1.2
Headless，私有DB、`-noanalysis`、显式函数范围；不改固件字节。
最终25 seeds／1847指令／16数据段用PT_LOAD原字节、Capstone和LLVM核对PASS。
反编译函数名和手工原型都是推断；failure helper和间接跳转CFG仍有限制。

静态PAS查询：wrapper `0x1c04f444` → verdict `0x1c04dae8` → lazy lookup
`0x1c04db34` → descriptor find `0x1c04d354`。计数15、stride0x238，PAS13
唯一项 `0x1c13f910`；初始化不清ID。**“固件完全没有PAS13”的解释已撤回**。
静态lazy cache为零不是运行时不支持证据。静态表与真机false仍未解释一致。

重定位核对：4584条type0x403的RELA-format记录。真正启动函数入口为
`0x1c03b274`，`0x1c03b3b0`只是循环；循环读target当前槽值、筛选后加偏移，
不是直接读取addend。原槽值全等于addend只属静态事实。
count／cache／descriptor ID不与重定位目标重叠，不能排除其他写入者。
入口ADR与字面量的相等检查不证明物理加载基址；`0x14690160`原偏移
`0x7eb980000`可在启动时更新，**不得按这些地址扫描活RAM**。
NS↔secure frame全程、初始化其他写入和运行映像选择仍未恢复／验证。

### QUERY7七次查询实测

da8ffc1仅新增一次INFO查询：command7 availability。正常64路径由6次变7次，
第6次 `0x42000601, arginfo=1, arg=0x02000207` 确认后才发第7次ID13查询。
新bit4只代表确认可用；false/error/未到该项要看REGS，不混记“不支持”。

- 未确认可用 → 不发PAS13查询。
- 可用但PAS13=false → 继续拒绝。
- 即使PAS13=true → 也不自动认证／上电／CP，不直接把旧false解释掉。
- 最大16次是本地预算，**不能抢占一个不返回的secure调用**。

旧da8ffc1的allprobes关闭基线触摸FAIL，其配对query7-da8ffc1未启动。
新b2b99e7基础控制已PASS，随后query7-basic实测7次完整：command7 availability
0/1、PAS13 supported0/0、available0x1f、QUERY7 confirmed1，查询后触摸/唤醒PASS。
生产策略转录核验通过；排除本轮command7不可用，false根因仍未知。
该查询不能提供运行TZ SHA／实际基址／handler／表内容，也不证明认证能力。

### Android取证预检

本地三份Android boot实际ramdisk都含data/metadata `formattable`，persist无ro
且check；当前userdata为iOS HFSX，**拒绝启动**，不删单个标志就假称安全。
RAM-only最小取证initramfs还没有通过安全验收，也不再是取得现有TZ的必要前置。

## 8. 固定构建、参考项目与取消事项

### 构建

GitHub仓库：`YangQi0408/UniXNU-cepheus`。GitHub访问使用 `gh`。

- `touch-kernel.yml`：`macos-15-intel`／Xcode16.2，目标arm64而非arm64e。
- ios-sweet：`45263bee51df4a0f55cdf9c649af886399488ddb`。
- darwin-xnu-build：`663906528d1cd5ea8fde7de6f16de73db9511549`。
- 最新完整构建成功：**42457537126 @153561e**，尚未打包/boot；最近已实测内核为42450429814 @b2b99e7。
- 逐一核验关键patch未跳过、staged source、linked symbol、无unresolved、
  PA64指令、触摸／QUERY7标记、取消USB代码缺席。
- 主机无mcopy；打包使用已有 `cepheus-build:latest`，`--network none`、当前UID，
  真实mtools。不是虚假成功工具，不借此改系统软件。
- 最近完整离线receipt为QUERY7准备的 **38 PASS／0 FAIL／0 SKIP**，本轮ZAP
  改动只跑相关检查，不追认其已通过整套38项。每个入口可含多个host程序／case。
- TZ mapping／RELA RAM-only单测14项；最终本地手工5项负向篡改／缺范围均拒绝。

上游UniXNU/Aloha/DarwinBoot候选已隔离同步及FD构建；CI和本地FD摘要不同，
未解释bit reproducibility，未混入当前已验收启动链。新A740/其他iPadOS profile
不能冒充SM8150/iOS16适配。两个只读审查子代理30min超时，没有完整报告，
**不能计独立审查通过**。本轮文档整理没有启动新子代理。

### 外部GPU参考

- HackiOS固定 `57722468a3774e2ec60017705ec356ae9b31f40e`：Raphael/SM8150/A640
  平台事实最接近，当前参考HEAD为GLES-only；其失败即假定旧ZAP已加载回退
  不允许复制。旧机报告不等于本机验证。
- UniXNU-pong固定 `e0ee6df109b4b5dc706cac660d4f8210dc911dac`及明确GPU子模块：
  生命周期／用户态分层值得参考，但A730/SM8475与较新iOS ABI不能直接套用。
- 较合理路线：A640平台与受限CP先完成 → Mesa freedreno GLES前端 →
  本机20H392 Metal ABI适配另行推进。此路线尚不是已交付实现。
- APSL/GPL/BSD/MIT及translator许可分别保留，不上传专有固件／密钥。

### 已取消／未启用

- USB自动重启／诊断控制端点：用户取消，已完整回退 `1b59abd`，不复活。
  保留的CDC-ECM日志不是这个被取消功能。
- panic自动回fastboot只完成可行性审查，可靠runtime reset/reason后端未实现／实测。
- “仅一次真机机会”已被用户取消，可多次逐阶段测试；旧标题／文字仅为历史。
- 本机pi自动压缩曾401，已禁用 `compaction.enabled=false`；不改提示词制造取证结果。

## 9. 镜像、内核和证据索引

### 镜像使用状态（全部位于 `../dist/`，相对gpu-work）

| 文件 | 当前判断 |
|---|---|
| `boot-cepheus-touch-power.img` | 历史多轮触摸／UI通过，**恢复用旧基线首选候选**；不是当前启动已恢复或长期保证 |
| `boot-cepheus-touchordered-60a2dc5.img` | 最新有序touch-only本轮用户UI/PWR/touch PASS；保留可用镜像，不证明长稳 |
| `boot-cepheus-soconly-ordered-60a2dc5.img` | **最新临时启动触摸FAIL**，同核仅加cephsoc；保留证据，不当可用升级版 |
| `boot-cepheus-touchonly-control-da8ffc1.img` | 同参数未修正控制镜像，打包通过、未启动；不是allprobes失败轮的相同profile |
| `boot-cepheus-allprobes-008b2cd.img` | 对应轮次触摸UI通过；GX只走不就绪拒绝，不是GX读取通过 |
| `boot-cepheus-allprobes-a0fa7a5.img` | SCM关闭旧轮UI通过；该验收不自动扩展到新核 |
| `boot-cepheus-scminfo-a0fa7a5.img` | 六次查询及查询后UI通过；PAS13=false，不能加载GPU |
| `boot-cepheus-allprobes-da8ffc1.img` | 历史触摸FAIL，不得作为可用升级版 |
| `boot-cepheus-query7-da8ffc1.img` | **旧kernel仅构建／打包、未启动；当前需新kernel逐项重验基线，不能直接跳过** |
| `boot-cepheus-workspace-6f93116.img`、`boot-cepheus-memcheck-6f93116.img` | CPU内存局部回归有通过证据，但后续对应触摸失败；不作可用UI候选 |
| `boot-cepheus-memcheck-9dc6e6e.img`、旧gxprobe/bisect/cephmem候选 | 有已知panic或不完整修复，不按旧README命令再次启动 |
| 其余历史perf/cpu/probe/root/FD产物 | 全保留；状态按专题及每轮receipt核对，不能因存在文件就视为已测 |

上述关键boot镜像都是70680576B；SHA为本次文档整理重新核对值：

| 镜像短名 | SHA256 |
|---|---|
| touch-power | `c8eae5a0abd8dd9f8cd58f954497c0510c576b941b09bc0ef1efb2648718ea88` |
| allprobes-008b2cd | `8e538843092e7101cd882fc544fdf7fa7e77a0c0ce4d1b4e81ac8608c7ddff87` |
| allprobes-a0fa7a5 | `f0517ba41d446dff3d81a6777da243464d541d9b0724392c7691b700d6f15a50` |
| scminfo-a0fa7a5 | `81dae8c4b97684ac0f08dd21142c846b340f6eeafdf5fae7eb0b77a82cfcfb04` |
| allprobes-da8ffc1 | `96fd757773fd876520a7389095258ab3d2da9398e7b451efae1e20029e5cf34d` |
| query7-da8ffc1 | `645e970bdd3a60592b7f28f46b1ba2457ad8559910bf7958f84ecca7f376637e` |

历史da8ffc1内核 `../inputs/ci-42380203110/kernel.macho`：
`6a03e92c8f3faec7d688c85a9ccbf60819cab9c6e615d6fd079aa6ecd3775e26`。
该历史两镜像ADT均 `6416d6d17b1b65fded10a4dfce1fbf0022216e03b013471aecb6db486703a28a`。
同名`.json`只描述**打包当时**状态；hardware_tested=false不否定后续实测，
也不能因为打包PASS而无视后续FAIL。

### 关键日志／receipt（路径相对 `gpu-work/`）

| 路径 | 内容 |
|---|---|
| `../logs/zap-staging-20261108/` | 153561e ZAP CPU准备、本机MDT/b02比较、ASan/UBSan相关验证与构建记录 |
| `../inputs/ci-42457537126/`、`../logs/ci-42457537126/` | 新153561e内核及完整构建日志，尚未打包/boot |
| `../logs/query7-basic-b2b99e7-live-20261108-1436/` | 7次SCM完整、PAS13=false、查询后触摸/唤醒PASS |
| `../logs/soccontrol-b2b99e7-live-20261108-1426/` | 配对基础控制PASS，无需重复 |
| `../logs/gpu-offline-query7-basic-20261108/` | QUERY7准备38项完整离线PASS，工具提交前dirty保留 |
| `../inputs/ci-42450429814/` | 当前真机已验收b2b99e7内核 |
| `../logs/socdelay240-b2b99e7-live-20261108-1401/` | 历史体验PASS但阶段INCONCLUSIVE；USB断流及signal中断、动作/主机观察/重放/完整SHA结果 |
| `../logs/touchcontrol-b2b99e7-live-20261108-1347/` | 新核touch-only本轮PASS；420s完整控制采集及结果 |
| `../logs/soconly-ordered-60a2dc5-live-20261108-1235/` | 历史45s触摸FAIL、中央动作确认，当时ECM现场保留；全量capture/result/replay/receive-timeline |
| `../logs/touchordered-60a2dc5-live-20261108-0111/` | 先前touch-only本轮用户PASS；raw/capture/包时间/动作指令/result/replay，尾USB变化待确认 |
| `../logs/allprobes-da8ffc1-scmoff-live-20261107/` | 最近失败boot.log、fastboot.log、identity、result；历史用户触摸FAIL |
| `../logs/touch-theory-replay-20261108/replay.json` | 10份保存RAW的host原函数重放与顺序模型差异；不是新真机结果 |
| `../logs/touch-ordered-preflight-20261108/replay.json`、`../logs/gpu-offline-touch-ordered-20261108/` | 修正版10日志0差异及34 PASS/0 FAIL/0 SKIP完整离线回归 |
| `../inputs/ci-42391773972/`、`../logs/ci-42391773972/` | 历史60a2dc5有序触摸上报内核、SHA/commit、完整构建日志 |
| `../logs/ci-42380203110/`、`../inputs/ci-42380203110/` | 历史da8ffc1完整内核构建日志、artifact、commit及SHA |
| `../logs/scminfo-a0fa7a5-live-20261107-155024/` | 六次SMC完整in/out及查询后用户UI PASS |
| `../logs/touchdiag-7ed0427-live-20261107-115339/` | 旧19事件／大量空FIFO触摸失败对照 |
| `../logs/touch-power-control-20261107-121209/` | 旧触摸基线恢复那一轮，不是最新失败之后已恢复 |
| `../logs/{touchonly,soconly,socfuse,socfusearc}-*/` | 逐项触摸隔离轮；精确名字由logs目录及gpu-acceptance索引核对 |
| `../logs/gpu-offline-tz-startup-query7-final-20261107/receipt.json` | 历史33项完整离线PASS |
| `../logs/recovery-tz-acquisition-20261107-clean/` | 4MiB TZ原件、身份、挂载／recovery日志及采集receipt |
| `../logs/tz-native-ghidra-20261107/` | 私有Ghidra DB、逐轮证据、失败／成功headless日志、verifier输出 |
| `../logs/pas13-source-audit-20261107/report.json` | 固定原厂加载路径审查 |
| `../logs/pildxe-static-20261107/`、`../logs/scm-static-20261106/` | UEFI客户端实物证据，不是TZ运行态 |
| `../logs/android-pas13-preflight-20261107/result.json` | 拒绝现有Android取证boot的安全预检 |

| 关键材料 | SHA256 |
|---|---|
| 最近失败boot.log | `551770b8b8832c345c8ec2982893c461f71f785027e1a39203b57583496b60b3` |
| 最近失败result.json | `8439a3df64465a6876e0f065cb3bf56a5391794c0ca901f60b48e0a0e2a031fe` |
| QUERY7最终离线receipt | `9532352ee229bc536a1f479cc0231f6b1bc13176c364260f8f7d7dfdfddd7064` |
| TZ原件 | `989389bcf4527b2b8e445998aba49454e556ac554c18d80e9eb999a8cbbdb114` |
| TZ clean采集receipt | `59ebe18cbff96a2ba5fc05d492761cc53685f1022e6e5c3d4556971994451980` |
| evidence-startup-init.json | `f632cb9475879797b8247eda13393e606f5f40b8dd4854b1ba684c4cc315cdfc` |
| verification-startup-final.json | `f3c48c7b94d49d87d2acd14f8b7b808914e505de52dc4e14ce70d75de50c90cb` |
| 已保存FDT `/tmp/live-cepheus.dtb` | `bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274` |

失败receipt的sample25初值false是匹配式错误，已改为true并解释：实际原日志
`CEPHEUS OSM: sample25`四组均存在。原boot.log未改；不把元数据纠正伪装成新测量。

专有GPU固件仅本地：`a640_gmu.bin` SHA `3ff0c02708bbe78641db887fa62f3a7f9337934d0c2ce0b961ef7c43172591d2`；
`a630_sqe.fw` SHA `a0e1b583f620fabe32729ce367959d1960638663244d7d0cfc21b9a5215a018b`；
`a640_zap.elf` SHA `74a948e84f0aac830b314ed05d26006b3571c9a3f90600c708ca76c25612660d`。
摘要与容器布局通过不验证ZAP签名／认证。

## 10. 操作约束和工具

- 只对核对过的67eea163/cepheus测试；首选临时`fastboot boot`，不刷boot。
- **不改persist、modemst1/2、fsg、fsc等校准分区**，不格式化现有iOS data。
- 不启动已被预检拒绝的Android boot；不把通用recovery称为整机只读。
- 触摸诊断默认关、严格无修复副作用；不自动reset／scan重发／电源试写／固件或校准更改。
- `cephscm=1`默认关、固定信息查询、一轮一次、fail-closed；不猜ID、不尝试auth
  探路、不加no_auth、不改PILDxe名单、不从失败推断旧ZAP已加载。
- 不开放用户态任意MMIO／PM4／电源投票接口，不绕过安全固件，不扫描secure活RAM。
- 不删除有效NC／PA检查，不用boolean覆盖“制造支持”，不伪造tools／测试成功。
- 新镜像另名、失败证据保留；每轮result绑定镜像／内核／日志SHA，逐镜像UI验收。
- 编辑用apply_patch，读取用read，GitHub用gh；不对脏树reset/clean，不复活USB控制功能。
- 子代理仅在用户授权范围内使用；此前授权是只读审查，不操作手机／派生子代理。

工具：现有 `cepheus-build:latest`／root-tools／apfs-tools；Ghidra在`/opt/ghidra`，
Capstone／LLVM／Rizin可离线核对，无IDA/BN/Hopper。换工具不等于改变证据等级。

USB日志：手机192.168.7.2，主机192.168.7.1/24，UDP5140，host MAC
`02:00:5e:77:ee:01`；`scripts/watch-usb-log.sh`只配置该MAC，需要用户sudo。
`scripts/udplog.py`用于接收，不新增USB控制端点。监听必须先于boot准备，
sudo密码不发到聊天；不要宽泛pkill导致无关任务被杀。

## 11. 可复跑的离线检查（不操作手机）

在 `/home/yangqi/cepheus-port/gpu-work`：

```bash
# 查询策略与实际callback仅编译／RAM验证，不执行SMC
python3 scripts/test-scm-hook.py
python3 scripts/test-scm-codegen.py
python3 scripts/test-scm-source.py /home/yangqi/android/kernel/xiaomi/sm8150
python3 scripts/test-tz-evidence.py

# 固定最新内核的本地核验
python3 scripts/verify-touch-kernel.py ../inputs/ci-42380203110/kernel.macho
python3 scripts/verify-pmap-cache-kernel.py ../inputs/ci-42380203110/kernel.macho

# 仅查询GitHub构建状态
gh run view 42380203110 --repo YangQi0408/UniXNU-cepheus
```

全量 `scripts/validate-gpu-offline.py` 的真实fixture／reference路径及命令记录在
最新receipt每个check中；另选不存在的output目录运行，不覆盖历史证据。
TZ verifier需显式原件／证据／新output；不在公共CI下载私有材料。
上述命令的成功不提供硬件执行授权。

## 12. 下一步／待办优先级

| 优先级 | 任务与依赖 | 验收标准 |
|---|---|---|
| 已完成本轮 | **#66：QUERY7固定支持查询** | 新基础控制及查询后触摸PASS；7次转录完整，command7可用而PAS13=false，不追加相同查询 |
| P1 | **#63：静态PAS13与真机false差异** | 不改现有事实，建立实际conduit／运行映像／参数／表变化的有界证据；无安全通道则保持UNKNOWN |
| P1 | **#33/#30/#58：GPU平台资源与加载后端** | 离线补齐ARC/clock/GDSC、DDR、SMMU及PAS buffer/加载契约；RAM测试不冒充真实资源所有权 |
| P2 | **#67／#60：触摸间歇根因** | 最新基础控制/查询后PASS，历史FAIL与240s阶段缺证据保留；继续离线调查，不试写恢复或重复整套触摸轮 |
| P2 | **#45：退休／停止** | 实际GX/CM3及DMA静默；不确定保留隔离资源 |
| P3 | **#15/#16：GMU→CP→图形前端** | 先真实fence，再绘制，再iOS接入，不能用一步成功代替全部 |
| 可选 | **#65：RAM-only取证镜像** | 全路径不挂持久FS／不自动写入审查，目前没有已验收候选 |

#62原厂路径审查、#64取得TZ原件已经完成，不再把它们写成材料缺失。
#63只完成了静态证据部分，原因调查未完成。历史#53标题“一次机会”不再适用。
用户后续要求直接下一阶段，并尽量省去测试。最新基础控制及查询轮均已结束，
本轮不追加手机测试或重跑完整离线套件。手机保持当前可用状态，不自动重启/
恢复；将工作转回离线GPU平台实现与PAS13差异。触摸间歇调查和GPU执行未完成，
不能因为固定查询完整而声称接近可运行GMU/CP/Metal。

## 13. 专题文档导航与阅读顺序

1. 本文 → [验收与纠错流水账](gpu-acceptance.md)（倒序最新在前）。
2. [触摸](touch-bringup.md) → [失败理论与重放](touch-failure-theory.md)
   → [QUERY7测试边界](scm-query7-preflight.md)。
3. [TZ原生证据](tz-native-evidence.md) → [PAS13源码审查](pas13-supported-audit.md)
   → [ZAP资源契约](zap-platform-contract.md)。
4. [CPU/GPU实现](cpu-gpu.md)、[性能](performance.md)、[根镜像历史](root-image.md)。
5. [参考项目](gpu-reference-projects.md)、[上游隔离同步](upstream-sync.md)、
   [RPMh固件证据](rpmh-firmware-evidence.md)、[SCM客户端](scm-static-evidence.md)、
   [PILDxe客户端](pildxe-static-evidence.md)。
6. [Android取证拒绝预检](android-pas13-preflight.md)、[panic回fastboot审查](panic-fastboot.md)、
   [原一次机会历史策略](gpu-one-shot.md)。

`README.md`、`../dist/README.md`及上述历史章节可能有“尚未／当前推荐／已可用”
等当时措辞；**以本文、最新gpu-acceptance和对应真机result为当前解释**。
不要照搬历史flash/recovery/失败镜像命令。本文是交接摘要，不声称全部缺口已解决。
# 2026-11-11 Native GX Subset

ceaa736 config-cold batch pushed. Fixed native GX23 writes and actual HFI/
OOB-held/pool guard chain implemented, CP halted/no startup. Fresh focused140
checks and full83-host preflight PASS, source hashes still match, real firmware
49-mode actual-pool and whole chain sanitizer-clean (hardware/owner emulated).
Exact receipts in `gx-native-backend.md`. CI42907357305 @31215d3 successful
and archived-source accepted, 106core/35memory; excludes ceaa736 and GX.
No phone operations or GPU execution. Next actual HWCG-before-GBIF binding,
GX CP workspace publication, genuine ownership/inherited state and PAS13.

# 2026-11-11 Config-Cold Follow-On

`31215d3` pushed; single CI42907357305 building, no redispatch. Later
config-required CM3/pre-cold ACD and final-callback checks pass fresh 135-check
focused and four-step/82-host preflight, source hashes unchanged. Exact
receipts, 31-mode original-firmware actual-pool tests and limitations:
`hfi-native-backend.md`. Next fixed native GX core backend, still no startup
caller/global owner/CP release. No phone operations, GPU/Metal never executed.
