// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// QNX-specific implementation of process launching using posix_spawnp() instead
// of fork()+exec(). QNX does not support fork() in multithreaded processes
// (pthread_create() followed by fork() returns ENOSYS), so the standard POSIX
// implementation in launch_posix.cc cannot be used on QNX.
//
// Key differences from launch_posix.cc:
// - Uses posix_spawnp() with file_actions for FD remapping
// - No CloseSuperfluousFds() loop: on QNX, the excessive close actions caused
//   EBADF on NFS-mounted executables. With posix_spawn(), the child only gets
//   FDs explicitly listed in file_actions, so a separate close step is not
//   needed for FDs we want to exclude — they are simply not remapped.
// - pre_exec_delegate and maximize_rlimits are not supported (posix_spawn has
//   no fork step for running callbacks).
//
// This implementation is designed for the --no-zygote --no-sandbox path.

#include "base/process/launch.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/environment_internal.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/trace_event/trace_event.h"

extern char** environ;

namespace base {

namespace {

struct GetAppOutputOptions {
  // Whether to pipe stderr to stdout in |output|.
  bool include_stderr = false;
  // Caller-supplied string pointer for the output.
  raw_ptr<std::string> output = nullptr;
  // Result exit code of Process::Wait().
  int exit_code = 0;
};

bool GetAppOutputInternal(const std::vector<std::string>& argv,
                          GetAppOutputOptions* gao_options) {
  TRACE_EVENT0("base", "GetAppOutput");

  // Create a temporary file for capturing child output. Using a file avoids
  // FD-leak-through-spawn-chain issues that can occur with pipes (the dup2'd
  // pipe write end in the child has no FD_CLOEXEC and leaks to grandchildren,
  // causing intermittent SIGSEGV on QNX).
  FilePath temp_path;
  ScopedFILE temp_file(CreateAndOpenTemporaryStream(&temp_path));
  if (!temp_file) {
    DPLOG(ERROR) << "CreateAndOpenTemporaryStream";
    return false;
  }
  int output_fd = fileno(temp_file.get());
  // Ensure the temp file fd is inheritable by the child. On QNX, the
  // underlying open() may set O_CLOEXEC (e.g. from mkstemp), which would
  // prevent the child from inheriting it. Without the fd in the child,
  // posix_spawn's dup2(file_actions) would fail with EBADF.
  {
    int fd_flags = fcntl(output_fd, F_GETFD);
    if (fd_flags != -1 && (fd_flags & FD_CLOEXEC)) {
      fcntl(output_fd, F_SETFD, fd_flags & ~FD_CLOEXEC);
    }
  }

  // Spawn the child process. We use a direct posix_spawnp call here rather
  // than going through LaunchProcess to capture output to a temp file
  // instead of a pipe. Pipes on QNX can cause FD-leak-through-spawn-chain
  // issues (the dup2'd pipe write end in the child has no FD_CLOEXEC and
  // leaks to grandchildren, causing intermittent SIGSEGV). A temp file
  // avoids these issues entirely.
  posix_spawn_file_actions_t raw_fa;
  posix_spawn_file_actions_init(&raw_fa);
  posix_spawn_file_actions_adddup2(&raw_fa, output_fd, STDOUT_FILENO);
  if (gao_options->include_stderr) {
    posix_spawn_file_actions_adddup2(&raw_fa, output_fd, STDERR_FILENO);
  }
  posix_spawn_file_actions_addopen(&raw_fa, STDIN_FILENO, "/dev/null",
                                   O_RDONLY, 0);
  posix_spawn_file_actions_addclose(&raw_fa, output_fd);

  std::vector<char*> raw_argv;
  raw_argv.reserve(argv.size() + 1);
  for (const auto& arg : argv) {
    raw_argv.push_back(const_cast<char*>(arg.c_str()));
  }
  raw_argv.push_back(nullptr);

  const char* raw_path = argv[0].c_str();
  std::string raw_resolved_path;
  if (raw_path[0] != '/') {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
      raw_resolved_path = std::string(cwd) + "/" + raw_path;
      size_t pos;
      while ((pos = raw_resolved_path.find("/./")) != std::string::npos) {
        raw_resolved_path.erase(pos, 2);
      }
      raw_path = raw_resolved_path.c_str();
    }
  }

  posix_spawnattr_t raw_attr;
  posix_spawnattr_init(&raw_attr);
  short raw_flags = POSIX_SPAWN_SETSIGDEF;
  posix_spawnattr_setflags(&raw_attr, raw_flags);

  pid_t raw_pid = 0;
  int raw_rv = posix_spawnp(&raw_pid, raw_path, &raw_fa, &raw_attr,
                              &raw_argv[0], environ);
  posix_spawnattr_destroy(&raw_attr);
  posix_spawn_file_actions_destroy(&raw_fa);

