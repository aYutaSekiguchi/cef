# DIR_USER_DESKTOP resolves but does not exist in the QNX QEMU environment

- Date: 2026-05-23
- Signature: PathServiceTest.Get key=8 DIR_USER_DESKTOP
- Stage: test
- Category: runtime-assumption
- Scope: base/path service

## Symptoms

- `PathServiceTest.Get` failed for key `DIR_USER_DESKTOP`.
- The resolved path `/data/home/root/Desktop` was syntactically valid but missing on the QNX QEMU image.
- Other `PathService` keys such as `DIR_TEMP`, `DIR_HOME`, and `DIR_CURRENT` behaved normally.

## Root cause

- The test assumed the desktop path should exist once resolved.
- In the QNX QEMU environment, the XDG-style desktop directory is absent, which matches behavior already tolerated on Linux CI in similar minimal environments.

## Fix pattern

- Treat optional user-directory existence checks as environment-sensitive rather than universally valid.
- Reuse the same platform guard or expectation relaxation already accepted for other minimal CI environments when QNX behaves equivalently.

## Applied change

- Relaxed the desktop existence check on QNX under the same condition family used for Linux.

## Verification

- `PathServiceTest.Get` passed after the QNX guard was added.

## Files touched

- `base/path_service_unittest.cc`

## Related notes

- `docs/qnx/build-error-index.md`
