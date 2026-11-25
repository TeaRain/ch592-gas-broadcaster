# OTA Counter Flush Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 发布 CH592 固件 1.6.2，使所有 OTA 请求在复位前保存当前燃气计数，避免丢失周期保存阈值以下的 RAM 余量。

**Architecture:** 在现有共同入口 `OtaMode_WriteRequest()` 中调用共享生产助手，先执行 `SaveToDataFlashNow()`，再按原 manual/scheduled reason 写 OTA 请求。主机 C 测试复用该助手验证真实调用顺序，固件全量构建和既有回归测试保护其余行为。

**Tech Stack:** C99、Python unittest、RISC-V GCC 13.2.0、picolibc 1.8.6、Git/GitHub/Gitee Release。

**Spec:** `docs/superpowers/specs/2026-11-25-ota-counter-flush-design.md`

## Global Constraints

- 只在 `/home/ops/nas/projects/Broadcaster-CH592/dist/firmware_repo` 当前工作副本操作，不使用 worktree；只暂存本任务文件。
- APP C/H 文件保持 GBK，使用保留其余字节的精确替换。
- 版本为 `1.6.2`，长度 5；BTHome 对象顺序、偏移、包长和协议 8 不变。
- 不修改 HA 校准、不触发设备 OTA，不宣称新保存逻辑已实机验证。
- 发布沿用仓库 `firmware/` 清单和二进制，以及现有 GitHub Actions 镜像流程。

## Review Focus

- manual 与 scheduled 是否都经过保存助手：两者继续分别调用共同 `OtaMode_WriteRequest()`，Task 1 测试两种 reason 且编译验证调用类型。
- 保存与 OTA flash 页是否互相覆盖：保存仍写 block 0/1，OTA 请求仍写 `0x2000`，Task 1 不改地址且全量测试保护恢复逻辑。
- BTHome 是否意外回归：Task 2 运行完整广播 C 测试和固件/HA 全套测试。
- 旧固件升级到 1.6.2 的最后一次余量是否仍可能丢失：文档明确首次升级按 B 或事后校准，不能宣称修复旧固件当前行为。
- 发布文件是否混入旧 1.6.1 或无关工作区内容：Task 2 检查 ELF/APP 版本、产物哈希和完整 HEX 非 APP 区域。

---

### Task 1: 用失败测试固定 OTA 前保存顺序

**Files:**
- Create: `APP/include/ota_entry_policy.h`
- Create: `tests/test_ota_entry_policy.c`
- Modify: `APP/broadcaster.c:739-758`
- Modify: `APP/include/broadcaster.h:49`
- Test: `tests/test_ota_entry_policy.c`

**Interfaces:**
- Produces: `OtaEntryFlushBeforeRequest(void (*save)(void), void (*write_request)(uint8_t reason), uint8_t reason)`。
- Consumes: 现有 `SaveToDataFlashNow()` 与实际 OTA flash 写入函数。

- [ ] **Step 1: 添加失败的行为测试**

测试用事件缓冲区记录 save/write 回调，分别传入 manual `0` 和 scheduled `1`，断言事件严格为 `SW`、两个回调各一次、reason 原样传递；连续调用时不得遗漏保存。

- [ ] **Step 2: 编译并确认旧代码失败**

Run: `cc -std=c99 -Wall -Wextra -Werror -IAPP/include tests/test_ota_entry_policy.c -o /tmp/test_ota_entry_policy && /tmp/test_ota_entry_policy`
Expected: 编译失败，因为 `ota_entry_policy.h` 和生产助手尚不存在。

- [ ] **Step 3: 实现共享助手并接入共同 OTA 请求入口**

新增内联助手，严格先 save 后 write。将原 `OtaMode_WriteRequest()` 的实际 flash 写记录部分保留为写回调，共同入口改为调用该助手；`OtaMode_Request()` 与 `OtaMode_RequestScheduled()` 的公开接口和 reason 不变。`FW_VERSION_STRING` 改为 `1.6.2`。

- [ ] **Step 4: 运行定向与相关回归测试**

Run: 上述 C 编译命令，以及 `test_scheduled_ota.c`、`test_remote_ota_runtime.c`、`test_threshold_feedback.py`。
Expected: 全部通过，证明顺序、reason、远程控制和 BTHome 布局无回归。

### Task 2: 全量构建、审查与发布

**Files:**
- Modify: `CHANGELOG.md`
- Modify: `README.md`
- Modify: `VERIFICATION.md`
- Modify: `firmware/manifest.json`
- Modify: `firmware/ch592_ota.bin`
- Modify: `firmware/ch592_ota.bin.json`
- Modify: `firmware/Broadcaster_full.hex`
- Add: 本计划、设计文档和 Task 1 文件
- Git tag: `v1.6.2`

**Interfaces:**
- Consumes: Task 1 的源码和测试。
- Produces: Gitee/GitHub 可发现的 1.6.2 OTA 清单、bin 和完整恢复 HEX。

- [ ] **Step 1: 干净重编译并生成镜像**

恢复上一版已验证的 GCC/picolibc 环境，对全部生产 C/汇编源重编译和链接，运行 `python3 -I tools/make_ota_images.py` 与发布脚本。核对三个中断 `mret`、APP 首地址、版本字符串和 sidecar/manifest 哈希。

- [ ] **Step 2: 运行固件和 HA 全量测试**

Run: `python3 -I -m unittest discover -s tests -v`，所有独立主机 C 测试，以及 HA 仓库完整 unittest discovery。
Expected: 全过；任何既有失败都如实记录，不跳过。

- [ ] **Step 3: 更新文档并独立审查**

记录实机 4.04→3.95 的根因、1.6.2 修复范围、测试证据，以及“旧固件最后一次升级仍需按 B 或事后校准；新保存逻辑待实机验证”。派 fresh reviewer 检查共同入口、GBK、版本、产物和测试有效性，Critical/Important 必须修复后再发布。

- [ ] **Step 4: 提交、打标签并发布**

只暂存任务文件，以 `fix: flush gas counter before OTA reset` 开头并带规定署名提交。推送 Gitee master 和 annotated `v1.6.2`，创建 Gitee Release；通过现有 Actions 镜像创建 GitHub Release。

- [ ] **Step 5: 回读线上产物**

逐字节核对两端 manifest、sidecar、bin、HEX 与本地一致。最终报告 URL、哈希和实机边界，不修改 HA 校准、不触发 OTA。
