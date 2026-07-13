# QNX ANGLE black screen / DMAbuf export extension investigation — PLAN

| field | value |
|---|---|
| date | 2026-07-12 |
| branch | `qnx_7727` @ `fd5d3a98a` |
| author | (planning pass, no execution) |
| status | PLAN — written, not executed |
| trigger | clean build で `--use-gl=egl` は sky blue OK、`--use-angle` は黒画面 |
| scope | exit139 解決後の残存問題（ANGLE 実描画）に絞る |

---

## 0. Context（前提条件）

### 0.1 直近 commit 状況

| commit | 役割 |
|---|---|
| `debf52a63` | ANGLE recursive GlobalMutex fix（Phase B）。`exit_code=134` 解消 |
| `f329965f2` | docs (17 件) + 新 QNX patch `ui_ozone_egl_gles_library_precedence_qnx.patch`（仮説: ANGLE vs system GLES namespace 混在） |
| `fd5d3a98a` | throw-tracer audit-trail 5 files |

### 0.2 直近 smoke 結果（90s × 7 runs, 既存 build すべて共通）

- `exit_code=139` = 0 across 7 runs（**exit139 は解決済み**）
- `exit_code=134` = 0（recursive fix 効いている）
- `std::terminate invoked` = 0
- 7 runs すべて以下 extension report で停止:
  - `EGL_MESA_drm_image`: ABSENT
  - `EGL_MESA_image_dma_buf_export`: ABSENT
  - `eglCreateDRMImageMESA`: NOT RESOLVED
  - `eglExportDMABUFImageMESA`: NOT RESOLVED
  - `eglExportDMABUFImageQueryMESA`: NOT RESOLVED
  - `eglDestroyImageKHR`: RESOLVED
  - `EGL_EXT_image_dma_buf_import`: PRESENT
  - `EGL_EXT_image_dma_buf_import_modifiers`: PRESENT

### 0.3 ユーザの clean build 後の観測

- `--use-gl=egl` → sky blue OK（Chromium default theme が system EGL/GLES path で正しく compositing）
- `--use-angle` → 黒画面（QnxRenderProducer の minimum requirement 未達で `producer_valid=false` → `SubmitTestFrameForWidget` SKIP → 0 frame 提出）

### 0.4 関連既知 issue（pre-existing、本調査範囲外）

- `patch/patches/linux_assets_path_1936.patch` の 2/3 hunk が upstream で既に適用済み → 新規 bootstrap は失敗する。ただし既存 build artifacts（`out/qnx_release/` 配下）は無傷。

---

## 1. Problem statement

### 1.1 期待動作

`--use-angle` で ANGLE の EGL backend 経由で少なくとも 1 フレーム以上を画面に出す。ANGLE が EGL surface に sky blue 相当の solid color を描く、または frame submit 後に内容が表示される。

### 1.2 観測動作

ANGLE path で `QnxRenderProducer::Initialize` が `producer_valid=false` で早期 return → `QnxGpuService::AttachWidget` で `SubmitTestFrameForWidget` が SKIP → 0 frame → 画面は default GL surface color（黒）のまま。

### 1.3 関連する source / file

| file | line | 内容 |
|---|---|---|
| `ui/ozone/platform/qnx/qnx_render_producer.cc` | 195-203 | `ResolveEGL<>(name)` で関数ポインタ取得（`eglGetProcAddress` 経由） |
| 同上 | 277-278 | `has_egl_mesa_image_dma_buf_export_` を `egl_exts` の `strstr` で判定 |
| 同上 | 260-261 | minimum requirement: `has_egl_mesa_drm_image_ && has_egl_mesa_image_dma_buf_export_ && egl_create_drm_image_mesa_ != nullptr && egl_export_dma_buf_image_mesa_ != nullptr` |
| 同上 | 289-298 | `ResolveEGL<Fn>(name)` 実装 = `eglGetProcAddress(name)` を呼ぶだけ（library handle 引数なし = current EGL display に紐付く） |
| `ui/ozone/common/egl_util.cc` | 129+ | `LoadEGLGLES2Bindings`（新 QNX patch で `#if BUILDFLAG(IS_QNX)` 内の `gl::AddGLNativeLibrary` 順序変更済） |
| `ui/gl/gl_implementation.cc` | 417-426 | `AddGLNativeLibrary` 実装（`g_libraries->push_back` のみ、order 保持） |
| `third_party/angle/src/libANGLE/display/DisplayEGL.cpp` | — | ANGLE の EGL extension probe（要 full path 確認） |

