# 触摸间歇失败：离线重放与理论排查

## 2026-11-08：延迟基础探测候选离线推进

用户问“然后呢”，继续离线准备同一新核的touch-only控制版和240s延迟
基础SoC探测版；保留当前失败现场，不新增手机命令。新参数
`cephsocdelay=240`仅接受cephsoc=1且无fuse/ARC/GX/SCM/boost/memory参数，
包括=0混入也拒绝。默认45s与原探测集合不变，仅延迟版增加内核时间标记。
240s是线程等待，不保证用户已验收；缺pre-begin PASS的轮次不能用于
判断探测后的触摸变化。当前代码与host测试已实现，完整离线37 PASS/0 FAIL/
0 SKIP（首轮GX测试截取范围FAIL保留），b2b99e7干净树全套重跑通过。
CI42450429814完整成功，新核控制版及同核240s延迟版已封装并严格delta
核验仅增cephsoc/cephsocdelay；两者真机未测试，停在控制版独立验收前。
详见[soc-delay-preflight](soc-delay-preflight.md)。

## 2026-11-08：有序修正版的SoC基础轮仍失败，现场保留

touch-only `touchordered-60a2dc5`本轮用户PASS；同kernel/ADT/其他payload和
TOML参数只新增cephsoc=1的`soconly-ordered-60a2dc5`随后触摸FAIL。
用户确认中央操作已完成，主屏幕/电源正常，并选择保留现场、先离线排查。
不重启/reset/重发scan/改电源/校准；停止fuse/ARC/GX/SCM的后续测试。

420s原日志452包/123804B，SHA256
`a929e459341d37375bd4f72452b5a9e98baa3f8a9452668b8f03713adf1ee566`。
证据在`../logs/soconly-ordered-60a2dc5-live-20261108-1235/`，包含result、
RAW重放和包边界receive-timeline。6RAW/4批完整重放：down1/move0/up1，
生产实现与顺序模型0差异。19条health到polls19000保持events6/hid_calls2/
errors0/active0/skip_next0/IRQ/SDA/SCL1，RAW未达128限额；仅右上角1079,0
enter/leave，没有中央触点。**本轮不能用批内上报丢失解释无中央FIFO。**

主机接收时间：基础GPU快照12:36:37，selected12:36:52，边缘enter
12:36:55，force-cal43:05:30在12:36:56；中央操作指令12:37:45，用户随后
报告触摸坏并确认完成。四组sample25在12:39:37，之后9条health仍events6，
最后12:41:48。时间来自UDP最终含该行的包，不是内核/控制器/手指时间戳。
没有单独确认sample25后的中央复测，不能声称采样结束后仍经用户复验失败。

### 能收紧的判断

- 两轮严格单变量镜像对照提高cephsoc组合/时序的排查优先级，但跨启动的
  控制器、电气、用户接触和继承状态不恒定，**不是单项因果证明**。
- 基础路径源码不写触摸GPIO/scan/reset。OSM试写只在boost opt-in；本轮
  缺boost，trial不激活，restore跳过；SCM入口缺cephscm返回。GMU读取被
  blockers0x7a拒绝，GX/fuse/ARC/memory不请求，日志无这些新增调用。
- OSM四组sample0..25齐全，触摸持续轮询到19000：不支持“触摸线程完全
  停止/死循环未读FIFO”。但没有I2C电气波形或原始sense-frame，成功空读
  不等于模拟感测健康，也不排除错误采样或固件内部状态。
- 基础GPU/GCC快照与历史正常soconly-7ed0427相同（包括CX_HW0x5ff4/
  gmu0x7a/memory0x37a），不是失败独有状态。SYSINFO及powered GPIO回读
  与同核正常touch-only一致；force-cal在正常旧轮也出现，不能据此改校准。
- sample25后仍有空读，削弱“只在持续OSM采样期间阻塞读FIFO”的简单解释，
  不排除启动期一次性影响、之前形成的内部状态或未确认后续操作。

### 下一离线/分轮方向，不自动执行

