# QNX heap profiler reenters pthread TLS and exits with SIGTRAP

## Signature

- Stage: `test`
- Category: `runtime-assumption`
- Process output:
  `TLS System: Failed to set thread specific data ... tls.h@257`
- Shell output: `trace trap (core dumped)`
- Exit status: `__PI_QNX_EXIT__:133`
- Relevant code: `PoissonAllocationSampler`, allocator dispatcher
  `ReentryGuard`, and QNX `pthread_setspecific`

## Root cause

The failure is not evidence of system-wide OOM. QNX lazily grows a thread's
pthread-key storage. In the failing thread, `pthread_setspecific` reallocates
that storage while `PoissonAllocationSampler::OnAllocation` is already running.
The allocation reenters AllocatorShim and the sampler, reaches
`pthread_setspecific` again, and QNX returns `ENOMEM` (12). Chromium's
`TLS_RAW_CHECK` converts that failure into SIGTRAP.

The captured core showed this cycle:

```text
PoissonAllocationSampler::OnAllocation
  -> GetThreadLocalData
  -> pthread_setspecific
  -> realloc
  -> AllocatorShim::ReallocFn
  -> PoissonAllocationSampler::OnAllocation
  -> GetThreadLocalData
  -> pthread_setspecific
  -> TLS_RAW_CHECK / SIGTRAP
```

QNX previously received the no-op allocator-dispatcher `ReentryGuard`.
Changing it to the pthread-backed guard used by some other platforms would not
solve this failure because initializing that guard's own pthread key has the
same lazy-allocation path.

## Durable fix

`components_heap_profiler_disable_collection_qnx.patch` makes
`DecideIfCollectionIsEnabled()` always return disabled on QNX. It deliberately
keeps `HeapProfilerController` alive because Chrome GPU, renderer, and utility
clients expect `GetInstance()` to return a controller. Only profile collection,
and therefore `PoissonAllocationSampler::Start()`, is suppressed.

Do not compile the controller out of `components/memory_system`: GPU startup
contains a `CHECK` for the controller and exits with status 133 if it is absent.

The patch is registered in `cef/patch/patch.cfg` and stored at:

```text
cef/patch/patches/qnx/chromium/components_heap_profiler_disable_collection_qnx.patch
```

## Reproduction and validation

Before the fix, this deterministic feature override crashed immediately:

```text
--enable-features=HeapProfilerReporting:stable-probability/1.0
```

Before-fix log:

```text
out/qnx_release/qnx_run_20260723_193412_cefsimple.log.serial
```

After the fix, the same override ran browser, GPU, utility, and renderer
processes for the full 60-second test window with no TLS message, SIGTRAP, or
GPU exit 133:

```text
out/qnx_release/qnx_run_20260724_080643_cefsimple.log.serial
```

A normal YouTube run without the old disable-feature workaround reached
`OnLoadEnd status=200`, kept its GPU and renderer processes alive, and showed
no TLS or GPU crash:

```text
out/qnx_release/qnx_run_20260724_081611_cefsimple.log.serial
```

The guest used explicit DNS `8.8.8.8` for this network validation.

## Operational result

Current patched builds do not require:

```text
--disable-features=HeapProfilerReporting
```

Keep that switch only when running an older binary that predates the permanent
fix. It remains useful as a quick diagnostic A/B switch for old artifacts.
