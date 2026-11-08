# 有序触摸后：同核SoC基础只读候选

更新：2026-11-08。本候选已临时启动，用户判定**触摸FAIL**，中央操作确认
完成，主屏幕/电源确认正常。420s452包/123804B，19条health到19000次
events6/hid_calls2/errors0，仅一组1079,0边缘enter/leave；四组sample25齐全。
原日志SHA `a929e459341d37375bd4f72452b5a9e98baa3f8a9452668b8f03713adf1ee566`。
证据`../logs/soconly-ordered-60a2dc5-live-20261108-1235/`。用户选择保留现场，
先离线排查；不恢复/重启/增加fuse/ARC/GX/SCM，镜像不能当可用基线。
同核touch-only先前PASS不覆盖本轮失败，也不证明cephsoc的单项因果。
下面打包receipt仍保留原始hardware_tested=false，实际验收以result为准。

## 已完成

- [x] touch-only有序上报候选本轮用户验收：主屏幕、触摸、电源熄屏/唤醒后
  输入均正常。日志575包/161064B，所见health最后events1148/errors0。
- [x] 沿用CI42391773972 / commit60a2dc5的相同kernel，无新内核改动/构建。
- [x] 独立另名打包SoC基础候选，保留所有已验收/失败镜像与历史receipt。
- [x] 真实mtools核验boot/header/ADT/UEFI/其他EFI文件；严格检查相同kernel、
  全部TOML字段不变，参数仅新增`cephsoc=1`，其余参数顺序也不变。
- [x] 配置门禁11项host回归通过，新增缺参数/重参数/额外探测/其他字段改变拒绝。
- [x] 真机同核单变量测试已执行：触摸FAIL，停止后续新增探测。

## 镜像与证据

| 材料 | SHA256 |
|---|---|
| `../dist/boot-cepheus-soconly-ordered-60a2dc5.img` (70680576B) | `5d7e64ae5740ad4ab82e2e6ef614e537bd344e483b77aa44f0470cc1f8be8a24` |
| 同名普通核验`.json` | `b503e7e2a68cff8850292e6329095f9603755bd86d322489b01b90ef26788d59` |
| 同名`-delta.json`严格单变量receipt | `2275f6494ecb9daa45643bbd9204f2316b5c5810e5d7964fafd112e1505d2e6f` |
| 相同`../inputs/ci-42391773972/kernel.macho` | `6134404e1d01586221f844aaba9579de452585bd9b9737f6f12980022d265211` |
| 本轮已验收touch-only参考镜像 | `a00d1e29087d0f28e695eb0ec91166c48a917fde45685b655c46162541c9f074` |
| 本轮touch-only原日志 | `6c9758bbc1ad2b937c9e9972441318e4c45734ab914667fd67ca8f717d1a959f` |

真机证据在`../logs/touchordered-60a2dc5-live-20261108-0111/`。
本轮host验证汇总在`../logs/soc-ordered-offline-20261108/receipt.json`。
SHA256 `02a4f67f41d92873391341590d174a72142bbd4c15b3bec1dba1d99f1a0fdd8f`。
旧kernel参考的反向测试按预期exit1/reference kernel changed，不生成receipt；
触摸重放11项和生产FIFO故障回归再次通过。没有为host工具变更重新构建XNU。
用户本轮通过不证明间歇空FIFO根因已修复。采集末ECM断开，后见MTP/ADB，
原因待用户说明；不得写成当前手机一定还在iOS。代理未重启/恢复触摸。

## 唯一新增范围

`cephsoc=1`启动已有独立诊断线程：45s后读OSM LUT/请求/计数器、GCC状态、
门控GPUCC/GMU状态和TSENS；等待后有界采样到sample25。不是纯CPU探测：
CFG_AHB不满足则不读GPUCC，GMU blockers非零则不读GMU，基础路径不读GX。
这些状态与blockers不证明所有权、固件启动或GPU可执行。

`cephfuse/cepharc/cephgx/cephscm/cephboost/cephmem`全部缺席，
不发SMC/PAS查询/认证、频率试写、ARC票、电源恢复、DMA/CP或触摸reset。
`osm_trial_start()`缺boost时返回，restore没有active trial则不写；SCM入口
缺`cephscm=1`直接返回。沿用已验收GPIO触摸/ldoc1 enable-only初始化，
运行iOS仍可能写既有root/持久日志，不能描述为整机无写入。

## 下一真机轮

1. 重新核对`67eea163 / cepheus`和镜像SHA，先开MAC限定watcher及新目录
   `capture-usb-log.py --duration 420`，再临时fastboot boot；不flash。
2. 确认ST-FTS/ordered标记，空闲20s，再中心拖动/短点/边缘/双指，物理电源
   熄屏唤醒后复测；用户分别验收主屏幕/触摸/电源，不用事件统计替代。
3. 等四组OSM sample25；检查无SCM/ARC/fuse/GX/boost/memory日志、总线错误
   或panic，绑定日志SHA与结果。UDP/RAW限额不提供完整时延或长稳证据。
4. 失败保留现场，停止增加探测，不自动reset/scan重发/电源恢复。
   通过也只证明本轮基础探测共存；下一轮才单增fuse，再ARC，再GX，
   各自同核独立验收后才考虑SCM QUERY7，禁止跳到GPU执行。

离线命令使用`cepheus-build:latest`容器，network-none/UID1000/cap-drop/
no-new-privileges，仅挂本工作区。严格核验新增选项：

```sh
python3 scripts/verify-ready-boot.py \
  ../dist/boot-cepheus-soconly-ordered-60a2dc5.img \
  ../dist/boot-cepheus-touchordered-60a2dc5.img \
  ../inputs/ci-42391773972/kernel.macho NEW-RECEIPT.json \
  --soc-only --only-added-arg cephsoc=1
```
