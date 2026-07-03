/*
 * qnx_scmrights_probe.c
 *
 * Phase 1B-scmrights and Phase 1B-devicefd prerequisite probe:
 * validate SCM_RIGHTS fd passing over Unix-domain sockets on QNX,
 * independently of EGL/Screen/DMAbuf.
 *
 * Tests three fd types:
 *   1. Regular file fd (temp file) - known broken on QNX/QEMU
 *   2. Character-device fd (/dev/null O_RDWR) - DMAbuf/PRIME proxy
 *   3. Pipe fd - confirmed working
 *
 * Single-process, self-contained.
 *
 * Build:
 *   cd /home/yuta/chromium/src/cef
 *   source ../out/qnx_release/qnx_env.sh
 *   export PATH="$QNX_HOST/usr/bin:$PATH"
 *   qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_scmrights_probe \
 *       tools/qnx_probes/qnx_scmrights_probe.c -lsocket
 *
 * Run:
 *   ./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_scmrights_probe
 *
 * Exit codes:
 *   0 = PASS
 *   1 = FAIL (one or more tests failed)
 *
 * Does NOT link against Chromium; no GN, no Ozone backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define PAYLOAD "QNX_SCMRIGHTS_PROBE_2026_07_02"
#define PAYLOAD_LEN (sizeof(PAYLOAD) - 1)   /* no NUL */

/* ---- Helpers ---- */

static void print_sep(void) {
    printf("--------------------------------------------------------------------------------\n");
}

/* Like strerror but falls back to numeric errno for unknown codes. */
static const char *sc_strerror(int e) {
    static char buf[64];
    const char *s = strerror(e);
    if (s && s[0] != '\0') return s;
    snprintf(buf, sizeof(buf), "Unknown error %d", e);
    return buf;
}

/*
 * send_one_fd: send a single fd over a Unix-domain socket using SCM_RIGHTS.
 * The file descriptor 'fd' must be valid.
 * Returns 0 on success, -1 on failure (errno set).
 */
static int send_one_fd(int sockfd, int fd) {
    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    char dummy = '\0';

    msg.msg_control    = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;

    /* one zero-length data segment so recvmsg doesn't return 0 bytes */
    msg.msg_iov        = &(struct iovec){ .iov_base = &dummy, .iov_len = 1 };
    msg.msg_iovlen     = 1;

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    ssize_t sent = sendmsg(sockfd, &msg, 0);
    if (sent < 0) {
        printf("  [SENDER ERROR] sendmsg: %s (errno=%d)\n", sc_strerror(errno), errno);
        return -1;
    }
    printf("  [SENDER] sendmsg(SCM_RIGHTS) returned %zd (expected >= 1)\n", sent);
    return 0;
}

/*
 * recv_one_fd: receive a single fd from a Unix-domain socket using SCM_RIGHTS.
 * The received fd is stored in *out_fd; caller must close it when done.
 * Returns 0 on success (a fd was received), -1 on failure (errno set),
 *  1 if no fd was present in the ancillary data (non-SCM_RIGHTS case).
 */
static int recv_one_fd(int sockfd, int *out_fd) {
    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    char dummy[1];

    msg.msg_control    = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;

    msg.msg_iov        = &(struct iovec){ .iov_base = dummy, .iov_len = sizeof(dummy) };
    msg.msg_iovlen     = 1;

    ssize_t received = recvmsg(sockfd, &msg, 0);
    if (received < 0) {
        printf("  [RECEIVER ERROR] recvmsg: %s (errno=%d)\n", sc_strerror(errno), errno);
        return -1;
    }
    printf("  [RECEIVER] recvmsg returned %zd (msg_flags=0x%x)\n",
           received, (unsigned)(msg.msg_flags));

    /* walk ancillary data for SCM_RIGHTS */
    int found_fd = -1;
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        printf("  [RECEIVER] cmsg level=%d type=%d len=%zu\n",
               cmsg->cmsg_level, cmsg->cmsg_type, cmsg->cmsg_len);
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            size_t fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            printf("  [RECEIVER] SCM_RIGHTS found, %zu fd(s) in ancillary data\n", fd_count);
            if (fd_count > 0) {
                memcpy(&found_fd, CMSG_DATA(cmsg), sizeof(int));
            }
            break;  /* take the first SCM_RIGHTS block */
        }
    }

    if (found_fd < 0) {
        printf("  [RECEIVER] No SCM_RIGHTS fd found in ancillary data.\n");
        return 1;
    }

    *out_fd = found_fd;
    return 0;
}

