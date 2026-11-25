# CH592 固件与 HA 修复验证记录

日期：2026-11-26

## 当前：1.6.2 OTA 前保存燃气计数

- 实机根因：1.5.0 → 1.6.1 自然 OTA 后原始计数从 4.04 m³回退到最后一次持久化的
  3.95 m³，损失 0.09 m³；HA 保持总量 2021.37 m³并标记 `raw_reset_detected=true`。
- 共同 `OtaMode_WriteRequest()` 现在通过生产助手先执行 `SaveToDataFlashNow()`，再以原
  reason 写 OTA 请求；manual 和 scheduled 两个公开入口均复用该路径，周期保存阈值不变。
- 新 C 测试在旧代码上因缺少生产助手失败，实现后验证每次请求事件严格为 SAVE→WRITE、
  回调各一次且 manual/scheduled reason 不变。RISC-V objdump 也确认两个公开入口的机器码
  均先调用 `SaveToDataFlashNow`，再跳转 `OtaMode_WriteFlashRequest`。
- GCC 13.2.0、`rv32imac_zicsr_zifencei`、`INT_SOFT`、picolibc 1.8.6 全量重编译并链接
  35 个源文件；GPIOB/RTC/BLE 中断 `mret` 检查通过。SDK 保留既有 GPIO、DisableAllIRQ、
  I2C 警告，IAP 宿主测试保留既有 main 返回类型警告。
- 与 1.6.1 对象文件对照，只有 `APP/broadcaster.o` 有实质变化；`peripheral.o` 和
  `gattprofile.o` 仅调试路径不同，去调试信息后逐字节一致。
- 固件 Python 14 项、HA 58 项通过；9 个 C 测试程序通过，其中 3 个由 Python 套件驱动，
  另 6 个单独编译运行。
- APP bin：156952 字节，CRC32 `0x8043E30E`，SHA-256
  `18cfc3142328e6b46c01392c6ef9e77cd5503ffffce15007632623330a33b92c`。
- 完整 HEX：439140 文件字节，SHA-256
  `9d959a35ce8cd5257434494ed10c3b5ebfebe36f54bca5182de9dd41cdf32d13`；APP 区与 bin
  逐字节一致，2700 个非 APP 字节与 1.6.1 完全一致。
- **尚未实机验证**：新保存路径在 natural/remote OTA 中保持最后不足 0.1 m³余量。
  当前 1.6.1 首次升到 1.6.2 仍可能重复旧问题，应按 B 先保存再升级或升级后校准；
  本轮未修改 HA 校准，也未触发新 OTA。

## 历史：1.6.1 BTHome 对象顺序合规

- 普通 BTHome 对象顺序修正为 `0x0C / 0x3D / 0x3E / 0x4C / 0x53`；交换完整五字节对象，
  阈值写入偏移改为 14，燃气写入偏移改为 19，字段含义与 uint32 小端编码不变。
- 生产初始化器与燃气写入函数由 C 测试直接复用，独立手工推导的完整 31 字节包覆盖
  对象顺序、燃气 `0x12345678`、阈值 20/2000、5 字节版本和版本偏移 24。测试先在旧代码
  因缺少新布局接口失败，再随生产实现通过；ELF 中实际静态布局也已用 objdump 核对。
- APP 使用 GCC 13.2.0、`rv32imac_zicsr_zifencei`、`INT_SOFT` 和 picolibc 1.8.6，
  35 个 C/汇编源文件逐一干净重编译并链接成功。GPIOB/RTC/BLE 中断均通过 `mret` 检查。
- 固件 Python 13 项、HA 58 项通过；8 个独立 C 测试程序断言通过，其中 2 个由 Python
  套件驱动，另 6 个单独编译运行。IAP 宿主测试仅保留嵌入式 `main` 改名后的既有返回类型警告。
- APP bin：156932 字节，CRC32 `0x29015F62`，SHA-256
  `a41a42d9447bce44ea6a704d1915a748850f13062e26b00c117d9f1598e73254`。
- 完整 HEX：439088 字节，SHA-256
  `0d1023dd0f291672afe7424d623bf9849e60209b090bd6ad2ec3176599e958f7`。