### 1.4 失敗 path（smoke log から確定）

```
QnxRenderProducer::Initialize:
  ResolveEGL("eglCreateDRMImageMESA") → null
  ResolveEGL("eglExportDMABUFImageMESA") → null
  strstr(eglQueryString(EGL_EXTENSIONS), "EGL_MESA_image_dma_buf_export") → null
  → init_error_ セット → return false
↓
QnxGpuService::AttachWidget: ... SKIP SubmitTestFrameForWidget (producer_valid=false)
↓
0 frame submitted → 画面黒
```

---

## 2. Root cause hypotheses（mutually exclusive ではない）

| ID | 仮説 | 現在の確度 |
|---|---|---|
| **H1** | system libEGL.so が Mesa 拡張を export していない（QNX BSP 制約） | 中 — desktop Mesa 以外の EGL 実装は Mesa 拡張を持たないのが一般的 |
| **H2** | ANGLE が `eglQueryString(EGL_EXTENSIONS)` 結果に Mesa 拡張を通さない（ANGLE 自身の仕様） | 中 — ANGLE は独自 extension セットを持つ。Mesa 拡張をそのまま pass-through する保証はない |
| **H3** | `eglGetProcAddress` の library handle / symbol 解決失敗 | 低 — `ResolveEGL` の単純構造からは見えないが、ANGLE library handle の lifecycle に依存する可能性 |
| **H4** | 新 QNX patch の scope 違い（library precedence は core GL 解決向けで、Mesa extension advertisement とは無関係） | 高（ユーザ観測と整合）— 新 patch 適用後の clean build でも症状変わらず |

**H4 は H1-H3 の否定材料**：新 patch が ANGLE libEGL を先頭に置く前提で作られている。patch 適用後に症状が消えないなら、(a) patch が library precedence を変えることに成功していない、または (b) precedence を変えても Mesa extension advertisement には影響しない、のいずれか。

---

## 3. Investigation steps

### 3.1 Investigation A: System / build libEGL.so Mesa extension inventory（READ-ONLY）

- **目的**: QNX BSP の system libEGL.so と、build 済 `out/qnx_release/libEGL.so`（ANGLE）がそれぞれ Mesa 拡張を export しているかを `objdump -T` で確認。H1/H2 切り分けの第一手。
- **コスト**: ~5 分、read-only、QEMU 不要、build 不要。

**実行コマンド**:

```bash
# 1) QNX system libEGL の位置確認
find /home/yuta/qnx800/target/qnx -name 'libEGL.so*' -o -name 'libGLES.so*' 2>/dev/null

# 2) 各 system libEGL について Mesa 関連 symbol を grep
for lib in $(find /home/yuta/qnx800/target/qnx -name 'libEGL.so*'); do
  echo "=== $lib ==="
  /home/yuta/qnx800/host/linux/x86_64/usr/bin/x86_64-pc-nto-qnx8.0.0-objdump -T "$lib" 2>&1 | \
    grep -iE 'EGL_MESA|dma_buf|DRMImageMESA' | head -20
done

# 3) build 済 libEGL.so (ANGLE) を同様確認
echo "=== out/qnx_release/libEGL.so (ANGLE) ==="
/home/yuta/qnx800/host/linux/x86_64/usr/bin/x86_64-pc-nto-qnx8.0.0-objdump -T \
  /home/yuta/chromium/src/out/qnx_release/libEGL.so 2>&1 | \
  grep -iE 'EGL_MESA|dma_buf|DRMImageMESA' | head -20

# 4) 比較対照: ANGLE の eglQueryString / eglGetProcAddress が export されているか
echo "=== out/qnx_release/libEGL.so egl core symbols ==="
/home/yuta/qnx800/host/linux/x86_64/usr/bin/x86_64-pc-nto-qnx8.0.0-objdump -T \
  /home/yuta/chromium/src/out/qnx_release/libEGL.so 2>&1 | \
  grep -E 'eglQueryString|eglGetProcAddress|eglCreateImage' | head -10

# 5) 結果を /tmp/qnx-dmabuf-export-investigation-2026-07-12/A-inventory.txt に保存
mkdir -p /tmp/qnx-dmabuf-export-investigation-2026-07-12
# (上記コマンド出力をファイルにリダイレクト)
```

