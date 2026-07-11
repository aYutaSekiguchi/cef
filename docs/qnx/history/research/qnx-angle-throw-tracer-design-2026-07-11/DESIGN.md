# 案 1: ANGLE EGL abort 診断用 LD_PRELOAD shared library (DESIGN)

- 状態: **設計のみ。実装・ビルド・patch 作成は次 commit 以降 (監督承認後)。**
- 直前 commit `1151c1cdd` の 3 層 cross-TU 実験 (case 1-3 全 PASS) に基づく。
- 取得可能 / 不可を FACT ベースで明示し、過大表現を避ける。

## 1. 制約

| 制約 | 根拠 | 含意 |
|------|------|------|
| 診断専用、恒久実装ではない | 監督明示 | 切り戻し前提。`.so` は `out/qnx_release/` 配下で commit しない (既存 out/ gitignore で十分) |
| catch 後は **意図的異常終了** (「正常終了」とは呼ばない) | RAII 漏れ前提 | POSIX async-signal-safe termination API で即時終了 (atexit / static dtor 走らせない) |
| **throw-site stack は catch では取れない** (case 3 で確認) | post-unwind 1-2 frame のみ | 過大表現禁止 |
| throw-site stack は **throw 時** なら取れ得る (仮説、未検証) | `__cxa_throw` interpose + `_Unwind_Backtrace` | G1-G4 gate 通過が前提 |
| -fexceptions を 1 TU だけに限定 | 監督指示 | ANGLE 本体無改変、独立 shared_library target |
| RTTI 不要 | 監督明示 | `catch (const std::system_error&)` で固定 |
| ANGLE 内部の RAII が catch site より手前で leak | `ScopedGlobalEGLMutexLock` が -fno-exceptions frame | catch から正常 return 不可 |

## 2. アーキテクチャ

```
[chromium GPU child]
   |
   |  LD_PRELOAD=libangle_throw_tracer.so
   v
+-------------------------------------------+
|  libangle_throw_tracer.so                |
|  (1 TU だけ -fexceptions)                 |
|                                          |
|  1) __attribute__((constructor))          |
|     → dlsym で EGL 入口と libc++abi の     |
|       __cxa_throw を事前解決 (仮説)        |
|  2) EGL 入口 4 種を interpose             |
|     → TLS call-stack push/pop              |
|     → try { real_fn(); }                 |
|        catch (const std::system_error& e) |
|          { raw write; ::_exit(1); }       |
|  3) __cxa_throw interpose (仮説)         |
|     → TLS recursion guard                  |
|     → raw return address + TLS call 名    |
|       (symbolize は host 側)               |
+-------------------------------------------+
   |
   v
[ANGLE libEGL.so (-fno-exceptions) 無改変]
[GlobalMutex::lock() → std::system_error throw]
```

## 3. ABI 仕様 (FACT と未確定に分離)

### 3.1 EGL 入口 — 公式 header 由来

ANGLE 同梱の `third_party/angle/include/EGL/egl.h` / `eglext.h` / `eglplatform.h` を
include し、自前 typedef しない。`EGLDisplay` / `EGLBoolean` / `EGLenum` / `EGLint`
/ `EGLAttrib` は公式 header の typedef をそのまま使う。

| 関数 | 入口ヘッダ | attrib_list 型 |
|------|------------|----------------|
| `eglGetPlatformDisplay` | `<EGL/egl.h>` | `const EGLAttrib *` |
| `eglGetPlatformDisplayEXT` | `<EGL/eglext.h>` | `const EGLint *` |
| `eglInitialize` | `<EGL/egl.h>` | n/a |
| `eglTerminate` | `<EGL/egl.h>` | n/a |

両 attribute 型の混在は EGL 仕様上の歴史的経緯。wrapper は別個の C 関数として
定義 (C には overload なし)。

### 3.2 `__cxa_throw` — Itanium C++ ABI (signature は G2 gate で確定)

- 公開されている ABI の signature は大まかに
  `extern "C" void __cxa_throw(void*, std::type_info*, void(*)(void*))` だが、
  **`noexcept` / `[[noreturn]]` / visibility** の付与は実装依存:
  - libsupc++ (GCC) と libc++abi (LLVM) で差あり
  - QNX 8.0 SDP の libc++abi (clang 12.2.0) では `<cxxabi.h>` 宣言に
    `__attribute__((__noreturn__))` 相当が付いているはずだが未確認