- **实机结果（2026-11-26）**：自然三天窗口自动升级成功，协议 8，耗时 40.7 秒，VDD
  2869 mV，阈值 100 和原 BTHome/CH592 实体保持。原始计数从 4.04 回退到 3.95 m³，
  暴露 OTA 前未保存余量的旧问题，由 1.6.2 修复；HA 防重置保护保持总量不变。
  HA 顺序警告是否停止增长及 1.6.0 普通窗口功耗仍需继续观察。

## 历史：1.6.0 / 0.8.0 普通窗口免按键 OTA

- 普通 BTHome 周期窗口为 300 ms、50 ms 间隔，约 6 帧/分钟；300 ms 采样和 60 秒周期不变。
- 普通 GATT 只接受 `0x88` 固定帧并复用现有 OTA 请求/复位；普通控制连接 5 秒超时。
  上传、协议 8、Image A/B 和 IAP 未改。
- HA 强制 size/CRC32/SHA-256，按广播事件等待和重试，要求新的 OTA 广播序号，模式切换后
  重新发现 GATT；待写阈值仍先于上传。1.5.0 及未知版本不尝试普通模式唤醒。
- APP 使用批准的 `INT_SOFT` 构建，35 个源文件编译、链接成功；GPIOB/RTC/BLE 均通过
  `mret` 检查。固件 Python/产物测试 13 项、HA 58 项通过，8 个独立 C 测试断言通过。
  独立审查发现的下载取消/重复点击、阈值 GATT 缓存、无界断开及 CONNECTED_ADV 超时绕过
  均已增加失败回归并修复。
  IAP 宿主测试仅保留嵌入式 `main` 改名后的既有返回类型警告，故障注入断言通过。
- APP bin：156940 字节，CRC32 `0x37414A46`，SHA-256
  `fc5c901732283b9185d9ebfc6ceeb14290e970e2976ade83485279ddab83eb6b`。
- 完整 HEX：439104 字节，SHA-256
  `8bd8a8b9d0242cf6b6eba550791879bba94c04e55b96683e144abbf7c291adbb`。
- HA ZIP：23 个文件，`testzip()` 无错误，SHA-256
  `21bf018b69ff08cc56c3fcd6b9874ea7ee80901937cb06ce63704b16d440fc68`。
- **尚未实机验证**：普通 300 ms 窗口连接成功率、免按键完整 OTA、睡眠/主动扫描功耗、
  CR2032 电压下陷、原始计数/offset/阈值保持及协议 8 实际 CRC 耗时。0.2～1 µA 仅为理论预算。

## 历史：1.5.0 / 0.7.5 快速 CRC 门槛与 P/N 渐进调整

- HA 协议 8 不再要求冗余 CRC 能力位才进入快速分支；协议 7 及以下仍走 VERIFY。
  升级状态新增 `device_features`，阈值只读 Sensor 修复为 diagnostic，解决 HA 拒绝加载。
- 实测 1.4.8 → 1.4.9 曾直接 85% → 86% VERIFY，上传约 25 秒、VERIFY 约 294 秒、
  总耗时 319.3 秒，证明当时快速分支没有执行；1.5.0 用于验证修复后的实际路径。
- P/N 按下立即 ±1，前 3 秒每 300 ms ±1，之后 ±10/±50，松手不重复步进；C 测试覆盖
  P/N 短按、前 9 拍、进入十位步进、释放不重复及上下限。
- APP 使用批准的 `INT_SOFT` 构建，35 个源文件编译、链接成功；GPIOB/RTC/BLE 中断
  均通过 `mret` 检查。固件 Python 11 项、独立 C 测试通过，HA 34 项通过。
- APP bin：157500 字节，CRC32 `0x68ECEA41`，SHA-256
  `0352c5b2faba17048359a5b7e74e29486b7024a5adf70a68f2b4e5250803a8ee`。
- **1.5.0 实机 P/N 手感、回显及快速 CRC 仍待本次升级验证。**

## 历史：1.4.9 / 0.7.4 P/N 回显与设备页中文化

- BTHome 新增 `0x3E count32` 当前阈值；C 测试验证版本长度 5 时总长 31、AD length
  27、阈值偏移 19～23、版本偏移 24，以及 20/2000 的 uint32 小端编码。