**期待される出力**:

- system libEGL: Mesa 関連 symbol 0 件（QNX BSP 制約）または N 件（system EGL が Mesa 系の場合）
- ANGLE build libEGL.so: ANGLE の upstream が Mesa 拡張を実装しているかに依存。Mesa upstream 由来ではないので、おそらく 0 件

**Decision rules**:

| 結果 | 結論 |
|---|---|
| system libEGL に Mesa symbol 無し | H1 真因確定 → Investigation D (代替 path design) |
| system libEGL に Mesa symbol 有り | H1 否定、ANGLE 側の問題 → Investigation C で ANGLE ext string を確認 |
| ANGLE libEGL に Mesa symbol 無し（system/ANGLE 共通） | H1 + H2 真因確定 → Investigation D |
| ANGLE libEGL に Mesa symbol 有り、system libEGL に無し | H2 真因（ANGLE は持っているが通さない）→ Investigation C で確認後 D |

### 3.2 Investigation B: Library load order trace（1 smoke run、env-only）

- **目的**: ANGLE mode で起動時、`libEGL.so` がどの順でロードされているかを `LD_DEBUG=libs` で確認。新 QNX patch が effective に ANGLE libEGL を先頭に置いたか、H4 の (a)/(b) 切り分け。
- **コスト**: ~5 分 + smoke 1 run（30-90s）、rebuild 不要。

**実行コマンド**:

```bash
mkdir -p /tmp/qnx-dmabuf-export-investigation-2026-07-12
cd /home/yuta/chromium/src/cef
TS=$(date +%Y%m%d-%H%M%S)
LOG=/tmp/qnx-dmabuf-export-investigation-2026-07-12/B-ld_debug-${TS}.log

# LD_DEBUG を --env で注入。LD_DEBUG_OUTPUT は guest writable な path にする
# QNX guest 上 /tmp は tmpfs で writable。NFS mount (/mnt/nfs) も writable。
# ここでは /tmp に書く想定（再起動で消えるが、調査用途なら十分）
bash tools/qnx_run.sh --virgl --kill-existing --timeout 30 --boot-timeout 120 -- \
  --env LD_DEBUG=libs --env LD_DEBUG_OUTPUT=/tmp/lib_load.txt \
  './cefsimple --ozone-platform=qnx --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr 2>&1' \
  > $LOG 2>&1

# 結果を host 側へコピー（または LOG を parse）
# /tmp は guest 側なので host から直接読めない。QEMU 内で読み出してリダイレクトする wrapper に変える必要がある。
# 簡易案: command 内で lib_load.txt を /mnt/nfs (NFS mount) にコピー
# bash tools/qnx_run.sh ... -- \
#   --env LD_DEBUG=libs --env LD_DEBUG_OUTPUT=/mnt/nfs/out/qnx_release/lib_load_${TS}.txt ...
```

**Practical workaround**: `LD_DEBUG_OUTPUT` を NFS mount 配下（`/mnt/nfs/out/qnx_release/`）に指定すれば host から読める。

```bash
bash tools/qnx_run.sh --virgl --kill-existing --timeout 30 --boot-timeout 120 -- \
  --env LD_DEBUG=libs --env LD_DEBUG_OUTPUT=/mnt/nfs/out/qnx_release/lib_load_${TS}.txt \
  './cefsimple --ozone-platform=qnx --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr 2>&1'

# その後 host 側で
grep -E 'libEGL|libGLES' /home/yuta/chromium/src/out/qnx_release/lib_load_${TS}.*.txt | head -40
```

