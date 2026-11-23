# CH592 Remote OTA Wake Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a low-power, no-button entry into the existing CH592 OTA flow using a 300 ms connectable BTHome window every minute with a 50 ms advertising interval.

**Architecture:** Normal mode gains a minimal Peripheral/GATT control task alongside the existing Broadcaster sampling task. A validated `ENTER_OTA` write records the existing manual OTA request and resets; HA then resumes the unchanged OTA Peripheral, threshold-first, protocol 8 CRC and IAP flow. No runtime Role rebuild and no new upload protocol.

**Tech Stack:** CH592 C/TMOS/BLE, Home Assistant Python/aiohttp/Bleak, `unittest`, host C tests, approved RISC-V `INT_SOFT` build.

**Spec:** `docs/superpowers/specs/2026-11-23-remote-ota-wake-design.md`

## Global Constraints

- Normal periodic advertising: **300 ms window, 50 ms interval, about 6 frames/minute**; BTHome content, 300 ms sampling and 60 s scheduling stay unchanged.
- Normal mode accepts only `ENTER_OTA`; no normal-mode ERASE/PROGRAM/CRC/END.
- Preserve raw `uint32` counting, DataFlash logs, HA offset model, P/N threshold behavior, B/3-day OTA and threshold persistence.
- Do not modify protocol 8, image layout or IAP. Do not mix in the reference project's later gas features.
- Firmware 1.5.0 and unknown versions do not attempt remote wake; their first upgrade still uses B or the timed window. Remote-capable firmware starts at **1.6.0**.
- HA integration version becomes **0.8.0**. Work directly in `dist/firmware_repo` and `dist/gitee`; do not commit or push.
- Preserve GBK encoding in existing firmware `.c/.h` files. Keep unrelated line-ending-only changes out of the change set.
- Every formal build uses `INT_SOFT` and passes GPIOB/RTC/BLE `mret` checks. No unverified real-device claims.

## Review Focus

- Normal and OTA GATT databases differ: the remote-wake path must not reuse cached normal-mode services for OTA; pinned by Task 3 plain-client/fresh-discovery tests.
- A user clicks Install on 1.5.0 in normal mode: fail immediately with the B/timed-window instruction, without BLE traffic; pinned by Task 3.
- Cancellation or a second click while waiting for a minute window: no stale wake or duplicate upload; pinned by Task 3 event-controlled tests.
- A pending threshold exists when remote wake succeeds: write it before any ERASE, and block upgrade if it fails; pinned by Task 3.
- The 300 ms disable event fires while connected: it must not terminate the control link or schedule a second reset; pinned by Task 1 policy and Task 2 behavioral state tests, then real-device verification in Task 4.

---

### Task 1: Host-Testable Remote Wake Policy

**Files:**
- Create: `APP/include/remote_ota_policy.h`
- Modify: `APP/include/ota.h:43-53`
- Test: `tests/test_remote_ota_policy.c`

**Interfaces:**
- Produces: `CMD_IAP_ENTER_OTA = 0x88`; six-byte frame `[88 00 A5 5A 4F 54]`.
- Produces: `OTA_RemoteEnterFrameValid(const uint8_t *frame, uint16_t len) -> int`.
- Produces: `REMOTE_OTA_ADV_INTERVAL_TICKS = 80`, `REMOTE_OTA_ADV_WINDOW_TICKS = 480`, `REMOTE_OTA_RESET_DELAY_TICKS = 80`.
- Produces: `RemoteOtaMayStopAdvertising(uint8_t normal_mode, uint8_t connected) -> int`.

- [ ] **Step 1: Write the failing C test**

Assert the exact timing values and valid frame; reject wrong length/magic and all OTA write commands in normal mode. Assert advertising may stop only when not in normal mode or no control link is active.

- [ ] **Step 2: Compile and run to verify RED**

Run: `cc -std=c99 -Wall -Wextra -Werror -DEEPROM_BLOCK_SIZE=4096 -IAPP/include tests/test_remote_ota_policy.c -o "$CLAUDE_JOB_DIR/tmp/test_remote_ota_policy" && "$CLAUDE_JOB_DIR/tmp/test_remote_ota_policy"`
Expected: compilation fails because `remote_ota_policy.h` and the new command do not exist.

- [ ] **Step 3: Implement the minimal inline policy**

Add the unused command `0x88`, timing constants and pure validation/link-policy helpers. Do not touch the existing generic OTA command validator, so `0x88` remains invalid in OTA mode.

