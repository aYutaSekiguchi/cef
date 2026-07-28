# Pseudonymization salt QNX patch needs contextual hunks for clean replay

- Date: 2026-07-27
- Signature: `content/common/pseudonymization_salt.cc:139:3: error: expected unqualified-id`
- Stage: bootstrap
- Category: build-graph
- Scope: `content/common/pseudonymization_salt.cc` managed QNX patch replay

## Symptoms

A clean QNX build failed while compiling `pseudonymization_salt.o`:

```text
content/common/pseudonymization_salt.cc:139:3: error: expected unqualified-id
  return;
  ^
```

The normal development tree showed the intended QNX guard inside
`MaybeInitializePseudonymizationSaltFromSharedMemory()`, but the clean
validation tree placed all five added lines after the `content` namespace:

```text
137 #include "build/build_config.h"
138 #if BUILDFLAG(IS_QNX)
139   return;
140 #else
141 #endif
```

## Root cause

`content_pseudonymization_salt_qnx_skip_shared_memory.patch` had been resaved
as three pure insertion hunks with zero context:

```text
@@ -15,0 +16 @@
@@ -121,0 +123,3 @@
@@ -132,0 +137 @@
```

CEF's patch application fallback could not anchor those insertions during a
clean replay and appended them at end of file. The resulting `return;` was at
namespace scope. Checking only the already-patched development tree missed
the durable replay failure.

## Fix pattern

- Do not use zero-context pure insertion hunks for CEF-managed patches.
- Regenerate the patch from the correct Chromium patch root with
  `git diff --no-prefix --relative --full-index`.
- Keep enough unchanged source context around each insertion for normal
  forward and reverse `git apply --check` detection.
- Verify the patch against a separately reset clean tree, not only against the
  current patched source.

## Applied change

The managed patch was regenerated with contextual include and function-body
hunks. The QNX early return remains inside
`MaybeInitializePseudonymizationSaltFromSharedMemory()`, with the normal
shared-memory import compiled in the `#else`.

No warning pragma or diagnostic suppression was added.

## Verification

- Standard reverse check succeeds on the patched source.
- Standard forward check fails on the patched source, proving the patch is no
  longer ambiguously re-applicable.
- `qnx_validate_patch_format.py` accepts all 515 managed patch files.
- Clean sync and bootstrap completed with 507 patches considered, 485
  applied, 22 skipped, and 0 failed.
- GN generated 33,074 targets with `treat_warnings_as_errors=true`.
- The clean tree generated the QNX guard at lines 123-137 inside the function.
- The original failing object completed successfully:

  ```text
  [4725/4725] CXX obj/content/common/common/pseudonymization_salt.o
  ```

## Files touched

- `cef/patch/patches/qnx/chromium/content_pseudonymization_salt_qnx_skip_shared_memory.patch`
- `content/common/pseudonymization_salt.cc`

## Related notes

- `docs/qnx/history/build-errors/compile/toolchain-config/qnx-warnings-as-errors-clean-build-2026-07-27.md`
- `docs/qnx/build-error-index.md`
