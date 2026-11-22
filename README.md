# CH592 燃气表 BLE 广播器固件

CH592（RISC-V，BLE 5.4）燃气表读数器固件，使用 CR2032 供电，TCRT5000 反射式
红外对管采样燃气表转轮，通过 **BTHome v2** 广播：

| 对象 | 内容 |
|------|------|
| `0x0C` | 电池电压（V） |
| `0x3D` | TCRT5000 差分 ADC 原始值 |
| `0x4C` | 原始累计计数（0.001 单位，uint32 自然回绕） |
| `0x53` | 固件版本文本 |

固件内置 WCH BackupUpgrade OTA：应用运行在 Image A，OTA 写入 Image B，校验通过
后由 IAP 在重启时拷贝回 A；失败继续跑旧固件。OTA 完成后通过 `0x53` 上报新版本。

## OTA 协议与实测性能

GATT 服务 `0xFEE0` / 特征 `0xFEE1`（可写、可写无响应、可读）。

| 命令 | 作用 | 响应 |
|------|------|------|
| `0x80` PROGRAM | 写入 Image B，**按 256 字节 Flash 页拆分多次写入** | 2 字节状态 |
| `0x81` ERASE | 按 4 KB 块擦除 Image B | 2 字节状态 |
| `0x82` VERIFY | 逐字节比对 Image B | 2 字节状态 |
| `0x83` END | 切换 ImageFlag 并复位，IAP 开始拷贝 | — |
| `0x84` INFO | 设备信息 | 20 字节，见下表 |
| `0x85` CRC32 | 整片 Image B 的 CRC32（zlib 兼容；1.4.8 起使用查找表缩短 BLE 阻塞） | `crc32_le[4] + 0x00 0x00` |
| `0x86` SET_GAS | **1.4.7 起停用**，返回 `0xFE`；真实读数只在 HA 校准 | 2 字节状态 |
| `0x87` SET_THRESHOLD | 写采样阈值 `[cmd,len,threshold_le16,magic=0x5AA5]`，**不复位** | 2 字节状态 |

`INFO` 响应（20 字节）：

| 偏移 | 内容 |
|------|------|
| 0 | ImageFlag |
| 1–4 | Image 区大小（216 KiB，LE） |
| 5–6 | Flash 块大小（4096） |
| 7–8 | Chip ID |
| 9 | 能力位：bit0 = PROGRAM 按页拆分（≥1.2.9），bit1 = 支持 `0x85`（≥1.3.0），bit2 = 旧版支持 `0x86`（1.4.7 起清零），bit3 = 支持 `0x87` 写阈值（≥1.4.1） |
| 10 | OTA 协议版本（1.3.x = 4，1.4.0 = 5，1.4.1 ~ 1.4.6 = 6，1.4.7 = 7，1.4.8 = 8） |
| 11–12 | OTA 模式下 PA9 实测电压（mV，≥1.3.0） |
| 13–14 | 当前采样阈值（≥1.4.1） |

**分片约束（踩过坑，别改）**

- PROGRAM 单条写入不能跨 256 字节 Flash 页。固件从 1.2.9 起在设备内部拆分，
  因此分片可以放大；但超过 128 字节会被 ESPHome 代理转成 prepare/execute 长写，
  每片十几次往返，慢且容易断链（240 字节实测 12% 处失败）。
- VERIFY 走同一套 ROM 页机制，分片**必须留在 64**，240 字节会卡死。

**原始计数与掉电保存**

- `0x4C` 只保存设备原始累计计数，不保存机械表真实读数；1.4.7 起 P/N 不再调燃气值，
  OTA `0x86` 停用，真实读数由 HA 0.7.0 通过 offset 计算。
- 原始计数使用 uint32 自然累加和回绕，不再使用 100000 m³ 特殊上限；自动保存增量
  使用无符号减法，跨回绕仍正确。
- DataFlash 用两个 4 KB 块（`0x0000` / `0x1000`），每块 256 条 16 字节记录
  `{magic, seq, value, crc16}`；块写满才擦除另一块，追加记录并写后读回校验。
- 失败当场重试 3 次，之后每 15 秒再试；异常写入后重扫最大有效序号，避免跨块复用序号。
- 启动时扫描两个块并加载最新有效原始计数；掉电最多丢正在追加的一条记录。
- Code Flash 与 DataFlash 是两套接口，OTA 不影响原始计数。

