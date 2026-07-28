# Variations QNX platform patch needs contextual hunks for clean replay

- Date: 2026-07-27
- Signature: `metrics_internals_utils.cc:41:11: enumeration value 'Study_Platform_PLATFORM_QNX' not handled in switch`
- Secondary signature: `metrics_internals_utils.cc:230:5: error: expected unqualified-id`
- Stage: bootstrap
- Category: build-graph
- Scope: `components/metrics/debug/metrics_internals_utils.cc` managed QNX patch replay

## Symptoms

With `treat_warnings_as_errors=true`, a clean QNX build reported both an
unhandled `PLATFORM_QNX` enum value in `PlatformToString()` and a
`case variations::Study::PLATFORM_QNX` statement at file scope.

The normal development tree had the same case correctly inside the switch,
so the failure appeared only after clean patch replay.

## Root cause

The `metrics_internals_utils.cc` part of
`variations_study_qnx_platform.patch` was a pure zero-context insertion:

```text
@@ -47,0 +48,2 @@
+    case variations::Study::PLATFORM_QNX:
+      return "QNX";
```

CEF's fallback patch application appended those lines at end of file. That
simultaneously left the enum unhandled and introduced invalid namespace-scope
statements.

## Applied change

The complete multi-file patch was regenerated with:

```text
python3 tools/patch_updater.py --resave \
  --patch qnx/chromium/variations_study_qnx_platform
```

The QNX case is now anchored by the neighboring Linux and ChromeOS cases.
No pragma, diagnostic suppression, or default switch branch was added.

## Verification

- `qnx_validate_patch_format.py` accepts all 515 managed patch files.
- Reverse apply check succeeds and forward apply check fails on the patched
  development source.
- A separately reset QNX tree completed sync, patch replay, and GN generation
  of 33,074 targets.
- The clean source places `PLATFORM_QNX` inside `PlatformToString()`.
- The formerly failing object completed successfully with `-j10`:

  ```text
  [1811/1811] CXX obj/components/metrics/debug/debug/metrics_internals_utils.o
  ```

## Files touched

- `cef/patch/patches/qnx/chromium/variations_study_qnx_platform.patch`
- `components/metrics/debug/metrics_internals_utils.cc`

## Related notes

- `docs/qnx/history/build-errors/bootstrap/build-graph/pseudonymization-salt-zero-context-patch.md`
- `docs/qnx/history/build-errors/bootstrap/build-graph/heap-profiler-controller-zero-context-patch.md`
- `docs/qnx/build-error-index.md`
