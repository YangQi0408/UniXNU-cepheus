# 首轮性能对照（不修改 userdata）

基线：用户已确认 `boot-cepheus-keys.img` 可见主屏幕、电源键可用，但刷新慢。
不能把 Home 触发、软渲染、后台 daemon 重启直接当作已测得的瓶颈。
之前声称“降低负载 60%–70%”“循环 Home 是最大卡顿源”没有测量依据。
Home 测试原代码最多 60 个边沿（30 次），并非永久运行。

## 此版本

- `sweethidtest=90 sweethidonce=1`：90 秒后尝试一次 Home press/release；
  无 HID 客户端则再等最多 5 分钟。完成后不再触发，不保证它发生时 SpringBoard
  已完成初始化，也不代表物理触摸能解锁。日志 `one-shot Home complete` 表示
  已发送一组事件，不代表操作系统已经解锁。
- `sweetkeys=1 sweetkeyspononly=1`：保留已目视验证的 PON 电源键路径，
  停止读取参考机 SID4 GPIO2。Mi 9 音量加实际不是这个 GPIO；基线日志把它
  报成一直按下，不能作为正确硬件支持保留。音量加暂不支持。
- `sweettouch=0`：不混入未实现的 GPI/GSI DMA，不进行只读 GPI 探测。
- 显示提交记录使用 workloop 专属固定数组，不在每帧 malloc/free。
- `sweetfb_fixedtimer=1`：绝对期限调度，晚到时跳过失去的时隙，不追赶忙循环；
  `=0` 使用原相对延时作为对照。两者都保留 `sweetfb_hz=30`。
- `sweetfb_stats=1`：每约 10 秒一行计数，保留 USB/persistent console 和 panic。
- 关 `startup_debug`、kprintf spam、进程栈周期采样、IOKit 查询追踪、devnull tee。
  不屏蔽崩溃报告，不禁用系统关键服务。不假设 `sweetgpu=3` 起作用（此源码
  没有消费该参数）。
- USB control transfer 正常处理，只限制重复 packet-filter 打印。

降低透明度/动态效果、禁止 daemon 重试、提高到60Hz均暂缓。前两项需要修改
根镜像/偏好并验证 iOS 16.7 实际读取的键和服务依赖，后者可能加重拷贝负担。
先保留基线与当前 userdata，避免重刷丢失已有应用数据库和开机状态。

## 测量含义

`SWEET PERF: ms=... submitted=... presented=... overwritten=... copy_us=...
max_us=... bytes=... missed=... off=...`

- submitted：收到的 SwapEnd；presented：调用 present 的次数，不等于面板实际FPS。
- overwritten：新提交覆盖仍待显示的记录数（合并帧）。
- copy_us / max_us：整个 present 函数累计/最大耗时，含查找surface和CPU复制，
  不包括用户态软件光栅化、display-off淡出或面板扫描时间。
- bytes：实际执行copy的字节数；可能为0（无有效layer或surface）。
- missed：绝对定时版本跳过的槽数，不是用户态丢帧数。
- off：取样时的display-off状态。静止主屏幕提交率低不等于动画卡顿。

比较应使用相同开机等待、相同动画/电源键动作。请求频率不是实测帧率，不能
仅从 `sweetfb_hz=30` 声称达到30fps，也不能仅从进程重启次数得出CPU占用。

## 构建

`performance-kernel` 使用固定 ios-sweet `45263bee...` 和darwin-xnu-build
`66390652...`，由 `prepare-performance-patch.py` 从源码严格生成0083；
没有overlay本地0082/touch/GPI改动。产物中必须存在性能、一次Home和PON-only标记。

```bash
gh workflow run performance-kernel.yml --ref master
# 下载成功构建产物、校验SHA256后，在带mtools的容器中：
python3 scripts/make-performance-boot.py boot-cepheus-keys.img kernel.macho boot-cepheus-perf.img
# 同内核另一份对照可加 --fixed-timer 0
```

打包保持ADT/UEFI/DTB和大小，更新Aloha payload digest，不覆盖源镜像。
先开UDP日志，再 `fastboot boot`；不刷boot或userdata。通过构建不等于已加速，
结果需记录实际日志和用户目视反馈。