/* file-scope so stage4_recv_fd can reference it for diagnostics */
static int g_sender_fd = -1;

/* ---- Test stages ---- */

static int stage1_create_socketpair(int sv[2]) {
    printf("\n=== Stage 1: socketpair(AF_UNIX, SOCK_STREAM, 0) ===\n");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        printf("  [FAIL] socketpair: %s (errno=%d)\n", sc_strerror(errno), errno);
        return -1;
    }
    printf("  [PASS] socketpair OK: fds=[%d, %d]\n", sv[0], sv[1]);
    return 0;
}

static int stage2_create_temp_file(const char *path) {
    printf("\n=== Stage 2: create temp file and write known payload ===\n");

    /* unlink first so we definitely start fresh */
    unlink(path);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  [FAIL] open(%s, O_WRONLY|O_CREAT|O_TRUNC): %s (errno=%d)\n",
               path, sc_strerror(errno), errno);
        return -1;
    }
    printf("  [PASS] opened temp file: fd=%d path='%s'\n", fd, path);

    ssize_t written = write(fd, PAYLOAD, PAYLOAD_LEN);
    if (written < 0) {
        printf("  [FAIL] write: %s (errno=%d)\n", sc_strerror(errno), errno);
        close(fd);
        return -1;
    }
    if ((size_t)written != PAYLOAD_LEN) {
        printf("  [FAIL] write: only %zd of %zu bytes written\n", written, (size_t)PAYLOAD_LEN);
        close(fd);
        return -1;
    }
    printf("  [PASS] wrote %zd bytes: \"%s\"\n", written, PAYLOAD);

    /* sync to ensure data hits storage */
    fsync(fd);

    /* seek to beginning so sender can re-read if needed */
    off_t pos = lseek(fd, 0, SEEK_SET);
    if (pos < 0) {
        printf("  [FAIL] lseek: %s (errno=%d)\n", sc_strerror(errno), errno);
        close(fd);
        return -1;
    }
    printf("  [PASS] lseek to SEEK_SET; current pos=%jd\n", (intmax_t)pos);

    return fd;
}

static int stage3_send_fd(int sockfd, int file_fd) {
    g_sender_fd = file_fd;
    printf("\n=== Stage 3: sendmsg(SCM_RIGHTS) sending file fd %d ===\n", file_fd);
    int ret = send_one_fd(sockfd, file_fd);
    if (ret < 0) {
        printf("  [FAIL] send_one_fd returned %d\n", ret);
        return -1;
    }
    printf("  [PASS] send_one_fd succeeded.\n");
    return 0;
}

static int stage4_recv_fd(int sockfd, int *out_recv_fd) {
    printf("\n=== Stage 4: recvmsg(SCM_RIGHTS) receiving fd ===\n");
    int ret = recv_one_fd(sockfd, out_recv_fd);
    if (ret < 0) {
        printf("  [FAIL] recv_one_fd returned %d\n", ret);
        return -1;
    }
    if (ret == 1) {
        printf("  [FAIL] recvmsg returned but no SCM_RIGHTS fd was present.\n");
        return -1;
    }
    printf("  [PASS] received fd: %d\n", *out_recv_fd);

    /* Additional fd diagnostics */
    struct stat st_recv;
    if (fstat(*out_recv_fd, &st_recv) < 0) {
        printf("  [DIAG] fstat(received_fd=%d): %s (errno=%d)\n",
               *out_recv_fd, sc_strerror(errno), errno);
    } else {
        printf("  [DIAG] fstat(received_fd=%d): OK  mode=0%o size=%jd\n",
               *out_recv_fd, (unsigned)(st_recv.st_mode),
               (intmax_t)st_recv.st_size);
    }

    /* Also check that original file_fd is still valid */
    struct stat st_orig;
    printf("  [DIAG] original file_fd=%d still valid check:\n", g_sender_fd);
    if (g_sender_fd >= 0 && fstat(g_sender_fd, &st_orig) < 0) {
        printf("  [DIAG]   fstat(original): %s (errno=%d)\n",
               sc_strerror(errno), errno);
    } else {
        printf("  [DIAG]   fstat(original): OK  mode=0%o size=%jd\n",
               (unsigned)(st_orig.st_mode), (intmax_t)st_orig.st_size);
    }

    return 0;
}

