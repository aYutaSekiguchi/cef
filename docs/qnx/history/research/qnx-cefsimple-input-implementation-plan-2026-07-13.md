# cefsimple pointer input implementation plan (2026-07-13)

## Scope

This plan covers one issue observed after cefsimple content rendering became
available with the native QNX Screen/Mesa path:

1. Pointer input does not reach either the cefsimple Views controls or web
   content.

The findings below are preliminary. Lower-probability prerequisites still need
runtime confirmation before the final fix is selected.

## Initial findings

`QnxPlatformEventSource` polls `screen_get_event()` every 16 ms and recognizes
`SCREEN_EVENT_POINTER`, but the pointer branch only reads position/buttons and
emits `DLOG(INFO)`. It explicitly defers `ui::MouseEvent` construction and does
not call `DispatchEvent()`. `QnxWindow` is not currently a
`PlatformEventDispatcher`, so there is also no standard Ozone window target to
receive a dispatched event. The absent logs in a Release build are expected
because the current diagnostics use `DLOG`/`VLOG`.

The current CEF-managed new-file source and the generated Chromium file are
identical. This is unfinished Phase 4 input work, not a regression introduced
by the render-only fallback.

Lower-probability prerequisites still need runtime confirmation: Screen must
actually deliver pointer events under the current QEMU device configuration,
and the event-source polling task must remain active on the UI sequence.

## Implementation sequence

### Phase 1: bounded diagnostics

- Temporarily make one polling-start marker and rate-limited pointer marker
  visible in Release logging. Include Screen event type, source window,
  position, button state, and property-read return codes.
- Confirm pointer events arrive when QEMU receives mouse movement/clicks.
- Confirm the source `screen_window_t` matches a registered
  `QnxWidgetRecord::screen_win`.
- If no pointer event arrives, inspect QEMU input devices and Screen domain
  configuration before implementing Chromium dispatch.

All probes must be timeout-bounded and use the working cefsimple launch
baseline: native CEF window mode, no sandbox, and system `libEGL.so.1`
preloaded. Record the exact guest commands and results in the runtime matrix.

### Phase 2: pointer MVP

- Add lookup by `screen_window_t` to `QnxWindowManager`; retain screen-point
  hit testing as a fallback only for events without a source window.
- Make `QnxWindow` a `PlatformEventDispatcher`, register/unregister it with the
  active `PlatformEventSource`, and forward accepted events through
  `PlatformWindowDelegate::DispatchEvent()`.
- Translate Screen pointer state into Chromium mouse move, press, and release
  events. Track the previous button mask so transitions are emitted exactly
  once, map QNX button bits to Chromium flags, and preserve both local and root
  coordinates.
- Set the target window before dispatch, following the existing Ozone backend
  contract. Respect capture when it is subsequently implemented; do not fake
  capture in the first patch.
- Keep touch, wheel, keyboard, IME, cursor confinement, and multi-window
  activation outside this MVP unless compilation requires a small shared
  abstraction.

Durable sources:

- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_platform_event_source.{h,cc}`
- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.{h,cc}`
- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.{h,cc}`

## Validation

Build gates:

- Re-bootstrap the managed patch stack on a clean Chromium base.
- Build the touched QNX Ozone objects, then `cefsimple`, using
  `out/qnx_release/ninja_qnx.sh`.
- Ensure the generated Chromium copies match CEF-managed new files and that
  all managed patches apply cleanly.

Runtime input gates:

- A click on reload triggers exactly one press and one release and reloads the
  page.
- Tabs and other cefsimple Views controls respond to clicks.
- Web-content pointer handlers receive move/down/up at correct coordinates.
- Moving outside the window, rapid clicks, and resize do not crash or produce
  stuck button state.
- Release diagnostics prove the path once, then noisy temporary logging is
  removed or reduced to `VLOG` after validation.

## Stop conditions

Stop and revise the plan if Screen emits no pointer events. This outcome
invalidates the leading Phase 2 dispatch hypothesis and requires re-checking
the QEMU input device configuration and Screen domain setup before any
Chromium-side dispatch work continues.

Implement pointer input as its own commit, separate from any network
provisioning change.

## Next blocker plan — diagnostics + verification on `qnx_pointer_input_dispatch.patch`

`QnxWindow` already derives from `PlatformEventDispatcher` (and
the internal `EventTarget`), registers / unregisters with
`PlatformEventSource`, implements `CanDispatchEvent` /
`DispatchEvent`, looks up windows by `screen_win` via
`QnxWindowManager::LookupWindowByScreenWin`, and synthesizes
`MouseMoved` / `MousePressed` / `MouseReleased` events from the
Screen button mask. All of that lives in
`cef/patch/patches/qnx/chromium/qnx_pointer_input_dispatch.patch`
and is **already applied** to the working tree — **not** a
re-implementation task. This plan only adds the next-blocker
diagnostics, the helper extraction, the wheel phase, and pins
the patch-regeneration contract.

### Confirmed vs. unobserved (2026-07-13 evidence)

Confirmed (Screen → translator, single-window QEMU):

- 259 `SCREEN_EVENT_POINTER` events over the test session;
  polling loop alive on the UI sequence.
- Screen button-mask edges were balanced: left 7 press / 7 release,
  middle 2 / 2, right 2 / 2. Whether the translator emitted and
  dispatched one Chromium event per edge is still unobserved.
- Source `screen_window_t` resolved to a registered
  `QnxWidgetRecord::screen_win` on every event; property-read
  failures = 0.

Unobserved (Screen → delegate, downstream of the translator):

- Whether `QnxWindow::CanDispatchEvent` is ever invoked, and with
  what `target()` value, against the 259 events produced.
- Whether `QnxWindow::DispatchEvent` ever reaches
  `PlatformWindowDelegate::DispatchEvent`, and with what return.
- The `uint32_t` returned by
  `QnxPlatformEventSource::DispatchEvent` /
  `PlatformEventSource::DispatchEvent`.
- Per-event-type count of generated `MouseMoved` /
  `MousePressed` / `MouseReleased` (moves can coalesce under
  Screen, so per-event-type sums need not equal 259).
- cefsimple Views / web-content response to clicks — still
  unobserved; UI non-responsiveness is the only externally
  visible symptom.

The "delegate unreachable" hypothesis is one possibility that
matches the symptoms — **not** a confirmed root cause. Phase 3
turns each unobserved item above into a counted, log-proven
observation so the next branch is evidence-driven.

### Phase 3 — counters + one-shots on the existing dispatch path

All edits land on top of the already-applied
`qnx_pointer_input_dispatch.patch`. No rewrite of
`qnx_window.{h,cc}`, `qnx_platform_event_source.{h,cc}`, or
`qnx_window_manager.{h,cc}` for behaviour — only diagnostic
additions.

Counters are sequence-local `uint32_t` fields on the existing event source
or window and are printed once from their normal shutdown paths. Do not add
an `AtExitManager` dependency for diagnostics:

- `registration_ok` / `registration_failed` — one-shot
  `LOG(INFO)` from the existing
  `PlatformEventSource::AddPlatformEventDispatcher` call site.
- `can_dispatch_calls` / `can_dispatch_accept` /
  `can_dispatch_reject` — incremented on every
  `CanDispatchEvent` call; record the existing predicate result and
  `event.target()` without changing the predicate; rate-limit diagnostic
  output and include totals in the shutdown summary.
- `dispatch_entry` — per `DispatchEvent` entry; one-shot `LOG`
  on first entry only.
- `delegate_dispatch_called` / `null_delegate` — increment immediately
  after the existing `delegate_->DispatchEvent(event)` call, which returns
  `void`; a null delegate increments `null_delegate` and skips the call.
- `event_source_dispatch_return` — the `uint32_t` returned by
  `PlatformEventSource::DispatchEvent`, bucketed into
  `POST_DISPATCH_NONE` / `…_PERFORM_DEFAULT` / `…_STOP_PROPAGATION`;
  summary printed once on shutdown.
- `synth_moved` / `synth_pressed` / `synth_released` — emitted
  `ui::MouseEvent` counts in the translator branch. Plus
  `pointer_polls_seen` (raw `SCREEN_EVENT_POINTER` count) for
  the balance check.

Stop conditions and evidence-based fix branches:

- `can_dispatch_calls == 0` with `pointer_polls_seen > 0` →
  `PlatformEventSource::DispatchEvent` is not routing to the
  QNX dispatcher. Inspect the source call site; do not add a
  parallel source.
- `can_dispatch_reject > 0 && can_dispatch_accept == 0` →
  `target()` differs from `this` or `screen_win_` is null. Log
  raw `target()` and `screen_win_` once; branch is dispatch
  wiring (`Event::DispatcherApi` / `set_target`), not the
  translator.
- `dispatch_entry > 0 && delegate_dispatch_called == 0` →
  `delegate_` is null. The delegate is set by the caller of the
  `QnxWindow` constructor; verify at
  `qnx/ozone_platform_qnx.cc` `CreatePlatformWindow` that the
  `PlatformWindowDelegate*` argument is non-null.
- `delegate_dispatch_called > 0` but `synth_* == 0` →
  translator is producing no events; mask handling regressed;
  check helper in Phase 4.
- If the event-source return flags do not include the value returned by
  `QnxWindow::DispatchEvent`, inspect the registered dispatcher list and
  base dispatch loop before changing event synthesis.

`POST_DISPATCH_*` and `PlatformEventDispatcher` /
`PlatformEventSource` contracts come from
`ui/events/platform/platform_event_source.h` and
`ui/events/platform/platform_event_dispatcher.h`. Do not redefine
them.

### Phase 4 — pure helper extraction + existing-target unit test

Goal: move the `QNX button mask/position → event specs` translation
into a free function or static helper and exercise it without
faking `screen_event_t`, so the test does not depend on Screen
runtime quirks.

- Extract a pure event-spec helper into a new
  `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_pointer_translate.{h,cc}`
  returning lightweight specs (`EventType`, flags, and
  `changed_button_flags`) rather than owning `MouseEvent` objects. Inputs
  include previous/current masks, previous/current positions, and whether
  prior state exists. The caller retains timestamps, target assignment,
  and `MouseEvent` construction.
- Keep `Event::DispatcherApi(&event).set_target(window)` in the caller
  before dispatch. The helper produces specs only and has no window or
  dispatcher dependency.
- Discover the test target with `gn ls
  //ui/ozone/platform/qnx:*` and `gn refs
  //ui/ozone/platform/qnx:<existing_test>`; pick the
  lowest-level existing QNX-side test (e.g.
  `qnx_window_manager_unittest` / `qnx_screen_context_unittest`).
  **Do not** introduce a new top-level `qnx_unittests` target.
  If no QNX-side test target exists, defer the test until one
  is created in a separate plan.
