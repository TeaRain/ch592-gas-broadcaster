# 1.4.7 / 0.7.0 修复验证记录

日期：2026-11-22

## 已完成

- 固件 APP：35 个 C/汇编源文件使用 RISC-V GCC 13.2.0 编译并链接成功。
- APP 链接占用：FLASH 157408 字节 / 216 KiB；链接脚本保留完整 26 KiB RAM（含栈）。
- IAP：编译、链接成功，FLASH 2696 字节 / 12 KiB。
- 生成 `obj/Broadcaster.hex`、`obj/Broadcaster.elf`、`obj/Broadcaster_full.hex`、
  `obj/Broadcaster_app.bin`，并更新 `firmware/` 发布文件。
- APP bin：157408 字节，CRC32 `0xEF4F1FD0`，SHA-256
  `a3e6299cf14484bdca573eb84122812f6cf991893faa085210b96350eb7a8fe3`。
- 完整 HEX 中的 APP、JumpIAP、IAP 与各自构建产物逐字节一致。
- Home Assistant 集成版本更新为 0.7.0。

## 自动化测试

- 固件 Python：9 项通过（MounRiver 测试目录排除、正常/异常 HEX 基址、拒绝时
  保留旧完整 HEX、Git commit/push 失败、零块大小、燃气日志序号恢复策略、中断 mret 检查）。
- APP 纯 C 校验：正常 OTA 命令、短包/超长包、地址/长度/擦除边界、阈值 magic 偏移通过。
- 定时 OTA 纯 C 策略：4319/4320 分钟边界、超期触发、手动 480 tick、定时 96 tick、
  未知原因回退手动超时均通过。
- IAP 主机故障注入：成功拷贝、A 擦除失败、写入失败、写后不一致 4 类场景通过。
- HA：22 项通过（全部任务取消、文件读取期间 abort、自动失败状态、BLE 写入互斥、
  END 前广播基线、前台写入 unload、并发路径启动预留、同版本新广播要求、冲突操作拒绝）。
- 两个仓库全部 Python 文件 AST 语法检查通过。

## 通用 GCC 中断兼容性（1.4.6）

- 1.4.4/1.4.5 的通用 GCC 构建未正确使用 WCH `WCH-Interrupt-fast`，GPIO/RTC 中断以普通 `ret` 返回，烧录后无法正常运行；这些产物不得再使用。
- 1.4.6 改用 SDK 自带的 `INT_SOFT` 模式，GPIO、RTC、BLE 三个 APP 中断入口均通过 objdump 检查，确认以 `mret` 返回。
- 新增 `tests/test_interrupt_returns.py`，防止后续构建重新产生普通 `ret` 的中断入口。
- 用户已实际 USB 烧录 1.4.6 并确认设备恢复广播/连接。实际功耗、3 天定时和自动 OTA 仍按后续实机结果验证。

## 1.4.7 纯计数与 HA 校准

- 纯 Python 模型通过负 offset、校准不改变消耗、旧广播回退、异常复位、uint32 自然回绕和序列化往返测试。
- 实体测试确认 raw / gas_total / gas_consumption_total 三个角色，按钮及旧 set_gas 只做 HA 校准，不进入 BLE 待写。
- 固件策略测试确认自然回绕差值、阈值步进和上下限；P/N 松手只保存一次阈值，原始计数不受按键影响。
- 独立审查发现跨 300 ms 短按未增减阈值后，新增方向状态机测试并修复：短按 ±1、长按 ±10/±50 均通过。
- INFO bit2 已清除，0x86 不再处理；协议版本 7。
- 1.4.7 尚未实机验证；1.4.6 的实机恢复记录仅用于证明 INT_SOFT 构建路径可用。

## 构建环境

使用临时目录中的 Ubuntu `gcc-riscv64-unknown-elf` 13.2.0、binutils 2.42、
picolibc 1.8.6 和主机 TCC 0.9.27，没有安装系统软件。GCC 13 需要显式的
`rv32imac_zicsr_zifencei`，以包含旧 MRS 工具链隐式接受的 CSR/FENCE.I 指令。

构建仍有 SDK/旧源码警告，包括 `__INTERRUPT` 属性形式、GPIO/I2C 宏转换和
`DisableAllIRQ` 先使用后定义；这些不是本次新增警告，未为消除警告扩大修复范围。

## 尚未执行

- 未通过 WCH-LinkE 烧录，未在 CH592 实机运行。
- 未执行真实 BLE OTA、阈值/燃气写入、低电压或断电测试。
- 未实测定时窗口的功耗、睡眠状态下 3 天计时误差或定时自动升级；这些需要实机验证。
- 普通 OTA 只更新 Image A；本次 **IAP 安全拷贝修复需要 USB 完整烧录**才会在设备上生效。
- 未提交、未推送 GitHub/Gitee。
