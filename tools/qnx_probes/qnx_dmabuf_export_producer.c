/*
 * qnx_dmabuf_export_producer.c
 *
 * Phase 1B-dmabuf-import producer: true DMAbuf export + SCM_RIGHTS fd passing.
 *
 * Architecture: fork() into GPU child + socket parent.
 *   - Parent: socket server. Listens on Unix-domain socket, accepts consumer,
 *     then relays header + SCM_RIGHTS FDs received from GPU child.
 *   - GPU child: creates Mesa DRM image (Path A), exports DMAbuf plane FDs,
 *     sends header + FDs to parent via socketpair, exits.
 *
 * Proven export path (Phase 1B-exportonly, 2026-07-03):
 *   eglCreateDRMImageMESA + eglExportDMABUFImageMESA produces real DMAbuf fds
 *   under QEMU virgl without entering the Mesa/QNX pbuffer crash path.
 *
 * Build:
 *   source ../out/qnx_release/qnx_env.sh
 *   qcc -Wall -Wextra -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_export_producer \
 *       tools/qnx_probes/qnx_dmabuf_export_producer.c \
 *       -lsocket -lscreen -lEGL -lGLESv2
 *
 * Run:
 *   (started by runner; see Phase 1B-dmabuf run command)
 *
 * This probe is standalone — no Chromium, no GN, no Ozone backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "qnx_dmabuf_ipc.h"

/* EGL */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* ---- DRM fourcc (drm_fourcc.h equivalent) ---- */
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888  0x34324241
#endif

/* ---- EGL_MESA_drm_image use flags ---- */
#ifndef EGL_DRM_BUFFER_USE_SCANOUT_MESA
#define EGL_DRM_BUFFER_USE_SCANOUT_MESA   0x00000001
#endif
#ifndef EGL_DRM_BUFFER_USE_SHARE_MESA
#define EGL_DRM_BUFFER_USE_SHARE_MESA     0x00000002
#endif

/* ---- EGL function pointers ---- */
static PFNEGLCREATEIMAGEKHRPROC        s_eglCreateImageKHR        = NULL;
static PFNEGLDESTROYIMAGEKHRPROC       s_eglDestroyImageKHR       = NULL;
static PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC s_eglExportDMABUFImageQueryMESA = NULL;
static PFNEGLEXPORTDMABUFIMAGEMESAPROC s_eglExportDMABUFImageMESA = NULL;
static PFNEGLCREATEDRMIMAGEMESAPROC   s_eglCreateDRMImageMESA   = NULL;

/* ---- Helpers ---- */
static void print_egl_error(const char *where) {
    EGLint err = eglGetError();
    const char *name;
    switch (err) {
        case EGL_SUCCESS:             name = "EGL_SUCCESS"; break;
        case EGL_NOT_INITIALIZED:     name = "EGL_NOT_INITIALIZED"; break;
        case EGL_BAD_ACCESS:          name = "EGL_BAD_ACCESS"; break;
        case EGL_BAD_ALLOC:           name = "EGL_BAD_ALLOC"; break;
        case EGL_BAD_ATTRIBUTE:       name = "EGL_BAD_ATTRIBUTE"; break;
        case EGL_BAD_CONFIG:          name = "EGL_BAD_CONFIG"; break;
        case EGL_BAD_CONTEXT:         name = "EGL_BAD_CONTEXT"; break;
        case EGL_BAD_CURRENT_SURFACE: name = "EGL_BAD_CURRENT_SURFACE"; break;
        case EGL_BAD_DISPLAY:         name = "EGL_BAD_DISPLAY"; break;
        case EGL_BAD_MATCH:           name = "EGL_BAD_MATCH"; break;
        case EGL_BAD_NATIVE_PIXMAP:   name = "EGL_BAD_NATIVE_PIXMAP"; break;
        case EGL_BAD_NATIVE_WINDOW:   name = "EGL_BAD_NATIVE_WINDOW"; break;
        case EGL_BAD_PARAMETER:       name = "EGL_BAD_PARAMETER"; break;
        case EGL_BAD_SURFACE:         name = "EGL_BAD_SURFACE"; break;
        case EGL_CONTEXT_LOST:        name = "EGL_CONTEXT_LOST"; break;
        default:                      name = "(unknown)"; break;
    }
    fprintf(stderr, "[PRODUCER ERROR] %s: EGL error 0x%x (%s)\n", where, err, name);
}