**期待される出力**:

```
   1234: finding library libEGL.so; searching
   1234: search path=/mnt/nfs/out/qnx_release  (LD_LIBRARY_PATH)
   1234: trying /mnt/nfs/out/qnx_release/libEGL.so -> /mnt/nfs/out/qnx_release/libEGL.so
   ...
   1234: calling init: /mnt/nfs/out/qnx_release/libEGL.so
```

**Decision rules**:

| 結果 | 結論 |
|---|---|
| `libEGL.so` の初回ロードが `out/qnx_release/libEGL.so`（ANGLE） | 新 QNX patch 効いている → H4(a) 否定、library precedence 起因は無い |
| `libEGL.so` の初回ロードが system 側（`/home/yuta/qnx800/...` または `/usr/lib/...`） | 新 QNX patch 効いていない → H4(a) 真因、patch 自体の検証必要 |
| `libEGL.so` が複数回ロード（ANGLE → system の順、逆順、両方） | ライブラリ解決の競合状態 |

### 3.3 Investigation C: ANGLE DisplayEGL extension string dump（rebuild + smoke）

- **目的**: ANGLE が application に報告する EGL extension 文字列を直接観測。H2 真因判定の決定打。
- **コスト**: ~30-60 分（rebuild 含む）、smoke 1 run。

**実行手順**:

1. **Transient edit**（commit しない、計画 Phase 末尾で revert）:

   `ui/ozone/platform/qnx/qnx_render_producer.cc` の `QnxRenderProducer::Initialize` 冒頭に以下を追加:
   ```cpp
   // QNX-DEBUG: dump ANGLE-side EGL state for dmabuf investigation
   const char* egl_vendor = eglQueryString(EGL_VENDOR);
   const char* egl_version = eglQueryString(EGL_VERSION);
   const char* egl_extensions = eglQueryString(EGL_EXTENSIONS);
   LOG(INFO) << "QNX-DEBUG EGL_VENDOR=" << (egl_vendor ? egl_vendor : "<null>");
   LOG(INFO) << "QNX-DEBUG EGL_VERSION=" << (egl_version ? egl_version : "<null>");
   LOG(INFO) << "QNX-DEBUG EGL_EXTENSIONS=" << (egl_extensions ? egl_extensions : "<null>");
   LOG(INFO) << "QNX-DEBUG eglGetProcAddress(eglCreateDRMImageMESA)="
             << (void*)eglGetProcAddress("eglCreateDRMImageMESA");
   LOG(INFO) << "QNX-DEBUG eglGetProcAddress(eglExportDMABUFImageMESA)="
             << (void*)eglGetProcAddress("eglExportDMABUFImageMESA");
   LOG(INFO) << "QNX-DEBUG eglGetProcAddress(eglExportDMABUFImageQueryMESA)="
             << (void*)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
   ```

   注: 既に同 file に `ExtensionReport()` で各 extension string / function pointer の PRESENT/NOT RESOLVED 判定があるので、追加 logging は `Initialize` の冒頭（`egl_exts` 取得前）と組み合わせる。重複可。

2. **Rebuild**:
   ```bash
   cd /home/yuta/chromium/src
   ./out/qnx_release/ninja_qnx.sh ui/ozone/platform/qnx cefsimple
   # 影響範囲は ui/ozone/platform/qnx と cefsimple のみ。~10-20 分想定
   ```

3. **Smoke run**:
   ```bash
   cd /home/yuta/chromium/src/cef
   TS=$(date +%Y%m%d-%H%M%S)
   LOG=/tmp/qnx-dmabuf-export-investigation-2026-07-12/C-angle-ext-${TS}.log

   bash tools/qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180 -- \
     './cefsimple --ozone-platform=qnx --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr 2>&1' \
     > $LOG 2>&1
   ```

4. **Extract**:
   ```bash
   grep -E 'QNX-DEBUG (EGL_|eglGetProcAddress)' $LOG
   ```

**期待される出力**:

```
[GPU_pid] QNX-DEBUG EGL_VENDOR=ANGLE
[GPU_pid] QNX-DEBUG EGL_VERSION=1.5 ANGLE 2.1 (...)
[GPU_pid] QNX-DEBUG EGL_EXTENSIONS=EGL_KHR_create_context EGL_KHR_surfaceless_context ... (NO MESA_*)
[GPU_pid] QNX-DEBUG eglGetProcAddress(eglCreateDRMImageMESA)=0x0
[GPU_pid] QNX-DEBUG eglGetProcAddress(eglExportDMABUFImageMESA)=0x0
```

または（仮説が違えば）:

```
[GPU_pid] QNX-DEBUG EGL_EXTENSIONS=EGL_KHR_create_context ... EGL_MESA_image_dma_buf_export ...
[GPU_pid] QNX-DEBUG eglGetProcAddress(eglCreateDRMImageMESA)=0x7f...
```

**Decision rules**:

| 結果 | 結論 |
|---|---|
| ext string に `EGL_MESA_*` 有り + `eglGetProcAddress` 非 null | H1+H2 否定、H3 または別問題 → `ResolveEGL` の直前/直後 logging を強化して再調査 |
| ext string に `EGL_MESA_*` 有り + `eglGetProcAddress` null | ANGLE が Mesa extension を advertise するが function pointer は ANGLE 内部で実装していない → H3 亜種（ANGLE 設計制約）→ D |
| ext string に `EGL_MESA_*` 無し + `eglGetProcAddress` null | H2 真因確定 → D（ANGLE 自身の extension advertisement 範囲外） |
| ext string に `EGL_MESA_*` 無し + vendor が `ANGLE` でない | system libEGL が current display になっている → H4(a) 真因（patch 効いてない）→ Investigation B の結果と組み合わせ |

### 3.4 Investigation D: Alternative buffer sharing path design（H1/H2 確定時のみ）

- **目的**: ANGLE が Mesa extension を通さない（H1+H2 真因）場合、ANGLE-mode で QNX 上描画するための代替 mechanism を design。
- **コスト**: 数時間〜数日（design + impl + smoke）

**Scope（要確認項目）**:

1. ANGLE 自身の alternative extension:
   - `EGL_ANGLE_stream` / `EGL_ANGLE_external_img_data` などの ANGLE-native buffer sharing
   - `GL_OES_EGL_image_external` (texture-side)

2. QNX native buffer API:
   - `screen_create_pixmap_buffer()` / `screen_get_buffer_propertylv()` 等の Screen Graphics Subsystem API
   - `screen_*` 関数群は QNX BSP に含まれる。`/home/yuta/qnx800/target/qnx/usr/include/screen/screen.h` で API 確認可能

3. `ui/ozone/platform/qnx/qnx_render_producer.cc` の代替パス:
   - 既存の `QnxRenderProducer::Initialize` 内の `eglCreateDRMImageMESA` 依存を置き換え
   - ANGLE → QNX screen_* API への bridge 設計

**設計の前提**: ANGLE 側にも軽微な変更が必要な可能性（`EGL_ANGLE_*` の query を追加）。その場合は CEF-managed patch として `patch/patches/qnx/chromium/angle_qnx_screen_buffer_bridge.patch` 等を新規作成。

**Decision rules**:

| 結果 | 結論 |
|---|---|
| `EGL_ANGLE_*` または `GL_OES_EGL_image_external` で代替可能 | Investigation E（実装）に進む |
| QNX `screen_*` API への直接 bridge が必要 | Investigation F（system EGL 経由の設計）に進む |
| どちらも困難 | ANGLE-mode on QNX は現 platform で不可、`--use-gl=egl` で運用継続。D では ANGLE-mode を disable する option を設計 |

---

## 4. Recommended execution sequence