static int stage5_read_and_verify(int recv_fd) {
    printf("\n=== Stage 5: read from received fd and verify payload ===\n");

    char buf[256];
    ssize_t n = read(recv_fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("  [FAIL] read from received fd: %s (errno=%d)\n",
               sc_strerror(errno), errno);
        /* Additional read-family diagnostics */
        ssize_t pn = pread(recv_fd, buf, sizeof(buf) - 1, 0);
        printf("  [DIAG] pread(received_fd): %s (errno=%d)\n",
               sc_strerror(errno), errno);
        off_t lo = lseek(recv_fd, 0, SEEK_CUR);
        printf("  [DIAG] lseek(received_fd,SEEK_CUR): %jd errno=%d\n",
               (intmax_t)lo, (lo < 0) ? errno : 0);
        int fl = fcntl(recv_fd, F_GETFL, 0);
        printf("  [DIAG] fcntl(received_fd,F_GETFL): 0x%x errno=%d\n",
               fl, (fl < 0) ? errno : 0);
        return -1;
    }
    buf[n] = '\0';
    printf("  read %zd bytes from received fd: \"%s\"\n", n, buf);

    if ((size_t)n != PAYLOAD_LEN) {
        printf("  [FAIL] length mismatch: read %zd, expected %zu\n", n, (size_t)PAYLOAD_LEN);
        return -1;
    }
    if (memcmp(buf, PAYLOAD, PAYLOAD_LEN) != 0) {
        printf("  [FAIL] payload mismatch.\n");
        printf("    expected: \"%s\"\n", PAYLOAD);
        printf("    got:      \"%s\"\n", buf);
        return -1;
    }

    printf("  [PASS] payload verified exactly.\n");
    return 0;
}

/* ---- Bonus: device fd (character device) test ---- */
/* Test SCM_RIGHTS with a character-device fd (/dev/null opened O_RDWR).
 * This is the closest proxy for a DMAbuf/PRIME device fd on Linux/QNX.
 *
 * DMAbuf/PRIME fds on Linux are backed by /dev/dri/renderD* character devices.
 * Phase 1B-scmrights showed regular-file fds are broken (EBADF on read).
 * This test determines whether character-device fds work with SCM_RIGHTS.
 */
