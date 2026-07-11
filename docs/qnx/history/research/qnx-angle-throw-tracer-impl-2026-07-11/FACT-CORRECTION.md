# fc8b36673 訂正提案 (2026-07-11)

## 訂正対象

Commit `fc8b36673` が導入した `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/`
配下のドキュメント群のうち、以下の断定は v4c smoke 撤回 + 今回の GlobalMutex
diagnostic smoke 結果で **未確定 / 訂正** が必要:

### 撤回する断定 (fc8b36673 時点)

| 断定 | 撤回理由 |
|---|---|
| **`__cxa_throw` interpose は ANGLE caller に届かない** | v4c の unwinder libc++ caller PC が不正 (disassembly 比較で 0x58691 は制御フロー上到達不可能)。「届かない」は事実だが理由は unwinder 信頼性であり LD_PRELOAD 設計欠陥ではない。 |
| **`pid=528415` の throw は `std::future_error`** | stripped binary addr2line の偽陽性。0x58691 は `__throw_future_error` 領域内 (0x58681-0x586c0) で `__throw_system_error` (0x58596) ではない。v4c RESULT で unwinder 信頼性問題として撤回済み。 |
| **`std::system_error(EDEADLK)` throw site は `libGLESv2.so std::set<string>::find`** | post-unwind 1 frame の `terminate_capture_qnx.cc` dladdr 結果から逆算。監督指示により逆算採用禁止。 |
| **「throw は別 thread」「eglInitialize が worker thread spawn」** | 完全に推測。v4c audit で撤回済み。 |

### 今回の smoke で新たに確認した事実

| 事実 | 出典 |
|---|---|
| **same-thread same-GlobalMutex 再入** が GPU child 2 件連続で観測 | RESULT.md §Verdict |
| REENTRY event → terminate → exit_code=134 が同一 run 内で連続 | RESULT.md §Event sequence |
| 2 個の distinct mutex (`this` 異なる) が nested に保持される間に同一 `this` 再 lock で `std::mutex::lock()` が EDEADLK 相当 abort | RESULT.md §Distinction |
| `libGLESv2.so+0x9eb85` = `EGL_GetDisplay` 内 (size 128) の `0x9eb80` で `ScopedGlobalMutexLock<0>::ScopedGlobalMutexLock()` ctor 直後の RA | disassembly で監督確認済み (v4c audit) |

### 未訂正 (残置)

| 項目 | 状態 |
|---|---|
| `libangle_throw_tracer.so` (LD_PRELOAD) 自体は技術的に動作 | G1-G9 gate pass、`__cxa_throw` interpose 発火は mini gate で確認 |
| 同一 run 内 correlation 用途には不適 | libc++ caller frame の unwinder 信頼性問題 (v4c audit 撤回) |
| ANGLE/libGLESv2 frames 用途には有用 (mini gate で確認) | unwinder は自分の TU 外 (libGLESv2.so 内) では正常 |

### 訂正の選択肢 (commit 前 監督判断待ち)

**Option A**: `fc8b36673` を **amend** し、本 RESULT.md の §撤回 §FACT §Speculation を
コミットメッセージに取り込み。既存 docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/
内の誤断定文書を訂正 commit で上書き。

**Option B**: `fc8b36673` を **そのまま** とし、追加 commit で
`docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/FACT-CORRECTION.md` を
追加 (本ファイル)。 既存 docs の訂正は別 issue/PR で実施。

**Option C**: `fc8b36673` を **revert** する。ただし throw-tracer 自体は
host-side correlation 用途 (ANGLE/libGLESv2 frames) で将来有用な可能性があり、
完全 revert は時期尚早。

**推奨**: Option B (監督コメントを引用する訂正文書を追加。既存 docs の
上書き訂正は別 PR)。ただし監督判断に従う。

### 訂正案 (本ファイル採用時) のコミット内容

1. **新規 CEF-managed files**:
   - `cef/patch/patches/qnx/chromium/angle_qnx_global_mutex_diag_sources.patch`
   - `cef/patch/patches/qnx/chromium/angle_qnx_global_mutex_diag.patch`
   - `cef/patch/patches/qnx/chromium/angle_qnx_global_mutex_diag_build.patch`
   - `cef/patch/qnx/chromium/new_files/third_party/angle/src/libANGLE/global_mutex_qnx_diag.cc`
2. **patch.cfg への 3 entry 追加** (`angle_qnx_global_mutex_diag_sources`,
   `angle_qnx_global_mutex_diag`, `angle_qnx_global_mutex_diag_build` —
   適用順)
3. **新規 research docs**:
   - `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/RESULT.md`
     (本 smoke の結果と事実/推測分離)
   - `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/FACT-CORRECTION.md`
     (本ファイル)
   - `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/logs/cefsimple-angle-gles-egl-gmdiag.log`
     (smoke raw log)
   - `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/logs/cefsimple-angle-gles-egl-gmdiag.parse.txt`
     (parser 出力)
   - `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/scripts/parse_gmd.py`
     (event sequence parser)

### 訂正適用範囲外 (今回は対象外)

- 既存 `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/GATES.md`
  (G1-G9 gate pass 記録) — 訂正不要 (gate 自体は mini test で通過、撤回
  対象は smoke 結論のみ)。
- 既存 `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/DESIGN.md`
  (LD_PRELOAD 設計書) — 訂正不要 (設計自体は abort-safe; 訂正対象は
  smoke 結論が design assumption と合わない点のみ、RESULT.md で言及済み)。
- `cef/patch/patch.cfg` の `qnx/chromium/angle_qnx_throw_tracer` エントリ
  — 訂正不要 (tracer 自体は維持、smoke 結論のみ訂正)。

### Patch 機械生成手順の遵守記録

本コミット (提案) の 3 patch は手書き header/hunk なし。`/tmp/angle_diag_patchroot`
で baseline (post-existing-CEF-patches ANGLE 状態) を `git init` + commit、
modified を上書き、`git diff --no-prefix --relative --full-index` で機械生成。
qnx-bootstrap skill の clean recipe に従い **491 patches (470 applied, 21
skipped, 0 failed)** で bootstrap 成功。`/tmp/angle_diag_patchroot` は削除済。
sudo 不使用。args.gn 手書きなし。RM -rf は `/tmp/angle_diag_patchroot` のみ。