# cepheus ST-FTS 触摸 bring-up

## 已验证与未验证

主屏幕、电源键和单次写黑熄屏已由用户确认。物理触摸尚未成功；不能把模拟
Home触发进入主屏幕当成触摸可用。
独立FIFO测试已证明SE17 `FIFO_DISABLE=1`，不是一项可写软件开关。
原厂驱动在此条件下走GPI/GSI DMA，不能绕过它直接访问FIFO。

## 只读GPI/SMMU探测：run 41287180662

kernel SHA256 `ae5c07fc0874850247c8e09b2f478763600d58a9e0d48ed56e7d2910827dcc5c`。
boot SHA256 `8478f45e96dcdc38de3420262f3ccef1aac940aae6ab8a3d7f34c3c664ffbc47`。
日志 `gpi-probe-boot.log`：允许mask0xfa内 i1/i3/i4/i5/i6/i7 的channel0/1、
event context、ring、scratch均读为0；这与未配置一致，但不能只凭全零断言
所有权或固件可用。SMMU GR0 `cr0=0x2b0406 id0=0x4c017e5e id1=0x60000038
id2=0x5555`，94个SMR中无stream0x7b6匹配，clientpd=0且unmatched_fault=1。
不能直接用CPU物理地址提交GPI DMA。探测不改变SMMU或GPI寄存器。

继承GPIO状态：SDA55/SCL56/RESET12/IRQ122/VDDIO84均cfg=1（输入下拉）、io=0。
不能假定上电/复位已由固件保持，更不能把无响应全归因为传输引擎。
AVDD来自L1C 1.8V，现有RPMh helper仅针对sweet L11C，不能照搬3.2V到L1C。

## 独立GPIO I²C诊断路径

为了在不配置SMMU/GPI情况下验证芯片，增加显式opt-in `sweettouchgpio=1`。
这是一种使用TLMM GPIO的独立I²C主机，不是强行打开GENI FIFO。
前置：SE17为I²C且命令不活跃；只有本驱动线程访问引脚。

- GPIO55/56为SOUTH，12/84为NORTH。SDA/SCL latch固定为0，输出低或输入释放，
  从不推挽输出高；10µs半周期加MMIO开销，速度低于50kHz。
- 拉高VDDIO84、释放RESET12；不写PMIC电压，不上传/擦除触摸firmware。
- 组合5字节芯片ID命令+重复START+2字节读取，期望36:39。
- START前检查总线空闲；clock stretching有界等待；NACK、仲裁丢失和超时
  明确错误。仲裁/忙/超时仅释放总线，不发送恢复脉冲；终止时还原SDA/SCL配置。
- 成功读ID后才执行厂商active multitouch命令并读取事件FIFO。
- 协议纯测试覆盖重复START、ACK/NACK、256字节读取、超时、busy、仲裁及长度。
  测试通过不等于硬件成功；引脚拉高时间、AVDD状态、芯片firmware仍待真机证据。

此路径保留性能/熄屏修正，用`make-performance-boot.py --touch-gpio`打包。
DMA驱动仍未实现，GPIO若可用可先提供输入基线；不能承诺同等性能或多点流畅度。
