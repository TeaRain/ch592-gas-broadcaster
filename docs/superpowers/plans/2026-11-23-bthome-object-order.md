# BTHome Object Order Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 发布 CH592 固件 1.6.1，使普通 BTHome 广播对象按 `0x0C < 0x3D < 0x3E < 0x4C < 0x53` 排列，消除 HA 顺序警告且不改变计量和 OTA 行为。

**Architecture:** 保留现有静态广播缓冲区和运行时写入流程，只交换两个等长五字节对象并同步阈值、燃气写入偏移。将实际初始化器和燃气小端写入封装在现有 BTHome 头文件中，让生产代码与 C 测试复用同一实现；测试用手工推导的完整 31 字节广播验证对象顺序、字段值、版本和长度，不做源码文本匹配。

**Tech Stack:** C99、Python 3 标准库 unittest、RISC-V GCC 13.2.0、Intel HEX、Git/GitHub/Gitee Release。

**Spec:** `docs/reports/2026-11-23-bthome-object-order-analysis.md`

## Global Constraints

- 只在 `/home/ops/nas/projects/Broadcaster-CH592/dist/firmware_repo` 当前工作副本修改，不使用 worktree。
- `APP/` 下 C/H 文件保持 GBK 编码，只能做保留其余字节的精确替换。
- 版本为 `1.6.1`，长度保持 5；版本偏移 24、AD length 27、完整广播长度 31 不变。
- 不改采样、计数、DataFlash、P/N、普通模式唤醒、OTA 协议或 IAP；不发布 HA 新版本。
- 只暂存本任务文件，不带入工作区已有的大量无关修改。
- 发布不等于实机验收；不手动触发设备 OTA，不宣称 HA 实机警告已消失。

## Review Focus

- 只交换 ID 而未移动数据：由完整初始化器顺序及燃气/阈值写入测试覆盖。
- 初始化器与运行时偏移不一致：由共享生产布局初始化器、燃气写入函数和完整包 C 测试共同覆盖。
- 等长交换意外改变版本或 BLE 长度：由偏移 24、AD length 27、总包长 31 断言覆盖。
- 阈值 uint32 小端编码在边界值回归：由 20 和 2000 的 C 测试覆盖。
- 发布清单引用旧构建或哈希不符：由重建后生成镜像、发布产物哈希测试及线上回读覆盖。

---

### Task 1: 用失败测试固定实际生产布局

**Files:**
- Modify: `tests/test_threshold_feedback.c`
- Test: `tests/test_threshold_feedback.py`

**Interfaces:**
- Consumes: 现有 `BthomeAdvertSize()`、`BthomeAdvertDataLength()` 和 `BthomeWriteThreshold()`。
- Produces: 对共享生产布局 `BTHOME_DATA_INITIALISER` 和 `BthomeWriteGas(uint8_t *, uint32_t)` 的完整包行为测试。

- [ ] **Step 1: 添加完整广播失败测试**

让测试包使用生产将提供的 `BTHOME_DATA_INITIALISER` 初始化，调用 `BthomeWriteGas(packet, 0x12345678)` 与现有阈值写入函数，并按生产相同偏移写入 5 字节版本。用独立手工推导的字节字面量验证完整 31 字节包：标志和服务头不变，对象顺序为 `0x0C / 0x3D / 0x3E / 0x4C / 0x53`，燃气小端值位于 20～23，阈值位于 15～18，版本位于 24～30。保留并更新阈值偏移 14、版本偏移 24、AD length 27、总包长 31 以及 20/2000 编码断言。

- [ ] **Step 2: 运行测试并确认旧代码失败**

Run: `python3 -I -m unittest discover -s tests -p 'test_threshold_feedback.py' -v`
Expected: FAIL；旧头文件没有共享布局初始化器和燃气写入函数，且阈值仍使用旧偏移 19。先确认失败来自这些缺失行为而非测试语法错误。

### Task 2: 最小修改生产布局并递增版本

**Files:**
- Modify: `APP/broadcaster.c:80-88,590-600`
- Modify: `APP/include/bthome_threshold.h:6-20`
- Modify: `APP/include/broadcaster.h:49`
- Test: `tests/test_threshold_feedback.py`

**Interfaces:**
- Consumes: Task 1 的完整包断言。
- Produces: `BTHOME_DATA_INITIALISER`、`BthomeWriteGas(uint8_t *, uint32_t)`，以及顺序合规且版本为 1.6.1 的固件源码。

- [ ] **Step 1: 用保留 GBK 字节的替换修改三处生产文件**