- Test cases (in the chosen target): left 1→0, 0→1, middle
  2→0, 0→2, right 4→0, 0→4, multi-button transitions
  (`1|2 → 4`) — assert exactly one release spec per cleared bit and one
  press spec per newly set bit; assert each spec carries the matching
  `EF_LEFT_MOUSE_BUTTON` / `EF_MIDDLE_MOUSE_BUTTON` /
  `EF_RIGHT_MOUSE_BUTTON` from `ui/events/event_constants.h`;
  assert `location` / `root_location` round-trip unchanged.

Stop conditions:

- Helper unit tests pass and, in a bounded runtime, each observed Screen
  press/release edge has exactly one matching synthesized event for the
  same button. The historical 7/2/2 capture is evidence, not a fixed count
  required from every run. Only then start Phase 5.
- If the helper disagrees with the runtime evidence, stop and
  re-derive; the per-button press / release balance is the
  contract.

### Phase 5 — wheel vertical / horizontal, after click is green

Goal: read `SCREEN_PROPERTY_MOUSE_WHEEL` (vertical) and
`SCREEN_PROPERTY_MOUSE_HORIZONTAL_WHEEL` (horizontal) on
`SCREEN_EVENT_POINTER` and emit `ui::MouseWheelEvent` using the
preferred constructor