static const char *fourcc_to_str(uint32_t fourcc) {
    static char buf[5];
    buf[0] = (fourcc >> 0)  & 0xff;
    buf[1] = (fourcc >> 8)  & 0xff;
    buf[2] = (fourcc >> 16) & 0xff;
    buf[3] = (fourcc >> 24) & 0xff;
    buf[4] = '\0';
    return buf;
}

/* ============================================================================
 * GPU child: creates DRM image (Path A), exports DMAbuf, sends header + FDs
 * to parent via socketpair.  Runs in its own process after fork().
 * ============================================================================ */
static int gpu_child_main(int sv_write_fd) {
    printf("\n[GPU CHILD] Starting Path A DMAbuf export...\n");

    /* ---- EGL init ---- */
    EGLDisplay egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "[GPU CHILD ERROR] Cannot get EGLDisplay\n");
        return 2;
    }

    EGLint maj = 0, min = 0;
    if (!eglInitialize(egl_dpy, &maj, &min)) {
        print_egl_error("eglInitialize");
        return 2;
    }
    printf("[GPU CHILD] EGL %d.%d initialized (display=%p)\n",
           maj, min, (void*)egl_dpy);
    printf("[GPU CHILD]   vendor: %s\n",
           eglQueryString(egl_dpy, EGL_VENDOR) ? : "(null)");

    /* ---- Resolve function pointers ---- */
    s_eglCreateImageKHR =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    s_eglDestroyImageKHR =
        (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    s_eglExportDMABUFImageQueryMESA =
        (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    s_eglExportDMABUFImageMESA =
        (PFNEGLEXPORTDMABUFIMAGEMESAPROC)eglGetProcAddress("eglExportDMABUFImageMESA");
    s_eglCreateDRMImageMESA =
        (PFNEGLCREATEDRMIMAGEMESAPROC)eglGetProcAddress("eglCreateDRMImageMESA");

    /* ---- Phase 3: Extension check ---- */
    const char *egl_exts = eglQueryString(egl_dpy, EGL_EXTENSIONS);
    int has_drm_img = egl_exts && strstr(egl_exts, "EGL_MESA_drm_image") != NULL;
    int has_exp = egl_exts && strstr(egl_exts, "EGL_MESA_image_dma_buf_export") != NULL;

    printf("[GPU CHILD]   EGL_MESA_drm_image:           %s\n",
           has_drm_img ? "[PRESENT]" : "[ABSENT]");
    printf("[GPU CHILD]   EGL_MESA_image_dma_buf_export: %s\n",
           has_exp ? "[PRESENT]" : "[ABSENT]");
    printf("[GPU CHILD]   eglCreateDRMImageMESA:        %s\n",
           s_eglCreateDRMImageMESA ? "[RESOLVED]" : "[NOT FOUND]");
    printf("[GPU CHILD]   eglExportDMABUFImageMESA:    %s\n",
           s_eglExportDMABUFImageMESA ? "[RESOLVED]" : "[NOT FOUND]");

    if (!has_drm_img || !s_eglCreateDRMImageMESA || !has_exp || !s_eglExportDMABUFImageMESA) {
        fprintf(stderr, "[GPU CHILD ERROR] Missing required extensions/functions for Path A.\n");
        eglTerminate(egl_dpy);
        return 1;
    }

    /* =========================================================================
     * Path A: eglCreateDRMImageMESA + eglExportDMABUFImageMESA
     * (No Screen, no GL, no pbuffer — proven in Phase 1B-exportonly)
     * ======================================================================== */
    printf("\n[GPU CHILD] === Path A: EGL_MESA_drm_image ===\n");

    EGLint drm_attrs[] = {
        /* key */ EGL_DRM_BUFFER_FORMAT_MESA,
        /* val */ 0x31D2, /* EGL_DRM_BUFFER_FORMAT_ARGB32_MESA */
        /* key */ EGL_DRM_BUFFER_USE_MESA,
        /* val */ (EGL_DRM_BUFFER_USE_SCANOUT_MESA | EGL_DRM_BUFFER_USE_SHARE_MESA),
        /* key */ EGL_WIDTH,
        /* val */ 64,
        /* key */ EGL_HEIGHT,
        /* val */ 64,
        EGL_NONE
    };

    EGLImageKHR egl_img = s_eglCreateDRMImageMESA(egl_dpy, drm_attrs);
    if (egl_img == EGL_NO_IMAGE_KHR) {
        print_egl_error("eglCreateDRMImageMESA");
        fprintf(stderr, "[GPU CHILD ERROR] Path A FAILED: eglCreateDRMImageMESA returned NULL.\n");
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[GPU CHILD]   eglCreateDRMImageMESA: created %p\n",
           (void*)(uintptr_t)egl_img);

    /* ---- Query DMAbuf metadata ---- */
    int query_n_planes = 0;
    int query_fourcc = 0;
    EGLuint64KHR query_modifier = 0;

    EGLBoolean qok = s_eglExportDMABUFImageQueryMESA(
        egl_dpy, egl_img, &query_fourcc, &query_n_planes, &query_modifier);
    if (!qok) {
        print_egl_error("eglExportDMABUFImageQueryMESA");
        fprintf(stderr, "[GPU CHILD ERROR] Path A export query FAILED.\n");
        s_eglDestroyImageKHR(egl_dpy, egl_img);
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[GPU CHILD]   Query OK: fourcc=0x%08x (%s), planes=%d, modifier=0x%llx\n",
           (unsigned)query_fourcc, fourcc_to_str((uint32_t)query_fourcc),
           query_n_planes,
           (unsigned long long)query_modifier);

    /* ---- Export DMAbuf plane FDs ---- */
    int export_fds[4]    = { -1, -1, -1, -1 };
    EGLint export_strides[4]  = { 0, 0, 0, 0 };
    EGLint export_offsets[4] = { 0, 0, 0, 0 };

    EGLBoolean eok = s_eglExportDMABUFImageMESA(
        egl_dpy, egl_img,
        export_fds, export_strides, export_offsets);
    if (!eok) {
        print_egl_error("eglExportDMABUFImageMESA");
        fprintf(stderr, "[GPU CHILD ERROR] Path A export FAILED.\n");
        s_eglDestroyImageKHR(egl_dpy, egl_img);
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[GPU CHILD]   eglExportDMABUFImageMESA: SUCCESS\n");

    int total_fds = 0;
    for (int i = 0; i < 4; i++) {
        if (export_fds[i] >= 0) {
            printf("[GPU CHILD]   Plane %d: fd=%d stride=%d offset=%d\n",
                   i, export_fds[i], export_strides[i], export_offsets[i]);
            total_fds++;
        }
    }
    printf("[GPU CHILD]   Total valid plane fds: %d\n", total_fds);

    if (total_fds == 0) {
        fprintf(stderr, "[GPU CHILD ERROR] Path A export produced zero valid fds.\n");
        s_eglDestroyImageKHR(egl_dpy, egl_img);
        eglTerminate(egl_dpy);
        return 1;
    }

    /* ---- Build IPC header ---- */
    qnx_dmabuf_ipc_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic     = QNX_DMABUF_PROBE_MAGIC;
    hdr.version   = QNX_DMABUF_PROBE_VERSION;
    hdr.width     = 64;
    hdr.height    = 64;
    hdr.fourcc    = (uint32_t)query_fourcc;
    hdr.n_planes  = (uint32_t)total_fds;
    hdr.exported  = 1;  /* 1 = DMAbuf export succeeded */
    /* stride/offset0: use plane 0 values for single-plane compatibility */
    hdr.stride    = (uint32_t)export_strides[0];
    hdr.offset0   = (uint32_t)export_offsets[0];
    hdr.modifier0_lo = (uint32_t)(query_modifier & 0xFFFFFFFFUL);

    printf("[GPU CHILD]   IPC header: %dx%d fourcc=0x%08x n_planes=%u exported=1\n",
           hdr.width, hdr.height, hdr.fourcc, hdr.n_planes);
    printf("[GPU CHILD]   stride=%u offset0=%u modifier0=0x%08x\n",
           hdr.stride, hdr.offset0, hdr.modifier0_lo);

    /* ---- Send header + FDs to parent via socketpair ---- */
    /*
     * sv_write_fd is our (GPU child's) end of the socketpair.
     * We send the header as a regular send(), then the FDs via sendmsg(SCM_RIGHTS).
     * The parent receives them and forwards to the consumer.
     *
     * We use the already-established socket connection (sock_conn_fd) instead of
     * the socketpair because sock_conn_fd is already connected to the consumer.
     * But wait — sock_conn_fd was opened by the parent, not the child...
     *
     * Actually, sock_conn_fd is -1 in the child because it was opened by the parent.
     * We must use sv_write_fd (socketpair write end) to talk to the parent.
     * The parent will forward to the consumer.
     */
    printf("[GPU CHILD]   Sending header to parent via socketpair (fd=%d)...\n", sv_write_fd);

    ssize_t sent_hdr = send(sv_write_fd, &hdr, sizeof(hdr), MSG_NOSIGNAL);
    if (sent_hdr < 0) {
        fprintf(stderr, "[GPU CHILD ERROR] send(hdr) failed: %s\n", strerror(errno));
        goto gpu_child_cleanup;
    }
    printf("[GPU CHILD]   Sent %zd-byte header to parent.\n", sent_hdr);

    /* Send plane FDs via sendmsg(SCM_RIGHTS) through socketpair */
    printf("[GPU CHILD]   Sending %d plane fds via sendmsg(SCM_RIGHTS)...\n", total_fds);

    struct iovec iov;
    iov.iov_base = (void*)"PLANE_FDS";
    iov.iov_len  = 10;  /* dummy payload to carry CMSG */

    unsigned char cmsg_buf[CMSG_SPACE(sizeof(int) * 4)];
    memset(cmsg_buf, 0, sizeof(cmsg_buf));

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov     = &iov;
    msg.msg_iovlen  = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int) * total_fds);

    int *fd_ptr = (int*)CMSG_DATA(cmsg);
    for (int i = 0; i < total_fds; i++) {
        fd_ptr[i] = export_fds[i];
        printf("[GPU CHILD]     SCM_RIGHTS fd[%d] = %d\n", i, export_fds[i]);
    }

    ssize_t sent_fds = sendmsg(sv_write_fd, &msg, MSG_NOSIGNAL);
    if (sent_fds < 0) {
        fprintf(stderr, "[GPU CHILD ERROR] sendmsg(SCM_RIGHTS) failed: %s\n", strerror(errno));
        goto gpu_child_cleanup;
    }
    printf("[GPU CHILD]   sendmsg(SCM_RIGHTS) returned %zd.\n", sent_fds);

    /* Wait for parent acknowledgment via socketpair */
    printf("[GPU CHILD]   Waiting for parent relay acknowledgment...\n");
    struct pollfd pfd = {sv_write_fd, POLLIN, 0};
    int poll_r = poll(&pfd, 1, 30000);
    if (poll_r > 0) {
        char ack_buf[64] = {0};
        ssize_t r = recv(sv_write_fd, ack_buf, sizeof(ack_buf) - 1, 0);
        if (r > 0) {
            printf("[GPU CHILD]   Parent relay ack: %.*s\n", (int)r, ack_buf);
        }
    } else {
        printf("[GPU CHILD]   No ack from parent (poll_r=%d, errno=%d).\n", poll_r, errno);
    }

    printf("[GPU CHILD]   DMAbuf export complete. Exiting.\n");

gpu_child_cleanup:
    /* Close exported FDs after sendmsg (kernel transfers ownership on sendmsg success) */
    for (int i = 0; i < 4; i++) {
        if (export_fds[i] >= 0) {
            close(export_fds[i]);
            export_fds[i] = -1;
        }
    }
    /* Close socketpair write end */
    close(sv_write_fd);

    if (s_eglDestroyImageKHR && egl_img != EGL_NO_IMAGE_KHR)
        s_eglDestroyImageKHR(egl_dpy, egl_img);
    eglTerminate(egl_dpy);

    return 0;
}