先重复同核touch-only重新获得本轮恢复验收，并记录启动前系统/关机方式、
手指无接触窗口和供电/USB状态；用户目前选择保留现场，暂不启动。
再将cephsoc从“OSM+GPUCC+TSENS+日志/线程”拆成显式单项只读profile，
或先让整组探测发生在触摸已验收之后，验证启动先后关联。分组或延迟版
都需独立源码/构建/参数门禁及新用户许可；当前没有这些新kernel候选。
日志/线程时序对照也应保留，不把“只读”当作不可能影响硬件或系统。
不增加自动scan/reset、sense-frame加载或触摸校准写入来掩盖失败。

新增`analyze-touch-soc-log.py`仅处理保存原件，验证SHA和accepted包长度/数目，
将console行定位到最后含该行的包；4项host测试覆盖断行、外来包过滤、
SHA/长度不符拒绝、CRLF/UTF8/空抓取。CI/离线入口已接入；本轮不改
生产XNU源码、不构建新内核、不新增真机命令。根因仍UNKNOWN。

## 前一阶段：接触转换修正已实现，构建与首轮完成

用户随后要求“继续推进，直到需要在真机测试”。生产`touch_fts()`现在
先验证整个批次，再用同一parser重放并按FIFO顺序上报；保持批内短点、
同slot重按及末段motion/release位置，失败批不部分投递。每批最多32个
事件、两遍CPU解析，没有新增分配/总线读取/scan/reset/供电/校准命令。
暂不合并motion，原厂逐事件sync及有限FIFO长度支持先验证这一路径。
SweetHID仍自行为每个变化构造collection，实际投递/开销须真机验收。

11项重放测试及生产FIFO故障测试通过；10份保存RAW修正版与顺序模型
逐批HID事件完全一致（0差异）。新重放receipt位于
`../logs/touch-ordered-preflight-20261108/replay.json`，SHA256
`2b3e2397c3a01986592b04014779a6e9a129368138409391e7344df72b0c4c02`。
最新失败重放down5/move3/up5，旧失败down5/move3/up5；这些仍只属host
证据。完整离线检查34 PASS/0 FAIL/0 SKIP，receipt为
`../logs/gpu-offline-touch-ordered-20261108/receipt.json`。随后固定XNU
CI42391773972 @60a2dc5及内核/linked/PA64核验、touch-only新旧镜像打包
完成；当时未真机测试，后续touch-only PASS及soc-only FAIL已在上面更新，
详见[touch-ordered-preflight](touch-ordered-preflight.md)。

下面保留先前理论轮的生产源码身份和未修正重放结果，不能用于描述修正版
的当前行为。间歇空FIFO根因仍UNKNOWN，QUERY7保持阻断。

更新：2026-11-08。用户已拿走手机，要求先做理论。本轮仅读取已有源码、
镜像receipt和保存日志，运行host RAM程序；没有新boot、触摸命令或SMC。
生产触摸驱动和已有镜像没有改变。**触摸整体根因仍UNKNOWN。**

## 1. 本轮确定的新缺陷：整批上报丢失接触转换

`xnu/cepheus_touch.c`的`touch_fts()`先解析整批FIFO到`frame`，最后只调用
一次`touch_report(frame.active, ...)`。因此同一批里的enter→leave最终active
回到0，HID完全看不到该次点击。已按下的slot若在一批内leave→enter，且
位置不变，也会完全丢失这两次转换。最后一批motion→leave时，release用
`st.x/y`里的上一批坐标，最新移动位置也会丢失。

固定原厂`fts_521/fts.c`的enter/motion handler在3383行、leave handler在
3529行分别`input_sync()`，不是仅靠4372行的批末sync。因此不能以原厂
也批量读FIFO为理由，把本实现的接触转换合并认定为协议等价。

新增`scripts/replay-touch-log.py`编译**当前生产`touch_fts()`和
`touch_report()`原函数**，以保存的FIFO字节替换传输、以host sink替换
SweetHID。另一模式仅作比较：使用相同parser/report，逐事件上报。
没有生成替代硬件数据，也没有改生产上报方式。

| 保存轮次 | 完整RAW重放范围 | 生产down/move/up | 顺序模型down/move/up |
|---|---|---|---|
| 最新allprobes-da8ffc1失败 | 19事件、6批 | 2 / 2 / 2 | 5 / 3 / 5 |
| 旧touchdiag-7ed0427失败 | 19事件、4批 | 5 / 0 / 5 | 5 / 3 / 5 |
| allprobes-008b2cd正常 | 前127事件、34批 | 6 / 22 / 5 | 6 / 113 / 5 |
| allprobes-a0fa7a5正常 | 前128事件 | 6 / 56 / 6 | 6 / 109 / 6 |