```cpp
MouseWheelEvent(const gfx::Vector2d& offset,
                const gfx::PointF& location,
                const gfx::PointF& root_location,
                base::TimeTicks time_stamp,
                int flags,
                int changed_button_flags,
                const std::optional<gfx::Vector2d> tick_120ths = std::nullopt);
```

(`ui/events/event.h` lines 642–651).

Screen semantics (each property is a **single int**, ticks per
event):

- `SCREEN_PROPERTY_MOUSE_WHEEL` (vertical): **up negative /
  down positive**.
- `SCREEN_PROPERTY_MOUSE_HORIZONTAL_WHEEL` (horizontal):
  **left negative / right positive**.
- `MouseWheelEvent` doc (`ui/events/event.h` lines 654–656):
  `x_offset > 0` / `y_offset > 0` ⇒ scroll **left / up**.

Translation rule: **negate both axes**, multiply by
`MouseWheelEvent::kWheelDelta`; `changed_button_flags = 0`.
Example: raw vertical = `+1` (down) ⇒ Chromium `y_offset = -1 *
kWheelDelta` (scroll down). Raw horizontal = `-3` (left) ⇒
`x_offset = +3 * kWheelDelta` (scroll left). `flags = 0` is valid
when no mouse button is held, but otherwise preserve the current mouse and
modifier flags. `changed_button_flags` remains zero.

Implementation steps:

1. **First, read and record both raw property values without dispatch.**
   Use `VLOG(1)` for ordinary samples and one `LOG(INFO)` for the first
   non-zero value; do not emit a warning for every pointer event.
   (no dispatch yet). Rebuild and run N0pre; record observed
   values. If both are always `0` or returns are always `-1`,
   **stop Phase 5** and route the failure to the QEMU / io-hid
   configuration before any scaling or sign work. **Do not**
   commit to a `usb-tablet` QEMU device as the first move;
   decide the device after raw values are observed.
2. Once raw values are observed, add the wheel arm to the
   pointer branch: construct `MouseWheelEvent` per the rule
   above and dispatch via the same
   `PlatformEventSource::DispatchEvent` path as move / press
   / release.
3. Preserve current event flags and use `changed_button_flags = 0`.

Stop conditions:

- Rebuilt binary running N0pre shows `wheel_v_raw ≠ 0` and
  `synth_wheel_v ≥ 1`; `qnx_option_a.html` (or equivalent) logs
  `document.scrollingElement.scrollTop` delta, or the
  scroll-bar visibly moves. Horizontal-wheel acceptance is conditional on
  a device that actually reports a non-zero horizontal property.
- If raw values remain `0` / `-1` for all 259 events, stop
  Phase 5 — fix the upstream device / Screen layer before any
  QNX-side scaling.
- Do **not** enter `ScrollEvent` / fling / inertia in this
  phase; that is a later plan once basic wheel works.

### Managed-patch discipline

The durable patch is
`cef/patch/patches/qnx/chromium/qnx_pointer_input_dispatch.patch`.
Phase 3 / 4 / 5 changes to the existing six files regenerate this patch
from the clean prerequisite baseline. A genuinely new helper file belongs
under `cef/patch/qnx/chromium/new_files/...`; do not represent a new QNX
source file only as an unmanaged live-tree edit.

Clean baseline and regeneration:

- Clean baseline = current `new_files/...` plus exactly the four
  prerequisite patches that precede this patch and touch these files:
  `qnx_gpu_attach_new_widget_after_connect`,
  `qnx_window_addwindow_initial_size`, `qnx_window_set_screen_window`, and
  `qnx_phase7_cef_runtime_diagnostics`. Verify the registered stack through
  the bootstrap/patch-updater workflow rather than applying every patch in
  isolation to one base.
