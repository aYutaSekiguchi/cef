# QNX cross-TU exception unwinding 実験結果 (2026-07-11)

- 目的: ANGLE が QNX runtime で `-fno-exceptions` の libangle_common を持つ状況で、
  `std::system_error` (EDEADLK) を投げる libEGL 入口を、外側に置いた `-fexceptions` ラッパで
  catch できるかを **QNX runtime で** 確定する。
- 結論 (FACT): **case 1〜3 全 PASS。unwrap は catch まで到達する。**
- ただし **「catch まで到達可能」≠「catch から正常復帰可能」** を分離して記録。

## 1. 結果サマリ

| Case | 経路 | catch 到達 | dtor 観測 | 備考 |
|------|------|------------|------------|------|
| 1 | C → A 直接 | YES | n/a | 最も単純 |
| 2 | C → BSimple → A | YES | n/a | B は simple frame (local なし) |
| 3 | C → BWithDtors → A | YES | **NO** | B の dtor (t1, t2) は走らず catch 直行 |

### 観測 raw (case 3)

```
[main] === case 3 start ===
[ctor] main-init-lock
[main] g_test_mutex locked, this thread holds it
[dtor] main-init-lock
[C] case-3: CCatchThroughBWithDtors entered
[ctor] BWithDtors-entry
[ctor] BWithDtors-before-call
[C] case-3: CAUGHT std::system_error code=45 what="mutex lock failed: Resource deadlock avoided"
[main] === case 3 end ===
```

BWithDtors 内 local の `DtorTracer t1`, `DtorTracer t2` (順不同でも) の `[dtor]`
ログが **catch までに一度も出現しない**。`std::string s` / `std::vector<int> v` の
non-trivial dtor も呼ばれず抜けている (実装上、catch まで戻った後に main スコープに
入る時に destructor 呼び出しが動くが、本 test では catch 内で即 return しているため観測されない)。
ANGLE entry point の stack に存在する `ScopedGlobalEGLMutexLock` 等の RAII と同じ
パターンが **走らない** ことを意味する。

## 2. なぜ case 3 で dtor が走らないか (FACT)

- `layer_b_simple.o` / `layer_b_dtors.o` を `__gxx_personality_v0` 参照で確認 → **0 件**。
  - `-fno-exceptions` TU は C++ 例外 personality を出力しない。
- 一方 `.eh_frame` (DWARF CFI) は両者にある → スタック walker は C レベルの unwind はできる。
- catch site (Layer C) の `.gcc_except_table` には B frame に対する landing pad がある。
- しかし B frame が C++ RAII を持つ場合、cleanup は C++ personality が見つけるため、
  B frame には personality がない → 結果として RAII dtor が呼ばれない。
- これが Itanium C++ ABI §15.2 (Throwable Exceptions, Cleanup) の `-fno-exceptions`
  TU における「ABI 仕様上想定外」挙動。

## 3. 案 1 (ANGLE diagnostic) への含意

- 案 1 で catch できる対象は `e.code()` と `e.what()` のみ。
- 案 1 から正常 return すると、ANGLE entry point 内部で `e.lock()` を取ったまま
  `ScopedGlobalEGLMutexLock` (RAII on stack) が unlock されない。**mutex leaked 状態で
  後続の EGL call が deadlock するか、ANGLE 内部状態が partial commit となる**。
- よって catch 後は **必ず即時プロセス終了** せねばならない。
- throw-site stack は catch 時に取得できない (post-unwind で B frame の saved register
  が破棄済み)。throw-site を取りたいなら `__cxa_throw` 自体を interpose して throw **時**
  に取る必要がある (production patch で別途設計)。

## 4. 診断取得可能範囲 (過大表現しない)

取得できるもの (FACT):
- call 名 (wrapper 側で call site を記録するため)
- `e.code().value()` (実測 case 1-3 で 45 = EDEADLK)
- `e.what()` (実測で "mutex lock failed: Resource deadlock avoided")
- catch 時点の wrapper 周辺の backtrace (post-unwind 1-2 frame 程度)

取得できないもの (FACT):
- **throw-site stack** (catch 時には B frame の saved register が破棄済)
- B frame 内部の RAII cleanup 状態
- 投げたスレッドの lock 取得前の call stack (上記と同根)

## 5. 観測した raw log 一覧

- `logs/case1.log` — 直 A 呼び出し, catch 成功
- `logs/case2.log` — 単純 -fno-exceptions B 経由, catch 成功
- `logs/case3.log` — non-trivial local 持ち -fno-exceptions B 経由, catch 成功だが dtor 走らず
- `logs/binary_metadata.txt` — 実行バイナリの NEEDED, sections, .eh_frame/.gcc_except_table
- `build.md` — コンパイル / リンク full command と per-TU フラグ

## 6. 結論から production patch への制約

1. **catch で code / what を取ったら即時 `std::_Exit(1)`** (RAII 漏れを許容しない)
2. **throw-site stack** は catch では取れない。production patch は `__cxa_throw` フックで
   throw **時** に取る方向 (Layer A の中で throw する瞬間を hook して `_Unwind_Backtrace` を
   呼ぶ) と組み合わせる。
3. **ANGLE source への侵襲は最小** に留める: ANGLE libangle_common / libANGLE を直接編集
   する代わりに、`LD_PRELOAD` で先にロードされる **独立 shared library** を間に挟む設計が安全。
   ANGLE 内部の catch 経路を増やす必要がない (ANGLE ソースは無改変)。
4. RTTI 不要 (catch 対象を `const std::system_error&` に固定、`typeid` / `dynamic_cast` 不使用)。
   既存 chromium source の -fno-rtti 既定と整合。