- 実装着手時に G2 gate (readelf + `<cxxabi.h>` 確認) で **確定 signature を
  得る**。確定までは wrapper 側 signature は `noexcept` を **付けず**、
  関数 attribute も付けない最小宣言に留め、G2 で確定した宣言に揃える。
- `destructor` は正常 unwinding 失敗時に呼ばれる cleanup 関数。NULL 可。
- 戻り値 `void`。hook 内で値を返さない (例外を伝搬)。

## 4. 再入対策と __cxa_throw hook の安全性 (HYPOTHESIS、要 gate 検証)

### 4.1 解決済みポインタ (constructor 解決は **仮説**)

| 対象 | 解決方法 | 解決失敗時 |
|------|----------|-----------|
| EGL 4 種 | `dlsym(RTLD_NEXT, "eglGetPlatformDisplay")` 等 | `::_exit(1)` (raw 1-byte write で stderr へ 1 行) |
| `__cxa_throw` (libc++abi 内) | `dlsym(RTLD_NEXT, "__cxa_throw")` | `::_exit(1)` |

**constructor での dlsym が「安全」とする断言は削除する**。以下が未検証:
- constructor の実行順 (動的ローダが複数の constructor をどう順序付けるか)
- constructor 実行前の throw (静的 init 段階の throw は未定義動作)
- dlsym 内部の lock / allocation (libc / libdl の lock 取得順)

G2-G4 gate で実測して初めて「constructor 解決が安全」と結論できる。**G2-G4 が
fail した場合、案 1 の `__cxa_throw` interpose 部分は放棄**し、EGL 入口
wrapper のみに縮退する — **これは方針転換にあたる**ので自動では行わず、
**必ず停止してユーザーに報告**する (DESIGN §9 末尾参照)。

`std::call_once` を使わない理由は維持: hook 内で `std::call_once` の初回
呼び出しが C++ runtime 初期化に依存し、hook 自体の throw で再帰する危険が
ある。constructor での静的初期化で解決する設計意図は同じ。

### 4.2 __cxa_throw hook 内の極小化

- **allocation 禁止**: `std::string` / `std::vector` / `new` / `malloc` 全部禁止。
- **lock 禁止**: mutex / spinlock 全部禁止。
- **C++ runtime 呼び出し最小化**: `std::cerr` / `std::fprintf` (内部で locale
  取得あり) も避ける。**`write(2, ...)`** のみ (POSIX raw I/O)。
- **TLS recursion guard**:

```cpp
static thread_local int tls_in_cxa_throw_depth = 0;
if (tls_in_cxa_throw_depth > 0) {
    s_real___cxa_throw(thrown_object, typeinfo, destructor);
    __builtin_unreachable();
}
++tls_in_cxa_throw_depth;
... raw write でログ ...
--tls_in_cxa_throw_depth;
s_real___cxa_throw(thrown_object, typeinfo, destructor);
```

### 4.3 TLS EGL call stack (再帰 safe)

`tls_egl_call_depth` + 固定長 `tls_egl_call_stack[8]` (depth と同名) で
再帰 safe。`tls_egl_call_stack[depth++]` で push、`--depth` で pop。
`depth == 8` は ASSERT / 早期 return (実用上 ANGLE 内で EGL 8 重は起きない)。

### 4.4 __cxa_throw hook の出力 — raw address のみ (第一版)

第一版の hook は:
- 投げた thread の **現在の return address** (`__builtin_return_address(0)` または
  register snapshot) を **raw な uintptr_t の列**として `write(2, ...)` で出す
- TLS から EGL call 名 (あれば) も同じく raw bytes として出す
- `typeinfo->name()` は **呼ばない** (これが C++ runtime に触れるかは未検証)
- `dladdr` / シンボル化は **hook 内では行わない** (C++ runtime / dynamic loader
  依存を最小化)

シンボル化 (関数名 / source line) は **host 側で post-mortem** に行う:
- hook の出力から raw address 群を抽出
- `addr2line` または `readelf --debug-syms` + `addr2line -e libEGL.so` で resolve
- これで QNX 上の binary に `dladdr` を呼ばずに同等情報を得られる