- Existing-patch diff scope remains six files: `qnx_window.{h,cc}`,
  `qnx_platform_event_source.{h,cc}`,
  `qnx_window_manager.{h,cc}`. Phase 4's new helper and any new QNX-only
  test source live under `new_files/...` and are copied by bootstrap; add
  their BUILD.gn entries in the same durable new-files tree. Do not encode
  a newly introduced source file only inside the modification patch.
- Regenerate with `git diff --no-prefix --relative
  --full-index` from the clean baseline, or
  `cef/tools/patch_updater.py --resave`. Verify: no `a/` /
  `b/` prefixes, no truncation, single root.

Acceptance gates, in order:

1. The registered patch stack applies cleanly in order through the CEF
   patch updater/bootstrap workflow.
2. `cef_create_projects_qnx.sh --build-type Release
   --qnx-sdp-root $QNX_SDP_ROOT` reaches `gn gen` with
   `0 failed`. If a patch fails, repair the stack before any
   `ninja_qnx.sh`; see `docs/qnx/patch-hygiene.md`.
3. Applying the regenerated input patch to the clean prerequisite baseline
   produces six files byte-identical to the final live Chromium sources.
   Do not compare final live files directly with `new_files`, because the
   prerequisite patches intentionally modify them.
4. `./out/qnx_release/ninja_qnx.sh cefsimple` reaches the
   `LINK ./cefsimple` line (per
   `qnx-cefsimple-runtime-matrix-2026-07-13.md` §9.2 step 3).
   Capture via `tee build.log`; parse with
   `ned parse build.log --format json > build.summary.json` and
   walk one cluster per turn.

Runtime acceptance, per phase:

- Phase 3 green: shutdown summary shows
  `pointer_polls_seen = 259`, `can_dispatch_calls ≥ 1`,
  `can_dispatch_accept ≥ 1`, `dispatch_entry ≥ 1`,
  `delegate_dispatch_called ≥ 1`,
  `synth_moved + synth_pressed + synth_released` equals the
  event count the translator claims, and the dominant
  `POST_DISPATCH_…` value matches what
  `PlatformEventSource::DispatchEvent` returns.
- Phase 4 green: helper unit test passes in the chosen
  existing QNX-side target; the synthetic counts agree with
  the runtime matrix `7/2/2` per-button press / release.
  Only then start Phase 5.
- Phase 5 green: `wheel_v_raw` is logged with a non-zero value; rebuilt
  N0pre binary shows `synth_wheel_v ≥ 1` in the shutdown summary;
  `qnx_option_a.html` shows a logged
  `scrollTop` delta or visible scroll-bar movement. Record
  guest commands and counters in the runtime matrix.
- After each phase, strip any temporary `LOG(WARNING)` /
  `LOG(INFO)` not on the final-summary path back to `VLOG` or
  remove entirely; re-run `git apply --check` and the
  byte-identical step.

Out of scope (deferred; own plans later): touch /
multi-touch, keyboard IME, cursor confinement, multi-window
z-order, capture (`HasCapture` source-of-truth not yet wired),
and fling / scroll inertia.

## Phase 3 実施結果 (2026-07-14)

