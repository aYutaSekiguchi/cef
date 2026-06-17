# QNX: live_translate_controller_factory on-device translation guard

## Stage

- stage: compile
- category: build-graph / feature-guard
- target: `obj/chrome/browser/browser/live_translate_controller_factory.o`

## Failure signature

```text
In file included from ../../chrome/browser/accessibility/live_translate_controller_factory.cc:14:
../../components/live_caption/translation_dispatcher_on_device.h:15:10: fatal error: 'components/on_device_translation/public/mojom/translator.mojom.h' file not found
   15 | #include "components/on_device_translation/public/mojom/translator.mojom.h"
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

## Root cause

QNX builds have:

```cpp
#define BUILDFLAG_INTERNAL_ENABLE_ON_DEVICE_TRANSLATION() (0)
```

and `components/live_caption:live_translate` only includes
`translation_dispatcher_on_device.{cc,h}` and the on-device translation mojom
deps when `enable_on_device_translation` is true.

`chrome/browser/accessibility/live_translate_controller_factory.cc` still
included `translation_dispatcher_on_device.h` and on-device translation service
controller headers unconditionally, then unconditionally compiled the dispatcher
construction block. That exposed a generated mojom header that does not exist in
the QNX build graph.

## Fix

Keep Live Translate itself available, but guard only the on-device translation
path:

```cpp
#include "components/on_device_translation/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_ON_DEVICE_TRANSLATION)
#include "chrome/browser/on_device_translation/service_controller_manager_factory.h"
#include "components/live_caption/translation_dispatcher_on_device.h"
#include "components/on_device_translation/service_controller.h"
#include "components/on_device_translation/service_controller_manager.h"
#endif

#if BUILDFLAG(ENABLE_ON_DEVICE_TRANSLATION)
  if (base::FeatureList::IsEnabled(
          live_caption::kLiveCaptionOnDeviceTranslation)) {
    on_device_dispatcher = std::make_unique<TranslationDispatcherOnDevice>(...);
  }
#endif
```

The Google API dispatcher remains active on QNX.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/browser/live_translate_controller_factory.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "translation_dispatcher_on_device|ENABLE_ON_DEVICE_TRANSLATION|translator.mojom.h|live_translate_controller_factory" docs/qnx/history/build-errors/compile
```
