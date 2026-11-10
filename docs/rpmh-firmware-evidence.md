# Cepheus RpmhDxe静态取证

## 2026-11-10：NPA查询/退出与已验收模块身份

新增`scripts/verify-npa-state-evidence.py`，直接从已验收配对boot
SHA256 `2c93dc3333665bdbc8f1c835c0e59226f5b2a0cfda2678d4bb121a3fc86903d2`
解码压缩FD/FFS/PE32。NpaDxe、RpmhDxe、ClockDxe及**Cambria CPRDxe**逐字节
等于本机原件。设备目录CPRDxe（c216b483...）未被此候选采用，不能把它的
行为当作本轮实际模块；选用Cambria SHA256
`63a777c11fc8006ca540886e2ea3ebdafa56bea752d716ea09f7514756821fbb`。
包含不证明DXE已dispatch、节点注册或live状态。

REA/Ghidra12.1.2同一NpaDxe SHA。四段REA读字节与原PE file-backed映射一致：

- Query0x1670和QueryByName0x169c的BL分别到0x8b14/0x8bb4；wrapper把查询
  状态写到输出，EFI return固定0。不能仅用EFI_SUCCESS验收查询结果。
  read ev_a550a7cd6cf77bb8bcf897c8e29162edeed57e366462a1b6df7094565199aedd。
- 0x8b14先尝试definition+0x38的link query；未处理则到0x89c4，后者先尝试
  definition+0x30的resource query，再处理通用ID。默认ID0在0x8a58读取
  resource+0x34并写result+8/type0/status0。参考ABI称CURRENT_STATE，布局
  对应active_state；**默认路径是软件字段读取，没有硬件READ/ACK**。
  专属query可能不同，未证明实际CX/MX采用哪个路径。
  pseudo ev_009c365953d8a0f632e68bf502e6fe80d11ea579f3bac8bf7eba81a91b57f6e1、
  ev_a6d245a3ca875afe96a6b44b1c10c65389cfc7285d8422db837efc4a57a55a04；
  read ev_717428169ce79d4235d9d992c94a65c990d1ceea2d918d5c1affe8843a0164c5。
- NpaDeinit0x1a74..1a9c构造handle/GUID/table/null，读取BootServices+0x150，
  尾跳UninstallMultipleProtocolInterfaces。此完整短路径无join/churn/释放
  client/投票恢复；**不能当全局完成屏障或EBS资源交接**。
  read ev_20c43373c4b3663c55b64488d44891e1d649a5237f77db66d24e0e4c2823e552。

在已验收内层FV129个plain PE32 DXE文件中检索NPA GUID、/arc/client/及
cx.lvl/mx.lvl字节，14个匹配模块及file offsets已保存。不涉及其他类型FFS、
间接构造或压缩子载荷。默认协议标识不能证明运行资源；后续目标不靠猜目录。

同轮进一步追模块注册路径，仅静态观察：

| 模块/地址 | 观察与仍缺的事实 |
|---|---|
| ClockDxe0x6bec→0xa6e0/0x6a4c | 查ClockRailConfig引导默认值、注册可用回调；回调创建clock required/suppressible客户端，属于消费者，不证明CX/MX节点定义 |
| ClockDxe0x3150→0xa764 | 可定义配置中的clock节点，未证明其就是/arc/client/节点 |
| Cambria CPR0x4c4c | 本体只处理0x106/mmcx和0x103/gfx；创建CPR_AGING，发RPMh并churn single；不是通用CX/MX聚合器，其他caller未穷举 |
| PmicDxe0x1455c→0x162f0 | 动态构造节点/资源，driver候选0x1522c，DefineNodeCb；0x14b08命名on_off/mV/mode PMIC资源，尚不能绑定到/arc/client/rail_cx |

Pmic driver候选未被Ghidra定义成procedure；该拒绝不是成功反编译，不以缺
函数推断不可达。完整bundle保留no-return/间接调用等限制。上述证据不建立
current_cx/mx、crypto/GPU租约或停止协议，没有生产资源后端/新GPU执行版。
下一沿**已打包模块**定位实际CX/MX节点/driver/completion与最后EBS写入者。

全部材料位于`../logs/pas13-instruction-replay-20261110/`：npa-query、
cambria-cpr、clock-init、clock-registration、pmic-npa的evidence/snapshot；
npa-state-verification-v3.json核验PASS，8项合成PE/检索覆盖测试PASS。
本轮无手机/协议调用/SMC/MMIO写，无内核编辑或重新构建。旧receipt保留。

```sh
python3 scripts/test-npa-state-evidence.py
python3 scripts/verify-npa-state-evidence.py --root .. \
  --output ../logs/pas13-instruction-replay-20261110/npa-state-new.json
```