## 首次启动记录：run 41284957253

构建成功，5项离线策略测试通过；新增日志解析测试后共6项。
已临时启动 `boot-cepheus-perf.img`（SHA256
`427da1ebfd649d39282f01f9385f4494c189b2b15fa98cd8b8f7dc050b39e0e4`），未改userdata。
同内核相对节拍版 `boot-cepheus-perf-relative.img`（SHA256
`1ba7e1d4382a6383384bdfd216b6bd4ce7a73aaf24c917cf2a5b60ef5aff134f`）仅打包，未上机。
日志位于本地 `logs/perf-boot.log`，MKB_INIT完成、SpringBoard启动、USB ping正常，
出现一组Home down/up及 `one-shot Home complete`，后续窗口无继续注入。

记录到的窗口（不是持续动画benchmark）：

| 窗口 ms | submitted/presented | present 总耗时 µs / 最大 µs | copied bytes | missed | off |
|---|---|---|---|---|---|
|10063|6 / 3|6533 / 2493|30326400|202|1|
|10003|0 / 0|0 / 0|0|34|1|
|10000|22 / 20|51424 / 4838|192067200|0|0|
|10000|1 / 1|2269 / 2269|10108800|0|0|

解释：呈现调用约2.3ms至4.8ms，启动窗口里拷屏不表现为数百毫秒耗时。
但off期间有大量超期槽；显示关闭淡出会读取write-combined framebuffer，可能
拖慢workloop，该路径未单独计时，不能把 missed 直接归因于此。
静止屏幕后续submitted=0正常。PosterBoard的旧SIGTRAP仍存在，未在此版本掩盖。
需用户对熄屏/点亮反馈及相同操作的A/B数据，不能声称已测得总体加速。

```bash
python3 scripts/summarize-performance.py ../logs/perf-boot.log
```

## 熄屏反馈与第二版

用户反馈第一版亮屏较快，但熄屏一层层变暗，触摸仍不可用。此前性能版明确
保持 `sweettouch=0`；触摸从未验证成功，不能当作性能版的回归。
该次电源切换窗口 `submitted=4 presented=3 copy_us=4526 max_us=2455 missed=210`，
支持进一步排查熄屏路径，但不是熄屏耗时的直接测量。

已确认源码每个vsync逐像素读取write-combined屏幕，连续6次减半再清黑。
第二版移除此淡出效果，每次进入off仅bzero一次屏幕，不从屏幕读回像素。
单次写黑持有fLock，避免并发on请求后执行过时的清黑；再次on将标记复位，
沿用原来最新帧重绘路径。每次实际清黑单独记录 `SWEET BLANK: clear_us=... bytes=...`。
此计时包含清黑及解锁开销，不是面板物理断电时间；黑色AMOLED像素变暗不等于
已实现完整DSI/PMIC关屏。构建/运行结果和感知改善必须另行验证。

### 第二版运行结果（run 41286172148）

构建/源码检查通过，7项离线测试通过。`boot-cepheus-perf-blank.img` SHA256：
`13dbf9985d248c086eb6507b891b18c729bc816fe537d4424de2f0a9e56d9724`。
已临时boot，未写boot或userdata，保留上一版回退。
`perf-blank-boot.log` 记录MKB_INIT完成、SpringBoard启动、one-shot Home完成，
USB ping正常，首次off：

```
SWEET BLANK: clear_us=716 bytes=10108800
SWEET PERF: ms=10000 submitted=6 presented=3 overwritten=2 copy_us=7274 max_us=2571 bytes=30326400 missed=0 off=1
SWEET PERF: ms=10016 submitted=0 presented=0 overwritten=0 copy_us=0 max_us=0 bytes=0 missed=0 off=1
```

该窗口写黑约0.716ms且无错过的定时槽，比上一版off窗口的missed=202/34更好，
但两次启动不是受控重复基准。仍待用户再次按电源键确认分层淡出是否消失。
触摸保持关闭、GPI DMA尚未实现；本镜像不能触摸，并非新故障。
