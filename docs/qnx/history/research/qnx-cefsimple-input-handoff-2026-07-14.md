# QNX cefsimple input handoff (2026-07-14)

## 1. 目的と DNS 問題との分離

本ドキュメントは `qnx-cefsimple-input-implementation-plan-2026-07-13` の **入力境界特定** 作業を、2026-07-14 に確定できた範囲まで引き継ぐ。**ネットワーク/DNS は完全に除外** しており、計測 HTML は `out/qnx_release/qnx-input-probe.html` を `--url=file:///` で配信する前提。`qnx-cefsimple-external-network-implementation-plan-2026-07-13` 側の調査とは独立。

## 2. 黒画面修正済み状態 (確認済み)

- `ui/aura/window_tree_host_platform.cc::IsWindowCompositingSupported` は `true` を返す (ozone_platform_qnx.cc 変更)。
- `QnxGpuService::Initialize` で `qnx_gpu_service.cc:172` "compositor pre-swap capture hook and post-swap completion marker installed" 発火。
- `QnxGpuHost::SubmitFrame: FINAL widget=1 generation=1 accepted=true display_ok=true; eglSwapBuffers reached` 連続発火。
- `/tmp/cefsimple-input.log` 上で `OnLoadEnd status=200`、`[QNX_PROBE] html ready at <ms>`、`FOCUS=1 FOCUSIN=1`、GPU 描画パイプライン end-to-end 動作。

## 3. 入力問題の確定済み時系列 (最新 `/tmp/cefsimple-input.log` 2026-07-14 20:13, 128KB)

| 境界 | 件数 | 状態 |
|---|---|---|
| `QNX_INPUT: POINTER` (Screen→QNX translator) | **151** | 動作 |
| `QNX_INPUT: registration ok widget=1` | 1 | 動作 |
| `QNX_INPUT_BOUNDARY aura=WindowTreeHostPlatform::DispatchEvent reached` | 1 (110414.943) | 動作 |
| `QNX_INPUT_BOUNDARY ui=EventProcessor::OnEventFromSource/ACQUIRE` | 1 (110414.946) | `target_null=0` |
| `QNX_INPUT_BOUNDARY ui=EventProcessor::OnEventFromSource/POST` | 1 (110414.950) | `event.handled=0` |
| `QNX_INPUT_BOUNDARY views=Widget::OnMouseEvent` | **0** | 未到達 |
| `QNX_INPUT_BOUNDARY content=RenderWidgetHostViewAura::OnMouseEvent reached` | **0** | 未到達 |
| `QNX_PROBE_CONSOLE MOVE/DOWN/UP/CLICK/CTX/WHEEL/BLUR` | **0** | DOM 未到達 |

**確認済み確定**: EP で `target_null=0` かつ `event.handled=0` 完了 → 次の `Widget::OnMouseEvent` / `RWHVA::OnMouseEvent` / DOM すべて未到達。**最初の欠落境界は EventProcessor POST (handled=0) 直後**。
**推測 (未確定)**: `--use-native` 経路のため CEF 内部の `PlatformWindowDelegate::DispatchEvent` → `CefWindow` → `CefBrowserHost::SendMouseEvent` のどこかで消失、または aura::Window delegate が mouse 種別を reject。`aura::Window::CanAcceptEvent` delegate_null 値で判定可能。

## 4. 追加済み計測 marker (全 QNX one-shot, `#if BUILDFLAG(IS_QNX)` ガード, 静的 bool 1回のみ)

| marker | 場所 | 出力フィールド |
|---|---|---|
| `[QNX_INPUT_BOUNDARY] aura=WindowTreeHostPlatform::DispatchEvent reached` | `ui/aura/window_tree_host_platform.cc:303` | 単純到達 |
| `[QNX_INPUT_BOUNDARY] ui=EventProcessor::OnEventFromSource/ACQUIRE` | `ui/events/event_processor.cc:55` | `target_null`, `event.type` |
| `[QNX_INPUT_BOUNDARY] ui=EventProcessor::OnEventFromSource/POST` | `ui/events/event_processor.cc:116` | `dispatcher_destroyed`, `target_destroyed`, `event.handled`, `event.type` |
| `[QNX_INPUT_BOUNDARY] views=Widget::OnMouseEvent` | `ui/views/widget/widget.cc:2121` | `event.type`, `has_native_widget` |
| `[QNX_INPUT_BOUNDARY] content=RenderWidgetHostViewAura::OnMouseEvent reached` | `content/browser/renderer_host/render_widget_host_view_aura.cc:2403` | 単純到達 |
| `[QNX_AURA_TARGET] WED::PreDispatchEvent` | `ui/aura/window_event_dispatcher.cc:521` | `target_name`, `target_null`, `bounds`, `target_delegate_null`, `event.type` |
| `[QNX_AURA_TARGET] Window::CanAcceptEvent` | `ui/aura/window.cc:1617` | `name`, `bounds`, `delegate_null`, `event.type` |
| `[QNX_AURA_TARGET] WED::PostDispatchEvent` | `ui/aura/window_event_dispatcher.cc:586` | `target_null`, `event.handled`, `event.type` |
| `[QNX_BLACKSCREEN] OnAfterCreated/OnLoadStart/OnLoadEnd/OnLoadingStateChange` | `tests/cefsimple/simple_handler.cc:64/113/121/129` | CEF ライフサイクル |
| `[QNX_BLACKSCREEN] OnLoadError` | 同 137 | エラー時のみ |
| `[QNX_PROBE_CONSOLE]` | `tests/cefsimple/simple_handler.cc:146` (`OnConsoleMessage` 実装) | JS console.log → CEF 転送 |

