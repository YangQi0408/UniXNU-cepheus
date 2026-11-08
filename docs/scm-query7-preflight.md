# Command7 availability：下一轮受限测试边界

## 2026-11-08：转入新核基础采样后的查询阶段

用户在240s延迟轮结束后明确要求“直接下一阶段吧”。因此不把补跑该延迟
时序对照当作所有后续离线工作的前置。其当前触摸/唤醒PASS保留，缺失的
SoC阶段仍为INCONCLUSIVE，不能追认探测完成或触摸根因修复。

下一阶段为既有固定INFO/PAS13支持查询，使用已通过新核touch-only控制的
CI42450429814/b2b99e7内核。没有改生产内核、触摸或SMC策略，也不启动旧
allprobes/query7-da8ffc1组合。新配对仅基础SoC采样，可关闭fuse/ARC/GX。
验证器新增支持显式 --soc-only --scm-info，并检查编译后的QUERY7标记；
cephsocdelay与SCM混用仍拒绝，boost/内存/GPU启动参数仍拒绝。

### 已完成镜像

| 镜像（../dist/） | SHA256 | 配置与状态 |
|---|---|---|
| boot-cepheus-soccontrol-b2b99e7.img | b239af44ded8f62e833b023f5983828355995751fad31522bac5848a53e8bc47 | 相对已验收touchcontrol只加cephsoc=1；未实测 |
| boot-cepheus-query7-basic-b2b99e7.img | a060bd435ad3581520f29c0bb6886516963cd4f0955b8f40ce2a4cf23e0b5f19 | 相对上行只加cephscm=1；未实测 |

两张均70680576B，真实mtools离线打包；容器network-none/UID1000/
cap-drop-ALL/no-new-privileges。同名.json严格核验kernel、ADT、其他payload、
boot header、所有TOML字段不变，仅各自声明的一个参数新增。
内核SHA256：
e9f8f3f735e2ab6d3f52fe2817ec51b898502b6f79130687f3b63c214ebd4978。
本次通过gh核对CI42450429814确为b2b99e7 completed/success；
复用已构建内核，未触发新内核构建。

**该组合恢复默认45秒基础等待，不是240秒延迟组合。** SoC线程先完成
sample0..25，再进入一次固定SCM查询；正常64路径7次，异常按既有策略停止。
延迟轮的体验PASS不替代新基础控制或查询候选的逐次验收。

### 查询日志核验

新增scripts/analyze-scm-query-log.py，离线编译生产cepheus_scm_discovery.h
查询引擎，把保存的x0..x7输出作为RAM callback返回，逐次核对实际输入、
中断恢复cookie、32位回退、调用数量、summary/QUERY7/end一致性。
不链接原生SMC callback，不接触设备。保存新receipt及日志/生产策略SHA。
13项测试覆盖成功、PAS13=false、QUERY7未确认、传输错误、非布尔值、
fallback、中断、16次预算、丢失/重复记录和非法输入；参数测试15项、
接收时间线6项。CI与完整离线入口已加入新测试。

TRANSCRIPT_COMPLETE表示记录与生产查询策略一致，不表示查询成功：
pas13_supported可为true/false/null，null包括未到达/错误/非布尔返回。
QUERY7 confirmed=false仍不能单独判“不支持”，须看寄存器。
旧六次SMC日志回放预期INCOMPLETE，不改旧实测结论；上一轮无SCM日志
同样INCOMPLETE。主机接收时间不充当内核或物理操作时间。

完整离线38 PASS/0 FAIL/0 SKIP：
../logs/gpu-offline-query7-basic-20261108/receipt.json。
该receipt保留本次工具修改未提交时的dirty=true，不能冒充干净树运行。

### 下一真机步骤

手机当前iOS由用户保留；进入fastboot仍使用物理电源+音量下。先完成新
soccontrol的独立UI/触摸/唤醒验收和sample25四组采集，再在下一启动临时
boot配对query7-basic。每轮监听在boot前就绪，并核对67eea163/cepheus和SHA。
查询版应采集sample25、SCM begin、每次REGS in/out、summary/QUERY7/end，
再记录查询后触摸/唤醒反馈；连接质量不足时只记缺证据，不自动恢复。
用户怀疑数据线不稳，应以稳定数据连接取得查询返回证据。

不flash，不PAS init/mem/auth/stop、不GPU电源/固件上传/CP、不secure活RAM；
supported=true也不自动进入这些动作。单次SMC可能不返回，本地16次预算
无法抢占secure world。当前PAS13=false原因仍UNKNOWN，GPU/Metal未运行。

## 当前状态

以下至历史实测更新为2026-11-07记录，当前候选以本文开头为准。