- Phase 3 診断カウンタ追加: registration_ok/failed, can_dispatch_calls/accept/reject, dispatch_entry, delegate_dispatch_called/null_delegate, pointer_polls_seen, synth_moved/pressed/released, es_dispatch_none/perform_default/stop_propagation。既存 predicate/target/event 仕様は不変、ログは one-shot + 既存 destructor からの shutdown summary のみ。
- 管理パッチ cef/patch/patches/qnx/chromium/qnx_pointer_input_dispatch.patch を 6 ヘッダで再生成。clean seeded tree (CEF new_files + 4 prereq) で patch -p0 dry-run/actual apply 通過、6 files cmp 一致、python3 tools/qnx_validate_patch_format.py OK、git diff --check clean、3 .o (qnx_platform_event_source.o 75.5K / qnx_window.o 83.2K / qnx_window_manager.o 109.3K) build 成功。
- 実機未確認: 既存 /tmp/cefsimple-input.log に新カウンタ出力 0 件 (バイナリは Phase 3 編集前の 2026-07-13 20:51 ビルド)。
- cefsimple 再リンクは qnx_gpu_init_ack_callback の既存 Mojo シグネチャ不一致 (Initialize 2-arg vs qnx_gpu_platform_support_host.cc:126 1-arg caller) で阻害、Phase 3 範囲外。
- 次の分岐のみ: (1) 再リンク修正 → (2) 診断入り cefsimple 実行 → (3) shutdown summary の dispatch 系カウンタで分岐確定 → (4) 結果に応じた修正。

## 実施結果・現時点の結論 (2026-07-14)

- qnx_frame_importer_defer_gl 修正後、実機で SubmitFrame が `eglSwapBuffers reached` まで到達 (display_ok=true 連続、複数回確認)。SCREEN_EVENT_POINTER 通過に同期した dispatch end-to-end を実機観測。
- 追加 first-time LOG(INFO): `can_dispatch first accept widget=1`、`dispatch entry widget=1`、`delegate dispatched widget=1`、`source dispatch first result=2 bucket=STOP_PROPAGATION`、すべて発火。Null-delegate / can_dispatch first reject は未発火 (非-null delegate、MouseEvent+target マッチ期待どおり)。
- `--with-input` を qnx_run.sh に追加 (既定動作不変、virtio-tablet-pci + unix QMP ソケット)。1秒ホールドの scaled QMP (abs axis 0..32767) で left (0x1) / middle (0x2) / right (0x4) を event として QNX Screen に到達、buttons 分布と座標が cefsimple window 内で取得。
- 静的監査: MouseEvent 構築 (event.h:491 ctor シグネチャ + flags/changed_button_flags + Event::DispatcherApi::set_target + QnxWindow::CanAcceptEvent)、PlatformWindowDelegate 契約、`WindowTreeHostPlatform::DispatchEvent` まで明確な違反なし。`POST_DISPATCH_STOP_PROPAGATION` 戻りも契約整合。
- UI 未反応の残余原因候補は (a) cefsimple browser_info_manager timeout (PageLoad / FIRST_PAINT 未到達) (b) Views hit-test 対象未生成 (c) QEMU virtio-tablet の wheel が BTN_WHEEL 経路で QNX Screen に反映されない (Phase 5 で別途解決予定)。

## 実施結果・現時点の結論 (final, 2026-07-14)

N0pre 経路で `qnx_pointer_input_dispatch.patch` を適用した実機ラン結果:

- defer_gl 有効化経路で `SubmitFrame` が `eglSwapBuffers reached` まで
  到達、content は framebuffer に反映。
- `--with-input` / QMP (left 0x1 / middle 0x2 / right 0x4) で
  `SCREEN_EVENT_POINTER` を Screen へ送出。`CanDispatchEvent` accept、
  `DispatchEvent` → `delegate_->DispatchEvent` 到達、
  `PlatformEventSource::DispatchEvent` は
  `POST_DISPATCH_STOP_PROPAGATION` を返却。
- `MouseEvent` (Moved / Pressed / Released) は Aura 契約違反なし
  (`EventFlags` 整合、target 一致、`location` / `root_location`
  round-trip)。

未解決 / 計画外:

- cefsimple Views / web content 依然未反応。dispatch は通っているため
  残りは browser content 初期化または Views 側ハンドラ未配線に絞られる。
- Phase 5 wheel は未着手。
## 実施結果・wheel durable化 + loader 復旧 (2026-07-14)

