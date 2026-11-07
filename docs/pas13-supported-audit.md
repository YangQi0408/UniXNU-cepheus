# PAS13 supported=0 的原厂路径审查（2026-11-07）

本轮只读检查源树、保存的FDT/配置/日志及既有UEFI静态分析；**没有重启、
重发SMC、认证、复位、写电源或操作手机**。修改仅为离线审查工具和文档，
XNU discovery、supported拒绝分支、CP halt等运行门禁不变。

## 结论与纠正

1. 本次原生查询 `x0=0/x1=0` 仍按布尔接口解释为false，不是传输失败。
   **“本项目查询门禁不通过”不等于“原厂GPU必须通过这个查询”**。
2. 固定SM8150下游源树的KGSL→SSR→PIL-TZ实际加载路径没有
   `qcom_scm_pas_supported()`或command7 availability前置检查；它按
   PAS init/mem/auth各阶段的传输及secure结果判断是否成功。
3. 因而“PAS13必然无法认证/加载”是过强推断，应撤回；当前准确结论是
   **supported7=false，真实PAS13认证能力与返回false的原因UNKNOWN**。
   这个纠正不是授权跳过门禁：XNU加载仍关闭，不猜ID，不尝试认证探路，
   不使用no_auth/直接TRUST_CONTROL或“失败说明旧ZAP还在”的回退。
4. 原厂PAS init/auth需要独立crypto带宽资源；缺少它是后续真实接入缺口，
   **不是已证实的supported7=false根因**。该查询函数本身未投票crypto。

## 证据范围和身份

- 本地Android参考源树 `/home/yangqi/android/kernel/xiaomi/sm8150`，
  HEAD `36f369fdb77319a2984a48b89f025ac3e5f97386`，审查时工作区干净。
  它是本地原厂/下游参考源码，不声称已证明与手机当前secure固件逐位对应。
- 保存本机FDT `/tmp/live-cepheus.dtb`，646932B，SHA256
  `bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274`。
- 本地Android构建 `.config`：QCOM_KGSL、QCOM_SCM/64、MSM_PIL、
  MSM_PIL_SSR_GENERIC均y，REMOTEPROC关闭；这是本地构建配置，非本轮Android实测。
- 既有scminfo-a0fa7a5实测记录、寄存器原转录和镜像SHA不改写：
  `../logs/scminfo-a0fa7a5-live-20261107-155024/result.json`。
- 可复跑证据 `../logs/pas13-source-audit-20261107/report.json`，包含每个
  参考文件/FDT/配置/UEFI header/原result的完整SHA256，22个源码断言PASS。
  这不是独立子代理审查，也不是完整二进制调用图或硬件认证测试。

## 原厂GPU加载路径

行号对应上述固定源树，仅用于定位，内容/hash以审查报告为准。

| 路径 | 源码定位 | 已证实的行为 |
|---|---|---|
| KGSL | `adreno_a6xx.c:992` | SQE地址设置后，非secure MMU提前返回；否则有zap_name且无handle才subsystem_get，错误返回不能继续 |
| GPU选择 | `adreno-gpulist.h:397/418` | A640分支zap_name为a640_zap |
| secure上下文 | `kgsl_iommu.c:2707` | secure context解析时设置mmu.secured；保存FDT包含gfx3d_secure SID2/mask0x400，不以关闭secure作为移植策略 |
| SSR | `subsystem_restart.c:971/860` | 首个引用subsys_start→注册的powerup；后续引用复用已有实例，不是新的认证证明 |
| PIL-TZ | `subsys-pil-tz.c:1084/896` | probe注册trusted ops/powerup；powerup→pil_boot |
| PIL | `peripheral-loader.c:1259` | map→proxy vote→init→mem_setup→load segments→auth/reset，逐阶段检查ret |
| PAS | `subsys-pil-tz.c:598/655/693` | PAS1/2/5由scm_call2发出；检查传输值与desc.ret[0]，不是调用supported7 |

上述四个加载文件均未找到supported7/availability admission调用。
`drivers/firmware/qcom_scm.c:182`的通用bool wrapper确实先检查command7
availability再调supported；全树直接调用者在`drivers/remoteproc/qcom_wcnss.c`
（此本地配置remoteproc关闭），不能把这个WCNSS/通用wrapper当KGSL加载入口。
`no_auth`会短路PAS回调，但保存GPU节点没有该属性，禁止添加。

## SMC调用约定：目前没有发现PAS13查询参数错误

下游`scm.c:556`先用FAST self发现64/32约定，普通`scm_call2`再使用
`fn_id | scm_version_mask`，不强加FAST。因此保存的PAS13 supported输入
`0x42000207, arginfo=1, arg=13`符合普通SMC64查询形式。
`qcom_scm-64.c:271`用一个VAL，`return ret ? false : !!res.a1`；UEFI
`scm_sip_interface.h:837/3719`也描述supported7一个VAL和boolean verdict。
**false不是secure错误码，x0=0也不代表认证成功**。

