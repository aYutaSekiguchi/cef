# pkey.cc QNX Build Exclusion Fix Confirmation

## Confirmation Result

Already fixed (by someone else earlier).

### Fix Location

**`base/allocator/partition_allocator/src/partition_alloc/BUILD.gn`** (lines 485-490)

```gn
      if (!is_qnx) {
        sources += [
          "thread_isolation/pkey.cc",
          "thread_isolation/pkey.h",
        ]
      }
```

### Behavior Verification

| Condition | pkey.cc |
|-----------|---------|
| Non-QNX (`!is_qnx`) | ✅ Built |
| QNX (`is_qnx`) | ❌ Excluded |

Even though `enable_pkeys` is false on QNX, the source list exclusion prevents accidental compilation. The `!is_qnx` condition addition resolves this.