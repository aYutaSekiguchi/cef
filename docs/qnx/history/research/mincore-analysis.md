# mincore() Usage Analysis: madv_free_discardable_memory_posix.cc

## 1. IsResident() / IsDiscarded() Details

### Target File
`base/memory/madv_free_discardable_memory_posix.cc`

### IsResident() Implementation (lines 294-305)
```cpp
bool MadvFreeDiscardableMemoryPosix::IsResident() const {
  DFAKE_SCOPED_RECURSIVE_LOCK(thread_collision_warner_);
#if BUILDFLAG(IS_APPLE)
  std::vector<char> vec(allocated_pages_);
#else
  std::vector<unsigned char> vec(allocated_pages_);
#endif

  int retval =
      mincore(data_, allocated_pages_ * base::GetPageSize(), vec.data());
  DPCHECK(retval == 0 || errno == EAGAIN);

  for (size_t i = 0; i < allocated_pages_; ++i) {
    if (!(vec[i] & 1)) {
      return false;
    }
  }
  return true;
}
```

### IsDiscarded() Implementation (line 315)
```cpp
bool MadvFreeDiscardableMemoryPosix::IsDiscarded() const {
  return !is_locked_ && !IsResident();
}
```

## 2. IsResident() Call Sites

| Location | Call Site | File:Line |
|----------|----------|----------|
| Inside Lock() | `DCHECK(IsResident());` | madv_free_discardable_memory_posix.cc:131 |
| Inside CreateMemoryAllocatorDump() | `bool is_discarded = IsDiscarded();` (calls IsResident internally) | madv_free_discardable_memory_posix.cc:246 |

### Impact of Each Call

**1. DCHECK(IsResident()) in Lock() (line 131)**
- Assertion when Lock succeeds (when all pages are resident)
- `DCHECK_IS_ON()` only (ignored in release builds)
- Only affects debug builds on QNX

**2. IsDiscarded() in CreateMemoryAllocatorDump() (line 246)**
- Used for `discarded_size` calculation in memory dump
- When always returns `IsResident() == false`:
  - Calculated as `is_discarded = true`
  - `discarded_size` gets all page sizes added (inflated value)
  - Impacts memory profiling/tracing tools

## 3. Impact When IsResident() Always Returns true

### Memory Management Issues
| Scenario | Normal Behavior | IsResident()=true Fixed |
|----------|---------------|------------------------|
| Eviction under memory pressure | OS discards MADV_FREE pages | Cannot detect if eviction is possible |
| Re-lock after Unlock | Fails if pages were discarded | Always succeeds (dangerous!) |
| Deallocate/reallocate | Allocates only when needed | Keeps unnecessary pages |

### Most Critical Impact: Post-Unlock Lock Detection Failure
- `Unlock()` sets `MADV_FREE`
- OS discards pages under memory pressure
- During `Lock()`, `LockPage()` detects page discard via CAS (Compare-And-Swap) on magic cookie
- **IsResident() is independent from this detection** (LockPage() handles actual discard detection)
- Even if IsResident() always returns true, LockPage()'s CAS-based discard detection still works

### Q: Can LockPage()'s detection break if IsResident() always returns true?
**A: No.** LockPage() detects discard via atomic CAS on the magic cookie, which does not depend on IsResident().

## 4. Impact When IsResident() Always Returns false

### IsDiscarded() Calculation Error
- `IsDiscarded() = !is_locked_ && !IsResident()`
- When unlocked and IsResident()=false → `is_discarded = true`
- When always false: **unlocked memory is always classified as discarded**
- Memory dump reports `discarded_size` as inflated

## 5. mincore() Alternatives on QNX

### QNX Investigation Results
```
$ grep -n 'mincore' ~/qnx800/target/qnx/usr/include/devs/sys/mman.h
333:int  mincore(const void *, size_t, char *);
```

**Finding: mincore() EXISTS on QNX!**

However:
- QNX sysroot has the declaration, but actual implementation status is unconfirmed
- mincore() is a POSIX extension, not part of the POSIX standard
- QNX 8.0 documentation doesn't explicitly document mincore() support

### Alternative API Investigation
```bash
$ grep -rn 'pmap_mincore\|pagemap\|page_resident' ~/qnx800/target/qnx/usr/include/
<QNX_SDP_ROOT>/target/qnx/usr/include/devs/vm/pmap.h:155:
  int pmap_mincore(pmap_t pmap, vm_offset_t addr, vm_paddr_t *pap);
```

**pmap_mincore()**: Kernel-level API. Direct use from userland is difficult.

## 6. BUILD.gn Build Condition

```python
# base/BUILD.gn (lines 1680-1690)
if (is_posix) {
  sources += [
    "memory/madv_free_discardable_memory_allocator_posix.cc",
    "memory/madv_free_discardable_memory_allocator_posix.cc",
    "memory/madv_free_discardable_memory_posix.cc",
    ...
  ]
}
```

**Important finding**: `is_posix` includes `is_qnx`.

```bash
# is_qnx judgment in BUILD.gn
if (is_qnx) { ... }  # QNX-specific processing exists, but madv_free is not excluded
```

## 7. Validity of Whole-File Exclusion

### Exclusion Conditions
The current code structure has no `is_posix && !is_qnx` style condition.

### Exclusion Validity Assessment

| Assessment Item | Description |
|----------------|------------|
| **Functional impact** | Low-medium - LockPage() maintains discard detection even if IsResident() is stubbed |
| **Memory profiling** | Medium - discarded_size reported inaccurately |
| **Debug/DCHECK** | Low - No impact in release builds |
| **mincore in QNX sysroot** | Declared in QNX sysroot. Actual availability needs confirmation |

### Recommended Approach

1. **First, verify mincore() symbol exists in QNX sysroot**
   ```bash
   # Check symbol in library
   nm ~/qnx800/target/qnx/usr/lib/libc.so 2>/dev/null | grep mincore
   ```

2. **If mincore() is available**: Use as-is. No BUILD.gn change needed.

3. **If mincore() is unavailable**:
   - Stub IsResident() to always return true only under `is_qnx` condition
   - Adjust IsDiscarded() accordingly
   - Must accept degraded memory dump accuracy

## 8. Conclusion

1. **When IsResident() always returns true**:
   - LockPage()'s CAS-based discard detection is maintained (direct impact is low)
   - `discarded_size` in memory dump becomes always 0

2. **Whether QNX has a mincore() alternative API**:
   - `pmap_mincore()` exists but is a kernel-level API
   - Difficult to use from userland

3. **Validity of whole-file exclusion**:
   - No direct functional issue (LockPage handles core discard detection)
   - Must consider impact on memory profiling/tracing

## Next Steps

1. Verify mincore() symbol exists in QNX sysroot
2. Test actual mincore() call on QNX target
3. If stubbing is needed, add `is_qnx` condition in BUILD.gn