最新失败的RAW n6..14批内包含finger1/2/3的完整enter→leave，生产上报
丢失这3组。模型统计精确复现最新日志HID桥6次及旧失败桥10次，但它们是
**离线重放数量**，不是新真机验证或用户态投递证明。正常轮中的中间move
合并本身未必造成不可用；短点击和slot生命周期被完全合并则是确定的信息损失。

这个缺陷解释了“原始5个接触、实际只有2组down/up”，**不能解释后段FIFO
没有新非空事件**。旧失败轮5组down/up全部发出仍被用户判定不可用，所以
不能把批内修正当作间歇故障已解决。

## 2. 已有证据进一步排除了哪些强推断

10份保存RAW日志均已重放，输入SHA及逐批差异保存在
`../logs/touch-theory-replay-20261108/replay.json`，SHA256：
`518c1e98c55c41e63be4700f417fbdc88817688dcdb750efa95bf07f0f7744e8`。

- 最新失败与008b2cd/a0fa7a5正常轮的可见初始化记录一致：RESET输入高、
  VDDIO输入低；接管后RESET cfg0x2c3/io3、VDDIO cfg0x200/io3；ID36:39。
  日志相同不代表真实电压、ramp、前序启动历史或控制器RAM相同。
- SYSINFO的fw0x60/svn0x2260/cfg0x37/cx0xffff/AFE3、尺寸2339x1079、
  channels16/34和CRC0在正常/失败轮一致，不是本轮失败专有指纹。
  这些是已加载snapshot身份字段，不能当持续扫描状态或校准健康检查。
- 初始化第一批03 ready→43:05:01→43:01:a0:00:01在正常/失败轮都出现。
  固定`ftsCore.c:339`的`setScanMode()`本身只走`fts_write_dma_safe()`，
  不调用`fts_writeFwCmd()`或`checkEcho()`。不能声称原厂此处必等echo而本
  实现漏掉；保存日志已有匹配scan echo，但echo不证明后来持续扫描。
- `fts.c:3623`的force-cal分支仅记录状态；0x01/0x30没有恢复写入。
  原厂命名提示固件内部检测/校准相关行为，未建立它与失灵的充分因果。
  正常a0fa7a5还曾有43:06:01 water状态及真实中央触点，单条状态不是验收。
- 最新和旧失败均无53:02:18，且health skipped0/skip_next0；本轮表征不能
  归因continuation抑制。这个判断限于已保存窗口。
- 正常allprobes-008b2cd前2000轮也只有3个初始化事件，后续才增长到2696；
  touchonly及socfusearc初始8000轮仅3事件，但后来用户确认并有补抓中央触点。
  不得把“空FIFO/IRQ高”单独设为故障分类器或自动恢复触发器。

## 3. 状态机差异与剩余假设

### 控制器重启／继承状态

原厂`fts.c:3587`的controller-ready handler释放接触、标记reset并重建
mode；本实现ready只清active，不重发scan。**这是行为差异，不是已证
本轮根因**：最新失败只见一次初始化ready，随后有scan echo和触点，没有
后段ready/error支持“运行中再次reset导致停止”。初始化ready也可能早已
排队，FIFO读到它的时刻不是控制器reset的时刻。

本实现不做原厂完整的powercycle、fw/config/CX初始化和feature恢复，而是
enable-only供电、释放RESET并发送active scan。原厂存在这些路径并不表示
它们在本机每次执行，也不授权复制自动恢复或初始化写入。前序Android/
recovery/UEFI历史、扫描启动时是否有接触、继承feature或模拟前端状态值得
记录；当前日志无法区分。

### 感测／电气／固件内部行为

失败的enter原字节已编码右边缘x1079，旧轮也有左边缘x4；HID映射不会
制造这批原坐标。I²C成功可读数字寄存器，不等于模拟感测电源稳定或矩阵
有正常信号。force-cal后安静也可能是滤波/内部状态或采样窗口内没有接触，
尚无同步中心操作或原始sense frame，因此“芯片损坏”“停扫”“校准坏”
均无充分依据。