static int test_device_scmrights(void) {
    printf("\n");
    print_sep();
    printf("  BONUS TEST: SCM_RIGHTS with character-device fd (/dev/null O_RDWR)\n");
    print_sep();

    /* Step 1: create Unix-domain socket pair */
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        printf("  [DEVICE] socketpair: %s (errno=%d)\n", sc_strerror(errno), errno);
        return -1;
    }
    printf("  [DEVICE] socketpair OK: fds=[%d,%d]\n", sv[0], sv[1]);

    /* Step 2: open /dev/null O_RDWR */
    int devnull_fd = open("/dev/null", O_RDWR);
    if (devnull_fd < 0) {
        printf("  [DEVICE] open(/dev/null, O_RDWR): %s (errno=%d)\n",
               sc_strerror(errno), errno);
        close(sv[0]); close(sv[1]);
        return -1;
    }
    printf("  [DEVICE] opened /dev/null O_RDWR: fd=%d\n", devnull_fd);

    /* Verify the fd is a character device via fstat */
    struct stat st_orig;
    if (fstat(devnull_fd, &st_orig) < 0) {
        printf("  [DEVICE] fstat(orig): %s (errno=%d)\n", sc_strerror(errno), errno);
        close(devnull_fd); close(sv[0]); close(sv[1]);
        return -1;
    }
    printf("  [DEVICE] fstat(orig): mode=0%o (S_ISCHR=%d, S_ISBLK=%d, S_ISREG=%d)\n",
           (unsigned)(st_orig.st_mode),
           S_ISCHR(st_orig.st_mode), S_ISBLK(st_orig.st_mode), S_ISREG(st_orig.st_mode));

    /* Step 3: verify we can write to /dev/null (baseline sanity check) */
    const char *test_bytes = "DEVICE_SCMRIGHTS_PROBE_2026_07_02";
    ssize_t wn = write(devnull_fd, test_bytes, strlen(test_bytes));
    printf("  [DEVICE] write(orig, %zu bytes to /dev/null): %zd errno=%d\n",
           strlen(test_bytes), wn, (wn < 0) ? errno : 0);
    if (wn < 0) {
        printf("  [DEVICE] baseline write to /dev/null failed — unexpected!\n");
        close(devnull_fd); close(sv[0]); close(sv[1]);
        return -1;
    }
    printf("  [DEVICE] baseline write to /dev/null: PASS (write returns %zd)\n", wn);

    /* Also try read (returns 0 / EOF on /dev/null — this is correct) */
    char rbuf[8];
    ssize_t rn = read(devnull_fd, rbuf, sizeof(rbuf));
    printf("  [DEVICE] read(orig, /dev/null): %zd errno=%d\n",
           rn, (rn < 0) ? errno : 0);

    /* Step 4: send /dev/null fd via SCM_RIGHTS */
    printf("  [DEVICE] sending /dev/null fd=%d via SCM_RIGHTS...\n", devnull_fd);
    if (send_one_fd(sv[0], devnull_fd) < 0) {
        close(devnull_fd); close(sv[0]); close(sv[1]);
        return -1;
    }
    close(devnull_fd);  /* close original after send */
    devnull_fd = -1;

    /* Step 5: receive the fd */
    int recv_dev_fd = -1;
    if (recv_one_fd(sv[1], &recv_dev_fd) != 0 || recv_dev_fd < 0) {
        printf("  [DEVICE] recv_one_fd failed or returned no fd\n");
        close(sv[0]); close(sv[1]);
        return -1;
    }
    printf("  [DEVICE] received device fd: %d\n", recv_dev_fd);

    /* Step 6: verify received fd with fstat */
    struct stat st_recv;
    if (fstat(recv_dev_fd, &st_recv) < 0) {
        printf("  [DEVICE] fstat(received_fd=%d): FAIL %s (errno=%d)\n",
               recv_dev_fd, sc_strerror(errno), errno);
        close(recv_dev_fd); close(sv[0]); close(sv[1]);
        return -1;
    }
    printf("  [DEVICE] fstat(received_fd=%d): OK mode=0%o (S_ISCHR=%d, S_ISBLK=%d, S_ISREG=%d)\n",
           recv_dev_fd, (unsigned)(st_recv.st_mode),
           S_ISCHR(st_recv.st_mode), S_ISBLK(st_recv.st_mode), S_ISREG(st_recv.st_mode));

    /* Step 7: fcntl F_GETFL on received fd */
    int flags = fcntl(recv_dev_fd, F_GETFL);
    printf("  [DEVICE] fcntl(received_fd=%d, F_GETFL): 0x%x errno=%d\n",
           recv_dev_fd, (unsigned)(flags), (flags < 0) ? errno : 0);

    /* Step 8: critical test — write to received /dev/null fd */
    /* For a writable /dev/null fd this MUST succeed (returns byte count, no error). */
    const char *send_bytes = "DEVICE_PROBE_TEST_DATA_123";
    ssize_t wn_recv = write(recv_dev_fd, send_bytes, strlen(send_bytes));
    printf("  [DEVICE] write(received_fd=%d, %zu bytes): %zd errno=%d\n",
           recv_dev_fd, strlen(send_bytes), wn_recv, (wn_recv < 0) ? errno : 0);

    /* Step 9: read from received fd (should return 0 / EOF for /dev/null — this is correct) */
    char rbuf_recv[8];
    ssize_t rn_recv = read(recv_dev_fd, rbuf_recv, sizeof(rbuf_recv));
    printf("  [DEVICE] read(received_fd=%d): %zd errno=%d\n",
           recv_dev_fd, rn_recv, (rn_recv < 0) ? errno : 0);

    /* Step 10: lseek on received fd (should succeed or return -1 with ESPIPE) */
    off_t lo = lseek(recv_dev_fd, 0, SEEK_CUR);
    printf("  [DEVICE] lseek(received_fd=%d, SEEK_CUR): %jd errno=%d\n",
           recv_dev_fd, (intmax_t)lo, (lo < 0) ? errno : 0);

    close(recv_dev_fd);
    close(sv[0]); close(sv[1]);

    /* ---- Verdict ---- */
    /* The definitive test: write to received /dev/null fd must succeed. */
    if (wn_recv == (ssize_t)strlen(send_bytes)) {
        printf("  [DEVICE] write to received /dev/null fd: PASS (%zd bytes)\n", wn_recv);
        printf("  [DEVICE RESULT] DEVICE fd SCM_RIGHTS: PASS\n");
        return 0;
    } else {
        printf("  [DEVICE] write to received /dev/null fd: FAIL (returned %zd)\n", wn_recv);
        printf("  [DEVICE RESULT] DEVICE fd SCM_RIGHTS: FAIL\n");
        return -1;
    }
}

