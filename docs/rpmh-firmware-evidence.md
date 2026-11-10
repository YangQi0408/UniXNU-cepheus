# Cepheus RpmhDxe静态取证

## 2026-11-10 14:17：DAL默认电压配置与Clock消费者绑定

复用已验收patched DALSys/ClockDxe身份，只读原件与REA记录。DAL PropBin
0x1da18为packed属性`0x12800011,index=1`，名称`ClockRailConfig`；sized entry
0x1d258（size8）指向0x1f230。该16B记录表依次是`/arc/client/rail_cx`、
`/vcs/vdd_gx`、`/arc/client/rail_mmcx`、`/arc/client/rail_mx`，每项默认值
`0x100`，后有NULL/0终止记录。没有单位或当前硬件状态证明。

Clock0xaf5c经0x1b6c取`ClockRailConfig`，按16B步长比较资源名、读记录+8
并写输出。0x6bec调用该helper，将默认值放入本地rail结构+0xdc，再以
callback0x6a4c调用ResourceAvailable wrapper0xa6e0。这进一步绑定了
**默认配置的消费者路径**，没有找到ARC节点注册者，也不能据此把current_cx/
current_mx填成0x100。Pmic/DAL动态命名、外部协议或其他初始化仍待追查。
读取“没有xrefs”的工具结果不作为不可达证明。

私有证据`dal-clock-rail-config-evidence.json`（25 records）和
`clock-rail-consumer-final-evidence.json`（57 records）及对应snapshot。
`verify-icb-registrar-evidence.py`增加属性/结构/四默认值/终止边界、14条
Clock读取与可用回调指令核验；v6普通/优化receipt PASS且相同，SHA256
`d50ad73ff9aaea7bcb0d903e26b3544f65b1ca48977127800aac8503def56e15`。
24合成测试普通/优化均通过，v1-v5均保留。没有内核/UEFI代码改动或构建，
手机保持正常配对版，无新测试点；PAS13=false与资源/SMMU接管尚未解决，
GPU/Metal/认证未执行。14:08批已提交推送`a6a332e`，本节为后续追加。

## 2026-11-10 14:08：内部隐式等待与EBS清理补证

同一已验收RpmhDxe原件SHA03ccd266...，REA/Ghidra12.1.2与本机Capstone
核验；只读文件，不执行固件/协议/SMC/MMIO，也没有新内核或手机测试。

**修正13:49的过宽推论：**slot0xb8/0xc0返回EFI_UNSUPPORTED仍成立，外层
barrier本身不保证完成；但不能由此推断整条提交链缺少等待，或断言CPR请求
没有完成。实际slot0x78→wrapper0x17f4，0x181c调用issue_command_set0x2cdc。
其共同返回点0x2ec8设置kind=1/churn=1、handle=x19/request_id=w20，再在
0x2ed8调用0x2a1c→0x4014；后者查pending record0x3e8c，必要时循环/递归
等待0x3f54。0x3f68→0x561c→0x735c读HAL IRQ位图，type0取base+0xd04&0xf，
type2取base+0x20d04&0xff；0x3f70无IRQ则继续，0x3f80进入完成处理0x2b20。
这是**handle范围的隐式churn**，不是所有客户端/EBS的全局屏障。该轮询循环
没有超时；静态代码不证明手机观察到ACK、错误处理正确或已有完整所有权。

**首次定位实际EBS写入路径：**entry0x14dc以BootServices+0x170创建type0x200、
TPL8的event，GUID0xe028为`27abf055-b1b8-4c26-8048-748f37baa2df`
（本机MdePkg EventGroup.h的ExitBootServices GUID），callback0x149c。
callback先0x14b0卸载RPMh协议，再尾跳0x38f4遍历配置的DRV/TCS：

- 0x3938→0x7d2c将TCS控制寄存器写0，type0为base+tcs*0x2a0+0xd14，
  type2为base+tcs*0x2a0+0x20d14。
- 0x3944→0x7e14将TCS命令使能写0，相应offset为0xd1c/0x20d1c。
- 完整0x38f4的两次直接调用只有上述clear函数，无pending/completion检查。
  这只是已定位的一个EBS回调，不是全部回调及顺序的证明；HAL校验失败路径
  仍存在，静态event构造也不证明运行时注册成功。

不能把TCS寄存器清零扩展成“ARC电源投票被清除”。但pre-EBS缓存快照不能
直接冒充最终XNU交接状态，必须处理此回调、其他最后写入者与接管顺序。
DarwinBoot.c现有GetMemoryMap→ExitBootServices→DarwinEnter源码路径没有
资源quiesce；这里仅作源码观察，不把未核对的源码当已验收二进制行为。