### 传输和调度

GPIO I²C每半周期至少10us，理论每字节9个clock至少180us。单次8B FIFO
读取加写地址/op/read地址约11字节，下限约1.98ms；31个剩余slot的bulk
读约251字节，下限45.18ms，均未计TLMM MMIO、START/STOP、stretch和
调度暂停。208B SYSINFO约213字节，下限38.34ms。`poll=10ms`是两轮间
sleep值，不是100Hz采样保证；大批时延可增加slot生命周期合并机会。

批内event[7]余量有时增长，正常日志也有同样现象，符合读取时仍可能有
新到达事件；不能把每个slot的余量非递减当字节错位证据。本实现和原厂
都按首slot的余量取一次bulk、下一轮再读，不要求同一批末余量归零。
零I²C错误削弱明显NACK/busy/timeout解释，仍不能排除bit采样、stretch
形状、线程长时间中断或未捕获的运行态变化。

## 4. 本轮验算范围和下一步

新增8项host测试覆盖批内短点击、同slot重按、抬起位置、RAW上限截断、
序号/深度/格式拒绝，以及continuation跨隐藏空slot的状态不一致拒绝。
原生产FIFO与SYSINFO测试也已结合固定下游源码通过。新测试接入CI及
离线检查入口；**本轮没有完整XNU构建、远端CI或新镜像**，旧33 PASS
receipt不改写为新增入口已全量通过。

重放仅使用从n1开始连续、最多128个非空RAW。预算在一批中途耗尽时整批
尾巴剔除，不补造；没有RAW或格式不符拒绝。记录中的skip状态必须与相同
parser重放一致，否则拒绝，避免将未记录的空continuation误当真实事件。
无时间戳的RAW不能还原传输/触摸时间轴，host sink也不能验证SweetHID
实际ABI、BackBoard收件或手势。

下一离线实现可以独立修正接触转换保留：先验证整批，再按事件顺序发布，
保留当前坏批不部分提交的契约；进一步决定是否仅合并没有生命周期变化
的motion，避免将所有高频RAW一律变成HID流量。需要覆盖不同finger交错、
ready/error、非法ID、bulk失败、坐标变化和最终release，不能只移动末尾
`touch_report()`后便宣称完成。本轮按用户理论要求仅给出模型和证据。

以后设备回到测试范围，再做同镜像独立轮次，并记录启动前状态和触摸
时间轴：初始化后无接触窗口→中心长按/拖动→重复短点→边缘→双指。
同时观察RAW、HID桥和用户UI；长期安静与主动触摸安静分开解释。需要
诊断补充时优先记录poll时间、传输耗时、IRQ低次数和有限新事件窗口，
不添加reset/scan重发/电源恢复/固件或校准修改。QUERY7仍被触摸阻断。

## 5. 固定源码身份

下游HEAD：`36f369fdb77319a2984a48b89f025ac3e5f97386`。

| 文件 | SHA256 |
|---|---|
| `xnu/cepheus_touch.c` | `2085e50b874e920933a67249b31c79f70f35f5c5f488d0169281f5f9f38d902f` |
| `xnu/cepheus_fts_events.h` | `ee67cb062d565c05bffffd8c688f9b1e04f4927d32d895ac8bfabf63357cf84c` |
| 下游`fts_521/fts.c` | `a90729937670d099176c3ed7bfc28c0c67e18be3914bd2142b86b4d4f63a3ebb` |
| 下游`fts_lib/ftsCore.c` | `5478156ad7182284374082888c5f6e1046e74488068683a6bd44d26f60e00bbe` |
| 下游`fts_lib/ftsIO.c` | `70a2ad24223ee32e27eba775ed7219283511db7e8d168a1011ec8dc7d54195f7` |

复跑（output需另名且父目录已存在）：

```bash
python3 scripts/test-touch-replay.py
python3 scripts/replay-touch-log.py \
  ../logs/allprobes-da8ffc1-scmoff-live-20261107/boot.log \
  ../logs/touchdiag-7ed0427-live-20261107-115339/boot.log \
  --output /tmp/cepheus-touch-replay-new.json
```
