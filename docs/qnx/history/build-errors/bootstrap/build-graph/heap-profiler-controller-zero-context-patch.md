# Heap profiler QNX patch needs contextual hunks for clean replay

- Date: 2026-07-27
- Signature: `components/heap_profiling/in_process/heap_profiler_controller.cc:629:3: error: expected unqualified-id`
- Stage: bootstrap
- Category: build-graph
- Scope: `components/heap_profiling/in_process/heap_profiler_controller.cc` managed QNX patch replay

## Symptoms

A clean QNX build placed the QNX-only collection guard after the closing
namespace in `heap_profiler_controller.cc`. Compilation then failed on the
first appended statement with `expected unqualified-id`.

The already-patched development tree had the intended guards inside
`GetChannelProbability()` and `DecideIfCollectionIsEnabled()`, so incremental
builds did not expose the durable patch replay problem.

## Root cause

`components_heap_profiler_disable_collection_qnx.patch` represented its
changes as pure insertion hunks with zero unchanged context. CEF's patch
fallback could not anchor those insertions during clean replay and appended
them at end of file.

## Fix pattern

- Regenerate CEF-managed patches with the project patch updater instead of
  preserving zero-context pure insertion hunks.
- Require unchanged source context around inserted preprocessor guards and
  function-body statements.
- Verify the regenerated patch in a separately reset clean tree.

## Applied change

The patch was regenerated with:

```text
python3 tools/patch_updater.py --resave \
  --patch qnx/chromium/components_heap_profiler_disable_collection_qnx
```

This produced normal contextual hunks. No warning pragma or diagnostic
suppression was added.

## Verification

- Reverse apply check succeeds on the patched development source.
- Forward apply check fails on the patched development source, confirming it
  is detected as already applied.
- Clean bootstrap generated the guards inside their intended functions.
- The formerly failing object completed successfully with `-j10`:

  ```text
  [1565/1565] CXX obj/components/heap_profiling/in_process/in_process/heap_profiler_controller.o
  ```

## Files touched

- `cef/patch/patches/qnx/chromium/components_heap_profiler_disable_collection_qnx.patch`
- `components/heap_profiling/in_process/heap_profiler_controller.cc`

## Related notes

- `docs/qnx/history/build-errors/bootstrap/build-graph/pseudonymization-salt-zero-context-patch.md`
- `docs/qnx/build-error-index.md`