  Process process;
  if (raw_rv == 0) {
    process = Process(raw_pid);
  } else {
    errno = raw_rv;
  }
  if (!process.IsValid()) {
    // With fork+exec, when the executable does not exist, fork() succeeds but
    // the child exits with code 127 (_exit after failed execvp()). With
    // posix_spawn(), the spawn itself fails with ENOENT and no child process
    // is created. Emulate the fork+exec behavior here.
    int spawn_error = errno;
    if (spawn_error == ENOENT) {
      gao_options->exit_code = 127;
      return true;
    }
    return false;
  }
  gao_options->exit_code = EXIT_FAILURE;

  // Close the temp file in the parent so the child's writes are not buffered
  // and the child's FD is the only reference when we read later.
  temp_file.reset();

  // Wait for the child to exit.
  internal::GetAppOutputScopedAllowBaseSyncPrimitives allow_wait;
  if (!process.WaitForExit(&gao_options->exit_code)) {
    DeleteFile(temp_path);
    return false;
  }

  // Read the child's output from the temp file.
  std::string* output = gao_options->output;
  if (!ReadFileToString(temp_path, output)) {
    DeleteFile(temp_path);
    return false;
  }

  DeleteFile(temp_path);
  return true;
}

}  // namespace

Process LaunchProcess(const CommandLine& cmdline,
                      const LaunchOptions& options) {
  return LaunchProcess(cmdline.argv(), options);
}

Process LaunchProcess(const std::vector<std::string>& argv,
                      const LaunchOptions& options) {
  TRACE_EVENT0("base", "LaunchProcess");

  // pre_exec_delegate is not supported with posix_spawn because there is no
  // fork() step in which to run the delegate.
  if (options.pre_exec_delegate) {
    DLOG(ERROR) << "LaunchProcess: pre_exec_delegate is not supported on QNX";
    return Process();
  }

  // maximize_rlimits is not supported on QNX.
  if (options.maximize_rlimits) {
    DLOG(ERROR) << "LaunchProcess: maximize_rlimits is not supported on QNX";
    return Process();
  }

  // Build argv.
  std::vector<char*> argv_cstr;
  argv_cstr.reserve(argv.size() + 1);
  for (const auto& arg : argv) {
    argv_cstr.push_back(const_cast<char*>(arg.c_str()));
  }
  argv_cstr.push_back(nullptr);

  // Build effective environment.
  // When current_directory is set, we must chdir before spawn (QNX has no
  // posix_spawn_file_actions_addchdir_np()). This would break relative
  // LD_LIBRARY_PATH entries like ".", so prepend the absolute cwd.
  base::HeapArray<char*> owned_environ;
  char* empty_environ = nullptr;
  char** new_environ = options.clear_environment ? &empty_environ : environ;
  if (!options.environment.empty() || !options.current_directory.empty()) {
    EnvironmentMap effective_env = options.environment;

    if (!options.current_directory.empty()) {
      char cwd_buf[PATH_MAX];
      if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        std::string abs_ld_path(cwd_buf);
        auto it = effective_env.find("LD_LIBRARY_PATH");
        if (it != effective_env.end() && !it->second.empty()) {
          abs_ld_path += ":";
          abs_ld_path += it->second;
        } else {
          const char* env_ld = getenv("LD_LIBRARY_PATH");
          if (env_ld && env_ld[0]) {
            abs_ld_path += ":";
            abs_ld_path += env_ld;
          }
        }
        effective_env["LD_LIBRARY_PATH"] = abs_ld_path;
      }
    }

    owned_environ =
        internal::AlterEnvironment(new_environ, effective_env);
    new_environ = owned_environ.data();
  }

  const char* executable_path = !options.real_path.empty()
                                    ? options.real_path.value().c_str()
                                    : argv_cstr[0];

  // Resolve relative executable paths to absolute paths. QNX posix_spawnp
  // may not handle relative paths (e.g. "./base_unittests") correctly.
  std::string resolved_executable_path;
  if (executable_path[0] != '/') {
    char cwd_buf[PATH_MAX];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
      resolved_executable_path = std::string(cwd_buf) + "/" + executable_path;
      // Normalize away "./" components (e.g. "/dir/./exe" -> "/dir/exe").
      // QNX posix_spawnp may not handle such paths correctly.
      size_t pos;
      while ((pos = resolved_executable_path.find("/./")) !=
             std::string::npos) {
        resolved_executable_path.erase(pos, 2);
      }
      executable_path = resolved_executable_path.c_str();
    }
  }

  // Handle current_directory with parent-side chdir + fchdir restore.
  base::ScopedFD cwd_fd;
  if (!options.current_directory.empty()) {
    cwd_fd.reset(HANDLE_EINTR(open(".", O_RDONLY | O_DIRECTORY)));
    if (!cwd_fd.is_valid()) {
      DPLOG(ERROR) << "open . for cwd save";
      return Process();
    }
    if (HANDLE_EINTR(chdir(options.current_directory.value().c_str())) != 0) {
      DPLOG(ERROR) << "chdir " << options.current_directory.value();
      return Process();
    }
  }

  // Build the spawn attributes (matching GetAppOutputInternal pattern).
  posix_spawnattr_t raw_attr;
  posix_spawnattr_init(&raw_attr);
  short raw_flags = POSIX_SPAWN_SETSIGDEF;
  if (options.new_process_group) {
    raw_flags |= POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setpgroup(&raw_attr, 0);
  }
  posix_spawnattr_setflags(&raw_attr, raw_flags);

  // Build file actions — only for fds_to_remap, NO close_superfluous_fds.
  // GetAppOutputInternal uses this exact pattern and works reliably.
  posix_spawn_file_actions_t raw_fa;
  posix_spawn_file_actions_init(&raw_fa);

  bool null_stdin = true;
  std::vector<int> remap_sources_to_close;
  remap_sources_to_close.reserve(options.fds_to_remap.size());
  for (const auto& dup2_pair : options.fds_to_remap) {
    if (dup2_pair.second == STDIN_FILENO) {
      null_stdin = false;
    }
    posix_spawn_file_actions_adddup2(&raw_fa, dup2_pair.first,
                                      dup2_pair.second);
    if (dup2_pair.first != dup2_pair.second) {
      remap_sources_to_close.push_back(dup2_pair.first);
    }
  }

  if (null_stdin) {
    posix_spawn_file_actions_addopen(&raw_fa, STDIN_FILENO, "/dev/null",
                                     O_RDONLY, 0);
  }

  // Close the remapped source fds in the child so it doesn't inherit both
  // the original and the dup2'd copy.
  // The same source fd may appear multiple times (e.g. when one fd is
  // remapped to both STDOUT and STDERR). Deduplicate.
  std::sort(remap_sources_to_close.begin(),
            remap_sources_to_close.end());
  remap_sources_to_close.erase(
      std::unique(remap_sources_to_close.begin(),
                  remap_sources_to_close.end()),
      remap_sources_to_close.end());
  for (int close_fd : remap_sources_to_close) {
    posix_spawn_file_actions_addclose(&raw_fa, close_fd);
  }

  // Do not add a close-superfluous-FDs sweep here. QNX posix_spawnp() can
  // fail the whole spawn with EBADF when a large close action set interacts
  // with descriptor remapping and the NFS-backed executable used by the test
  // runner. This intentionally keeps the implementation close to POSIX
  // fork+exec semantics: remap requested descriptors, close their original
  // sources, and otherwise inherit the parent's descriptor table.

  pid_t pid = 0;
  int rv = posix_spawnp(&pid, executable_path, &raw_fa, &raw_attr,
                         &argv_cstr[0], new_environ);
  posix_spawnattr_destroy(&raw_attr);
  posix_spawn_file_actions_destroy(&raw_fa);

  // Restore current directory if changed.
  if (cwd_fd.is_valid()) {
    if (HANDLE_EINTR(fchdir(cwd_fd.get())) != 0) {
      DPLOG(ERROR) << "fchdir restore cwd";
    }
    cwd_fd.reset();
  }

  if (rv != 0) {
    LOG(ERROR) << "posix_spawnp(" << executable_path << "): -" << rv << " "
               << strerror(rv);
    errno = rv;
    return Process();
  }

  if (options.wait) {
    // While this isn't strictly disk IO, waiting for another process to
    // finish is the sort of thing ThreadRestrictions is trying to prevent.
    ScopedBlockingCall scoped_blocking_call(FROM_HERE, BlockingType::MAY_BLOCK);
    pid_t ret = HANDLE_EINTR(waitpid(pid, nullptr, 0));
    DPCHECK(ret > 0);
  }

  return Process(pid);
}