目标来自本机xiaomi-cepheus UEFI设备目录，PE/arm64，65536字节：
`RpmhDxe.efi` SHA256
`03ccd266ce0c9e268ac199c0f27903c9dbdac675c9374c797e4178673564a7fa`。
REA/Ghidra12.1.2分析，不执行/修改固件。完整证据在cepheus-port的
`logs/rpmhdxe-03ccd266-evidence-final.json`，快照
`logs/rpmhdxe-03ccd266.snapshot.json`。地址是此静态PE image-relative VA，
不是手机物理地址，不能直接在XNU读这些地址。

## 观察

- `0x14dc`通过UEFI BootServices安装表`0xe0f8`；表头为`0x10001`，包含
  `0x15b4..0x18e4`包装函数。ev_7c07ef033afd8ffd0bfb23bf1e0e72580d31c20f9cec5e00526ee3bfacddaefb，
  表字节ev_5bcfd2c50c3b7d9113298417de8b7fb3e9ff480e0375ba805e45fc3ebd5e7dc4。
- `0x2f80`按资源地址查找/插入24字节记录，计数位于image+0xe644，数组指针
  image+0xe648。ev_3d4d654200b59c2b29b2d6ce9c8e6988a9be1d763c2620e9eece939969ff6708。
- `0x48d4`在每个资源中查找DRV id对应的72字节链表记录，不存在则zero-init
  分配。记录+0为DRV，+0x40为next，资源+8为首指针。
  ev_0e359d2db24982f890ef9b7438bc23f978a076430ed712a613a04399e7ef7335。
- `0x4b38`写入set相关triplet：value在+0x18+12*set、valid字节在+0x1c+12*set、
  状态在+0x20+12*set，再经`0x4a3c`比较缓存数据。
  ev_10104ed4d0eb41791878e28b19561e627f66f40f37e8b02561c068174c1bbf87，
  ev_934c566b2948279469110b4ca8dd390a204fc146386b26d81769c959246e1fee。
- `0x4c34`选择缓存set值并写入+0xc和+0x10(valid)，可能将+4设为1。
  **它由`0x5ae4`的TCS配置路径在触发命令前调用**，因此这个影子值不是
  “硬件已经确认成功”的证明。
  ev_2b9de1a52a93df3363e8a5b71ef8cf0b83ad41672ebf84b69ded72a75fab5e5d，
  ev_6e8b164f46fb5682ca6f8c8ad31646e16c1e8907f5ee61dace20d15eb903f514。

## 推论与限制

观察支持存在“资源/DRV/set”软件缓存，可作为未来UEFI受控handoff的研究方向；
**不支持**直接把+0xc影子值当作当前ARC投票，更不证明READ响应语义。
需先处理未完成请求、锁与ExitBootServices期间的最后写入、loaded-image身份、
内存映射可读边界、空链表/外部投票、跨内核时序。不能按固定位移扫描手机RAM。

本次没有找到已验证的公开只读“返回本DRV已完成投票”协议函数；没有穷举所有
函数，不能据此说不存在。字符串`holding_read_request`存在，但没有交叉引用
不代表不可达。Ghidra对部分包装函数存在函数边界限制，必要处以反汇编补证。
REA bundle中unknowns=0仅表示工具记录格式，不表示硬件语义没有未知项。

因此GPU电源写入仍需可靠当前投票/所有权证据，不能因发现缓存就取消门槛。

## 2026-11-06：READ response 路径补证（仍不是 current-vote 证据）

复用同SHA的REA snapshot；本轮导出
`../logs/rpmh-read-static-20261106/evidence.json`。没有读取活RAM或执行固件。

- 字符串RscHalReadResponseData交叉引用指向`0x7578`：按DRV/TCS/CMD做
  geometry验证，然后读MMIO `base+0xd40+tcs*0x2a0+cmd*0x14`，另一DRV
  模式为`base+0x20d40+...`。这是HAL response寄存器，不是软件投票缓存。
  `ev_5698e6b71c0bf22fdf73b7d7ce700f69da6a6154839aa083152784c857a7e8bf`。
- `0x590c`按DRV/TCS映射调用HAL，返回response值；`0x4784`在完成请求队列中
  针对类型2的命令读response，再由`0x3e48`写入每client请求记录（valid=1、
  value在+4）。其他类型才走`0x4d60`完成缓存。
  `ev_bdac4a1f967dc9cfa60f49a36438c8550654149a0b028943b1ab82fb9ddbfc85`、
  `ev_668dbfadee54cb109d6991d680d97d839bfe61e9d7420750846afebda7d645fd`、
  `ev_1f62b0890bc0600d886cdc7b9e7a077edb85d59c2691b0f14710caf5086e8728`。
