# 本机 PAS13 内存合同续查

更新：2026-11-10 17:11 +08:00。**离线静态证据，不是PAS认证或运行态准入。**
手机保持已验收SCM配对版，不追加查询、触摸或内存轮。GPU scratch/fence、
Metal和PAS mutation均未执行，supported=false/CP halt保持。

## PAS13 配置决定实际路径

固定TZ原件4194304B，SHA256
`989389bcf4527b2b8e445998aba49454e556ac554c18d80e9eb999a8cbbdb114`。
PAS13 descriptor位于`0x1c13f910`，stride `0x238`；原字节为：

| 字段 | 静态值 | 有界路径中的作用 |
|---|---:|---|
| +`0x1f8` | 13 | PAS ID |
| +`0x20c` | 0 | 默认权限转换helper不调用assign；认证前执行权限检查 |
| +`0x20d` | 1 | mem_setup跳过load-region登记helper |
| +`0x228` | 0 | 权限阶段走默认helper，不调用自定义callback |

这些值位于lazy初始化清零prefix `0x1e0`之外；已解析init本体主要写prefix，
parser callee的全部副作用未建立。
已分析范围未发现对两个具体flag槽的直接引用；**间接writer和运行态修改
没有全部排除，分区原件也不证明当前运行TZ身份。** 不将静态值冒充live值。

`mem_setup`入口`0x1c04dbe8`检查ID/长度加法并读取cache，非零cache加锁后进
`0x1c04dcc8`。后者要求非零state，先调`0x1c04f0c8`，再调`0x1c04d954`，
与30项cache中state>=2的区间检查重叠；成功才置state2。
`0x1c04d954`仍要求正长度、起止4KiB对齐、跨度覆盖解析出的load段以及入口/
溢出约束，随后记录原load区间和新PA区间。1MiB分配容量不能替代4KiB逻辑长度。

`0x1c04f0c8`对PAS5有专用检查；其他ID在flag+20d非零时直接返回0，否则调用
`0x1c0bc950`登记区间。**静态PAS13跳过后者**。该helper确会以启动OEM配置的
subsystem load-region限制一般ID，但不能因此要求PAS13分配必须落在那个
carveout。初查“PAS13需要这个范围”的推断已撤回，不据此硬编码地址或换内存。

## 认证前权限检查

auth入口`0x1c04e0bc`将state推进到2/3/4/5，调用`0x1c04e2a8`处理权限阶段。
默认helper `0x1c04ef04`在flag+20c为0时直接返回0，不调用`0x1c0a74d0`的
权限转换链。**不应给该PAS13固定路径无条件添加一次hyp_assign/XPU转换。**

认证检查`0x1c04f164`在flag+20c为0时，先调用`0x1c055820`；若false，再调
`0x1c055648`，两者都false返回`0x30004c`，否则进入逐段校验`0x1c04ea4c`。
检查范围来自descriptor+80/+88，即mem_setup记录的逻辑区间，不是整个分配预算。

| helper | 传入的权限mask及匹配模式 |
|---|---|
| `0x1c055820` | `0x10/0x20/0x40/0x1000/0x4000000000`，逐个exact候选 |
| `0x1c055648` | `8/0x800000000/0x100000/0x4000`，逐个subset候选；带锁 |

两者先尝试secure地址翻译，失败使用原地址；经`0x1c0555b0`检查溢出并向外
取整到4KiB，再调用`0x1c0a8478`。该函数先取得最多15条、stride40B的访问
记录；有效记录的两个64位mask在subset模式均须包含请求位，exact模式均须
等于候选（动态mode下会去掉bit1）。collector失败不能作为允许结果。
record具体含义仍为反汇编推断，不把数值mask当已观测的硬件权限。

本地Android参考`36f369fdb77319a2984a48b89f025ac3e5f97386`的
`include/soc/qcom/secure_buffer.h:27`命名VMID_HLOS=3，对应mask8；这只支持
HLOS候选的解释，不证明运行TZ枚举/页记录一致。原件collector两模式分别从
页权限表或范围树读取；DDR范围由启动SMEM item `0x192`构造，不是文件中
静态零字段。实际页权限、DDR表、动态mode、XPU硬件同步仍未观测。

本地`sm8150-gpu.dtsi`的PAS13节点没有`qcom,mem-protect-id`；已有本机FDT
核验也未给ZAP指定subsys-vmid。`peripheral-loader.c:1128/1346/1390`仅在
subsys_vmid>0时调用相应归属转换。这与上述静态路径相容，不构成当前
XNU分配pair已有secure可达权限的证明。MDT共享的reader/cache合同仍要单独
确认，不能将image检查推及MDT或整个1MiB预算。

## 下一实际缺口

1. 当前分配pair的secure可达、页权限/CPU别名证据及同epoch独占；不能按静态
   VA扫描live secure RAM或使用其他启动的PA作为本次分配事实。
2. PAS13=false的运行身份/dispatch差异；固定lookup的state/cache假设回放
   没有解决真机false，不准用本合同跳过supported门禁。
3. crypto带宽/时钟和GPU电源/SMMU的真实owner/acquire/lease/quiesce后端。

无需重复相同查询或CPU预检。仅原字节证据不能提供成功lease；没有新增boot
参数、生产后端或可boot GPU执行候选。

## 可复核材料

私有目录`../logs/pas13-instruction-replay-20261110/`保留v1/v2/final/verified
REA bundle及匹配snapshot。verified含112 records，bundle SHA256
`31f7186a8394547ea6541850c0f9a3418fecdf3543b36fbcf9ba6754d6cc3ea1`。
`audit-tz-pas-memory-contract.py`绑定完整TZ身份/PT_LOAD及25条raw记录，解码
634条AArch64指令、核对17项关键分支/ABI；普通和`-O`通过，两receipt相同：
`2a3bfce61e8eacea757a614ea33d4f9d936dbae0f660a47d4b6a550abd4d904c`。
无固件执行/MMIO/SMC/手机操作；guard显式异常，不依赖assert。

关键REA Evidence：PAS13字节
`ev_d39e4c1521ee4e5f21dd44abeac84282049659f49c73b77bc47ed76e90ab1820`；
range分支`ev_c5e60f7f1ce2d55ac82b0d098d7f472d642c84e8ec402b9c20f8e2818b02f775`；
auth检查`ev_4f90087f65ded13c5d13b5a0fa341e5b29845bbea5601081f4ad76de45433331`；
mask比较`ev_98529aec035a0e04d59636775a227c1180214d9b404cf3ff933486be65c1d9d5`。
Ghidra未知调用约定可能遗漏unchanged寄存器；callee空参数不能据此当未传参。
failure-helper的noreturn模型不是独立证明，不推断邻函数为实际调用边。