marker 総数 11 (QNX one-shot 9 + 既存 2)、誤target/delegate null/handled 比較で原因 3 分岐を一意分離可能。

## 5. CEF 管理 patch と patch.cfg 登録 (確認済み, no-prefix/full-index, `git apply --check --reverse` 0)

| patch | path 行数 | 適用 root | patch.cfg line | entry name |
|---|---|---|---|---|
| `cef/patch/patches/qnx/chromium/qnx_input_boundary_ui_markers.patch` | 151 行 / 5 files | `ui/` | 1500 | `qnx/chromium/qnx_input_boundary_ui_markers` |
| `cef/patch/patches/qnx/chromium/qnx_input_boundary_content_marker.patch` | 17 行 / 1 file | `content/` | 1508 | `qnx/chromium/qnx_input_boundary_content_marker` |

両 patch は **qnx_pointer_input_dispatch** (line 1488) の直後、**qnx_screen_bridge_render_only_fallback** (line 1540) の前に登録。`patch.cfg` 変更は `M` (未commit) 状態、`patch_updater.py --resave` 未実行。

**ui_markers.patch 対象 5 files** (`/tmp/<name>.pre6` → `/tmp/<name>.post6` の diff 由来、hash 全部 `/tmp` で管理):
- `ui/events/event_processor.cc` (include `base/logging.h` + 2 LOGs)
- `ui/aura/window_tree_host_platform.cc` (1 LOG)
- `ui/views/widget/widget.cc` (1 LOG)
- `ui/aura/window_event_dispatcher.cc` (2 LOGs, 新規追加)
- `ui/aura/window.cc` (1 LOG, 新規追加)

**content_marker.patch 対象 1 file**: `content/browser/renderer_host/render_widget_host_view_aura.cc` (1 LOG)。

## 6. build 結果 (確認済み)

- コマンド: `out/qnx_release/ninja_qnx.sh cefsimple` (直前の build: `BUILD_EXIT=0`)
- `libcef.so` mtime: **2026-07-14 20:10:05** 1,558,931,808 B
- `cefsimple` mtime: **2026-07-14 17:11:56** 3,460,056 B (cefsimple バイナリ自体は依存なし、変化なし)
- `out/qnx_release/qnx-input-probe.html` mtime: 2026-07-14 17:09:34 7,827 B
- 警告: 0 件 (本 session の marker 由来)、pre-existing `fling_scheduler_base.h:27 trailing whitespace` 1 件は無関係

## 7. ユーザー実行コマンド (確認済み, **非 detach** 推奨)

detach モードでは qemu ready 後にラッパーが即 return し、その直後の QMP 注入が guest 側 Screen ポーリングに到達しない事例を本 session で再現確認済み (RUNTIME_BOUNDARY_RESULT/RESULT2 参照)。**非 detach モード**で qemu ready 待ち → QMP 注入 → app log 採取 → `kill <QEMU_PID>` 終了。

```bash
# 1) 起動 (qemu ready まで同期ブロック)
/home/yuta/chromium/src/cef/tools/qnx_run.sh \
  --virgl --preload-system-egl --with-input --kill-existing --detach \
  --qconn-port 8000 -- \
  /mnt/nfs/out/qnx_release/cefsimple \
    --ozone-platform=qnx --use-gl=egl --use-native --no-sandbox \
    --enable-logging=stderr --v=1 \
    --vmodule=qnx_platform_event_source=2,qnx_window=2 \
    --ozone-qnx-gpu-trace \
    --url=file:///mnt/nfs/out/qnx_release/qnx-input-probe.html
# → wrapper は "Detach complete." で return するが残り qemu は稼働継続
# → 直後 QMP /tmp/qnx-qmp.sock へ QEMU 10.1.2 schema (data:{axis/value, button/down}) で
#    input-send-event device="QEMU Virtio Tablet" を送る。
#    もしくは HMP: mouse_set 3 → mouse_move X Y → mouse_button 1/0

# 2) app log 採取 (host 側)
/home/yuta/chromium/src/out/qnx_release/qnx_run_<stamp>_cefsimple_app.log
/home/yuta/chromium/src/out/qnx_release/qnx_run_<stamp>_cefsimple.log.serial

# 3) 終了
kill <QEMU_PID>      # wrapper 出力の "QEMU kept running: PID=..." を指定
```