### 4.5 LD_PRELOAD 伝播と EGL filter

- LD_PRELOAD は **全 child process** (browser / GPU / renderer / utility) に
  継承される (Linux/QNX の ELF dynamic loader 仕様)。
- ただし EGL 入口を叩くのは **GPU child のみ**。browser / renderer は
  EGLDisplay を作らないので wrapper は無音で通過する。
- `__cxa_throw` は **どの thread からも** 呼ばれる。`tls_egl_call_depth == 0`
  のときは **EGL 起源ではない** ので **即 forward (log なし)**。
- EGL 起源の throw のみを log 対象とする (filter)。
- **GPU child への到達検証 (G3 にも関連)** は smoke で:
  ```
  [GPU child 内で eglGetPlatformDisplay 呼び出し前に EGL 起源フラグが立つか]
  [browser / renderer で __cxa_throw が起きても log が出ないこと]
  ```

## 5. catch 経路 — POSIX async-signal-safe termination API

```cpp
try {
    result = s_real_eglGetPlatformDisplay(platform, native_display,
                                          attrib_list);
} catch (const std::system_error &e) {
    write(2, "[ANGLE-THROW-TRACER] call=eglGetPlatformDisplay code=", 49);
    write_num(2, (int)e.code().value());
    write(2, " what=\"", 7);
    write(2, e.what(), strlen(e.what()));
    write(2, "\"\n", 2);
    ::_exit(1);
}
```

- `::_exit(2)` は **POSIX async-signal-safe termination API** であり raw
  syscall ではない (POSIX 標準関数)。
- `atexit` / static dtor / signal handler cleanup を **完全にバイパス** する。
- `std::_Exit` ではなく `::_exit` を使う理由: `std::_Exit` は C++ 標準
  library 関数で実装依存 (`atexit` を経由する可能性あり)。`::_exit`
  の方が spec 上の保証が明確。
- exit code 1 = abnormal termination の慣習。EGL の EGL_BAD_DISPLAY (5)
  などとは別物。
- mutex leak した process を 1 で終わらせる以上、最も厳格な終了が必要。

## 6. 取得可能 / 不可 (FACT ベース)

| 項目 | 取得経路 | 可否 |
|------|----------|------|
| どの EGL call がトリガか | TLS stack | **可** |
| `e.code().value()` | catch で `.code().value()` | **可** (実測 45) |
| `e.what()` | catch で `.what()` | **可** (実測 "mutex lock failed: Resource deadlock avoided") |
| throw **時** の call stack | `__cxa_throw` hook + `__builtin_return_address` (raw) | **仮説: 可 (要 gate)** |
| 関数名 / source line | host 側で addr2line (QNX 上の binary に) | post-mortem、in-hook ではない |
| catch site の call stack | catch 内で `_Unwind_Backtrace` | 部分可 (1-2 frame のみ、post-unwind) |
| B frame 内部の RAII 状態 | 取得不能 | **不可** |
| mutex leak 状態そのもの | 取得不能 (即時 exit するため) | **不可** |

過大表現禁止: throw-site stack は **未検証 HYPOTHESIS** であり、gate を
通るまで「取れるはず」とだけ記載する。**in-hook でのシンボル化は第一版では
行わない** (C++ runtime 依存最小化のため)。

## 7. GN 構造 — 独立 target

```gn
# in third_party/angle/BUILD.gn (1 hunk で追加)
config("angle_throw_tracer_exceptions") {
  if (is_qnx) {
    cflags_cxx = [ "-fexceptions" ]
  }
}

shared_library("libangle_throw_tracer") {
  if (is_qnx) {
    sources = [ "src/libangle_throw_tracer.cc" ]
    configs -= [ "//build/config/compiler:no_exceptions" ]
    configs += [ ":angle_throw_tracer_exceptions" ]
    cflags_cc += [ "-fvisibility=default" ]
    output_name = "libangle_throw_tracer"
    deps = []
  }
}
```

- `static_library` ではなく `shared_library` (LD_PRELOAD は動的 library のみ)。
- `cflags_cc += "-fvisibility=default"` で interpose シンボルを default 可視化。
  `angle_gl_visibility_config` は **継承しない** (GL 専用 visibility 設定)。
