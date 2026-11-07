# Command7 availability：下一轮受限测试边界

## 当前状态

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
