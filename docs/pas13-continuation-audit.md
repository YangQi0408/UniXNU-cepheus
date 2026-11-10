# PAS13 查询差异的后续定位（2026-11-08）

## 2026-11-10：受限原指令回放

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

16项不含私有材料的合成回放测试通过，包含越界/代码写/异常指令/无返回/
错误wrapper/空调用拒绝及访问日志上限。原件回放8场景通过。v1/v2保留，
最终receipt为`../logs/pas13-instruction-replay-20261110/replay-v3.json`，
SHA256 `7982a916f81387f83ae3cc48ccdaeb0795779304e6e9bd5e93751a6dccf326b1`。
逐访存日志最多2048项，完整read/write/omitted计数另存，边界检查不因截断关闭。

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