- `output_name = "libangle_throw_tracer"` だが、ANGLE の shared_library
  template (`angle_shared_library` from `BUILD.gn`) が **自動的に "lib" 接頭辞を
  付ける可能性がある** → 出力ファイル名が `liblibangle_throw_tracer.so` に
  なるリスクあり。**G8 gate で実出力を確認**する。

### 自動 build 問題

- chromium の default all-target (`ninja -C out/qnx_release` または
  `out/qnx_release/ninja_qnx.sh`) は `libangle_throw_tracer` を **自動的に build
  しない** (依存がないため)。
- `.so` は **opt-in** で build する。bootstrap には追加しない。
- 起動方法 (実装後):
  ```
  $ cd /home/yuta/chromium/src
  $ ./out/qnx_release/ninja_qnx.sh -C out/qnx_release libangle_throw_tracer
  ```
  (G6 gate で `ninja -t commands` の実出力を確認)
- 起動時の build にも含めたければ `group("all_diagnostics")` を作り
  bootstrap から依存させる (推奨しない: opt-in 維持)。

## 8. CEF-managed layout

```
cef/patch/patches/qnx/chromium/angle_qnx_throw_tracer.patch
    → third_party/angle/BUILD.gn に config + shared_library target を追加 (1-2 hunk)
cef/patch/qnx/chromium/new_files/third_party/angle/src/libangle_throw_tracer.cc
    → wrapper ソース
cef/patch/patch.cfg
    → angle_qnx_throw_tracer エントリ追加 (angle_qnx_terminate_capture の後)
```

### commit 計画 (実装許可後)

```
[1] angle_qnx_throw_tracer.patch
[2] new_files/.../libangle_throw_tracer.cc
[3] patch.cfg 更新
[4] docs/.../qnx-angle-throw-tracer-impl-.../RESULT.md (smoke 結果)
```

## 9. 実装前 gate (readelf / source inspection ベース、未着手だが事前計画)

実装に取りかかる前に、以下の gate を **全て** pass すること。1 つでも fail なら
**停止してユーザーに報告**する (auto-fallback しない)。

| # | 確認項目 | コマンド例 |
|---|----------|-----------|
| G1 | QNX libc++abi の `__cxa_throw` が default visibility global | `qnx-readelf -Ws $QNX_TARGET/lib/libc++.so.2 \| grep __cxa_throw` |
| G2 | QNX libc++abi の `__cxa_throw` 正確な signature + visibility + noreturn 付与 | `qnx-readelf --dyn-syms $QNX_TARGET/lib/libc++.so.2 \| grep __cxa_throw` + `qnx-readelf -d $QNX_TARGET/lib/libc++.so.2 \| grep -E 'FLAGS\|SYMBOLIC\|BIND_NOW'` + `<cxxabi.h>` 確認。**version script / Bsymbolic で local-only にされていない**こと |
| G3 | mini `__cxa_throw` interpose .so (10 行) を QEMU で LD_PRELOAD し、throw 1 回で stderr に出る + 正常 throw 2 回でも reentrancy しない | smoke `sh -c 'LD_PRELOAD=./mini_throw.so ./thrower; echo $?'` |
| G4 | mini EGL interpose .so (10 行) を QEMU で LD_PRELOAD し、`eglGetPlatformDisplay` が hook を経由 + 解決失敗時に `::_exit(1)` | smoke + readelf |
| G5 | wrapper の `__cxa_throw` hook 本体内に **`dlsym` / `std::call_once` / `fprintf` / `std::*` / `new` / `malloc` / `lock`** のいずれも含まれないことを **source inspection** で確認 + `objdump -d libangle_throw_tracer.so \| grep -A 30 '<__cxa_throw>:'` で disassembly が `write` / `::_exit` / `__tls_get_addr` のみであることを確認。`readelf` 単独では証明不可、**source + objdump の併記が必要**。 |
| G6 | `ninja -t commands libangle_throw_tracer` の実コンパイルコマンドに `-fexceptions -fno-rtti` が **この順で** 含まれる。**`./out/qnx_release/ninja_qnx.sh -t commands ...` を使う** (素の `ninja -C` ではない) | `./out/qnx_release/ninja_qnx.sh -C out/qnx_release -t commands libangle_throw_tracer` |
| G7 | 出力 `libangle_throw_tracer.so` の NEEDED に **ANGLE 由来 DSOs (libEGL, libGLESv2, libangle_common 等) を含まない**こと、また libc++abi / libc++ / libgcc_s / libc / libdl 等の **循環依存がない**こと。**libgcc/libc/libdl のみと限定しない** — catch / system_error 経路で libc++ 等が必要になり得るため | `qnx-readelf -d out/qnx_release/libangle_throw_tracer.so` |
| G8 | 出力ファイル名が **`libangle_throw_tracer.so`** (liblib でない) であることを確認。ANGLE の shared_library template が `lib` 接頭辞を付けると `liblib*` になるリスク | `ls out/qnx_release/libangle_throw_tracer.so` + `qnx-readelf -Ws` の SONAME 確認 |
| G9 | exported symbol `eglGetPlatformDisplay` / `eglInitialize` / `eglTerminate` / `__cxa_throw` が **default visibility** で他 DSOs から見えている | `qnx-readelf --dyn-syms libangle_throw_tracer.so \| grep -E 'egl\|cxa_throw'` |

