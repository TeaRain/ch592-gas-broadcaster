# CH592 燃气表 BLE 广播器固件

CH592（RISC-V，BLE 5.4）燃气表读数器固件，使用 CR2032 供电，TCRT5000 反射式
红外对管采样燃气表转轮，通过 **BTHome v2** 广播：

| 对象 | 内容 |
|------|------|
| `0x0C` | 电池电压（V） |
| `0x3D` | 计数（本次周期内的脉冲数） |
| `0x4C` | 燃气累计值（0.001 m³，原始值 / 100000 有回绕） |
| `0x53` | 固件版本文本 |

固件内置 WCH BackupUpgrade OTA：应用运行在 Image A，OTA 写入 Image B，校验通过
后由 IAP 在重启时拷贝回 A；失败继续跑旧固件。OTA 完成后通过 `0x53` 上报新版本。

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
  "version": "1.2.5",
  "file": "ch592_ota.bin",
  "size": 155808,
  "crc32": "0xA1D19F5F",
  "sha256": "…",
  "built": "2026-11-15T11:02:56+00:00",
  "notes": "1.2.5: …"
}
```

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
