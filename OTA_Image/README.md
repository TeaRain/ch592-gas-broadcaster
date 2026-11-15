# CH592 OTA 引导与升级镜像

本目录是 WCH "BackupUpgrade" 方案在本项目上的落地，用来把 APP 从
`0x1000` 搬到 `0x6D000` 的 IAP 之后，实现“先下载到备份区、校验通过再切换”的
BLE OTA。

## 1. 内存布局

| 区域 | 起始地址 | 大小 | 说明 |
|------|----------|------|------|
| JumpIAP | `0x000000` | 4 KB | 上电第一条指令：`j 0x6D000` |
| Image A | `0x001000` | 216 KB | APP 运行区（本项目固件链接在这里） |
| Image B | `0x037000` | 216 KB | OTA 下载/备份区 |
| Image IAP | `0x06D000` | 12 KB | 校验并拷贝 B → A，然后跳转 A |
| DataFlash | `0x070000` | 32 KB | 燃气值、OTA 标志（代码区之外） |

DataFlash 里的两个标志：

| 地址（DataFlash 偏移） | 用途 | 值 |
|------------------------|------|----|
| `0x2000` | 长按 B 请求进入 OTA 模式 | `0xA5` |
| `0x6000` | WCH 当前镜像标志 | `0x01`=A，`0x02`=B，`0x03`=IAP |

> 燃气值存放在 DataFlash 的 `0x0000` / `0x1000` 两个槽位，与上述地址不冲突。
> **首次 USB 烧录不要执行全片擦除**，只擦代码区即可保留燃气值。

## 2. 构建

在 MounRiver Studio 中正常 Build APP，得到 `obj/Broadcaster.hex`（链接地址已经是
`0x1000`）。然后构建引导程序：

```bash
make -C OTA_Image
```

最后合并镜像并生成 OTA 用的 APP bin：

```bash
python tools/make_ota_images.py
```

输出：

| 文件 | 用途 |
|------|------|
| `obj/Broadcaster_full.hex` | 首次 USB 烧录：JumpIAP + APP + IAP |
| `obj/Broadcaster_app.bin` | OTA 上传的 APP 镜像（文件偏移 0 对应 `0x1000`） |
| `obj/Broadcaster_app.bin.json` | 版本/大小/CRC32 元数据，HA 集成会读取 |

脚本会检查三段地址范围、把 APP 转成从 `0x1000` 开始的 bin，并从
`APP/include/broadcaster.h` 的 `FW_VERSION_STRING` 提取版本号。

## 3. 首次 USB 烧录

1. 用 WCH-LinkE 连接芯片，打开 WCH-LinkUtility。
2. 选择 `obj/Broadcaster_full.hex`。
3. **不要勾选 Chip Erase / 全片擦除**，只下载代码区。
4. 烧录完成后断电重启，确认：
   - HA 里燃气值仍是 `4511.62 m³`（或烧录前的值）；
   - BTHome 出现文本对象 `0x53`，值为 `FW_VERSION_STRING`（当前 `1.2.4`）。

## 4. 进入 OTA 模式

长按 B 键 3 秒。固件会：

1. 在 DataFlash `0x2000` 写 `0xA5`；
2. 软复位；
3. 启动时读到标志，擦除标志并进入可连接外设模式；
4. 广播名 `GasMeter OTA`，广播 UUID 包含 `0xFEE0`。

OTA 模式超时：

- 无连接 **5 分钟** 自动复位退出；
- 总时长 **15 分钟** 强制复位退出。

## 5. 通过 Home Assistant 升级

把 `homeassistant/custom_components/ch592_ota` 复制到 HA 的
`/config/custom_components/`，重启 HA，然后按
[`homeassistant/README.md`](../homeassistant/README.md) 配置并调用
`ch592_ota.flash` 服务。

固件文件放到：

```
/config/www/firmware/ch592_ota.bin
/config/www/firmware/ch592_ota.bin.json
```

## 6. 通过 PC 测试（可选）

```bash
py -m pip install bleak
py tools/ch592_ota_client.py --address AA:BB:CC:DD:EE:FF --file obj/Broadcaster_app.bin
```

## 7. 升级流程与断电保护

HA/PC 客户端按 WCH 协议执行：

1. `INFO 0x84` 读取备份区信息；
2. `ERASE 0x81` 擦除 Image B（54 × 4 KB）；
3. `PROGRAM 0x80` 按 16 字节地址写入 Image B；
4. `VERIFY 0x82` 逐块比较；
5. `END 0x83` 写 `IMAGE_IAP_FLAG` 并复位。

> 本项目的客户端会把 APP 镜像补齐到 216 KB，并在 Image B 最后 16 字节写入
> 镜像标记 `0x35415347`。IAP 只接受带这个标记的 Image B，因此请使用本仓库的
> HA 集成或 `tools/ch592_ota_client.py`；直接拿原始 `Broadcaster_app.bin`
> 用通用 WCH 手机 App 上传不会通过 IAP 检查。

复位后 IAP 会：

1. 检查 Image B 末尾 16 字节的镜像标记是否为 `0x35415347`（防止把空白/损坏区当固件）；
2. 有效则擦除 A、把 B 的 216 KB 拷贝到 A；
3. 把标志改回 `IMAGE_A_FLAG`，擦除 B，跳转 A。

因此：

- `END` 之前断线/断电：标志仍是 A，设备继续运行旧固件；
- `END` 之后拷贝过程中断电：标志仍是 IAP，下次上电重新拷贝 B；
- Image B 无效：IAP 不会覆盖 A，直接运行 A。

## 8. 发布新版本

1. 修改 `APP/include/broadcaster.h` 中的 `FW_VERSION_STRING`（例如 `1.2.5`，
   建议不超过 10 字节，否则 31 字节的广播包放不下）；
2. MounRiver Build；
3. `python tools/make_ota_images.py`；
4. 把新的 `obj/Broadcaster_app.bin` 和 `.json` 复制到
   `/config/www/firmware/ch592_ota.bin` / `.json`；
5. 长按 B 3 秒，调用 `ch592_ota.flash`；
6. 在 HA 里确认 `sensor.ch592_firmware_version` 变成 `1.2.5`。

> 版本号同时通过 BTHome `0x53` 文本对象广播，HA 的 BTHome 集成也会自动创建
> 一个文本传感器；本集成另外创建 `sensor.ch592_firmware_version` 用于 OTA 确认。
