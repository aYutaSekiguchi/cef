# QNX: Read Anything TFLite build flag

## Stage

- stage: compile
- category: build-graph / dependency-config
- target: `//chrome/renderer:renderer`
- files:
  - `components/optimization_guide/features.gni`
  - `chrome/renderer/accessibility/read_anything/read_aloud_app_model.*`
  - `chrome/renderer/accessibility/phrase_segmentation/dependency_parser_model.h`

## Failure signature

```text
FAILED: obj/chrome/renderer/renderer/read_aloud_app_model.o
In file included from ../../chrome/renderer/accessibility/read_anything/read_aloud_app_model.cc:5:
In file included from ../../chrome/renderer/accessibility/read_anything/read_aloud_app_model.h:11:
In file included from ../../chrome/renderer/accessibility/phrase_segmentation/dependency_parser_model.h:15:
../../third_party/tflite/src/tensorflow/lite/core/interpreter.h:43:10: fatal error: 'tensorflow/compiler/mlir/lite/allocation.h' file not found
   43 | #include "tensorflow/compiler/mlir/lite/allocation.h"
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

The same include chain also broke:

- `read_anything_app_controller.o`
- `chrome_render_frame_observer.o`

## Root cause

`chrome/renderer/BUILD.gn` always builds the Read Aloud model for non-Android renderer builds. Its headers include phrase segmentation code, and `dependency_parser_model.h` includes TFLite's `interpreter.h`.

The phrase segmentation source/dependency block is gated on `build_with_tflite_lib`. QNX was not included in `components/optimization_guide/features.gni`, so the renderer target did not receive the TFLite deps or public include config. The header existed on disk at:

```text
third_party/tflite/src/tensorflow/compiler/mlir/lite/allocation.h
```

but the compile command lacked the `third_party/tflite/src` include path.

## Fix

Patch: `qnx/chromium/optimization_guide_tflite_enable_qnx`

Add `is_qnx` to `build_with_tflite_lib` in `components/optimization_guide/features.gni`. QNX already has `qnx/chromium/tflite_features_qnx`, which keeps XNNPACK disabled because QNX lacks the pthreadpool implementation expected by that delegate. Enabling the main TFLite build flag lets the renderer receive the TFLite sources, deps, and public include config while preserving the QNX-specific XNNPACK disable.

## Verification

After `gn gen`, the narrow object builds passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
gn gen .
ninja -C . \
  obj/chrome/renderer/renderer/read_aloud_app_model.o \
  obj/chrome/renderer/renderer/read_anything_app_controller.o \
  obj/chrome/renderer/renderer/chrome_render_frame_observer.o
```

Result:

```text
[102/102] CXX obj/chrome/renderer/renderer/read_aloud_app_model.o
[100/101] CXX obj/chrome/renderer/renderer/read_anything_app_controller.o
[101/101] CXX obj/chrome/renderer/renderer/chrome_render_frame_observer.o
```

## Search hints

```bash
rg -n "tensorflow/compiler/mlir/lite/allocation.h|read_aloud_app_model|dependency_parser_model|build_with_tflite_lib" /tmp/*.log docs/qnx/history/build-errors
```