```
Step 1: Investigation A (5 min, read-only)
  ├─ system libEGL に Mesa symbol 無し → H1 真因確定 → goto Step 4
  ├─ ANGLE libEGL に Mesa symbol 無し → H1+H2 真因確定 → goto Step 4
  └─ 両方にある / ANGLE のみにある → goto Step 2

Step 2: Investigation B (5 min, 1 smoke)
  ├─ ANGLE libEGL 先頭 → 新 patch 効いている、H4(a) 否定 → goto Step 3
  ├─ system libEGL 先頭 → 新 patch 効いていない、H4(a) 真因 → patch を見直し後 Step 3
  └─ 両方がロード → 競合状態 → Step 3 で ANGLE ext string を確認

Step 3: Investigation C (30-60 min, rebuild + 1 smoke)
  ├─ ANGLE ext string に MESA_* 有り + proc addr 非 null → H3 / 別問題 → ResolveEGL 周辺詳細 logging
  ├─ ANGLE ext string に MESA_* 有り + proc addr null → H3 亜種 → D
  └─ ANGLE ext string に MESA_* 無し → H2 真因確定 → Step 4

Step 4: Investigation D (design, only if H1 or H2 confirmed)
  ├─ EGL_ANGLE_* で代替可 → Investigation E
  ├─ screen_* bridge 必要 → Investigation F
  └─ どちらも困難 → ANGLE-mode disable option
```

### 各 Step の前提コスト

| Step | wall time | risk |
|---|---|---|
| A | 5 min | なし (read-only) |
| B | 5-10 min | 低 (smoke 1 run, rebuild 不要) |
| C | 30-60 min | 中 (rebuild 失敗時トラブルシュート必要) |
| D | 数時間〜数日 | 高 (design + 実装 + テスト) |

---

## 5. Stop conditions & decision matrix（集約）

| 調査結果 | 確定仮説 | 次の手 |
|---|---|---|
| A: system libEGL に Mesa symbol 無し | H1 真因 | D |
| A: ANGLE libEGL に Mesa symbol 無し（両方無し） | H1 + H2 真因 | D |
| A: ANGLE のみ Mesa symbol 有り | H2 真因候補 | C |
| B: ANGLE libEGL 先頭 | H4(a) 否定 | C |
| B: system libEGL 先頭 | H4(a) 真因、patch 検証必要 | C（patch 評価後） |
| C: ANGLE ext string に MESA_* 有り + proc addr 非 null | H1-H3 否定、別問題 | qnx_render_producer.cc 詳細 logging |
| C: ANGLE ext string に MESA_* 有り + proc addr null | H3 亜種 | D（ANGLE 設計制約回避） |
| C: ANGLE ext string に MESA_* 無し | H2 真因 | D（ANGLE-native path） |

---

## 6. Pre-flight checks（全 Step 開始前に実行）

```bash
# Disk space
df -h /home/yuta /tmp /home/yuta/chromium/src/out

# Existing build artifacts
ls -la /home/yuta/chromium/src/out/qnx_release/cefsimple \
       /home/yuta/chromium/src/out/qnx_release/libGLESv2.so \
       /home/yuta/chromium/src/out/qnx_release/libEGL.so \
       /home/yuta/chromium/src/out/qnx_release/qnx_env.sh

# Chromium tree state
cd /home/yuta/chromium/src && git status -s | wc -l   # Expect ~1065 (bootstrap 由来)

# Toolchain
which q++ && q++ --version 2>&1 | head -3

# QNX system libs（QEMU guest 上に import される library の host 配置）
find /home/yuta/qnx800/target/qnx/x86_64 -name 'libEGL.so*' -o -name 'libGLES*' 2>&1 | head -10

# Previous smoke logs（reference 用）
ls /tmp/qnx-smoke-2026-07-12/*.log 2>&1 | head -10

# Pre-existing 障害（fresh bootstrap のみ影響）
cd /home/yuta/chromium/src/cef && grep -l 'DIR_MODULE' patch/patches/linux_assets_path_1936.patch
```

---

## 7. Risks & constraints

### 7.1 Build 関連

