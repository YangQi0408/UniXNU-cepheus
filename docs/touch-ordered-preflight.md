# 有序触摸上报：真机前候选

更新：2026-11-08。用户要求继续至需要真机测试；当前不操作手机。

## 变更与完成检查

- [x] 源码修正：整批先验证，按FIFO事件顺序提交；保留批内短点、slot重按、
  最后motion/release位置。两遍有界解析，不增加总线/供电/scan/reset命令。
- [x] 生产FIFO故障回归：非法ID后不部分上报，第二段读取失败不部分上报，
  consumed continuation状态保持，原限额/NOEVENT契约保留。
- [x] 11项重放回归：短点、重按、release位置、双指顺序、ready/error释放、
  32事件16次点击，RAW截断及continuation/格式拒绝。
- [x] 10份已保存RAW重放：修正版与顺序模型逐批0差异；不是新真机结果。
- [x] 完整离线回归：34 PASS/0 FAIL/0 SKIP。源码测试时工作树未提交，
  receipt记录a5a74b4/dirty=true；后续提交60a2dc5包含该受测实现。
- [x] 提交推送：`60a2dc51c009c1f28443e25bf4d63ea18faafd95`。
- [x] 完整XNU构建：`42391773972`成功，固定ios-sweet及dxb版本。
- [x] 内核产物核验：device-commit、SHA、ordered-contact标记、PA64、linked
  symbols及无unresolved；CI核对所有关键补丁及staged生产文件。
- [x] 另名触摸候选打包、只读boot/payload核验，历史镜像保留。
- [ ] 真机独立验收：**现在已到需要手机的边界，未执行**。

## 身份与离线证据

| 材料 | SHA256 |
|---|---|
| 修正版`xnu/cepheus_touch.c` | `672ce030f38efc4d8ef4764061f318237e9b67723f9fc27103edc2abb5e3700b` |
| 未变`xnu/cepheus_fts_events.h` | `ee67cb062d565c05bffffd8c688f9b1e04f4927d32d895ac8bfabf63357cf84c` |
| 新10日志重放receipt | `2b3e2397c3a01986592b04014779a6e9a129368138409391e7344df72b0c4c02` |
| 完整离线receipt | `221f2d4d6f636bfb1993b97bef13a0dd43203629a9dbceeac0381135d2070d9f` |

重放路径：`../logs/touch-ordered-preflight-20261108/replay.json`。
完整离线路径：`../logs/gpu-offline-touch-ordered-20261108/receipt.json`。
原始未修正重放保留在`touch-theory-replay-20261108`，不能覆盖。

## 构建与镜像已核验

CI：`42391773972 @ 60a2dc5`，kernel job14m37s成功。下载原件保存在
`../inputs/ci-42391773972/`，日志在`../logs/ci-42391773972/build.log`。
device-commit为60a2dc51...，ios-sweet为45263bee...，SHA256SUMS通过。
CI逐文件cmp staged生产源码，通过75个GPU core及13个内存/固件API linked
检查，无unresolved；本地也核对ordered-contact标记与16KiB PA64 UBFIZ。
LLVM对linked`touch_thread`核对到第二遍parse后调用`touch_report`。
参考driver和取消USB重启代码缺席，由CI现有门禁确认。

| 产物 | SHA256 |
|---|---|
| `kernel.macho` | `6134404e1d01586221f844aaba9579de452585bd9b9737f6f12980022d265211` |
| `boot-cepheus-touchordered-60a2dc5.img` | `a00d1e29087d0f28e695eb0ec91166c48a917fde45685b655c46162541c9f074` |
| 同名打包receipt `.json` | `96cc6fbed9a5c896716b6133514ed38c5b7d04c74ddfa2a71f6ad3ebcc3dbd74` |
| 原CI build.log | `6fa47ba46435df208df954260d55d0eaf8e488e8baf0101d47cfdf08dde4ee1b` |

新镜像70680576B。network-none、UID1000的既有构建容器使用真实mtools
打包；verify-ready-boot将新镜像与下面da8ffc1控制逐文件核对，除kernel/
允许config文件外其他EFI文件、UEFI/DTB/ADT/header相同，实际args完全相同。
receipt的hardware_tested=false准确，尚无新真机结论。全部已有镜像保留。

CI仍报告几个通用补丁跳过（TrustCache、monterey IOBuffer、sonoma skywalk/
syntax checker）；不是所有补丁零warning。0082..0088和既有关键补丁未
跳过，生产内存源码/PA门槛及符号核验通过，不借warning放宽硬件门禁。

旧临时FDT消失；本轮使用磁盘上的
`/home/yangqi/uwu_out/optimization_audit/dtbo_inspection/live_merged.dtb`，SHA
`bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274`，与交接固定
原件一致。ios-sweet在独立`../inputs/ios-sweet-ordered-touch`checkout固定
45263bee，未修改用户原树。初次离线预检因checkout未完成缺patches退出，
等其完成后另行正常执行34项；没有将缺输入视为通过。

未修正控制镜像已打包并只读核验，未启动：
`../dist/boot-cepheus-touchonly-control-da8ffc1.img`，70680576B，SHA256
`547828a5535d35285fb13cfa04e59a7324089b04b91c0993bc1150c41953a692`。
同名JSON将它与历史touchonly-7ed0427对比；除内核外EFI文件、ADT、firmware/
DTB/header相同，参数与历史touch-only相同。用于将来同参数旧/新核对照，
不是已恢复基线，也不覆盖touch-power及其他历史镜像。

## 首轮测试范围

候选与da8ffc1 touch-only控制镜像配对，使用相同参数、ADT和其他payload；
两者触摸代码的差异只有本次有序上报修正。da8ffc1控制镜像未在touch-only
参数下实测，不能用allprobes-da8ffc1失败自动替代它的验收。

候选沿用历史touch-only参数：GPIO/ldoc1 enable-only/60s延时、物理电源键、
30Hz framebuffer请求、限量RAW/health和one-shot Home；关闭cephsoc及所有
GPU/ARC/fuse/GX/SCM/boost/内存测试。仅换内核，不覆盖ADT/UEFI/DTB或root。
完整新核包含QUERY7代码不代表该查询会执行，`cephscm`不得出现在参数中。

构建/打包验收完成后停在这里，真机阶段需用户带回手机。未来先核对
67eea163/cepheus，先开日志再临时fastboot boot；不flash、不格式化userdata。
新镜像实际iOS运行仍可写既有root/持久日志，不描述为整机无写入。

同步记录从touch selected后的操作窗口：

1. 先无接触20s，记录空闲状态，不将空FIFO直接判失败。
2. 中心长按/拖动5次，确认中央RAW与实际UI移动。
3. 中心短点20次、边缘滑动、双指交错，确认接触生命周期与UI结果。
4. 物理电源熄屏/点亮，再重复中心点按；分别验收主屏幕/触摸/电源。

RAW最多128条、health最多30条，超过预算后的缺记录不能当故障或恢复
证据。每轮result绑定镜像/内核/日志SHA和操作时间。失败保留证据，禁止
自动reset/scan重发/电源恢复/固件或校准写入。新增逐事件HID可能增加事件
分配及投递负担，host回归无法验证持续时延、BackBoard收件或内存压力。

本修正处理确定的上报信息损失；失败轮“边缘接触后无新非空FIFO”原因
仍UNKNOWN。即使新触摸候选通过，仍需同核逐项重新验证GPU只读基线，
再另轮考虑QUERY7；本候选不越过任何PAS/GMU/CP门禁。
