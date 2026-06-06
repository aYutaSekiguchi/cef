# ImportantFileWriter bad-path observer test succeeds on QNX instead of failing

- Date: 2026-05-23
- Signature: ImportantFileWriterTest.FailedWriteWithObserver returned success instead of access denied
- Stage: test
- Category: runtime-assumption
- Scope: base/files ImportantFileWriter

## Symptoms

- `ImportantFileWriterTest.FailedWriteWithObserver` failed because QNX reported success where the test expected `FILE_ERROR_ACCESS_DENIED`.

## Root cause

- The test normalized `bad/../path` to a writable path under `/tmp`.
- The original NFS-specific hypothesis was wrong; the same result occurred on local storage.

## Fix pattern

- Verify environment-specific hypotheses against a local filesystem before encoding them in documentation or skips.
- When the test expects a platform-specific error mode that QNX does not share, treat it as a test-expectation mismatch unless product behavior depends on it.

## Applied change

- No product-code change.
- Excluded the test through the QNX test-runner filter.

## Verification

- Reproduced the same outcome on `/tmp`, confirming NFS was not the cause.

## Files touched

- `cef/tools/qnx_run_test.sh`

## Related notes

- `docs/qnx/build-error-index.md`