在现有 BTHome 头文件定义共享静态初始化宏，顺序为 `0x0C / 0x3D / 0x3E / 0x4C`；新增燃气 uint32 小端写入函数，值写入 20～23；`BTHOME_THRESHOLD_OFFSET` 改为 14。让 `bthomeData` 和 `updata_bthome_data()` 使用这两个生产接口，`FW_VERSION_STRING` 改为 `1.6.1`。保持 `BTHOME_VERSION_OFFSET == 24` 和其他代码不变。

- [ ] **Step 2: 运行定向测试并确认通过**

Run: `python3 -I -m unittest discover -s tests -p 'test_threshold_feedback.py' -v`
Expected: PASS，包含完整包字节、燃气和阈值小端值、对象顺序、版本和长度检查。

- [ ] **Step 3: 构建 APP 并生成 OTA/完整镜像**

当前环境没有 `make` 和生成的 `obj/makefile`，因此使用现有 `/tmp/ch592-toolchain-20261122` 中的 GCC 13.2.0 对既有 35 个对象对应的 C/汇编源逐一干净重编译，再统一链接。通过 `LD_LIBRARY_PATH` 使用缓存的 libisl/libmpc，用工作区内重定位的 `picolibc.specs` 和三个 Linux 大小写兼容头链接恢复上一版实际构建环境；保留 `INT_SOFT`、`CLK_OSC32K=2`、上一版 DWARF 编译参数和链接库顺序。成功后运行 `python3 -I tools/make_ota_images.py`。
Expected: 35 个源文件编译、链接成功；sidecar 版本为 1.6.1，APP 从 `0x01000` 开始，三个中断 `mret` 检查通过。

### Task 3: 全量验证、文档和发布产物

**Files:**
- Modify: `CHANGELOG.md`
- Modify: `README.md`
- Modify: `VERIFICATION.md`
- Modify: `firmware/manifest.json`
- Modify: `firmware/ch592_ota.bin`
- Modify: `firmware/ch592_ota.bin.json`
- Modify: `firmware/Broadcaster_full.hex`
- Create: `docs/superpowers/plans/2026-11-23-bthome-object-order.md`
- Add: `docs/reports/2026-11-23-bthome-object-order-analysis.md`

**Interfaces:**
- Consumes: Task 2 的 ELF、HEX、APP bin 和 sidecar。
- Produces: 可提交、可由 HA 发现的 1.6.1 发布产物及验证记录。

- [ ] **Step 1: 生成发布目录但不推送**

Run: `python3 -I tools/publish_firmware.py --repo . --notes 'BTHome 对象按 ID 升序排列，消除 HA 顺序警告；燃气、阈值和版本含义不变。'`
Expected: `firmware/manifest.json` 版本 1.6.1，size/CRC32/SHA-256 来自本次 APP bin。

- [ ] **Step 2: 运行固件和 HA 全量测试**

Run: `python3 -I -m unittest discover -s tests -v`，以及 HA 仓库对应完整 unittest discovery。
Expected: 全部通过，包括三个中断入口 `mret`、发布产物哈希和 HA 现有 BTHome/OTA 回归。

- [ ] **Step 3: 更新发布文档**

在 CHANGELOG 添加 1.6.1；README 记录新顺序；VERIFICATION 记录实际测试数、构建结果、APP/HEX 哈希，并保留 1.6.0 尚待自然窗口实机验证的事实。明确 1.6.1 的实机 OTA、计数/阈值保持和 HA 警告停止增长仍待验收。

- [ ] **Step 4: 审查差异并提交**

只暂存上述任务文件，检查 GBK 文件未发生整文件编码转换、无无关工作区修改进入提交。提交信息以 `fix: sort BTHome advertisement objects` 开头，并带规定署名。

### Task 4: 发布并回读验证

**Files:**
- Git tag: `v1.6.1`
- GitHub/Gitee Release: `v1.6.1`

**Interfaces:**
- Consumes: Task 3 的提交和固件产物。
- Produces: HA 可发现的线上 1.6.1 manifest 与可下载恢复镜像。

- [ ] **Step 1: 推送 master 和标签**

推送同一提交到 GitHub 与 Gitee master，创建带发布说明的 `v1.6.1` annotated tag 并推送两个远程；任一步失败都停止并如实报告，不假装发布完成。

- [ ] **Step 2: 创建 Release 并上传产物**

GitHub 创建正式 `v1.6.1` Release，上传 `ch592_ota.bin`、sidecar、`Broadcaster_full.hex` 和 `manifest.json`；Gitee 按现有备份方式同步 Release。发布说明引用协议顺序修复、测试证据和待实机验收边界，并带规定署名。

- [ ] **Step 3: 回读线上版本与资产**

读取 GitHub master 的 `firmware/manifest.json` 和 Release 资产，核对版本、文件名、大小、CRC32、SHA-256 与本地一致；确认 Gitee 备份可访问。最终报告线上 URL 和验证结果，实机项目保持未验证状态。
