# CH592 BTHome 对象 ID 顺序导致 HA 重复警告的分析

> 分析日期：2026-11-23（UTC）
> 整改源码仓库：`/home/ops/nas/projects/Broadcaster-CH592/dist/firmware_repo`
> 当前状态：**仅完成只读定位；未修改源码、未编译、未 OTA**

## 1. 结论摘要

CH592 普通 BTHome 广播中的对象 ID 顺序为：

```text
0x0C → 0x3D → 0x4C → 0x3E → 0x53
```

其中 `0x4C` 排在 `0x3E` 前面，不满足 BTHome 接收器要求的对象 ID 升序排列。HA 的 `bthome_ble.parser` 因此持续产生警告。

正确顺序应为：

```text
0x0C → 0x3D → 0x3E → 0x4C → 0x53
```

当前广播数据仍能被解析，燃气采样值和阈值仍在更新；目前证据支持“**协议顺序不规范并造成日志刷屏**”，没有证据表明它正在破坏燃气计数。但后续固件发布前应修正，避免不同接收器出现兼容性问题。

## 2. 线上证据

HA 日志中的典型广播负载：

```text
0c800b3deb034c100400003e640000005305312e352e30
```

按 BTHome 对象解析：

| 顺序 | 对象 ID | 数据 | 含义 |
|---|---|---|---|
| 1 | `0x0C` | `80 0B` | 电池电压，小端值 `0x0B80 = 2944` |
| 2 | `0x3D` | `EB 03` | ADC/采样值，小端值 `0x03EB = 1003` |
| 3 | `0x4C` | `10 04 00 00` | 燃气原始计数，小端值 `0x00000410 = 1040` |
| 4 | `0x3E` | `64 00 00 00` | 当前阈值，小端值 `100` |
| 5 | `0x53` | `05 31 2E 35 2E 30` | 文本版本 `1.5.0` |

日志明确指出对象 ID 没有按从低到高排列，实际负载也直接证明了 `0x4C > 0x3E` 的逆序。

线上固件报告为 `1.5.0`；本仓库 `APP/include/broadcaster.h` 当前版本为 `1.6.0`，但仍保留同样的布局，因此该问题尚未被后续源码自然消除。

## 3. 生效源码位置

后续整改必须以本仓库为准：

```text
/home/ops/nas/projects/Broadcaster-CH592/dist/firmware_repo
```

不要把外层 `/home/ops/nas/projects/Broadcaster-CH592/APP/broadcaster.c` 的旧副本当作本次整改目标；外层副本的版本和字段布局与当前发布源码不同。

### 3.1 广播初始化

`APP/broadcaster.c:80-88` 当前定义：

```c
static uint8_t bthomeData[BthomeAdvertSize(FW_VERSION_LEN)] =
{
    0x02, 0x01, 0x06,
    0x0F, 0x16, 0xD2, 0xFC, 0x40,
    0x0C, 0x00, 0x00,
    0x3D, 0x00, 0x00,
    0x4C, 0x00, 0x00, 0x00, 0x00,
    0x3E, 0x00, 0x00, 0x00, 0x00
};
```

### 3.2 阈值偏移

`APP/include/bthome_threshold.h` 当前定义：

```c
#define BTHOME_THRESHOLD_OFFSET 19
#define BTHOME_VERSION_OFFSET 24
```

`BthomeWriteThreshold()` 在偏移 19 写入 `0x3E`，随后在 20～23 写入阈值。

### 3.3 燃气值写入

`APP/broadcaster.c:590-600` 的 `updata_bthome_data()` 当前将燃气值写入：

```c
bthomeData[15] = s_u32GasMeter & 0xFF;
bthomeData[16] = (s_u32GasMeter >> 8) & 0xFF;
bthomeData[17] = (s_u32GasMeter >> 16) & 0xFF;
bthomeData[18] = (s_u32GasMeter >> 24) & 0xFF;
BthomeWriteThreshold(bthomeData, s_u16TcrtThreshold);
```

版本对象由 `APP/broadcaster.c:793-796` 写入 `BTHOME_VERSION_OFFSET`，当前为 24。

## 4. 推荐修复布局

交换完整的 `0x3E` 和 `0x4C` 五字节块，而不是只交换对象 ID 字节。数据必须跟随各自字段移动。

| 内容 | 当前完整广播偏移 | 修复后偏移 |
|---|---:|---:|
| `0x0C` 电压对象 | 8 | 8，不变 |
| 电压值 | 9～10 | 9～10，不变 |
| `0x3D` ADC 对象 | 11 | 11，不变 |
| ADC 值 | 12～13 | 12～13，不变 |
| `0x3E` 阈值对象 | 19 | **14** |
| 阈值四字节值 | 20～23 | **15～18** |
| `0x4C` 燃气对象 | 14 | **19** |
| 燃气四字节值 | 15～18 | **20～23** |
| `0x53` 版本对象 | 24 | 24，不变 |
| 版本长度及文本 | 25 起 | 25 起，不变 |