- `qnx_pointer_input_dispatch.patch` を 656→**1743行** に再生成 (33 wheel tokens 含む, `SCREEN_PROPERTY_MOUSE_WHEEL/_HORIZONTAL_WHEEL`, `MouseWheelEvent::kWheelDelta`, `synth_wheel_v_/h_`, `first_wheel_marker_`)。`git diff --no-prefix --relative --full-index` で生成、no a/b prefixes、`patch.cfg` 1488行 entry 未変更。
- Clean apply: `/tmp/qnx-clean-seed` 6 files 削除 → `patch -p0 --batch --forward` 6/6 OK → test-dir vs worktree md5 **6/6 byte-identical**。`python3 tools/qnx_validate_patch_format.py` → OK 498。
- Object build: `qnx_platform_event_source.o / qnx_window.o / qnx_window_manager.o` 3/3 OK (1 件の `kHandledEventTypes` unused 警告は pre-existing)。`cefsimple` 再リンク mtime 07:36:45 → 09:24:34 (source 09:23:02 より後)、`[3/3] LINK ./cefsimple` exit=0、`libcef.so` に wheel 文字列 (`synth_wheel_v=`, `synth_wheel_h=`) 確認。

## 実施結果・loader 復旧 + runtime (2026-07-14)

- 外部条件 (loader path) を `export LD_LIBRARY_PATH=/mnt/nfs/out/qnx_release && export LD_PRELOAD=/usr/lib/libEGL.so.1` で **FIXED**。`ldd: FATAL: Could not load library libcef.so` 消失、cefsimple 起動成功 (pidin 確認)、log 2行 → 54行 (前回 external-fix probe)。
- 観測 markers (54行 log): `registration_ok=1`, `pointer_dispatch=3` (QNX_INPUT: POINTER 到達), `SubmitFrame=5`, `display_ok=1`, `eglSwapBuffers=1` → **GPU 入力→dispatch→描画パイプライン end-to-end 動作確認**。
- QMP injection (QEMU 8+ 形式: `axis="x"/"y"`, `button="left"` down/up) 3/3 `{"return": {}}` QEMU受理 → guest 到達 → 上記 markers で観測。
- wheel: `first_wheel=0 / synth_wheel=0 / SCREEN_PROPERTY_MOUSE_WHEEL=0` → **virtio-tablet 構造的制約** (QEMU BTN_WHEEL → `SCREEN_PROPERTY_MOUSE_WHEEL` パススルー無し)。実装失敗ではない (Phase 5 wheel code は binary 内)。
- UI surface partial: GPU render = YES (framebuffer 到達) ≠ UI 初期化 = NO (`FIRST_PAINT / browser_info_manager / BrowserCreated / PageLoad` すべて 0)。

## 実施結果・browser/content init (2026-07-14)

- cefsimple 2 processes (pid 8093734 + 8093737) 確認、両方 **CONDVAR/REPLY 固着**、5s / 15s snapshot で child process 追加無し (content_browser / content_renderer fork 未到達)。
- log 4行 (前回は 54行 → 早期 hang)、warning=1 (既知 root_cache_path)、全 content/browser markers = 0。
- hang 箇所: browser_main_loop 開始前と推定、`qnx_gpu_init_ack_callback` chain (base::BindOnce → AttachExistingWidgets) は trace flags 下で不可視、別検証が必要。
- **QNX source bug 証明不可** (log 4行で evidence 不足、固着スレッド backtrace 無し) → CEF 管理 patch は編集せず、推測修正なし。

## 次の一手 (2026-07-14)

- 固着スレッドの backtrace 採取 (host gdbserver attach または QNX 側 debugger) が最優先。backtrace が `qnx_gpu_init_ack_callback.cc` 内の `Initialize` / `AttachExistingWidgets` を指せば CEF 管理 patch に最小限の ack timeout + fallback を追加する根拠が得られる。
- backtrace が chromium 標準コード内なら QNX port bug ではなく upstream 側調査に切替。
- wheel 観測には `--device usb-tablet` + QEMU BTN_WHEEL→`SCREEN_PROPERTY_MOUSE_WHEEL` passthrough build が必要 (AGENTS scope 外、明示指示待ち)。