/* ============================================================================
 * Main: fork into GPU child + socket parent
 *
 * Synchronization order:
 *   1. Parent creates socketpair (for GPU child → parent header+FD relay).
 *   2. Parent creates sync pipe (for parent → GPU child "start GPU work" signal).
 *   3. Parent forks GPU child.
 *   4. GPU child: blocks reading sync pipe (waiting for signal).
 *   5. Parent: creates listening socket, accepts consumer connection.
 *   6. Parent: writes "GO" to sync pipe, unblocking GPU child.
 *   7. GPU child: does GPU work, sends header+FDs to parent via socketpair.
 *   8. Parent: receives from socketpair, forwards to consumer, waits for ACK.
 *   9. Parent: writes ACK to GPU child.
 *   10. GPU child: exits.
 *   11. Parent: exits.
 * ============================================================================ */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("\n");
    printf("================================================================================\n");
    printf("  QNX OOP-GPU Phase 1B-dmabuf: DMAbuf Producer (Path A + SCM_RIGHTS)\n");
    printf("  Built: " __DATE__ " " __TIME__ "\n");
    printf("================================================================================\n\n");

    /* Suppress Mesa fatal errors */
    {
        const char *env = "MESA_NO_FATAL_ERROR=1";
        if (putenv((char*)env) == 0)
            printf("[SETUP] Mesa suppression: %s\n", env);
    }

    /* ---- Step 1: Socketpair for GPU child → parent header+FD relay ---- */
    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        fprintf(stderr, "[PRODUCER ERROR] socketpair() failed: %s\n", strerror(errno));
        return 1;
    }
    printf("[SETUP] socketpair: sv[0]=%d (parent read), sv[1]=%d (GPU child write)\n",
           sv[0], sv[1]);

    /* ---- Step 2: Sync socketpair (SOCK_DGRAM) for parent → GPU child signal ---- */
    /*
     * Using SOCK_DGRAM socketpair instead of pipe() to avoid any QNX pipe inheritance
     * edge cases. SOCK_DGRAM is reliable for small datagram sync messages.
     * sv[0] = parent's end, sv[1] = GPU child's end.
     * Parent writes "GO" to sv[0]; GPU child reads from its end.
     */
    int sync_sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sync_sv) < 0) {
        fprintf(stderr, "[PRODUCER ERROR] socketpair(SOCK_DGRAM sync) failed: %s\n",
                strerror(errno));
        close(sv[0]); close(sv[1]);
        return 1;
    }
    printf("[SETUP] sync socketpair: sync_sv[0]=%d (parent send), sync_sv[1]=%d (GPU child recv)\n",
           sync_sv[0], sync_sv[1]);

    /* ---- Step 3: Fork GPU child ---- */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[PRODUCER ERROR] fork() failed: %s\n", strerror(errno));
        close(sv[0]); close(sv[1]);
        close(sync_sv[0]); close(sync_sv[1]);
        return 1;
    }

    if (pid == 0) {
        /* ---- GPU child ---- */
        close(sv[0]);      /* close read end — we write to sv[1] */
        close(sync_sv[0]);  /* close parent's send end — we recv from sync_sv[1] */

        printf("[GPU CHILD] Waiting for parent sync signal (fd=%d)...\n", sync_sv[1]);

        /* Block on sync socketpair until parent accepts consumer and signals "GO" */
        char go_buf[8] = {0};
        ssize_t r = recv(sync_sv[1], go_buf, sizeof(go_buf) - 1, 0);
        if (r <= 0) {
            fprintf(stderr, "[GPU CHILD ERROR] sync recv failed: %s\n", strerror(errno));
            close(sync_sv[1]); close(sv[1]);
            _exit(1);
            return 1;
        }
        printf("[GPU CHILD] Received sync signal: '%s'\n", go_buf);
        close(sync_sv[1]);  /* done reading sync socket */

        /* Do GPU work and send to parent via socketpair */
        int ret = gpu_child_main(sv[1]);
        printf("[GPU CHILD] Exiting with code %d.\n", ret);
        _exit(ret);
        return ret;  /* never reached */
    }

    /* ---- Parent: socket server ---- */
    close(sv[1]);         /* close write end — we read from sv[0] */
    close(sync_sv[1]);    /* close GPU child's recv end — we send to sync_sv[0] */

    /* ---- Step 5: Create listening socket and accept consumer ---- */
    printf("[PARENT] Creating server socket...\n");
    unlink(QNX_DMABUF_PROBE_SOCK_PATH);
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        fprintf(stderr, "[PARENT ERROR] socket(AF_UNIX) failed: %s\n", strerror(errno));
        close(sv[0]); close(sync_sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, QNX_DMABUF_PROBE_SOCK_PATH,
            sizeof(addr.sun_path) - 1);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[PARENT ERROR] bind(%s) failed: %s\n",
                QNX_DMABUF_PROBE_SOCK_PATH, strerror(errno));
        close(listen_fd); close(sv[0]); close(sync_sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    if (listen(listen_fd, 1) < 0) {
        fprintf(stderr, "[PARENT ERROR] listen() failed: %s\n", strerror(errno));
        close(listen_fd); close(sv[0]); close(sync_sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    printf("[PARENT] Listening on %s\n", QNX_DMABUF_PROBE_SOCK_PATH);

    printf("[PARENT] Waiting for consumer connection (30s timeout)...\n");
    struct pollfd lpfd = {listen_fd, POLLIN, 0};
    int poll_r = poll(&lpfd, 1, 30000);
    if (poll_r <= 0) {
        fprintf(stderr, "[PARENT ERROR] poll/accept timeout (poll_r=%d)\n", poll_r);
        close(listen_fd); close(sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    struct sockaddr_un from;
    socklen_t fromlen = sizeof(from);
    int consumer_fd = accept(listen_fd, (struct sockaddr*)&from, &fromlen);
    close(listen_fd);
    if (consumer_fd < 0) {
        fprintf(stderr, "[PARENT ERROR] accept() failed: %s\n", strerror(errno));
        close(sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    printf("[PARENT] Consumer connected (consumer_fd=%d).\n", consumer_fd);

    /* ---- Step 6: Signal GPU child to start GPU work ---- */
    printf("[PARENT] Signaling GPU child to start...\n");
    const char *go_msg = "GO";
    ssize_t w = send(sync_sv[0], go_msg, strlen(go_msg), MSG_NOSIGNAL);
    if (w < 0) {
        fprintf(stderr, "[PARENT ERROR] send(sync) failed: %s\n", strerror(errno));
        close(sync_sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    printf("[PARENT] Sync signal sent (send returned %zd).\n", w);
    close(sync_sv[0]);  /* done sending sync signal */

    /* ---- Step 8: Receive IPC header from GPU child via socketpair ---- */
    printf("[PARENT] Waiting for IPC header from GPU child (via socketpair)...\n");
    qnx_dmabuf_ipc_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    struct pollfd spfd = {sv[0], POLLIN, 0};
    poll_r = poll(&spfd, 1, 60000);
    if (poll_r <= 0) {
        fprintf(stderr, "[PARENT ERROR] poll(socketpair from GPU child) timeout: %d\n", poll_r);
        close(consumer_fd); close(sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }

    /* Receive header */
    ssize_t r = recv(sv[0], &hdr, sizeof(hdr), 0);
    if (r != sizeof(hdr)) {
        fprintf(stderr, "[PARENT ERROR] recv(hdr) from GPU child got %zd bytes\n", r);
        close(consumer_fd); close(sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    printf("[PARENT] Received IPC header: %dx%d fourcc=0x%08x n_planes=%u exported=%u\n",
           hdr.width, hdr.height, hdr.fourcc, hdr.n_planes, hdr.exported);

    /* Receive SCM_RIGHTS FDs from GPU child via socketpair */
    printf("[PARENT] Waiting for SCM_RIGHTS FDs from GPU child...\n");
    struct iovec siov;
    char dummy[64];
    siov.iov_base = dummy;
    siov.iov_len  = sizeof(dummy);

    unsigned char scmsg_buf[CMSG_SPACE(sizeof(int) * 4)];
    memset(scmsg_buf, 0, sizeof(scmsg_buf));

    struct msghdr smsg;
    memset(&smsg, 0, sizeof(smsg));
    smsg.msg_iov     = &siov;
    smsg.msg_iovlen  = 1;
    smsg.msg_control = scmsg_buf;
    smsg.msg_controllen = sizeof(scmsg_buf);

    ssize_t r_fds = recvmsg(sv[0], &smsg, 0);
    if (r_fds < 0) {
        fprintf(stderr, "[PARENT ERROR] recvmsg(SCM_RIGHTS from GPU child) failed: %s\n",
                strerror(errno));
        close(consumer_fd); close(sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }

    /* Extract FDs from GPU child's SCM_RIGHTS */
    int plane_fds[4] = { -1, -1, -1, -1 };
    int n_fds = 0;
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&smsg);
         cmsg != NULL;
         cmsg = CMSG_NXTHDR(&smsg, cmsg)) {
        if (cmsg->cmsg_type == SCM_RIGHTS &&
            (cmsg->cmsg_level == SOL_SOCKET || cmsg->cmsg_level == 65535)) {
            int *fds = (int*)CMSG_DATA(cmsg);
            size_t cnt = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < cnt && n_fds < 4; i++) {
                plane_fds[n_fds++] = fds[i];
                printf("[PARENT]   Received plane FD[%zu] from GPU child: %d\n", i, fds[i]);
            }
            break;
        }
    }
    printf("[PARENT] Total plane FDs received from GPU child: %d\n", n_fds);

    if (n_fds == 0) {
        fprintf(stderr, "[PARENT ERROR] GPU child sent zero FDs.\n");
        close(consumer_fd); close(sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }

    /* ---- Forward header + FDs to consumer via single sendmsg(SCM_RIGHTS) ---- */
    printf("[PARENT] Forwarding header + %d plane fds to consumer via single sendmsg(SCM_RIGHTS)...\n",
           n_fds);

    /*
     * Single sendmsg() carries both the fixed-size header as iovec payload
     * AND the SCM_RIGHTS ancillary fds.  This matches the consumer's
     * recv_with_fds() which does a single recvmsg() expecting header+fds
     * together — the ancillary fds are associated with the byte range from
     * this sendmsg, not from any prior send() call.
     */
    struct iovec ciov;
    ciov.iov_base = (void*)&hdr;
    ciov.iov_len  = sizeof(hdr);

    unsigned char ccmsg_buf[CMSG_SPACE(sizeof(int) * 4)];
    memset(ccmsg_buf, 0, sizeof(ccmsg_buf));

    struct msghdr cmsg_send;
    memset(&cmsg_send, 0, sizeof(cmsg_send));
    cmsg_send.msg_iov     = &ciov;
    cmsg_send.msg_iovlen  = 1;
    cmsg_send.msg_control = ccmsg_buf;
    cmsg_send.msg_controllen = sizeof(ccmsg_buf);

    struct cmsghdr *cmsg_to_send = CMSG_FIRSTHDR(&cmsg_send);
    cmsg_to_send->cmsg_level = SOL_SOCKET;
    cmsg_to_send->cmsg_type  = SCM_RIGHTS;
    cmsg_to_send->cmsg_len   = CMSG_LEN(sizeof(int) * n_fds);

    int *cfd_ptr = (int*)CMSG_DATA(cmsg_to_send);
    for (int i = 0; i < n_fds; i++) {
        cfd_ptr[i] = plane_fds[i];
        printf("[PARENT]   Forwarding plane FD[%d] = %d\n", i, plane_fds[i]);
    }

    ssize_t sent_total = sendmsg(consumer_fd, &cmsg_send, MSG_NOSIGNAL);
    if (sent_total < 0) {
        fprintf(stderr, "[PARENT ERROR] sendmsg(header+SCM_RIGHTS) to consumer failed: %s\n",
                strerror(errno));
        close(consumer_fd); close(sv[0]); kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return 1;
    }
    printf("[PARENT] sendmsg(header+SCM_RIGHTS) to consumer returned %zd (expected %zu).\n",
           sent_total, sizeof(hdr));

    /* ---- Wait for consumer ACK ---- */
    printf("[PARENT] Waiting for consumer ACK...\n");
    struct pollfd apfd = {consumer_fd, POLLIN, 0};
    poll_r = poll(&apfd, 1, 30000);
    if (poll_r > 0) {
        char ack_buf[64] = {0};
        ssize_t r = recv(consumer_fd, ack_buf, sizeof(ack_buf) - 1, 0);
        if (r > 0) {
            printf("[PARENT] Consumer ACK: %.*s\n", (int)r, ack_buf);
        }
    } else {
        printf("[PARENT] No consumer ACK received (poll_r=%d).\n", poll_r);
    }

    /* ---- Step 9: Forward ACK to GPU child ---- */
    printf("[PARENT] Forwarding ACK to GPU child via socketpair...\n");
    const char *fwd_ack = "PARENT_OK";
    send(sv[0], fwd_ack, strlen(fwd_ack), MSG_NOSIGNAL);

    /* ---- Cleanup ---- */
    close(consumer_fd);
    close(sv[0]);
    unlink(QNX_DMABUF_PROBE_SOCK_PATH);

    /* Wait for GPU child */
    int child_status = 0;
    waitpid(pid, &child_status, 0);
    int child_exit = WIFEXITED(child_status) ? WEXITSTATUS(child_status) : -1;
    printf("\n");
    printf("================================================================================\n");
    printf("  Producer probe complete. parent_exit=%d child_exit=%d\n", 0, child_exit);
    printf("================================================================================\n");
    return (child_exit != 0) ? 1 : 0;
}