- P/N 调整和松手保存均请求在下一个 300 ms 周期补发；平时 60 秒广播不变。
- HA 新增中文当前阈值实体，并将现有设备页实体显示名统一中文化；ID 与接口不变。
- APP 使用批准的 `INT_SOFT` 构建，35 个源文件编译、链接成功；GPIOB/RTC/BLE 中断
  均通过 `mret` 检查。固件 Python 11 项、独立 C 测试通过，HA 32 项通过。
- APP bin：157500 字节，CRC32 `0x342E9C2D`，SHA-256
  `bbff11179b3bfe3fe81291944f43e5575712d56bc21f7fcd3bff52f1214b8018`。
- **1.4.9 实机 P/N 回显及协议 8 快速 CRC 仍待本次升级验证。**

## 历史：1.4.8 / 0.7.2 快速 CRC32 恢复

- 固件 `0x85` 从逐位 CRC32 改为 64 字节查找表实现，协议版本升为 8；HA 只对协议
  8 及以上启用单次 CRC32，协议 7 及以下保留安全的逐块 VERIFY。
- 主机 C 测试把新算法与原逐位算法在 0、1、15、16、255、256、4096、221184
  字节输入上逐一比较，结果全部一致。
- APP 使用批准的 `INT_SOFT` 构建路径，35 个 C/汇编源文件编译、链接成功。
- APP bin：157476 字节，CRC32 `0x21728275`，SHA-256
  `8c567561b9b68af29744528a2639ec0b404be57bdfd14b9774bc8c6bc8e117f9`。
- GPIOB、RTC、BLE 三个中断入口通过 objdump 检查，均以 `mret` 返回。
- 固件 Python：10 项通过；独立 OTA 边界、计数/阈值、定时策略 C 测试通过。
- HA：26 项通过，包含旧协议 VERIFY、新协议单次 CRC32 和原始 BLE 广播识别。
- 用户已确认升级到 1.4.8；该次从协议 7 升级仍走 VERIFY。快速 CRC 留待下一次 OTA 验证。

## 实机记录：1.4.7 / 0.7.1

- 用户已确认从 1.4.6 自动 OTA 到 1.4.7 成功。
- 0.7.1 为绕过旧固件 CRC 命令期间断链，临时对所有固件使用逐块 VERIFY；升级
  成功但末尾明显变慢。1.4.8 / 0.7.2 恢复协议门控的快速 CRC。
- 上次失败并非设备 CRC 数值不一致：HA 未收到 CRC 响应，代理先返回
  `Insufficient authorization (8)`，随后报告 `is not connected`。旧固件长时间
  同步计算 CRC 是断链风险，但设备端最终断开原因码未取得，不能把推断写成定论。

## 历史：1.4.7 纯计数与 HA 校准

- 原始计数、P/N 阈值按键、uint32 自然回绕、DataFlash 双块追加日志均保留。
- HA 使用 raw / offset / gas_total / gas_consumption_total 模型；校准不通过 BLE
  写设备，也不改变统计消耗。
- INFO bit2 清零，`0x86` 停用，协议版本 7。

## 历史：1.4.6 中断兼容性

- 通用 GCC 未正确使用 WCH `WCH-Interrupt-fast` 的旧产物不得使用。
- 1.4.6 起改用 SDK `INT_SOFT`，并经用户 USB 烧录确认恢复广播/连接。
- 后续每次正式构建都必须保留 `tests/test_interrupt_returns.py` 的 `mret` 检查。

## 构建环境

使用 `/tmp/ch592-toolchain-20261122` 中的 Ubuntu RISC-V GCC 13.2.0、binutils
2.42、picolibc 1.8.6 和主机 TCC。GCC 13 显式使用
`rv32imac_zicsr_zifencei`，编译定义包含 `CLK_OSC32K=2` 与 `INT_SOFT`。

## 未覆盖项目

- 1.5.0 的真实 P/N 手感、回显、快速 CRC 耗时、低电压与断电测试尚未执行。
- IAP 安全拷贝修复只在 USB 完整烧录时更新；普通 OTA 只更新 APP。
- 3 天定时窗口功耗、睡眠计时误差及长期运行仍需实机观察。