完整私有证据：`../logs/pas13-instruction-replay-20261110/`
`rpmh-implicit-churn-final-evidence.json`（61 records）和
`rpmh-ebs-final-evidence.json`（81 records），分别保留snapshot。
EBS clear原字节Evidence为`ev_c2646efd12d8a4b9aa30e293414570a79a88574de6ccba45bb14d053e59aa49c`、
`ev_4cbb1fef0d38d7f4e662fb98de819faf8bd8354ec6fd5f16288f93222741e79c`、
`ev_105a5d4029cef478b7c8ad72ca8f58a5897d492ec799c8d9047bc734d2910589`。
核验脚本增加issue slot、20条隐式等待指令、43条event/uninstall/clear指令
以及完整clear loop调用约束。`icb-registrar-verification-v5.json`和优化模式
receipt均PASS且逐字节相同，SHA256
`363162d04711addb3047d1f4367505fe6e42089d5775037267bcbe5fab8b5e73`。
21项合成测试普通/优化模式通过，NPA8测试通过；旧v1-v4 receipt不覆盖。

下一查ARC节点/配置、其他EBS写入者和唯一XNU共享所有者。尚无资源生产后端
或新的GPU真机点；手机保持正常配对版，PAS13=false/CP halt不改，
GPU scratch/fence、Metal、PAS认证均未执行。

## 2026-11-10 13:49：ICB注册者已定位，完成屏障有不支持分支

本机只做原件静态分析与有界文件核验，无手机操作、协议调用、SMC或MMIO。
已验收boot内10个模块PE字节与原件一致，新增patched DALSys及Epsilon Limits；
129 plain DXE inventory新增`/icb/arbiter`字节匹配。不是运行态注册证明。

**首次连通实际配置与注册路径：**

- patched DALSys SHA97231a87...的device表0x1dd30中`/icb/arb`对应属性offset
  0x368。packed property0x1db00是`icb_info`、type0x12、结构索引20，不是
  SMMU的索引21。sized entry0x1d388→config0x2dfd0→node0x2df78→resource
  0x30e80，节点/资源同名`/icb/arbiter`。原件中driver/plugin均初始NULL；
  这不是可直接运行的后端，也不能把NULL当注册失败。
- 实际ClockDxe SHA babe80a2...入口0x1a74调用0xc520。该初始化先调用0xe474
  准备fabric/RPMh handle，再0xd9d4取得配置；为主node写driver0xc650，
  为resource写plugin0x432a0，0xc5d4→0xa764→NPA slot0x20定义节点。
  standalone node也经0xc628定义。外部DAL提供数据、Clock携带ICB实现；此前
  “Clock只是消费者”仅适用于已查ClockRailConfig/MDSS路径，不覆盖本链。
- plugin0x432a0字段`[0xc704,0x1400,0,0xc930,0xca0c,0,0xcd50]`：0xc704
  更新，0xca0c create。创建参数每master/slave pair8B；更新len>>3，每请求
  32B。0xcc24原指令解低16位type1/2/4，bit16附加字段位于+0x18；具体单位、
  master/slave身份及crypto请求ABI还需独立关联，不能仅凭数字推断带宽。
- 主node driver0xc650仅`LDR w0,[x0,#0x30]; RET`，直接返回requested_state。
  真正工作在plugin→0xdb80→0xe9dc。节点状态/EFI同步成功仍不是hardware ACK。

**外层barrier分支不保证完成（内部等待见14:08修正）：**

0xe9dc批量提交active/sleep/wake；条件分支0xeb78调用0x128fc。该包装读取
RPMh协议slot0xb8（固定0x10001参考布局名`rpmh_barrier_all`），0x12964 BLR后
直接恢复栈返回，不检查或传播EFI结果。本机RpmhDxe协议表0xe0f8+0xb8→0x18dc，
实际`MOV x0,0x8000000000000003; RET`即EFI_UNSUPPORTED；slot0xc0同样不支持。
这是**已验证的函数字节/分支关联**，不是手机执行记录，不证明该分支本轮触发；
也未穷举替代churn/IRQ路径。该barrier分支本身不能证明请求已经硬件完成。
此前已查Cambria CPR0xafa0调用slot0xc0，同样不能把包装调用当成功churn。

