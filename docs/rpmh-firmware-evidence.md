# Cepheus RpmhDxe静态取证

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
