# Progress - Error Research

## Status
In Progress - External API research complete

## Tasks

### Completed: Local Context Scouting
- [x] Inspected all 14 failing build targets
- [x] Cross-referenced with QNX sysroot headers
- [x] Documented findings in `research/local-context.md`

### Completed: External API Research
- [x] Researched QNX SDP 8 API availability for all 12 missing symbols/headers
- [x] Documented findings in `research/external-apis.md`

### Key Findings Summary:

#### External API Research Results:

| # | Symbol | QNX Available | Recommended Fix |
|---|--------|---------------|-----------------|
| 1 | sys/syscall.h | NO | #ifdef guard + stub |
| 2 | linux/futex.h | NO | sem_t / pthread_mutex POSIX primitives |
| 3 | elf.h | PARTIAL (sys/elf.h) | qnx_compat.h shim |
| 4 | RLIMIT_NICE, NZERO | NO | setpri() as equivalent |
| 5 | MADV_FREE | NO | munmap + mmap pattern |
| 6 | MADV_DONTNEED, mincore | PARTIAL/NO | qnx_macros.h check |
| 7 | SO_PASSCRED, ucred | NO | stub as disabled |
| 8 | pthread_getattr_np | Available | Use directly ✓ |
| 9 | sem_init | Available | Standard POSIX ✓ |
| 10 | REG_RBP/RBX/gregs | DIFFERENT | __gregs[_REG_*] shim |
| 11 | report_modified_path | REMOVED | Current API only |
| 12 | kSystemDefaultMaxFds | NO | sysconf(_SC_OPEN_MAX) |

#### Already Known from Local Context:
- **pthread_getattr_np** - Available on QNX ✓
- **sem_init** - Signature matches QNX ✓
- **elf.h** - QNX uses sys/elf.h
- **mcontext_t layout** - DIFFERENT (uses cpu struct, not gregs[])

### External API Research Highlights:

1. **Most critical**: REG_* / mcontext.h shim needed for all register access code
2. **elf.h**: Must use sys/elf.h on QNX
3. **futex**: No equivalent; need POSIX semaphore/mutex abstraction
4. **Credentials**: QNX socket API lacks SCM_CREDENTIALS; stub required
5. **syscall.h**: QNX uses MsgSend() IPC, no syscall numbers

## Files Changed
- `research/external-apis.md` - Created comprehensive API availability research

## Next Steps
- [ ] Create `qnx_compat.h` with all shim definitions
- [ ] Create `qnx_mcontext.h` with register access macros
- [ ] Verify qnx_macros.h for MADV_DONTNEED
- [ ] Verify QNX SDK headers (sys/elf.h, ucontext.h _REG_* constants)
- [ ] Apply fixes to each build target
- [ ] Test build after fixes

## Notes
- External research confirms local context findings
- pthread_getattr_np and sem_init confirmed working on QNX
- Need to create compatibility shim headers for most Linux APIs

## 2026-05-11 - fix-patterns.md Complete

### Fix Strategy Analysis

Investigated Fuchsia/BSD porting patterns across all 14 failing modules.

**BUILD.gn changes (sources -=):**
- stack_copier_signal.cc — Linux futex/syscall/ucontext (QNX has none)
- thread_delegate_posix.cc — Linux gregs[] register array (QNX uses named fields)
- stack_base_address_posix.cc — /proc/self/maps (QNX has no /proc)
- launch_posix.cc — syscall.h / SYS_rt_sigaction (QNX has no syscall.h)
- base_paths_posix.cc — sysctl() API (QNX has no sysctl)

**Header include additions:**
- elf_reader.h — add qnx_macros.h + elf_w.h (Elf32_Phdr / Elf64_Phdr types)
- madv_free_discardable_memory_posix.cc — add qnx_macros.h (MADV_DONTNEED macro)

**Inline guard additions:**
- unix_domain_socket.cc — extend IS_APPLE exclusion to also exclude QNX (SO_PASSCRED/ucred absent)

**Already handled:**
- process_metrics_posix.cc — has IS_FUCHSIA guards
- file_path_watcher_inotify.cc — has IS_FUCHSIA guard
- process_metrics.cc — platform-neutral
- elf_reader.cc — works via elf_reader.h include
- module_cache_posix.cc — works via elf_reader.h include
- can_lower_nice_to.cc — POSIX portable
- cancelable_event_posix.cc — likely POSIX portable

**Key finding:** QNX's sysroot already provides all ELF types via sys/elf.h + elfdefinitions.h. The primary issue is missing macros (ElfW, MADV_DONTNEED) that qnx_macros.h already defines. Adding includes for that header to failing files resolves most issues.

Output: research/fix-patterns.md