- `0x3174`只是初始化全局单命令请求，`0x30fc`再走`0x2cdc`命令set机制。
  当前观察的协议wrapper `0x1850`设置回调，`0x18ac`进入sleep，
  `0x18c4`退出sleep；不是三种“返回当前投票”的接口。
  `ev_db8f2a65ed10c303853aa93dcbe2934ae8f7c451073888f96479df0f2a40b6b3`、
  `ev_7d2c529f56e8c7cab38319474515bb42298635625e321069cad3284a78bf8920`。

推论：READ的返回值来自硬件response，不可与resource/DRV/set影子投票混为一谈。
尚无RPMh硬件规范或本机证据证明ARC READ response是本DRV旧request，而非
聚合/当前输出。因此不使用它填写power.prepare的current_cx/current_mx。
这轮调查缩小了未知范围，**没有解除投票继承或共享所有权阻塞**。
协议多个wrapper仍未被Ghidra定义为函数，其原始字节已读取留证，不能据此宣称
穷举所有公开接口；没有按image-relative地址扫描活RAM。

## 2026-11-08：协议资源记录getter和完成标记补证

同一SHA原件、Ghidra12.1.2，只做静态分析；导出
../logs/gpu-power-backend-20261108/rpmh-protocol-evidence.json。没有执行固件、
调用协议或访问手机RAM。以下地址仍为PE分析VA，不是手机物理地址。

- 协议表0xe0f8中slot偏移0x50指向0x1758；wrapper在0x176c调用0x293c，
  将返回指针写入x1所指输出。0x293c按资源地址二分搜索已有24字节记录，
  未命中返回NULL，无分配/触发请求。这给出未来受控交接的入口候选，
  不是已核验UEFI ABI或可在XNU间接调用的接口。
  表ev_346506b31ed0a4be1a2ff380b3cc2340346d48298bf5cadf5498124513b4509c；
  call指令ev_e3c9473ec22acb72ee32e9e28b86277c2eb61d36a1fa34f251e69601c31d5100；
  getter ev_56424597c7151bcc5fa0246b3da4c0d4986763195bc15661d922f6138438fae6。
- 0x4d60在完成路径把DRV记录+4清零，再调用0x4a3c重新计算各set状态。
  0x4784区分类型2的READ response和其他完成命令，后者调用0x4d60。
  ev_39739129e0a83fb9d33a9078545068d6ff53ee97517a18f58abac661dbe7428c、
  ev_d709b3ad2109c5279c436d317409d90e9cb364a77bb788b4877b4e9deac21cec。
- 0x5ae4配置TCS时，无论是否AMC，非READ命令都先调用0x4c34写影子值；
  只有AMC分支随后实际触发。因此+4==0和+0x10有效仍不足以证明+0xc是
  已完成active请求。休眠/唤醒配置及最后一次ExitBootServices写入还需处理。
  ev_3c87926d1abcbec25c2afc33476f26648a90d3949b47ba4391a696b0d0fc8718。
- 协议尾部0x1868、0x18dc、0x18e4的实际指令直接返回
  0x8000000000000003；不能把未定义成Ghidra函数当成隐藏getter。
  0x18dc指令ev_37deb161915230939100468a83ba3a9720e920a727beb4ebe480846992905297。

结论：找到了无需扫描RAM的记录查找入口候选，但未建立完成请求快照或最终
交接。不能据此实现一个始终成功的power.prepare，也不将缓存状态冒充硬件
ACK。下一必须在有身份/边界验证的UEFI上下文处理请求完成及最终交接，再
由XNU唯一共享资源管理者接管；当前没有这段实现。没有取消GPU启动门禁。

同轮通过gh取得SDM670参考源码，固定仓库Rivko/android-firmware-qti-sdm670
commit 20bb8ae36c93fc16bbadda0e0a83f930c0c8a271；4个文件的git blob/SHA256
保存在同证据目录reference-sdm670/identity.json。这是异型号参考源码，
不声称它就是本机RpmhDxe的构建来源。EFIRpmh.h的0x10001协议布局在偏移
0x50命名为rpmh_find_resource_command(address, out)，与已查本机表和call相符；
rpmh_resource_commands.h中DRV记录的state/priority/vote_at_rpmh/local_votes/
next布局也与上述静态访问吻合，尚不构成运行态交接保证。

参考rpmh_resource_commands.c:457的update直接覆盖该DRV/set的data，不按
client handle求max。不能通过“新建RPMh handle并投最小GPU档”自动保留原有
消费者需求；handle用于请求跟踪，不是新的硬件投票槽。churn_all也只等待
对应handle的请求，不能冒充全局所有客户端完成屏障。需进一步接入已有的
上层资源聚合器，或建立已核验的最终请求交接，不能用裸RPMh新client绕过。