## 8. 次ログの判定表 (marker 観測パターン → 推定原因)

| 観測 | 推定 | 次の計測 |
|---|---|---|
| WTHP=0, POINTER=151 | QNX Window へ dispatch 未到達 | 既存 QNXWindow::DispatchEvent chain の pre-Aura 計測追加 |
| WTHP=1, ACQUIRE=0 | Aura 受信なし | WTHP 直前で `SendEventToSink` 戻り値チェック |
| ACQUIRE=0, target_null=0 | target 取得失敗 | WED PreDispatchEvent の target 比較 |
| ACQUIRE=1, POST=0 | dispatch ループで即 return (weak_target/this null) | WED PreDispatchEvent 追加計測 |
| ACQUIRE=1, POST=1 (handled=0), Widget=0, RWHVA=0 | aura::Window → 委譲先未到達 | WED::PreDispatchEvent + Window::CanAcceptEvent で target_window.name と delegate_null を確認 |
| CanAcceptEvent delegate_null=true | delegate 不在 | QnxWindow の `PlatformWindowDelegate*` 構築経路を再監査 |
| Widget=1, RWHVA=0 | Views → content 境界で停止 | `WebContents` 受領経路に one-shot 追加 |
| Widget=0, RWHVA=1 | `--use-native` 経路の Views スキップは正常、RWHVA 直前で停止 | CEF native → WebContents forwarding 経路に one-shot 追加 |
| Widget=1, RWHVA=1, DOM=0 | Renderer 側 IPC 失敗 | `RenderProcessHost` / `Browser compositor` 経路に計測 |
| DOM > 0 | 解決 | smoke へ |

## 9. commit 08ae54443 以降の未 commit 変更 (確認済み)

```
$ git status --short
 M patch/patch.cfg                                 ← 3 entry 追記 (pointer_input_dispatch 直後)
?? patch/patches/qnx/chromium/qnx_input_boundary_content_marker.patch
?? patch/patches/qnx/chromium/qnx_input_boundary_ui_markers.patch
```

chromium/ 側 (cef/patch/patches/ 外) の直接編集は **一切なし**。本 session の marker 追加も全 patch 経由 (CIEF管理patch) で再現可能。`cefsimple` / `libcef.so` / `qnx-input-probe.html` は前回 commit 時点から変化なし (`libcef.so` のみ再リンクで mtime 更新)。

## 10. 禁止事項・patch hygiene・既知 pitfall

### 禁止
- `git commit` / `git push` / `smoke` テスト (commit hash 安定後の smoke 以外)。
- 既存 chromium ソースを patch 経由でなく直接編集すること。
- `cef/patch/patch.cfg` に登録されていない CEF管理 patch の追加。
- chromium/ 側ツリーへの QNX patch をマージせず放置。
- Pi セッション中での `git stash` / `git reset --hard` / `git checkout HEAD -- <cef_file>` の濫用 (本 session で `git checkout HEAD -- ui/views/view.cc ui/aura/window_event_dispatcher.cc` 1回のみ実行、Pi broad-replace 事故 → 修復済み)。

### patch hygiene
- **a/b prefix 禁止** (`--- a/...` / `+++ b/...` 形式不可)、`diff --git <path> <path>` 形式。
- `index <pre40>..<post40> 100644` の pre hash は **`/tmp/<name>.pre` 方式** で生成 (working tree から marker block 削除した状態の `git hash-object`)。HEAD を pre にできるのは pre-existing 差分が無い場合のみ。
- `index 0000...0000` 禁止 (新規ファイル作成 patch は `cef/patch/qnx/chromium/new_files/` 配下を使う)。
- `git apply --check --reverse` を patch root 別に毎回実行 (ui/ root: `cd ui && git apply --check --reverse`、content/ root: `cd content && git apply --check --reverse`)。
- marker block 以外の差分混入禁止 (`git diff --check` を併走、warning が出たら marker block 由来か確認)。
- 複数 root にまたがる patch は 1 ファイルにまとめず **root 別 patch に分割** (本 session 反省: 初回 3 files 混合 patch で reverse check 失敗 → ui/ と content/ に分割して解決)。
- `python3` の `replace` 一括変換はスコープ外を置換するため、**marker block 内のみ**に限定。CHECK/switch 内の `event.type()` を `static_cast<int>(event.type())` に置換しないこと (RWHVA 等の CHECK で `int == EventType` 比較不可、本 session で 4 回誤修正 → 修復済み)。