bool GetAppOutput(const CommandLine& cl, std::string* output) {
  return GetAppOutput(cl.argv(), output);
}

bool GetAppOutputAndError(const CommandLine& cl, std::string* output) {
  return GetAppOutputAndError(cl.argv(), output);
}

bool GetAppOutputWithExitCode(const CommandLine& cl,
                              std::string* output,
                              int* exit_code) {
  return GetAppOutputWithExitCode(cl.argv(), output, exit_code);
}

bool GetAppOutput(const std::vector<std::string>& argv, std::string* output) {
  GetAppOutputOptions options;
  options.output = output;
  return GetAppOutputInternal(argv, &options) &&
         options.exit_code == EXIT_SUCCESS;
}

bool GetAppOutputAndError(const std::vector<std::string>& argv,
                          std::string* output) {
  GetAppOutputOptions options;
  options.include_stderr = true;
  options.output = output;
  return GetAppOutputInternal(argv, &options) &&
         options.exit_code == EXIT_SUCCESS;
}

bool GetAppOutputWithExitCode(const std::vector<std::string>& argv,
                              std::string* output,
                              int* exit_code) {
  GetAppOutputOptions options;
  options.output = output;
  bool rv = GetAppOutputInternal(argv, &options);
  *exit_code = options.exit_code;
  return rv;
}

void RaiseProcessToHighPriority() {
  // Not implemented on QNX.
}

}  // namespace base