- [ ] **Step 4: Re-run Task 1 test and existing policy tests**

Run the command in Step 2, then compile/run `test_ota_validation.c` and `test_scheduled_ota.c` with the same include/define conventions.
Expected: all three C tests pass; existing OTA command behavior is unchanged.

### Task 2: Normal Peripheral/GATT Entry and Existing Reset Path

**Files:**
- Modify: `APP/broadcaster_main.c:71-85`
- Modify: `APP/broadcaster.c:773-915`
- Modify: `APP/include/broadcaster.h:27-49`
- Modify: `APP/peripheral.c:95-145,232-319,342-545,677-916`
- Modify: `APP/include/peripheral.h:28-51`
- Test: `tests/test_remote_ota_runtime.c`

**Interfaces:**
- Consumes: Task 1 frame/timing/link policy and `OtaMode_Request()`.
- Produces: `RemoteOtaControlState`, `RemoteOtaControlInit(state)`, `RemoteOtaControlSetConnected(state, connected)` and `RemoteOtaControlRequestReset(state) -> int` in `remote_ota_policy.h`.
- Produces: `Peripheral_InitNormal(void)` and `Peripheral_NormalLinkActive(void) -> uint8_t`.
- Keeps: `Peripheral_Init()` and the existing OTA task/event behavior unchanged for B/timed upgrades.

- [ ] **Step 1: Write failing behavioral C tests**

Exercise the real control state: a fresh normal controller is disconnected and not resetting; a valid ENTER request schedules exactly one reset; duplicate requests do not schedule another; malformed/OTA write commands are rejected by the Task 1 validator; disconnect without a reset permits later advertising while a pending reset keeps the normal window closed. Do not write source-text grep tests.

- [ ] **Step 2: Compile and run to verify RED**

Run the new C test with the preserved TCC, C99, warnings-as-errors and APP include path.
Expected: compilation fails because the remote control state API does not exist.

- [ ] **Step 3: Implement the contained dual-mode Peripheral task**

Refactor only shared initialization needed by `Peripheral_InitNormal`: empty normal scan response, no normal SimpleProfile, no normal OTA idle/total timers, and no forced advertisement restart on normal disconnect. Normal startup runs both the Peripheral control task and the existing Broadcaster sampling task.

Guard Broadcaster advertise enable/disable with `Peripheral_NormalLinkActive()`. In normal `OTA_IAPWriteData`, accept only the Task 1 frame, store status `00 00`, call `OtaMode_Request()`, and schedule one 50 ms reset event. Reject all other commands and suppress duplicate reset scheduling. Do not alter the OTA ERASE/PROGRAM/CRC/END implementations.

- [ ] **Step 4: Run behavioral C and existing firmware Python tests**

Run: `python3 -W error -m unittest tests.test_firmware_tools tests.test_crc32_fast tests.test_threshold_feedback tests.test_gas_log_policy -v`; compile/run `test_remote_ota_runtime.c` plus all Task 1 host C tests. `test_interrupt_returns.py` waits for the Task 4 ELF.
Expected: all runnable tests pass; no changes to counter, threshold, CRC or IAP test expectations. Role wiring remains covered by the formal build and Task 4 real-device gate, not source-text assertions.

### Task 3: HA Download-Validate-Wait-Wake-Upgrade Orchestration

**Files:**
- Modify: `dist/gitee/custom_components/ch592_ota/const.py`
- Modify: `dist/gitee/custom_components/ch592_ota/ble_ota.py:1-160`
- Modify: `dist/gitee/custom_components/ch592_ota/coordinator.py:185-302,625-980`
- Modify: `dist/gitee/custom_components/ch592_ota/update.py:99-196`
- Test: `dist/gitee/tests/test_ble_ota_reliability.py`
- Test: `dist/gitee/tests/test_coordinator_reliability.py`
- Create: `dist/gitee/tests/test_remote_ota.py`

**Interfaces:**
- Produces: `async_request_ota(hass, address) -> None`, using plain `BleakClient`, write-with-response of the six-byte Task 1 frame, a two-byte `00 00` read-back and disconnect.
- Produces: `Ch592OtaClient(..., fresh_services: bool = False)`; `fresh_services=True` selects plain `BleakClient` after a mode switch, while existing OTA defaults retain `BleakClientWithServiceCache`.
- Produces: coordinator phases `waiting_window`, `connecting_control`, `requesting_ota`, `waiting_ota_ad` under `status="starting"`; existing upload/reboot phases remain unchanged.
- Keeps: `async_start_ota(...)` public signature and automatic B/timed-window behavior.