首次六次SMC检查PAS1/2/5/6 availability，**没有单独检查command7
availability**，不能称为完整复刻通用`qcom_scm_pas_supported` wrapper。
这不抹去已获得的0回复；也没有证据证明多一次availability查询会使结果改变。
本轮不补发查询、不改FAST/32-bit路径或轮询其他PAS ID。

现有UEFI头文件枚举在SSC后即UNSUPPORTED（从0计数恰为13），而本机FDT
明确GPU PAS13。故这个公共枚举不能作为本机secure固件的完整ID表，也不能
据此改成其他ID。其过时/范围不足是否解释secure返回0仍UNKNOWN。
`docs/scm-static-evidence.md`中ScmDxeLA原件确有SMC64位、BootServices/TPL/
allocator/listener依赖；不据此从XNU调用DXE函数或认定运行时上下文已可接管。
`uefipil.cfg`的retail列表ABL/ImageFv/SPSS属于UEFI加载配置，非Android GPU
允许ID的完整表；禁止添加A640_ZAP条目“制造支持”。

HackiOS固定参考`57722468a3774e2ec60017705ec356ae9b31f40e`的
`hakios.adreno.c:1580–1620`把supported当诊断输出，不作为PAS加载门禁。
这是另一设备/项目的源码设计，不是本机false可忽略的运行证据；其PAS失败
即假定已有ZAP的分支仍禁止移植。不能宣称“所有Linux GPU驱动都不查”。

## Crypto资源上下文（与查询false的原因分开）

`subsys-pil-tz.c:141–224/1149`使用全局scm_pas bus client，默认master
CRYPTO_CORE_0（125）→EBI_CH0（512）；可由qcom,mas-crypto覆盖。
GPU保存节点无该覆盖，不能从qcrypto设备地址或无cell-id误推出加载PAS ID。
`scm_pas_init`注册失败仅warn，但init/auth的`scm_pas_enable_bw`在client=0时
返回-EINVAL，SMC尚未发生。active表为AB=393600000、IB=3936000000 B/s，
不同于qcrypto-noc用户设备表，也不同于GPU/GMU带宽表。

保存FDT的唯一有向路径，经审查工具从phandle/cell-id图遍历：

```
mas-qxm-crypto [8B x1, CE0]
 → slv-qns-a2noc-snoc [16B x1]
 → mas-qnm-aggre2-noc [16B x1, SN11]
 → slv-qns-gemnoc-sf [16B x1, SN0]
 → mas-qnm-snoc-sf [16B x1]
 → slv-qns-llcc [16B x4, SH0]
 → mas-llcc-mc [4B x4]
 → slv-ebi [4B x4, MC0 + ACV]
```

这只证明静态拓扑/源表，**没有发送这些票**；尚无CE0/SN11/SN0 cmd-db
unit/width/VCD、真实共享继承/租约及可验证归还证据。不能复用GPU票或自己
改crypto时钟/安全寄存器。supported查询没有上述资源函数调用，故“补带宽
就能把0变1”无证据，不能试写验证。

## 下一步安全边界

- 已完成源码审查，不意味着secure实现已逆向或PAS13支持阻塞已解除。
- 如有必要，下一轮先取得**本机既有原厂Android GPU/ZAP成功记录**或静态
  secure dispatch证据；不得把需要重启/认证的取证默认为本轮只读授权。
- PAS专用对齐连续NC元数据、XPU粒度/PA访问、区域所有权、crypto租约、
  可靠stop/quarantine仍需补齐。加载支持策略变更须另行显式决定。
- 每次未来镜像仍独立验收触摸；旧间歇性触摸失败根因仍UNKNOWN。

## 本轮离线验证

- `scripts/test-pas13-audit.py`：6个RAM-only测试PASS，覆盖函数边界的注释/
  字符串/声明隔离、负调用断言、唯一/循环/缺失/歧义路径及BCM/RSC拒绝。
- `scripts/audit-pas13-source.py`带本地config、UEFI header及原boot result：
  22项保存证据断言PASS；报告SHA256
  `2859233e4e1b394730e23ad332b98a33c5b222a2460d9923b0bf58274465c0af`。
- 完整`validate-gpu-offline.py`供应原GMU/SQE/ZAP、保存FDT、固定内核源码、
  XNU参考及ios-sweet patches：**32项PASS，零FAIL/零SKIP**，包括原SCM
  codegen/hook、触摸、GX拒绝分支、缓存PA、host与sanitizers等回归。
  `../logs/gpu-offline-pas13-audit-20261107/receipt.json` SHA256
  `e01450656a9f04fad913d2385a495e6c3df8922dea9c31a4a3ab40f0d6541d25`。
  新的source-only条目纳入通用离线验收；缺参考输入时仍不冒称源码验证。
- 本轮没有XNU新构建、镜像打包、真机启动或新SMC，不将离线PASS当GPU认证。