> **降级注意**：1.4.2 起会把记录追加到块的中间偏移。降回旧固件前应先确认旧版
> 日志格式兼容性；1.4.7 起真实读数由 HA 保存，不受设备降级影响。

**采样阈值（0x87，1.4.1 起）**

- 判据是 `采样值 > 动态基线 + 阈值`，默认 100，可设 20~2000
- 存在 DataFlash **块 3（0x3000）**，记录 `{magic, threshold, crc16}`；掉电重启后
  从那里加载，非法则回落到 100
- 写入不复位，便于和 `0x86` 在同一次 OTA 会话里连续写（先阈值后燃气）
- 命令不带 magic 或数值越界回 `0xFE`，写入校验失败回 `0xFF`，旧值不受影响

**实测（1.3.0 → 1.3.1，CR2032 新电池）**

| 项目 | 值 |
|------|-----|
| PROGRAM 分片 / VERIFY | 128 字节 / CRC32 一次往返 |
| 上传速度 | 约 54 片/秒（≈6.9 KB/s） |
| 总耗时 | 约 35~40 秒 |
| OTA 负载电压 | 3.04 V（全程无下探） |

设备进入 OTA 模式后：无连接约 5 分钟、总时长约 15 分钟自动复位退出；失败重试需重新
长按 B 键 3 秒。

## 1.4.8 快速 CRC32 恢复

- `0x85` 从每字节 8 次迭代的逐位算法改为 64 字节查找表、每字节两次 4 位查表，
  保持 zlib CRC32 完全兼容，同时缩短 OTA BLE 任务被连续占用的时间。
- 协议版本升为 8。HA 0.7.2 仅对协议 8 及以上启用单次 CRC32 快速校验；旧固件
  继续走逐块 VERIFY，避免再次暴露旧实现的断链风险。
- 新算法已与旧逐位实现对 0～221184 字节的多种长度做结果对照；实机链路稳定性仍需复测。

## 1.4.7 纯计数与 HA 校准

- P/N 只调整采样阈值：短按 ±1，长按 3 秒后每 300 ms ±10，长按 9 秒后每 300 ms ±50，
  范围 20~2000；长按只改 RAM，松手保存一次 DataFlash。P 增大更保守，N 减小更灵敏。
- 原始计数不再受按键或 OTA 校准命令影响；`0x86` 能力位清零，协议版本 7。
- HA 0.7.0 通过 `offset = 真实表读数 - 当前 raw` 校准，真实读数与统计消耗分离。

## 1.4.5 定时 OTA

- 普通模式每累计运行 **3 天（4320 分钟）**，自动写定时 OTA 请求并复位，无需按 B。
- 定时模式使用请求记录 `reserved[0] = 1`，无连接 **60 秒**后自动回普通模式；
  B 键手动模式仍为约 5 分钟，未知原因回退手动长超时。
- HA 已连接后仍清零 idle 计数，并保留约 15 分钟总时长，上传不会被 60 秒窗口中断。
- 定时退出或升级完成后重新计时，不形成 OTA 重启循环；不增加日常 Flash 写入。
- 定时窗口与复位后的探头基线建立期间不计量，发布更新不紧急时可接受约每 3 天一次空窗。

## 1.4.4 可靠性修复

- 修正 `0x87` 阈值命令 magic 偏移，按既有 6 字节格式读取 `p[4..5]`。
- 所有 OTA 命令增加帧长检查，PROGRAM/VERIFY/ERASE 限制在 Image B 范围内，
  拒绝零长度、短包、超长包和越界擦写；INFO 未使用字节固定为零。
- 燃气日志在写入或回读失败后重新扫描两块最大有效序号，避免未确认的物理写入
  导致后续跨块记录复用序号；正常保存不增加全块扫描。
- 镜像工具强制 APP 从 `0x1000` 开始，独立客户端拒绝零 Flash 块大小，发布脚本
  不再忽略 Git commit/push 失败。
- IAP 检查 Image A 擦除及每次写入，拷贝后逐块比较 A/B；失败保留 IAP 标志和
  Image B 并复位重试，成功后才切换 Image A。**新 IAP 需要 USB 烧录，普通 OTA
  只更新 APP。**