### 既知 pitfall
- **QEMU 10.1.2 input-send-event**: `data:{axis/value, button/down}` 形式必須。QEMU 8+ の `axis="x"/"y"` 形式は無効。
- **QEMU 10.1.2 device 引数**: `query-mice` に出る `"QEMU Virtio Tablet"` は label で、device id は空。`input-send-event device=...` で `DeviceNotFound` → HMP `mouse_set 3` 経由で `mouse_move`/`mouse_button` を使うのが確実。
- **detach モード + QMP 注入**: `qnx_run.sh --detach` は wrapper 終了時に QEMU 稼働を維持するが、**detach 経路で投入した QMP は本 session で guest Screen ポーリングに到達しなかった** (再現確認)。非 detach で QEMU ready 待ち → 注入、を推奨。
- **`synth_*` VLOG(2) 隠蔽**: 既知の `qnx_pointer_input_dispatch.patch` 由来の `synth_moved/pressed/released` カウンタは `vmodule=qnx_window=3` 必須。本 session の marker は `LOG(INFO)` なので隠蔽なし。
- **EP POST `event.handled=0`**: 初期実装で `int == EventType` 比較が CHECK で失敗して build error → `static_cast<int>(event->type())` で回避。型注記重要。
- **CefWindow/CefBrowser と aura::Window**: `--use-native` 経路では `views::Widget` を経由せず `Widget::OnMouseEvent=0` は正常な経路スキップの場合あり。原因分離には `RWHVA::OnMouseEvent` の有無を併用。
- **CHROMIUM_SRC path**: `cef_create_projects_qnx.sh` bootstrap 後の `out/qnx_release` は **host path = `/home/yuta/chromium/src/out/qnx_release`**, guest path = `/mnt/nfs/out/qnx_release`。NFS マウント経由。`LD_PRELOAD=/usr/lib/libEGL.so.1` 必須 (本 session の run.sh 内で export 済)。

## 11. 次エージェントの最初の 5 手

1. **`/tmp/cefsimple-input.log` を `awk '/QNX_AURA_TARGET/{print}'` で時系列表示**し、PreDispatchEvent/CanAcceptEvent/PostDispatchEvent の 3 marker が同一 event に同発火するか確認。**未発火なら marker 位置が dispatch path 外** であり marker 設置位置の再選定 (例: `aura/window_targeter.cc::FindTargetInRootWindow` に移動) を検討。
2. **marker 全発火でも `event.handled=0` が維持** されている場合: `aura::Window::OnEvent` 経由の `WindowDelegate::OnEvent` 委譲呼び出し箇所 (`aura::Window::GetEventHandlerForPoint` 直後) に `[QNX_AURA_DELEGATE] delegate_name=… event.handled_before=… event.handled_after=…` one-shot を追加し、**delegate 自体が呼ばれているか / 呼出し後 handled が変化するか** を一意分離。
3. **delegate 呼ばれていない場合**: `aura/window_event_dispatcher.cc` の `WindowEventDispatcher::OnEventFromSource` 内、aura tree 走査ループに `[QNX_AURA_TREE] cur_window=… match=… dispatching=…` one-shot を追加し、hit-test 結果がどの `aura::Window` か可視化。
4. **delegate 呼ばれて handled 不変**: CEF native 経路の `CefWindow` / `CefBrowserHost` 側に one-shot を追加 (chromium/ 側のファイルではないため CEF 本体への追加 → `libcef_dll_wrapper.cc` か `cef/libcef/browser/...` 配下)。**この段階で chromium ソース直接編集になるため CEF管理 patch 化が必要**。
5. **delegate 呼ばれて handled=true だが DOM 0**: `RenderWidgetHostViewAura::OnMouseEvent` 直後の `event_handler_->OnMouseEvent(event)` 戻り値と IPC 経路に one-shot を追加。`qnx_input_boundary_content_marker.patch` に追記する形で 1 ファイルで完結。

---

**確認済み vs 推測**: §3 数値・§4 marker 一覧・§5 patch 登録・§6 build 結果・§9 未commit 一覧は本 session の log/`git status`/`stat` で確認。§3 「次の欠落境界」原因候補と §10 pitfall の QEMU 10 schema / detach 注入不可 は本 session で再現確認。他原因候補は推測。
