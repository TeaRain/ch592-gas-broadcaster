# CH592 固件与 HA 修复验证记录

日期：2026-11-22

## 当前：1.4.8 / 0.7.2 快速 CRC32 恢复

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
- **尚未完成 1.4.8 实机 OTA；快速 CRC 在你当前 ESPHome 代理上的实际耗时与连接
  稳定性仍需实机确认。**

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

- 1.4.8 的真实 BLE OTA、快速 CRC 耗时、低电压与断电测试尚未执行。
- IAP 安全拷贝修复只在 USB 完整烧录时更新；普通 OTA 只更新 APP。
- 3 天定时窗口功耗、睡眠计时误差及长期运行仍需实机观察。