## 目录结构

```
APP/           应用代码（main、Broadcaster、BTHome 编码、按键、OTA 入口）
Profile/       BTHome/OTA GATT profile（0xFEE0 / 0xFEE1）
HAL/           低功耗、时钟、GPIO、CONFIG.h（BLE 缓冲等）
Ld/            Link.ld（APP 区 216K，栈 2048）
Startup/       RISC-V 启动代码
RVMSIS/        CMSIS 头文件
StdPeriphDriver/  外设驱动与 flash ROM 接口
LIB/           WCH BLE 库
OTA_Image/     JumpIAP + IAP 源码（首次 USB 烧录用）
tools/         make_ota_images.py / publish_firmware.py
firmware/      发布产物（OTA app bin、sidecar、manifest、full hex）
Broadcaster.wvproj  MounRiver Studio 工程
```

> **注意**：`APP/`、`HAL/` 下的 `.c/.h` 是 **GBK** 编码，用编辑器改成 UTF-8
> 会破坏中文注释。

## 编译

1. 用 **MounRiver Studio 2.4+** 导入 `Broadcaster.wvproj`；
2. 修改版本号：`APP/include/broadcaster.h` 的 `FW_VERSION_STRING`；
3. Build，产物在 `obj/`：`Broadcaster.elf/.hex`。

`obj/` 是 MRS 生成的构建目录（不在仓库里），其中的 `makefile` 也可在命令行用
`riscv-none-embed-gcc` 工具链直接 `make`。

## 生成 OTA 镜像

```bash
python tools/make_ota_images.py
```

输出：

- `obj/Broadcaster_app.bin`：APP 区镜像（OTA 实际传输的内容）
- `obj/Broadcaster_app.bin.json`：版本/大小/CRC32
- `obj/Broadcaster_full.hex`：JumpIAP + APP + IAP 合并的完整固件（USB 烧录）

## 发布固件（给 HA 自动检查用）

```bash
python tools/publish_firmware.py --repo . --notes "改了什么" --push
```

把 `obj/Broadcaster_app.bin`、sidecar、`Broadcaster_full.hex` 复制到本仓库
`firmware/`，并生成 `firmware/manifest.json`；`--push` 会提交并推送到已配置的
远程（`origin` = Gitee，`github` = GitHub）。

`manifest.json`：

```json
{
  "version": "1.3.1",
  "file": "ch592_ota.bin",
  "size": 156048,
  "crc32": "0x0FA7C0DC",
  "sha256": "…",
  "built": "2026-11-15T13:58:00+00:00",
  "notes": "1.3.1: 电池 OTA 实测版本"
}
```

`publish_firmware.py` 会交叉核对 `broadcaster.h` 里的 `FW_VERSION_STRING` 与
`.bin.json` 的 `version`，不一致直接报错。发布后记得打 tag 并推送：GitHub Actions
会为缺失的 `v*` tag 自动建 release，Gitee 需要手动建。

## HA 自动检查新固件

Home Assistant 的 [`ch592_ota`](https://github.com/TeaRain/ch592_ota) 集成会定时
拉取 manifest（默认 Gitee raw 地址）：

```
https://gitee.com/zigbridge/ch592-gas-broadcaster/raw/master/firmware/manifest.json
```

对比设备通过 BTHome `0x53` 上报的版本，有新版本时 `update.*` 实体变 `on`，
在“设置 → 更新”或设备页点安装即可：集成下载 bin、校验 size/CRC32/SHA-256，
然后走 OTA。检查间隔 30 分钟，也可在集成选项里改成 GitHub raw 或其他地址。

> Gitee 仓库需要设为**公开**，HA 才能免认证拉取。

## 烧录

- **首次/恢复**：WCH-LinkE + WCH-LinkUtility 烧 `firmware/Broadcaster_full.hex`，
  **不要**选 Chip Erase / 全片擦除，只下载代码区，DataFlash 里的燃气值会保留。
- **日常更新**：HA 里点更新，或长按 B 键 3 秒进入 OTA 模式后用集成升级。

## 版权

`StdPeriphDriver/`、`LIB/`、`RVMSIS/`、`Startup/` 及 OTA 参考实现来自南京沁恒
（WCH）CH592 SDK，版权归 WCH 所有；其余应用代码由作者维护。
