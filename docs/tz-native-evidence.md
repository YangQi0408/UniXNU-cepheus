# 本机 TZ 采集与原生 Ghidra 查询链证据（2026-11-07）

## 安全与材料身份

用户将手机置于 recovery 后，核对 serial `67eea163`、`cepheus`、root、
TWRP `3.7.1_12-Unified Ver By Helium_Studio`、内核 `4.14.356-XTurbo-v2.7`。
固定 `by-name/tz` → `/dev/block/sde37`，sysfs `PARTNAME=tz`、`PARTN=37`、
8192 sectors（4 MiB）。只读采集，远端读取前/主机/远端读取后 SHA256 一致：

`989389bcf4527b2b8e445998aba49454e556ac554c18d80e9eb999a8cbbdb114`。

原件与采集 receipt：
`../logs/recovery-tz-acquisition-20261107-clean/{tz-partition.img,result.json}`。
ELF64 AArch64，23 program headers，无 section headers；字符串：
`TZ.XF.5.2-00209`、`YAQAANAAA`、`CRM`。
这证明**该分区原件身份**，不证明之前 XNU 查询时实际运行的 secure image
与它完全一致，不证明原厂 Android ZAP 成功。

首读 `../logs/recovery-tz-acquisition-20261107/` 失败，4194390B 比分区多
86B（dd 统计输出），长度门禁拒绝；未拿它做分析。加入 `status=none` 后
另建 clean 目录重采，精确 4194304B。失败材料保留，不覆盖。

Recovery 并非整机只读环境：`cache=sda29`、`metadata=sda19` 已 rw 挂载；
采集前后 mounts 相同，persist/userdata 未出现在挂载表，但 recovery.log
记录了自动 metadata 解密和多次 `/data` 挂载尝试，最终失败。
**只能声明代理采集命令没有分区写入，不能保证 recovery 自身未写入。**
代理未执行 boot/reboot/flash/mount、SMC、PAS 认证或 GPU 电源控制。
随后原生 Ghidra 这一轮完全离线，没有新手机命令。

## 工具切换及可复核边界

REA/Ghidra MCP strings/xrefs 超时；REA CLI 初次缺 GHIDRA_INSTALL_DIR，
按命令设置 `/opt/ghidra` 后返回候选地址没有 procedure。错误输出留在
clean 目录 `pas-supported-rea-cli*.json`，不是成功证据。
用户明确授权改用原生 Ghidra 后，用已有 12.1.2 Headless 导入同一原件，
`-noanalysis`，在私有数据库显式创建**分析者指定范围**，没有改固件字节。

脚本 `scripts/ghidra/TzPasEvidence.java` 导出：

- `../logs/tz-native-ghidra-20261107/evidence.json`：首次 6 seeds。
- `evidence-typed.json`：12 seeds，添加按原指令推断的 AAPCS64 参数。
- `evidence-dispatch.json`：17 seeds，676 条指令、6 段原数据。
- 同目录保存三轮 headless/script 日志、私有 `tz-pas-private` 数据库。

这些是**原生 Ghidra 导出**，不是伪造 REA Evidence ID。函数名以 inferred
开头，非原始符号。手工原型是推断，不是函数签名恢复；Ghidra 仍提示
unknown calling convention。首轮缺原型时伪代码丢掉 unchanged w0 参数，
因此解释依赖原指令，不以伪代码参数遗漏推断实际不传 ID。

`scripts/verify-tz-evidence.py` 验证固定原件完整 SHA、每条指令/PT_LOAD
映射、数据原字节、连续 seed 范围、Capstone 直接分支目的地与 Ghidra refs，
并用 LLVM 解码临时 `.inst` 副本（不执行；副本分支地址重定位，不用于
推断运行目的地址）。结果 `verification.json`：676 指令/6 数据段 PASS。
`scripts/test-tz-evidence.py` 的 8 个 RAM-only ELF bounds 单测不需专有材料。

**反编译成功不等于 CFG 正确**：大 dispatcher 对 stack-failure 调用
`0x1c03a718` 的终止/栈契约未建立，伪代码出现通向下一函数的候选流程；
参数 trampoline 的间接尾跳有 unrecovered jumptable 警告。保留这些限制，
不把它当完整 secure 调用图。本报告对这些位置使用已核对的原指令和
明确读取的 switch 数据，而不是自动推断的错误跨函数路径。
不能未经完整 helper 语义核验就把该调用标为 noreturn 来改善伪代码。

## supported7 实现：布尔转换与 lazy 描述符查询

| 地址 | 原件 file offset | 观察 |
|---|---:|---|
| `0x1c130458` | `0x132458` | 24B 调度记录：reserved=0、FNID=`0x02000207`、arginfo=1、flags=1、handler=`0x1c04f444` |
| `0x1c04f444..f487` | `0x51444` | w0 ID unchanged → `0x1c04dae8`；result 非空时 return 0，result[0]=(内部返回==0)，result[1/2]=0；空 result 返回 -18 |
| `0x1c04dae8..db33` | `0x4fae8` | ID >29 返回内部值 `0x300040`；否则 lazy lookup 后查 `0x1c165618 + id*8`；非零→0，零→`0x300040` |
| `0x1c04db34..dbb7` | `0x4fb34` | 按 ID 加锁，若指针缓存为零则查描述符、初始化并缓存，最后解锁 |
| `0x1c04d354..d3b7` | `0x4f354` | 从计数 `0x1c11c1f0`，起点 `0x1c13edf8`，步长 `0x238`，比较每项 +`0x1f8` 的 ID，返回匹配指针或零 |
| `0x1c04dbb8..dbbf` | `0x4fbb8` | 仅读取 descriptor+`0x1f8` 的 32-bit ID |