/* ---- Bonus: pipe fd test ---- */
/* Test SCM_RIGHTS with a pipe fd, where the sender writes data and
 * the receiver reads it from the transferred fd. */
static int test_pipe_scmrights(void) {
    printf("\n");
    print_sep();
    printf("  BONUS TEST: SCM_RIGHTS with pipe fd\n");
    print_sep();

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        printf("  [BONUS] socketpair: %s (errno=%d)\n", sc_strerror(errno), errno);
        return -1;
    }
    printf("  [BONUS] socketpair OK: fds=[%d,%d]\n", sv[0], sv[1]);

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        printf("  [BONUS] pipe: %s (errno=%d)\n", sc_strerror(errno), errno);
        close(sv[0]); close(sv[1]);
        return -1;
    }
    printf("  [BONUS] pipe OK: read_fd=%d write_fd=%d\n", pipefd[0], pipefd[1]);

    /* write to pipe so receiver can read */
    const char *pipe_payload = "PIPE_PONG_30_BYTES_HERE_YES_REALLY";
    ssize_t wn = write(pipefd[1], pipe_payload, strlen(pipe_payload));
    printf("  [BONUS] wrote %zd bytes to pipe write end\n", wn);
    /* close write end so read end gets EOF */
    close(pipefd[1]);

    /* send read end of pipe via SCM_RIGHTS */
    printf("  [BONUS] sending pipe read_fd=%d via SCM_RIGHTS...\n", pipefd[0]);
    if (send_one_fd(sv[0], pipefd[0]) < 0) {
        close(pipefd[0]); close(sv[0]); close(sv[1]);
        return -1;
    }
    close(pipefd[0]);  /* close original after send */

    /* receive pipe read fd */
    int recv_pipe_fd = -1;
    if (recv_one_fd(sv[1], &recv_pipe_fd) != 0 || recv_pipe_fd < 0) {
        printf("  [BONUS] recv_one_fd failed or returned no fd\n");
        close(sv[0]); close(sv[1]);
        return -1;
    }
    printf("  [BONUS] received pipe fd: %d\n", recv_pipe_fd);

    /* try to read from received pipe fd */
    char rbuf[64] = {0};
    ssize_t rn = read(recv_pipe_fd, rbuf, sizeof(rbuf) - 1);
    printf("  [BONUS] read(received_pipe_fd=%d): %zd bytes, errno=%d (%s)\n",
           recv_pipe_fd, rn, (rn < 0) ? errno : 0,
           (rn < 0) ? sc_strerror(errno) : "OK");
    if (rn > 0) {
        rbuf[rn] = '\0';
        printf("  [BONUS] received content: \"%s\"\n", rbuf);
    }

    /* also fstat on received pipe fd */
    struct stat st_pipe;
    if (fstat(recv_pipe_fd, &st_pipe) < 0) {
        printf("  [BONUS] fstat(received_pipe_fd=%d): FAIL %s (errno=%d)\n",
               recv_pipe_fd, sc_strerror(errno), errno);
    } else {
        printf("  [BONUS] fstat(received_pipe_fd=%d): OK mode=0%o size=%jd\n",
               recv_pipe_fd, (unsigned)(st_pipe.st_mode), (intmax_t)st_pipe.st_size);
    }

    close(recv_pipe_fd);
    close(sv[0]); close(sv[1]);

    if (rn == (ssize_t)strlen(pipe_payload)) {
        printf("  [BONUS RESULT] PIPE fd SCM_RIGHTS: PASS\n");
        return 0;
    } else {
        printf("  [BONUS RESULT] PIPE fd SCM_RIGHTS: FAIL\n");
        return -1;
    }
}

/* ---- Main ---- */