- [ ] **Step 1: Write failing BLE and coordinator tests**

Cover: exact wake frame/status; fresh OTA service discovery only after wake; 1.5.0/unknown versions never connect in normal mode; remote path requires size+CRC32+SHA-256 and a valid image before connection; first missed connection waits for a later BTHome sequence before retry; cancellation prevents a delayed retry; duplicate install is rejected; stale OTA advertisements do not satisfy the post-wake switch; remote success writes a pending threshold before ERASE; threshold failure prevents ERASE; OTA auto-install cannot start a second task.

Use `asyncio.Event` for advertisement/save timing rather than real sleeps. Existing tests must remain unchanged except for explicit interface extension.

- [ ] **Step 2: Run focused tests to verify RED**

Run: `python3 -W error -m unittest discover -s /home/ops/nas/projects/Broadcaster-CH592/dist/gitee/tests -v`
Expected: new tests fail because the wake client, remote version gate and waiting state machine are absent; existing tests still pass.

- [ ] **Step 3: Implement the wake client and coordinator state machine**

After download and existing image checks, capture `wake_required = not ota_mode`. For remote-capable normal firmware, start the background task immediately; wait for new normal BTHome advertisements and make bounded connection attempts for up to 240 s, then require a fresh raw OTA advertisement within 20 s. Record attempts/errors and make every wait cancellable.

After a real OTA switch, write any pending threshold before entering the current upload loop and use fresh GATT discovery for that loop. If already in OTA mode, retain current threshold-first and service-cache behavior. Update release text/progress to distinguish legacy bootstrap, waiting, connecting and uploading.

- [ ] **Step 4: Run the complete HA suite**

Run: `python3 -W error -m unittest discover -s /home/ops/nas/projects/Broadcaster-CH592/dist/gitee/tests -v`
Expected: all existing and new tests pass with no expected-failure traceback leaking outside `assertLogs`.

### Task 4: Versions, Approved Build, Artifacts and Real-Device Gate

**Files:**
- Modify: `APP/include/broadcaster.h` (`FW_VERSION_STRING = "1.6.0"`)
- Modify: `dist/gitee/custom_components/ch592_ota/manifest.json` (`0.8.0`)
- Modify: both repositories' `README.md`, `CHANGELOG.md` and relevant usage/status text
- Modify: `VERIFICATION.md`
- Generate: `firmware/ch592_ota.bin`, sidecar/manifest and `firmware/Broadcaster_full.hex`; regenerate `dist/ch592_ota.zip`

- [ ] **Step 1: Add failing release-artifact consistency checks**

Execute real artifacts: published firmware must meet `REMOTE_OTA_MIN_VERSION`, bin size/CRC32/SHA-256 must match the manifest, the full HEX must exist, and the HACS ZIP manifest must match the integration working tree. Human documentation is reviewed manually rather than tested by grepping prose.

- [ ] **Step 2: Run checks, then update versions and documentation**

Document the exact click flow, first-upgrade requirement, timeout/cancel behavior and theoretical—not measured—power budget. Do not modify unrelated newline-only translation files.

- [ ] **Step 3: Build with the preserved approved toolchain**

Reuse the verified environment represented by `/tmp/ch592_build_release.py`: GCC 13.2, `rv32imac_zicsr_zifencei`, `CLK_OSC32K=2`, `INT_SOFT`. Build all APP/HAL/Profile/driver sources, link `obj/Broadcaster.elf`, and generate the HEX.
Expected: compile and link exit 0; no missing symbol or flash overflow.

- [ ] **Step 4: Run every host test and interrupt check**

Run both repositories' full Python suites and all standalone C tests, including `tests/test_interrupt_returns.py` against the newly built ELF.
Expected: all pass; GPIOB/RTC/LLE return with `mret`.

- [ ] **Step 5: Generate and inspect artifacts**

Run `python3 -I tools/make_ota_images.py` and the publish script without `--push`; regenerate the HA ZIP without tests/cache files. Verify APP size/JAL/CRC32/SHA-256, manifest values, full HEX composition and ZIP `testzip()`. Record hashes in `VERIFICATION.md` only after reading the generated files.

- [ ] **Step 6: Review the complete diff and report the real-device gate**

Check no unrelated files or IAP changes entered the diff. Leave work uncommitted/unpublished. Provide a test build for the user to bootstrap via B/timed OTA; then observe a subsequent no-button OTA, raw count/offset/threshold preservation, connection retries and power. Do not claim those real-device items before they happen.
