# QNX patch hygiene and stale-patch prevention

This note exists because repeated QNX bootstrap failures were caused by stale
CEF-managed patches rather than by compiler or GN build errors.

## Recent failure patterns

Two recurring bootstrap hygiene failures are now known:

1. **Stale patch contents.** A bootstrap run failed while applying
   `qnx/chromium/libdrm_qnx_memstream_makedev`.  The patch still contained
   hunks for `third_party/libdrm/src/xf86drm.c`, but that file no longer exists
   in the current Chromium/libdrm tree.  The patch was stale: its original
   assumptions no longer matched the source being bootstrapped.
2. **Unregistered patch files.** Several validated V8 QNX patches existed under
   `cef/patch/patches/qnx/chromium/`, but clean bootstrap did not apply them
   because `cef/patch/patch.cfg` did not register them.  The local working tree
   had the fix, while the durable bootstrap path silently missed it until a
   fresh `ceftests` build hit QNX-missing `<sys/syscall.h>` in V8.

The risky follow-up in both cases is to bypass bootstrap by manually restoring
or editing generated build state and continuing with a partially refreshed tree.
That can hide the real problem: the durable source of truth is the registered
patch stack plus QNX new files, and a working local tree is not evidence that a
clean bootstrap will work.

## Root-cause classification

The existing `AGENTS.md` already said to regenerate CEF-managed patch files with
`git diff --no-prefix --relative --full-index` and verify they apply cleanly.
The stale-patch recurrence was therefore not only a missing instruction about
patch format. The missing rule was stricter workflow control when bootstrap patch
application fails:

- do not manually apply or skip patches merely to get past bootstrap;
- treat a failed patch as the current blocker until the CEF-managed patch stack is
  repaired;
- distinguish patch-application failures from later GN or build failures.

In other words, the error was a mix of instruction-following drift and an
insufficiently explicit no-bypass rule.

## Required workflow when a patch fails to apply

1. Stop normal build-debugging work. Do not continue from the partially patched
   tree as if bootstrap succeeded.
2. Identify the exact failed patch and failed file/hunk from the bootstrap log.
3. Check whether the target file moved, was deleted, or already contains the
   intended upstream change.
4. Choose one durable fix:
   - regenerate the patch from the current source state;
   - split it if unrelated hunks now have different lifetimes;
   - add the patch to `cef/patch/patch.cfg` if the file exists but bootstrap
     should apply it;
   - remove the obsolete patch from both `cef/patch/patch.cfg` and
     `cef/patch/patches/...` when it is no longer needed, with a note explaining
     why.
5. Validate the repaired patch stack on a clean or reset tree:
   - run `cef/tools/qnx_sync_sources.sh -f -R` before bootstrap when refreshing a
     QNX working tree;
   - run `./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>`;
   - confirm the patch phase reports `0 failed` before treating any later GN or
     Ninja error as the next blocker.
6. Run `python3 cef/tools/qnx_validate_patch_format.py --root cef` and ensure all
   patch files use no-prefix format.

## Prohibited bootstrap bypasses

Do not use these actions to get around a failed bootstrap patch phase:

- manually applying individual patch files to the Chromium tree;
- deleting failed hunks or patch files without updating `patch.cfg` and recording
  the rationale;
- leaving a validated patch file under `cef/patch/patches/qnx/chromium/` without
  registering it in `cef/patch/patch.cfg` when bootstrap must apply it;
- hand-writing or restoring generated `out/qnx_release/args.gn` as a substitute
  for a successful bootstrap;
- continuing with Ninja builds after `cef_create_projects_qnx.sh` reports failed
  patches, except for explicitly labeled forensic experiments.

Manual source edits are acceptable only as the temporary working state used to
regenerate a CEF-managed patch. They are not a durable fix until the patch stack
applies cleanly from bootstrap.

## Verifying patch format before committing

A CEF-managed patch is durable source of truth, so its format is part of the
contract. The bootstrap aborts on a "corrupt patch" — there is no fuzzy fallback
for structural problems (only for context mismatches in otherwise-valid hunks).

Before committing a new or edited QNX patch, confirm:

- The hunk header (`@@ -OLD,COUNT +NEW,COUNT @@`) matches the body:
  `COUNT` on the old side must equal `context + removed` lines in the body;
  `COUNT` on the new side must equal `context + added` lines.
- The file ends with a trailing empty line after the last hunk. Both `git apply`
  and `patch` reject a hunk that is missing this terminator with
  `corrupt patch at line N` (where N is one past the last visible line).
- `python3 cef/tools/qnx_validate_patch_format.py --root cef` reports no
  failures. This catches no-prefix violations and other format issues.
- The patch is registered in `cef/patch/patch.cfg` if it is meant to replay
  during bootstrap.  A well-formatted but unregistered patch is still not a
  durable bootstrap fix.
- `patch -p0 --batch --dry-run` against a clean source tree exits 0 with
  `checking file <path>` output and no `failed` lines.

If any check fails, regenerate the patch with
`python3 cef/tools/patch_updater.py --resave --patch=<patch-name>` from the
correct patch root. Do not hand-edit hunks to satisfy these checks; the
regenerate-from-tree path is faster and provably correct.

See `history/build-errors/bootstrap/build-graph/qnx-patch-hunk-missing-trailing-empty-line-breaks-patch-application.md`
for a worked example of a corrupt-hunk failure and its durable fix.
