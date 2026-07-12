# Investigation D — ANGLE-native buffer bridge design (design only)

Date: 2026-07-12  
Scope: PLAN §3.4 only (no rebuild, no code edit except this doc)

## 1. Inputs and constraints

- A結果で `system libEGL` / `ANGLE libEGL` ともに `EGL_MESA_*` symbol が 0。  
  → PLAN §5 の判定どおり **H1 + H2 真因確定**。
- このフェーズは設計のみ。実装・commit は行わない。
- 目標は、`qnx_render_producer.cc` の Mesa 依存 export path を、ANGLE 実装前提で成立する代替 path に置換可能な設計に落とすこと。

## 2. Investigation D inventory

## 2.1 ANGLE extension inventory（`EGL_ANGLE_*` / `egl_exts` 経路）

### 2.1.1 `egl_exts` がどう作られるか

`qnx_render_producer.cc` の `egl_exts` は `eglQueryString(..., EGL_EXTENSIONS)` で取得:

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.cc:236-258` (`ProbeExtensions`)

ANGLE 側の生成経路は以下:

- `third_party/angle/src/libGLESv2/egl_stubs.cpp:573` `QueryString(...)`
  - `:590` `Display::GetClientExtensionString()`
  - `:594` `display->getExtensionString()`
- `third_party/angle/src/libANGLE/Display.cpp:2305` `Display::initDisplayExtensions()`
  - `:2307` `mDisplayExtensions = mImplementation->getExtensions();`
  - `:2340` `mDisplayExtensionString = GenerateExtensionsString(mDisplayExtensions);`
- `third_party/angle/src/libANGLE/Display.cpp:2280` `GenerateExtensionsString(...)`
- `third_party/angle/src/libANGLE/Caps.cpp:1288` `DisplayExtensions::getStrings()`
  - `InsertExtensionString(...)` で extension 文字列を構築

### 2.1.2 `EGL_ANGLE_*` inventory（Caps.cppベース）

`DisplayExtensions::getStrings()` / `DeviceExtensions::getStrings()` / `ClientExtensions::getStrings()` 内で  
`InsertExtensionString("EGL_ANGLE_...")` は **65件**（`Caps.cpp:1295-1453`）。

buffer sharing 観点で重要なもの:

- `EGL_ANGLE_stream_producer_d3d_texture` (`Caps.cpp:1330`, `Caps.h:537`)
- `EGL_ANGLE_d3d_share_handle_client_buffer` (`Caps.cpp:1295`)
- `EGL_ANGLE_d3d_texture_client_buffer` (`Caps.cpp:1296`)
- `EGL_ANGLE_surface_d3d_texture_2d_share_handle` (`Caps.cpp:1297`)
- `EGL_ANGLE_image_d3d11_texture` (`Caps.cpp:1356`)
- `EGL_ANGLE_iosurface_client_buffer` (`Caps.cpp:1343`)
- `EGL_ANGLE_metal_texture_client_buffer` (`Caps.cpp:1344`)
- `EGL_ANGLE_vulkan_image` (`Caps.cpp:1376`)
- `EGL_ANGLE_webgpu_texture_client_buffer` (`Caps.cpp:1384`)
- `EGL_ANGLE_external_context_and_surface` (`Caps.cpp:1369`)

補助的に参照される non-ANGLE 拡張:

- `EGL_EXT_image_dma_buf_import` (`Caps.cpp:1364`, `Caps.h:675`)
- `EGL_EXT_image_dma_buf_import_modifiers` (`Caps.cpp:1365`, `Caps.h:678`)
- `EGL_ANDROID_image_native_buffer` (`Caps.cpp:1349`, `Caps.h:597`)

### 2.1.3 `GL_OES_EGL_image_external` / stream API の証跡

- `third_party/angle/include/GLES2/gl2ext.h:243` `glEGLImageTargetTexture2DOES`
- `third_party/angle/include/GLES2/gl2ext.h:248-257`  
  `GL_OES_EGL_image_external` / `_essl3`
- `third_party/angle/src/libGLESv2/libGLESv2_autogen.cpp:5336`  
  `glEGLImageTargetTexture2DOES` 実体
- `third_party/angle/include/EGL/eglext_angle.h:175-184`  
  `EGL_ANGLE_stream_producer_d3d_texture` と
  `eglCreateStreamProducerD3DTextureANGLE` / `eglStreamPostD3DTextureANGLE`

### 2.1.4 重要結論（ANGLE inventory）

1. `EGL_ANGLE_stream_producer_d3d_texture` は **D3D専用**。QNX直接適用はできない。  
2. `GL_OES_EGL_image_external` は **consumer 側 texture bind 用**であり、producer 側 export API ではない。  
3. `EGL_ANGLE_stream`（generic）や `EGL_ANGLE_external_img_data` はこの ANGLE tree で確認できない。  
4. したがって、Mesa関数置換は **ANGLE generic export extension だけでは完結しない**。

## 2.2 QNX `screen_*` API inventory（`screen.h`）

参照: `/home/yuta/qnx800/target/qnx/usr/include/screen/screen.h`

### 2.2.1 主要 API（line number）

- Context:
  - `6297` `screen_create_context`
  - `6315` `screen_destroy_context`
- Pixmap / buffer:
  - `8463` `screen_create_pixmap`
  - `8486` `screen_create_pixmap_buffer`
  - `8505` `screen_destroy_pixmap`
  - `8529` `screen_destroy_pixmap_buffer`
  - `8646` `screen_get_pixmap_property_pv`
  - `8792` `screen_set_pixmap_property_pv`
- Buffer properties:
  - `6031` `screen_get_buffer_property_cv`
  - `6063` `screen_get_buffer_property_iv`
  - `6086` `screen_get_buffer_property_llv`
  - `6110` `screen_get_buffer_property_pv`
  - `6185` `screen_set_buffer_property_cv`
  - `6218` `screen_set_buffer_property_iv`
  - `6244` `screen_set_buffer_property_llv`
  - `6271` `screen_set_buffer_property_pv`
- Window/post:
  - `9717` `screen_create_window`
  - `9787` `screen_create_window_buffers`
  - `9882` `screen_destroy_window`
  - `10042` `screen_get_window_property_iv`
  - `10102` `screen_get_window_property_pv`
  - `10294` `screen_post_window`
  - `10388` `screen_dequeue_window_render_buffer`
  - `6130` `screen_enqueue_render_buffer`

注意:

- PLAN で言及されている `screen_get_buffer_propertylv()` は本 header では見つからず、  
  実在するのは `screen_get_buffer_property_llv()` (`6086`)。

### 2.2.2 主要 `SCREEN_PROPERTY_*`（line number）

- `657` `SCREEN_PROPERTY_BUFFER_COUNT`
- `672` `SCREEN_PROPERTY_BUFFER_SIZE`
- `820` `SCREEN_PROPERTY_EGL_HANDLE`
- `851` `SCREEN_PROPERTY_FORMAT`
- `1190` `SCREEN_PROPERTY_PHYSICALLY_CONTIGUOUS`
- `1217` `SCREEN_PROPERTY_POINTER`
- `1289` `SCREEN_PROPERTY_RENDER_BUFFERS`
- `1373` `SCREEN_PROPERTY_SIZE`
- `1436` `SCREEN_PROPERTY_STRIDE`
- `1531` `SCREEN_PROPERTY_USAGE`
- `1620` `SCREEN_PROPERTY_RENDER_BUFFER_COUNT`
- `2485` `SCREEN_PROPERTY_PIXMAP_COUNT`
- `2500` `SCREEN_PROPERTY_PIXMAPS`
- `2582` `SCREEN_PROPERTY_NATIVE_IMAGE`
- `3319` `SCREEN_PROPERTY_PIXMAP`
- `3402` `SCREEN_PROPERTY_BUFFERS`
- `3479` `SCREEN_PROPERTY_FD`

`SCREEN_USAGE_*`（用途指定）:

- `4031` `SCREEN_USAGE_DISPLAY`
- `4036` `SCREEN_USAGE_READ`
- `4041` `SCREEN_USAGE_WRITE`
- `4047` `SCREEN_USAGE_NATIVE`
- `4052` `SCREEN_USAGE_OPENGL_ES2`

## 3. 現行 Mesa path（置換対象）の具体箇所

対象: `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.cc`

- `178-209` `QnxRenderProducer::Initialize`
  - `eglCreateDRMImageMESA` / `eglExportDMABUFImageMESA` / `eglExportDMABUFImageQueryMESA` を解決
- `212-225` `ExtensionReport`
  - `EGL_MESA_*` 前提で report
- `228-233` `CanExportDmaBuf`
  - `EGL_MESA_drm_image` + `EGL_MESA_image_dma_buf_export` 必須
- `236-258` `ProbeExtensions`
  - `EGL_MESA_*` 依存で判定
- `275-319` `CreateDRMImage`
  - `eglCreateDRMImageMESA` 依存
- `343-383` `QueryDmaBufMetadata`
  - `eglExportDMABUFImageQueryMESA` 依存
- `385-447` `ExportDmaBufImage`
  - `eglExportDMABUFImageMESA` 依存
- `449-535` `CreateExportFrame`
  - 上記 Mesa path を通して `QnxDmaBufFrame` を構築

## 4. 置換設計（ANGLE-native + QNX screen bridge）

## 4.1 設計方針

1. **Mojo wire format (`QnxDmaBufFrame`) は当面維持**（影響最小化）。  
2. producer 側で Mesa API を使わず、QNX `screen_*` バッファ経由で fd/stride/offset を取得する。  
3. ANGLE拡張は capability 判定と texture/EGLImage 連携に使うが、export本体は screen 側を主軸にする。  

## 4.2 新しい export flow（案）

1. `Initialize` で path 選択:
   - 既存 Mesa path（将来互換のため残す）  
   - 新規 `kScreenBridge` path（QNX/ANGLE の主経路）
2. `kScreenBridge` 初期化:
   - `screen_create_context`  
   - `screen_create_pixmap` + サイズ/format/usage設定  
   - `screen_create_pixmap_buffer`
3. buffer metadata 取得:
   - `screen_get_buffer_property_iv/pv/llv` で
     `SCREEN_PROPERTY_FD` / `STRIDE` / `BUFFER_SIZE` / `FORMAT` / `NATIVE_IMAGE` / `EGL_HANDLE` を回収
4. rendering/bind:
   - `EGLImageKHR` を screen native handle から生成する path を追加
   - `glEGLImageTargetTexture2DOES` で bind して描画（`GL_OES_EGL_image_external` 利用）
5. frame生成:
   - `QnxDmaBufFrame` に fd/stride/offset/size/fourcc/modifier を設定し既存 SubmitFrame を流用

## 4.3 既存 consumer との整合

consumer 側 (`QnxFrameImporter`) は現状 `EGL_LINUX_DMA_BUF_EXT` import 前提:

- `qnx_frame_importer.h:45-46`, `:79-82`, `:137-149`
- `qnx_frame_importer.cc:642` `BuildDmaBufAttrs`
- `qnx_frame_importer.cc:705` `ImportDmaBufToTexture`
- `qnx_frame_importer.cc:718-723` `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)`

したがって `kScreenBridge` でも、producer が渡す fd が `EGL_LINUX_DMA_BUF_EXT` と整合することが条件。  
ここが成立しない場合は Investigation F（system EGL 経由設計）へ分岐。

## 5. 影響ファイル（設計上の変更点マップ）

| File | 変更予定箇所（line/function） | 設計変更内容 |
|---|---|---|
| `.../qnx_render_producer.h` | `131-174` (`CanExportDmaBuf`, `Initialize`, `CreateDRMImage`, `QueryDmaBufMetadata`, `ExportDmaBufImage`) | Mesa専用 API を path abstraction 化（例: `ExportPath` enum, `InitializeScreenBridge`, `CreateScreenBackedImage`, `ExportScreenBufferFrame`） |
| `.../qnx_render_producer.h` | `220-249` (Mesa function ptr/flags) | Mesa ptr に加えて screen context/pixmap/buffer ハンドルと screen property キャッシュを追加 |
| `.../qnx_render_producer.h` | `204-206` コメント | 「GPU producer は screen を使わない」前提を撤回し、新 bridge 前提に更新 |
| `.../qnx_render_producer.cc` | `178-209` `Initialize` | capability probe と path 選択ロジックに置換（Mesa hard fail を除去） |
| `.../qnx_render_producer.cc` | `212-258` (`ExtensionReport`, `CanExportDmaBuf`, `ProbeExtensions`) | ANGLE_ prefix と screen bridge readiness を report 可能に拡張 |
| `.../qnx_render_producer.cc` | `275-447` (Mesa export関数群) | `kMesa` / `kScreenBridge` 実装に分岐。`kScreenBridge` は screen_* metadata 回収中心 |
| `.../qnx_render_producer.cc` | `449-535` `CreateExportFrame` | `CreateDRMImage` 直呼びを廃止し、選択 path 経由で `QnxDmaBufFrame` 組み立て |
| `.../qnx_gpu_service.cc` | `154-160`, `377-410` | APIシグネチャ変更がなければ最小修正。diagnostic に path 種別を追加 |
| `.../qnx_frame_importer.{h,cc}` | `45-46`, `642-723`, `830+` | 初期設計では変更しない（fd互換維持前提）。互換不成立時のみ Investigation F で改修対象化 |
| `.../mojom/qnx_gpu.mojom` | `38-77` | 初期設計では変更しない。必要時のみ screen-native handle 用フィールド追加を検討 |

## 6. Decision mapping（PLAN §5 との整合）

- 本 inventory では、ANGLE 側に QNX で直接使える汎用 `EGL_ANGLE_stream` 系 export API は確認できず、  
  `stream_producer` は D3D 特化。  
- 従って D の実装候補は **screen_* bridge 中心** が妥当。  
- ただし producer fd が importer の `EGL_LINUX_DMA_BUF_EXT` と整合しない場合は、  
  PLAN の分岐どおり **Investigation F**（system EGL 経由）へ進む。

## 7. Investigation E 着手前チェック（実装ゲート）

1. `screen_*` buffer から得る `FD/FORMAT/STRIDE/SIZE` を `QnxDmaBufFrame` に落とせること。  
2. Browser importer (`EGL_LINUX_DMA_BUF_EXT`) で import 成立すること。  
3. `egl_exts` logging で `EGL_ANGLE_*` の有効集合を収集できること。  
4. Mesa path は fallback として保持し、QNX/ANGLE では `kScreenBridge` を優先すること。  

