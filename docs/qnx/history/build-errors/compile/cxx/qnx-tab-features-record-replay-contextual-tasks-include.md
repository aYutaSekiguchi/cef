# QNX: TabFeatures record_replay/contextual_tasks include guard

## Stage

- stage: compile
- category: cxx / incomplete-type
- target: `obj/chrome/browser/ui/tabs/impl/tab_features.o`

## Failure signature

```text
/home/yuta/qnx800/target/qnx/usr/include/c++/v1/__memory/unique_ptr.h:64:19: error: invalid application of 'sizeof' to an incomplete type 'RecordReplayPageActionController'
../../chrome/browser/ui/tabs/tab_features.cc:150:14: note: in instantiation of member function 'std::unique_ptr<RecordReplayPageActionController>::~unique_ptr' requested here
  150 | TabFeatures::TabFeatures() = default;
../../chrome/browser/ui/tabs/public/tab_features.h:31:7: note: forward declaration of 'RecordReplayPageActionController'
   31 | class RecordReplayPageActionController;
```

## Root cause

`tab_features.h` forward-declares `RecordReplayPageActionController`,
`record_replay::RecordReplayClient`, and
`contextual_tasks::ContextualTasksTabVisitTracker`, and stores
`std::unique_ptr` of each as a data member.

`tab_features.cc` defines `TabFeatures::TabFeatures() = default;` and
`~TabFeatures() = default;`. The defaulted members instantiate the
destructor of each `unique_ptr<...>`, which requires the full type.

The .cc only included the `record_replay/...` and
`contextual_tasks/...` headers under:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
```

QNX is not in that guard, so the headers were excluded and the
forward-declared types stayed incomplete.

## Fix

Extend the include guard in `tab_features.cc` to include `BUILDFLAG(IS_QNX)`.
The required deps (`record_replay`, `contextual_tasks`) are already added
under `!is_android` in `chrome/browser/ui/tabs/BUILD.gn`, and both targets
already allow QNX in their own BUILD asserts.

## Verification

```bash
cd /home/yuta/chromium/test/src
ninja -C out/qnx_release obj/chrome/browser/ui/tabs/impl/tab_features.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "RecordReplayPageActionController|ChromeRecordReplayClient|ContextualTasksTabVisitTracker|tab_features" docs/qnx/history/build-errors/compile
```