关键Evidence：
`ev_b25bf89e4bee18760b4eb6a724949939a00309c9f682299fecf9e977c04a733b`
（Clock注册），`ev_a3cc7f01211cd7134a7a55bf902298b2685ba143c0cfe5c3574cb794eedfeedb`
（plugin原指令），`ev_1c536a902e0a0972d0b7a6ca39d5c25cc4605c64447eb2f7dadec6d8e81fd2b4`
（DAL node），`ev_ca77c40358cd6202e996a0b66e57ea3bf78034a5ada526d02a6ae1a0dc508a8d`
（Clock包装），`ev_0953fd40f4c70bb9e76a8c1e2dd53f9392a2d035245d674d0b4e641a9e2fec84`
（RPMh不支持指令）。bundle/snapshot位于同私有logs目录：dal-icb-config、
clock-icb-barrier、rpmh-barrier-slot。Limits0x4ae4为PMIC/GFX消费者且包含GPU/GDSC
写入，未执行；其证据limits-resource-evidence保留，不当ARC registrar。

新`verify-icb-registrar-evidence.py`连同原boot解析器核验上述原字节、DAL配置链、
plugin表及17条关键指令，`icb-registrar-verification-v3.json` PASS，SHA256
`024527a2771aa3726ac75c2b8f70db513c61681f096511505322466e9b36bb84`。
13项合成evidence/packed-DAL/边界拒绝测试通过，`python -O`同样通过；原NPA
8测试通过，10模块receipt为npa-state-verification-v5.json，旧receipt不覆盖。

下一继续查实际ARC rail registrar、ICB硬件完成记录与最后EBS写入者，再设计
唯一XNU共享所有者。尚无生产资源acquire/lease/quiesce后端、无新内核/构建/
真机测试点；PAS13=false、CP halt维持，GPU scratch/fence/Metal/认证未执行。

```sh
python3 scripts/test-icb-registrar-evidence.py
python3 -O scripts/test-icb-registrar-evidence.py
python3 scripts/verify-icb-registrar-evidence.py --root .. \
  --output ../logs/pas13-instruction-replay-20261110/icb-registrar-new.json
```

## 2026-11-10 13:32：crypto资源链与模块身份续查

模块身份核验扩为8项，新增Env/Pmic及Epsilon Hash/Cipher；最终receipt
`../logs/pas13-instruction-replay-20261110/npa-state-verification-v4.json`，SHA256
`47d35ae4ee23e26735337cf4a6f6e722c59431a7bb78aec4301c8adfb92403be`。
8项合成测试仍通过。这只更新离线脚本，无生产内核/固件/phone操作。

已验收HashDxe的SHA是`0fd065458824be63d61a851d039b5d3198d556a6bee3ecfec423b9a24e3be266`，
来自Epsilon/BOOT.XF.3.0-00527-SM8150LZB-1，不是cepheus设备目录的58afbd5e...。
首次误选设备目录版只有strings/xrefs，已停止，另存
hash-unselected-reference-evidence.json；不把其结果当本轮已打包行为。
Cipher也为Epsilon版SHA94ed2f29...。两者PE与已验收内层FFS逐字节一致，
这仍不是运行时dispatch或PAS对其资源需求的证明。

- EnvDxe0x2358在NPA协议可用后为`/icb/arbiter`注册ResourceAvailableCb；
  所展开路径只有client创建/向量请求/可用回调，不定义ARC/ICB节点。
  ev_6b183416d2e52345ff8de84c98bdc1dbfd1048598beb081173bb72d31f35f81f，
  证据env-npa-evidence.json。
- 实际Hash0x5138创建名为`/clk/ce1`的**向量客户端**，其resource是
  `/icb/arbiter`，type0x400、创建参数8、配置输入0x17；不是直接时钟API。
  准备的请求字中有0x10004、1600000000、0x34b0c50，0x9d90验证请求数后
  通过0x9ee0调用NPA IssueVectorRequest(slot0x90)，长度8。这些数值的
  ICB结构/单位/master-slave含义尚未核验，不能直接移植为PAS/GPU带宽票。
  ev_0d6febbc2af7c460bb2e428973c36c0af053ec06a26d5105edc74e0f8b969bd7、
  ev_8c0ccc8dcacd0d71313aadaa6afdad551f60cb1d1849c981704d85d57cd59809；
  hash-selected-vector-evidence.json（保留原字节读和Ghidra局限）。
- Clock目标helper0xaa64在展开本体内构造4组CPU频率表；0xad68只设置
  context+0x60字节1。它们未提供新的ARC DefineNode证据；不是穷举整个
  ClockDxe/间接callee。clock-target-followup-evidence.json。

结论：找到了本候选crypto带宽的具体消费者调用链，但还没有仲裁器driver/
完成屏障/最终EBS状态或XNU所有者。不能从NPA客户端调用返回推导hardware
ACK，不能把DXE函数指针当运行时接口，不能让PAS acquire返回成功。
下一定位实际`/arc/client/rail_cx`、rail_mx与`/icb/arbiter`注册者及driver，
连同最后EBS写入/停止与lease；supported=false和CP halt保持，GPU/Metal未执行。

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