- chromium tree は bootstrap 由来の 1065 files modified が normal state。rebuild はこの状態を前提とする。
- `linux_assets_path_1936.patch` の 2/3 hunk が upstream で obsolete → **新規 bootstrap は失敗**する。既存 `out/qnx_release/` をそのまま使うか、ninja 単体で incremental build のみ可能。
- rebuild 対象を `ui/ozone/platform/qnx` と `cefsimple` のみに限定すれば 10-20 分で済む想定。

### 7.2 Smoke 関連

- 1 run 30-90s。bash tool の timeout と qnx_run.sh cleanup phase の race があるため、過去 turn で hang した事例あり。`LD_DEBUG` で出力量が増えると同様の race が発生する可能性あり、必要に応じて `timeout 110` を `bash` の外側に追加。
- NFS write が増えるため `/export/chromium-src` の capacity を `df -h` で確認。

### 7.3 LD_DEBUG の副作用

- `LD_DEBUG=libs` は全 library 解決を log するため、出力が数十〜数百 MB に達する場合がある。disk 容量と性能影響に注意。
- `LD_DEBUG_OUTPUT` path は QEMU guest 上で writable な場所。NFS mount (`/mnt/nfs`) または tmpfs (`/tmp`) を使用。

---

## 8. References

### 8.1 Source files

- `ui/ozone/platform/qnx/qnx_render_producer.cc` (lines 195-203, 260-261, 277-278, 289-298)
- `ui/ozone/common/egl_util.cc` (新 QNX patch 適用済、`gl::AddGLNativeLibrary` 順序変更)
- `ui/gl/gl_implementation.cc` (line 417-426 `AddGLNativeLibrary`)
- `third_party/angle/src/libANGLE/display/` (ANGLE の EGL display 実装、chromium tree 上の full path 要確認)

### 8.2 Commits

- `debf52a63` ANGLE recursive GlobalMutex fix（Phase B）
- `f329965f2` Documentation (17 md) + new QNX patch `ui_ozone_egl_gles_library_precedence_qnx`
- `fd5d3a98a` throw-tracer audit-trail 5 files

### 8.3 既存 docs

- `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/PHASE-B.md` § Residual problem
- `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/RESULT.md` § REENTRY event
- `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/BASELINE.md` exit139 baseline
- `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/WAITSTATUS-RESULT.md` wait status decode

### 8.4 過去 smoke log（reference）

- `/tmp/qnx-smoke-2026-07-12/*.log`（7 runs + 5 hung/untracked runs、計 12 ファイル）
- `/tmp/qnx-egl-provider-order-build-final.log` (14:15 時点の ccache エラー記録)

### 8.5 Pre-existing 障害（本調査範囲外）

- `patch/patches/linux_assets_path_1936.patch` の 2/3 hunk obsolete（fresh bootstrap のみ影響）

---

## 9. Expected output of this investigation

完了時に作成する成果物:

1. **`docs/qnx/history/research/qnx-angle-dmabuf-export-investigation-2026-07-12/RESULT.md`**:
   - 仮説ごとの真因確定状況
   - Investigation A/B/C/D 各 Step の結果
   - 採用された mitigation（コード fix があれば patch として、なければ設計文書）

2. **修正が必要な場合**:
   - `patch/patches/qnx/chromium/angle_qnx_screen_buffer_bridge.patch` 等（新規 CEF-managed patch）
   - `docs/qnx/history/research/qnx-angle-dmabuf-export-investigation-2026-07-12/BRIDGE-DESIGN.md`（design 記録）

3. **修正が不要な場合**（ANGLE-mode disable option 採用時）:
   - `docs/qnx/history/research/qnx-angle-dmabuf-export-investigation-2026-07-12/CONCLUSION.md` に `qnx_7727` での ANGLE-mode non-support を明文化

---

## 10. この PLAN 自体の不変条件

- 既存の committed file (`debf52a63`, `f329965f2`, `fd5d3a98a`) への変更禁止
- `out/qnx_release/` の build artifact に直接手を入れない（incremental rebuild のみ）
- `/tmp` 以外の CEF tree 内 path に smoke log を残さない
- Transient edit（Investigation C の logging 追加）は必ず revert、commit しない
- Investigation A → B → C → D の順序を基本とし、skip する場合は判断理由を result に明記