只在主机修改查询策略、做 RAM 单测及 AArch64 编译；**尚无本轮完整 XNU
构建、启动镜像或真机结果**。此前六次 SMC 的 PAS13 `x0=0/x1=0` 不变。
这不是 false 根因修复，也不是 PAS 加载或 GPU 加速通过。

新增查询有固定源码依据：SM8150
`drivers/firmware/qcom_scm.c:qcom_scm_pas_supported()` 先调用
`__qcom_scm_is_call_available(PIL, supported7)`，`ret<=0` 返回 false，
确认可用才调用 `__qcom_scm_pas_supported(peripheral)`。
下游 KGSL 实际加载路径不使用这个 wrapper 作前置门槛；这里不声称复刻
原厂 GPU 加载，也不因此放宽 XNU 的 fail-closed 策略。

## 唯一新增的调用

正常 ARMv8_64 路径固定顺序（不含 interrupted resume）：

| 序号 | x0 | x1 | x2 | 用途 |
|---|---|---|---|---|
| 1 | `0xc2000601` | 1 | `0x82000601` | 原有 convention discovery |
| 2–5 | `0x42000601` | 1 | `0x02000201/202/205/206` | 原有四个 availability 查询 |
| 6 | `0x42000601` | 1 | `0x02000207` | **新增 command7 availability** |
| 7 | `0x42000207` | 1 | 13 | 原有 PAS13 supported，仅前六项确认通过才发送 |

初始 x3–x7 均零；仅现有 resume 契约保留 secure 返回 x6 cookie。
正常64成功为7次，已有约定探测的32 fallback为8次，总预算仍16次。
只允许 convention discovery 的已定义 not-supported 回退；**不在 PAS13
false 后换约定、换 ID、重发或试认证**。单次 secure 调用若不返回，主机
预算/OS 定时器不能保证抢占它，仍是硬件测试风险。

`available_mask` 低四位保持旧含义，新 bit4 只代表 command7 已返回
`x0=0/x1=1`。新日志 `CEPHEUS SCM QUERY7: confirmed=0` 可能表示 false、
error 或未到该项，必须看原始 `REGS`，不能直接记为“不支持”。

## 结果解释与停止条件

- 第6项 false/error：停止，不发送第7项，不认证。只能确定本轮未确认
  command7 可用，不解释旧第6次 PAS13 false 的根因。
- 第6项 true、第7项 false：查询命令确认暴露，但 PAS13 false 差异仍未解；
  停止，不绕过门禁。
- 第7项 true：只能记本轮 supported 查询通过；旧 false 与新 true 的
  差异仍需调查，**不得自动开始 PAS init/mem/auth、复位、上电或 CP**。
- 无返回/异常/触摸回归：保留原始日志和镜像，停下；不自动复位触摸。

此测试**不能观察 secure RAM、实际 TZ 基址、调度 handler 或描述符内容**，
也不能绑定运行中 secure image 与采集分区的 SHA。不得为了获得这些值
扫描镜像相对地址的活 RAM；当前没有通过安全验收的运行态取证通道。

## 测试前尚需完成

1. 完整 XNU 构建及 linked kernel 核验；现有主机 compile 不替代它。
2. 另命名打包查询关闭基线和开启候选，保留所有已验收镜像，校验除
   kernel/config 外的 payload、固件、ADT、boot header 均不变。
3. 用户许可实际测试后，先核对 `67eea163`/cepheus；仅临时
   `fastboot boot`，不刷写分区，不启动被拒的 Android boot。
4. 每张新镜像单独确认主屏幕、触摸、电源/熄屏，result.json 绑定镜像、
   内核及日志 SHA。不开 CPU boost/内存 workspace/PAS mutation。

用户要求推进至需要真机时停下；本轮不执行上述手机步骤，亦未推送/触发
构建。安全边界仍是不认证、不猜 ID、不改 allowlist/no_auth、不读 secure RAM。

## 后续实测更新：关闭基线失败，开启候选禁止推进

上面状态是离线轮结束时的记录，下面为用户置fastboot后的更新。
da8ffc1的完整CI42380203110成功、linked核验及两张独立镜像打包核验通过，
但仅临时启动的SCM关闭 `boot-cepheus-allprobes-da8ffc1.img` 已被用户
明确报告**触摸坏，验收FAIL**。原日志和receipt保存在
`../logs/allprobes-da8ffc1-scmoff-live-20261107/`，详细纠错见gpu-acceptance。
SCM开启 `boot-cepheus-query7-da8ffc1.img` **未启动**；无本轮查询返回值，
没有新PAS认证/上电/CP，不能据编译/打包成功跳过触摸门槛。
停止本候选推进，先恢复或隔离触摸回归；不自动复位、重发scan或改校准。