两个数据块都是五字节，因此：

- `BTHOME_VERSION_OFFSET` 继续保持 **24**。
- 版本长度为 5 时，广播总长度继续保持 **31 字节**。
- `BthomeAdvertDataLength(5)` 继续保持 **27**。
- 不增加广播长度，不引入新的 BLE 31 字节长度压力。

## 5. 后续需要修改的文件

### 5.1 `APP/broadcaster.c`

1. 在 `bthomeData` 初始化器中，将 `0x3E` 块放到 `0x4C` 块之前。
2. 将燃气值写入下标从 15～18 调整为 20～23。
3. 保持电池、ADC、版本和广播长度逻辑不变。

### 5.2 `APP/include/bthome_threshold.h`

```c
#define BTHOME_THRESHOLD_OFFSET 14
#define BTHOME_VERSION_OFFSET 24
```

`BthomeWriteThreshold()` 的相对写入逻辑可以保持不变，因为偏移调整后，阈值会自然写入 15～18。

### 5.3 `tests/test_threshold_feedback.c`

现有测试把旧布局当成正确结果：

```c
CHECK(BTHOME_THRESHOLD_OFFSET == 19);
CHECK(packet[19] == 0x3E);
```

必须同步修改，否则测试会继续保护错误布局。

同时，现有测试主要验证阈值偏移和 uint32 编码，**没有完整验证所有对象 ID 的升序关系**。整改时应增加完整广播布局断言，至少覆盖：

```text
0x0C < 0x3D < 0x3E < 0x4C < 0x53
```

以及：

- 阈值位于 14，值位于 15～18。
- 燃气对象位于 19，值位于 20～23。
- 版本对象仍位于 24。
- 版本长度为 5 时，AD length 为 27、总包长为 31。
- 阈值边界值 20 和 2000 的编码保持正确。

## 6. 推荐整改顺序

1. 先为完整广播对象顺序和燃气新偏移添加回归测试。
2. 运行测试，确认当前源码因 `0x4C` 位于 `0x3E` 前而失败。
3. 交换初始化器中的完整字段块，并调整阈值和燃气偏移。
4. 更新旧测试中的偏移断言，不要仅为了让测试变绿而保留旧布局。
5. 运行阈值测试和完整测试集。
6. 按项目现有构建流程生成新固件，并递增 `FW_VERSION_STRING`，确保 OTA 后可以明确区分新旧版本。
7. 记录升级前燃气读数、ADC 值、阈值和版本，再单独执行 OTA。
8. 升级后先核对数据，再观察 HA 日志；不要仅凭“OTA 成功”判定整改完成。

可运行的目标测试形式：

```bash
cd /home/ops/nas/projects/Broadcaster-CH592/dist/firmware_repo
python3 -I -m unittest discover -s tests -p 'test_threshold_feedback.py' -v
python3 -I -m unittest discover -s tests -v
```

## 7. OTA 后验收标准

- [ ] 广播对象顺序为 `0C / 3D / 3E / 4C / 53`。
- [ ] 广播总长度和版本字段位置符合预期。
- [ ] 燃气读数未因布局调整发生无解释跳变。
- [ ] 阈值仍为升级前实际值，P/N 调整后仍能正确广播和回显。
- [ ] 电池、电压、ADC 和版本字段解析正常。
- [ ] HA 中 BTHome 与 CH592 OTA 实体仍合并到原设备，未产生重复设备。
- [ ] 连续观察多个广播周期后，`bthome_ble.parser` 的顺序警告计数不再增长。
- [ ] OTA 通道、阈值设置和燃气校准功能未出现回归。
- [ ] 必要时可回滚到升级前已验证的固件镜像。

注意：历史日志不会追溯性消失，应以“计数不再增长”或“HA 重启后不再产生新警告”为验收依据。

## 8. 风险与边界

- 不能只交换 `0x3E` 和 `0x4C` 两个 ID 字节而不移动数据，否则燃气值和阈值会互换解释。
- 不要改变版本偏移；两个块等长，版本不需要移动。
- 不要用屏蔽 `bthome_ble.parser` 日志代替协议修复。
- 当前没有证据要求紧急停用燃气设备，但应在下一次固件发布前完成布局修正和回归验证。
- 本报告创建过程没有修改 CH592 的 C 源码、头文件、测试或固件镜像。