int main(void) {
    int ret = 0;

    printf("\n");
    print_sep();
    printf("  QNX OOP-GPU Phase 1B-scmrights: SCM_RIGHTS fd-passing Probe\n");
    printf("  Built: " __DATE__ " " __TIME__ "\n");
    print_sep();
    printf("\n");

    const char *tmp_path = "/tmp/qnx_scmrights_probe_file.txt";

    int sv[2] = { -1, -1 };
    int file_fd = -1;
    int recv_fd = -1;

    /* Stage 1: socketpair */
    if (stage1_create_socketpair(sv) < 0) {
        ret = 1; goto cleanup;
    }

    /* Stage 2: create temp file */
    file_fd = stage2_create_temp_file(tmp_path);
    if (file_fd < 0) {
        ret = 1; goto cleanup;
    }
    /* Stage 3: send fd over socket 0 */
    if (stage3_send_fd(sv[0], file_fd) < 0) {
        ret = 1; goto cleanup;
    }
    /* Stage 4: receive fd from socket 1 */
    if (stage4_recv_fd(sv[1], &recv_fd) < 0) {
        ret = 1; goto cleanup;
    }
    /* Close original fd right after sending to isolate fd-table vs file-description issue */
    printf("\n=== Stage 4b: close original fd after send ===\n");
    if (close(file_fd) < 0) {
        printf("  [DIAG] close(original_fd): %s (errno=%d)\n",
               sc_strerror(errno), errno);
    } else {
        printf("  [DIAG] close(original_fd=%d) succeeded (original now closed)\n", file_fd);
    }
    file_fd = -1;  /* mark closed so cleanup doesn't double-close */

    /* Stage 5: read and verify payload from received fd */
    if (stage5_read_and_verify(recv_fd) < 0) {
        ret = 1;
        /* fall through: run bonus test even after main test failure */
    } else {
        ret = 0;
    }
    /* close received fd before bonus test to avoid fd-number confusion */
    if (recv_fd >= 0) { close(recv_fd); recv_fd = -1; }

    /* ---- Bonus test: character-device fd (/dev/null) ---- */
    int device_ret = test_device_scmrights();
    if (device_ret != 0) {
        printf("\n  *** DEVICE fd SCM_RIGHTS: FAIL ***\n");
        ret = 1;
    }

    /* ---- Bonus test: pipe fd ---- */
    int bonus_ret = test_pipe_scmrights();
    if (bonus_ret != 0 && ret == 0) {
        /* main test passed but bonus failed; upgrade to partial-fail */
        ret = 1;
    }

    /* ---- Final verdict ---- */
    if (ret == 0) {
        print_sep();
        printf("  FINAL RESULT: All SCM_RIGHTS fd passing tests PASSED.\n");
        printf("  PASS\n");
        print_sep();
    } else {
        print_sep();
        printf("  FINAL RESULT: SCM_RIGHTS fd passing has one or more FAILURES.\n");
        printf("  See above diagnostics for details.\n");
        print_sep();
    }
    /* ---- Print per-test summary ---- */
    printf("\n");
    printf("  SCM_RIGHTS TEST SUMMARY:\n");
    printf("    Regular-file fd: %s (known broken on QNX/QEMU)\n",
           (ret == 0 && device_ret == 0 && bonus_ret == 0) ? "not-run" : "FAIL");
    printf("    Character-device fd (/dev/null O_RDWR): %s\n",
           device_ret == 0 ? "PASS" : "FAIL");
    printf("    Pipe fd: %s\n",
           bonus_ret == 0 ? "PASS" : "FAIL");
    printf("\n");

cleanup:
    /* close fds, report errno from any close failures */
    if (file_fd >= 0) {
        if (close(file_fd) < 0)
            printf("  [CLEANUP WARNING] close(file_fd): %s (errno=%d)\n",
                   sc_strerror(errno), errno);
    }
    if (recv_fd >= 0) {
        if (close(recv_fd) < 0)
            printf("  [CLEANUP WARNING] close(recv_fd): %s (errno=%d)\n",
                   sc_strerror(errno), errno);
    }
    if (sv[0] >= 0) {
        if (close(sv[0]) < 0)
            printf("  [CLEANUP WARNING] close(sv[0]): %s (errno=%d)\n",
                   sc_strerror(errno), errno);
    }
    if (sv[1] >= 0) {
        if (close(sv[1]) < 0)
            printf("  [CLEANUP WARNING] close(sv[1]): %s (errno=%d)\n",
                   sc_strerror(errno), errno);
    }

    /* remove temp file */
    if (unlink(tmp_path) < 0 && errno != ENOENT)
        printf("  [CLEANUP WARNING] unlink(%s): %s (errno=%d)\n",
               tmp_path, sc_strerror(errno), errno);

    if (ret == 0) {
        printf("\n  EXIT: PASS (0)\n");
    } else {
        printf("\n  EXIT: FAIL (%d)\n", ret);
    }
    return ret;
}