**G1-G4 fail 時の対応**: `__cxa_throw` interpose 部分を放棄し、EGL 入口 wrapper
のみに縮退する案を提示して **停止**。**auto で SIGABRT handler 案に
切替えない** (方針転換のため)。**ユーザー承認後にのみ次へ進む**。

## 10. smoke 成功条件 (gate 通過後)

### 10.1 期待出力 (ANGLE EGL が throw した場合)

```
[ANGLE-THROW-TRACER] __cxa_throw (raw) 0x... 0x... 0x...
[ANGLE-THROW-TRACER]   egl-context=eglGetPlatformDisplay
[ANGLE-THROW-TRACER] catch in eglGetPlatformDisplay:
[ANGLE-THROW-TRACER]   e.code()=45
[ANGLE-THROW-TRACER]   e.what()=mutex lock failed: Resource deadlock avoided
$ echo $?
1
```

ホスト側で `0x...` の raw address 群を `addr2line -e libEGL.so` で resolve
することで、関数名 / source line を後追いで取得。

### 10.2 pass 条件

- 上記 5 行が stderr に出る。
- throw-site raw address 群に **2 frame 以上** (G1-G4 gate 通過前提)。
- プロセスが exit code 1 で終了 (0 ではない)。
- EGL 起源でない throw は stderr に何も出ない (filter 確認)。

## 11. rollback (CEF 通常の patch ライフサイクル)

- `cef/patch/patch.cfg` から `angle_qnx_throw_tracer` エントリを削除。
- `cef/patch/patches/qnx/chromium/angle_qnx_throw_tracer.patch` を削除。
- `cef/patch/qnx/chromium/new_files/third_party/angle/src/libangle_throw_tracer.cc` を削除。
- `bootstrap` (cef_create_projects_qnx.sh) 再実行で ANGLE 側に target なしを確認。
- `./out/qnx_release/ninja_qnx.sh -C out/qnx_release` で通常 build 結果に差分がないことを確認。
- chromium 側 `git diff HEAD` で **CEF 関連以外の差分 0** を確認。
- `out/qnx_release/libangle_throw_tracer.so` を `rm` (再 build されない限り再生成されない)。

git の破壊的操作 (`git reset HEAD~1`, `git rm --cached` 等) は使用しない。
CEF repo 内のファイル削除 + commit で完結する。

## 12. 監督承認待ち項目

1. 本 DESIGN.md 修正版 (313 行) のレビュー
2. G1-G4 mini smoke 着手可否
3. 取得可能 / 不可の線引き (本 DESIGN §6、特に in-hook シンボル化非実施)
4. `::_exit(1)` を「意図的異常終了」と呼ぶこと
5. G1-G4 fail 時に **SIGABRT handler 案に auto-fallback しない** こと
6. 方針転換の指示があればそれに従う

---

**注**: 本 commit は DESIGN.md のみ更新。**実装・ビルド・patch 作成・bootstrap
再実行は監督承認後に次 commit 以降で着手。** G1-G4 gate は **未検証仮説** であり、
pass しなければ `__cxa_throw` interpose を捨てて EGL wrapper のみに縮退する案を
提示し、**停止**する (auto-fallback なし)。
</content>
</invoke>