PAS8 的 `0x1c04f400` 包装器在该原件中也调用同一个 lookup；这仅是本版本
静态观察，不向外推广为所有固件中 supported/mandated 的通用语义。

Lazy 查询存在 secure 内部写入：锁、缓存指针、描述符初始化。它不是
纯粹无内存副作用的读；但此链没有 PAS init/auth/reset 或 GPU 电源调用。
初始化清零 `[descriptor,descriptor+0x1e0)`，另清 +`0x230` 的 8B，缺省
ops 写 +`0x1e8/0x1f0`；**没有清掉 +`0x1f8` 的 ID**。具体函数：
`0x1c04d6ec`→`0x1c040030`→`0x1c040470`（memset）、`0x1c04eea0`（ops）。
`0x14682be4/0x14682c34` 的原指令是 exclusive lock / barrier unlock。

## PAS13 确实在静态描述符表中，但不证明认证能力

计数原值 15；ID 顺序：

`1,12,9,24,29,13,14,15,19,18,20,21,23,4,27`。

PAS13 唯一项 index5，VA `0x1c13f910` / file `0x141910`，+`0x1f8`=13。
静态缓存 `0x1c165618` 的30项为零，**lazy 初始化正是为此设计**，不能
把原文件缓存零值当作真机未初始化/不支持证据。

若实际执行同一实现、ID确为13、描述符表及计数保持上述内容、初始化正常
完成且未被其他代码修改，则该分支应找到非零指针并产生 true。
这是条件性静态推断，不是实际执行结果。
之前 XNU 真机 `x0=0/x1=0` 保留；当前准确状态是：

**静态原件存在 PAS13 描述符和 supported7 查找链；它与既有 false 结果
尚未解释一致。真实运行态原因、认证能力仍 UNKNOWN。**

不再使用“固件完全没有 PAS13”“只补 crypto 就变 true”等解释。
也不能反向把静态表当成授权跳过 supported 门禁或试认证。

## SMC64 调度核对（静态路径，不是 EL3 实测）

`0x1c147e28/0x1c147e30` 原值指向 `[0x1c1303f8,0x1c131238)` 调度表。
`0x1c056b90` 每24B比较 +4 FNID；普通 SIP mask=`0x3f00ffff`，清掉
FAST/SMC64两位，故 `0x42000207` 归一化为 `0x02000207`。
`0x1c056d74` dispatcher 检查 arginfo 完整值=entry+8，匹配 PAS7 的1。
`0x1c05724c` 参数 trampoline，switch表 `0x1c11d188` 的case1→
`0x1c057290`，原指令将 saved+0x10 传 x0、saved+0xf8 传 x1，再尾跳
handler。与 PAS7 的 ID/result 形式一致。

这没有追完从 EL3入口到该 saved register frame 的全过程，也没有证明
之前实测一定进入这个 handler；relocation、运行映像选择、初始表修改、
安全上下文及结果传回仍需补证。不新增/猜测调用或换约定试探。

## 后续与门禁

- #64 TZ材料补充完成；#63 静态证据取得，但解释 false 的调查仍未完成。
- 下一项离线核对加载/relocation、寄存器 frame 的上游来源及表修改路径。
  不扫活RAM、不启动Android、不调用PAS auth探路。
- 既有拒绝 Android boot 的预检仍有效；RAM-only collector 候选不再是
  取得这份 TZ 的必要前置，但也没有任何候选已被安全验收。
- XNU、所有启动镜像、supported/CP halt门禁本轮完全未改；GPU尚未加速。
- 本机 crypto租约、XPU内存、可靠stop/quarantine仍是独立缺口；触摸旧
  间歇失败根因保持未知，未来每轮镜像仍要单独UI验收。

## 封存摘要与回归

| 材料 | SHA256 |
|---|---|
| recovery clean result.json | `59ebe18cbff96a2ba5fc05d492761cc53685f1022e6e5c3d4556971994451980` |
| evidence-dispatch.json | `922d5062a04e77966491d95e64776519362661bb7d81de9839ceb0dbf00167aa` |
| verification.json | `92ed4cbc30c596fd11699d7746b1a93f148ac2300f8e1c2e3321576af6496cf1` |
| GPU offline receipt | `11cd3fc8cacfdc27406544c163c1c321246227cb2678ef3657fb7792d452850d` |

完整回归 `../logs/gpu-offline-tz-native-20261107/receipt.json`：
**33 PASS，零FAIL、零SKIP**，含新8项RAM-only映射测试及既有SCM、触摸、GX、
缓存PA、源对照、固件fixture、host/sanitizer测试。不依赖TZ专有原件的
映射单测纳入普通离线列表；TZ实物验证仅本地显式输入，不上传公共CI。
另手工负向核对：证据SHA错、指令改动、缺指令、反编译未完成、数据改动、
截短TZ均被verifier拒绝。这6例为本地手工检查，不是新增CI检查数。

本轮没有内核构建、镜像打包、真机启动、认证或加速性能结果。
