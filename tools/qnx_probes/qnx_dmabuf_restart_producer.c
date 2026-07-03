/*
 * qnx_dmabuf_restart_producer.c
 *
 * Phase 1B-crash producer: standalone DMAbuf producer that connects to a
 * browser-like consumer, exports one frame via Path A
 * (eglCreateDRMImageMESA + eglExportDMABUFImageMESA), sends header + FDs via
 * sendmsg(SCM_RIGHTS), waits for a simple ACK, then exits with a configurable
 * code (--exit-code=N).  Designed to run twice against the same consumer socket:
 *   1. First run exits non-zero to simulate GPU crash/death.
 *   2. Second run exits zero to simulate GPU restart.
 *
 * Architecture: flat single-process (no fork).  Connects to the consumer's
 * listening socket, performs DMAbuf export + IPC send, and exits.
 * This matches the producer-side process lifecycle in the final OOP-GPU
 * architecture where the GPU process is started, sends frames, and may die.
 *
 * Build:
 *   source ../out/qnx_release/qnx_env.sh
 *   export PATH="$QNX_HOST/usr/bin:$PATH"
 *   qcc -Wall -Wextra -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_restart_producer \
 *       tools/qnx_probes/qnx_dmabuf_restart_producer.c \
 *       -lsocket -lscreen -lEGL -lGLESv2
 *
 * Usage examples:
 *   ./qnx_dmabuf_restart_producer --exit-code=37    # simulate crash
 *   ./qnx_dmabuf_restart_producer --exit-code=0      # simulate clean restart
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

/* Shared IPC definitions */
#include "qnx_dmabuf_ipc.h"

/* EGL */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* ---- EGL_MESA_drm_image use flags ---- */
#ifndef EGL_DRM_BUFFER_USE_SCANOUT_MESA
#define EGL_DRM_BUFFER_USE_SCANOUT_MESA   0x00000001
#endif
#ifndef EGL_DRM_BUFFER_USE_SHARE_MESA
#define EGL_DRM_BUFFER_USE_SHARE_MESA     0x00000002
#endif

/* ---- Helpers ---- */
static void print_egl_error(const char *where) {
    EGLint err = eglGetError();
    const char *name;
    switch (err) {
        case EGL_SUCCESS:             name = "EGL_SUCCESS"; break;
        case EGL_NOT_INITIALIZED:      name = "EGL_NOT_INITIALIZED"; break;
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
 * Entry point
 * ============================================================================ */
int main(int argc, char *argv[]) {
    /* Default exit code = 0 (clean) */
    int exit_code = 0;
    const char *sock_path = "/tmp/qnx_dmabuf_restart.sock";

    /* Parse --exit-code=N and --socket=PATH */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--exit-code=", 12) == 0) {
            exit_code = atoi(argv[i] + 12);
        } else if (strncmp(argv[i], "--socket=", 9) == 0) {
            sock_path = argv[i] + 9;
        }
    }

    printf("\n");
    printf("================================================================================\n");
    printf("  QNX OOP-GPU Phase 1B-crash: DMAbuf Restart Producer\n");
    printf("  Built: " __DATE__ " " __TIME__ "\n");
    printf("  Socket: %s\n", sock_path);
    printf("  Exit code: %d\n", exit_code);
    printf("================================================================================\n\n");

    /* Suppress Mesa fatal errors */
    {
        const char *env = "MESA_NO_FATAL_ERROR=1";
        if (putenv((char*)env) == 0)
            printf("[SETUP] Mesa suppression: %s\n", env);
    }

    /* ---- EGL init ---- */
    EGLDisplay egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "[PRODUCER ERROR] Cannot get EGLDisplay\n");
        return 1;
    }

    EGLint maj = 0, min = 0;
    if (!eglInitialize(egl_dpy, &maj, &min)) {
        print_egl_error("eglInitialize");
        return 1;
    }
    printf("[SETUP] EGL %d.%d initialized (display=%p)\n", maj, min, (void*)egl_dpy);

    /* ---- Resolve function pointers ---- */
    PFNEGLCREATEDRMIMAGEMESAPROC create_drm =
        (PFNEGLCREATEDRMIMAGEMESAPROC)eglGetProcAddress("eglCreateDRMImageMESA");
    PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC query_dmabuf =
        (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    PFNEGLEXPORTDMABUFIMAGEMESAPROC export_dmabuf =
        (PFNEGLEXPORTDMABUFIMAGEMESAPROC)eglGetProcAddress("eglExportDMABUFImageMESA");

    if (!create_drm || !query_dmabuf || !export_dmabuf) {
        fprintf(stderr, "[PRODUCER ERROR] Missing required EGL_MESA_drm_image functions.\n");
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[SETUP]   eglCreateDRMImageMESA:     %s\n",
           create_drm ? "[RESOLVED]" : "[NOT FOUND]");
    printf("[SETUP]   eglExportDMABUFImageMESA:  %s\n",
           export_dmabuf ? "[RESOLVED]" : "[NOT FOUND]");

    /* ---- Path A: eglCreateDRMImageMESA ---- */
    printf("[PRODUCER] === Path A: eglCreateDRMImageMESA ===\n");
    EGLint drm_attrs[] = {
        EGL_DRM_BUFFER_FORMAT_MESA, 0x31D2, /* EGL_DRM_BUFFER_FORMAT_ARGB32_MESA */
        EGL_DRM_BUFFER_USE_MESA,    (EGL_DRM_BUFFER_USE_SCANOUT_MESA | EGL_DRM_BUFFER_USE_SHARE_MESA),
        EGL_WIDTH,  64,
        EGL_HEIGHT, 64,
        EGL_NONE
    };

    EGLImageKHR egl_img = create_drm(egl_dpy, drm_attrs);
    if (egl_img == EGL_NO_IMAGE_KHR) {
        print_egl_error("eglCreateDRMImageMESA");
        fprintf(stderr, "[PRODUCER ERROR] Path A FAILED: eglCreateDRMImageMESA returned NULL.\n");
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[PRODUCER]   eglCreateDRMImageMESA: created %p\n", (void*)(uintptr_t)egl_img);

    /* ---- Query DMAbuf metadata ---- */
    int query_n_planes = 0;
    int query_fourcc   = 0;
    EGLuint64KHR query_modifier = 0;

    EGLBoolean qok = query_dmabuf(egl_dpy, egl_img, &query_fourcc,
                                  &query_n_planes, &query_modifier);
    if (!qok) {
        print_egl_error("eglExportDMABUFImageQueryMESA");
        fprintf(stderr, "[PRODUCER ERROR] Path A export query FAILED.\n");
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[PRODUCER]   Query OK: fourcc=0x%08x (%s), planes=%d, modifier=0x%llx\n",
           (unsigned)query_fourcc, fourcc_to_str((uint32_t)query_fourcc),
           query_n_planes, (unsigned long long)query_modifier);

    /* ---- Export DMAbuf plane FDs ---- */
    int export_fds[4]     = { -1, -1, -1, -1 };
    EGLint export_strides[4]  = { 0, 0, 0, 0 };
    EGLint export_offsets[4] = { 0, 0, 0, 0 };

    EGLBoolean eok = export_dmabuf(egl_dpy, egl_img,
                                    export_fds, export_strides, export_offsets);
    if (!eok) {
        print_egl_error("eglExportDMABUFImageMESA");
        fprintf(stderr, "[PRODUCER ERROR] Path A export FAILED.\n");
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[PRODUCER]   eglExportDMABUFImageMESA: SUCCESS\n");

    int total_fds = 0;
    for (int i = 0; i < 4; i++) {
        if (export_fds[i] >= 0) {
            printf("[PRODUCER]   Plane %d: fd=%d stride=%d offset=%d\n",
                   i, export_fds[i], export_strides[i], export_offsets[i]);
            total_fds++;
        }
    }
    printf("[PRODUCER]   Total valid plane fds: %d\n", total_fds);

    if (total_fds == 0) {
        fprintf(stderr, "[PRODUCER ERROR] Path A export produced zero valid fds.\n");
        eglTerminate(egl_dpy);
        return 1;
    }

    /* ---- Build IPC header ---- */
    qnx_dmabuf_ipc_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic        = QNX_DMABUF_PROBE_MAGIC;
    hdr.version      = QNX_DMABUF_PROBE_VERSION;
    hdr.width        = 64;
    hdr.height       = 64;
    hdr.fourcc       = (uint32_t)query_fourcc;
    hdr.n_planes     = (uint32_t)total_fds;
    hdr.exported     = 1;  /* 1 = DMAbuf export succeeded */
    hdr.stride       = (uint32_t)export_strides[0];
    hdr.offset0      = (uint32_t)export_offsets[0];
    hdr.modifier0_lo = (uint32_t)(query_modifier & 0xFFFFFFFFUL);

    printf("[PRODUCER]   IPC header: %dx%d fourcc=0x%08x n_planes=%u exported=1\n",
           hdr.width, hdr.height, hdr.fourcc, hdr.n_planes);
    printf("[PRODUCER]   stride=%u offset0=%u modifier0=0x%08x\n",
           hdr.stride, hdr.offset0, hdr.modifier0_lo);

    /* ---- Connect to consumer socket ---- */
    printf("[SETUP] Connecting to consumer at %s...\n", sock_path);
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "[PRODUCER ERROR] socket(AF_UNIX) failed: %s\n", strerror(errno));
        for (int i = 0; i < 4; i++) {
            if (export_fds[i] >= 0) close(export_fds[i]);
        }
        eglTerminate(egl_dpy);
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[PRODUCER ERROR] connect(%s) failed: %s\n", sock_path, strerror(errno));
        close(sock_fd);
        for (int i = 0; i < 4; i++) {
            if (export_fds[i] >= 0) close(export_fds[i]);
        }
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[SETUP] Connected to consumer (sock_fd=%d).\n", sock_fd);

    /* ---- Send header + FDs via single sendmsg(SCM_RIGHTS) ---- */
    /*
     * Single sendmsg() carries the fixed-size header as iovec payload
     * AND the SCM_RIGHTS ancillary fds together.  This is the proven
     * framing from Phase 1B-dmabuf-import and Phase 1B-display.
     */
    struct iovec iov;
    iov.iov_base = (void*)&hdr;
    iov.iov_len  = sizeof(hdr);

    unsigned char cmsg_buf[CMSG_SPACE(sizeof(int) * 4)];
    memset(cmsg_buf, 0, sizeof(cmsg_buf));

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int) * total_fds);

    int *fd_ptr = (int*)CMSG_DATA(cmsg);
    for (int i = 0; i < total_fds; i++) {
        fd_ptr[i] = export_fds[i];
        printf("[PRODUCER]   SCM_RIGHTS fd[%d] = %d\n", i, export_fds[i]);
    }

    ssize_t sent = sendmsg(sock_fd, &msg, MSG_NOSIGNAL);
    if (sent < 0) {
        fprintf(stderr, "[PRODUCER ERROR] sendmsg(header+SCM_RIGHTS) failed: %s\n",
                strerror(errno));
        close(sock_fd);
        for (int i = 0; i < 4; i++) {
            if (export_fds[i] >= 0) close(export_fds[i]);
        }
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("[PRODUCER]   sendmsg(header+SCM_RIGHTS) returned %zd (expected %zu).\n",
           sent, sizeof(hdr));

    /* Close the FDs after sendmsg (kernel transfers ownership on success) */
    for (int i = 0; i < 4; i++) {
        if (export_fds[i] >= 0) {
            close(export_fds[i]);
            export_fds[i] = -1;
        }
    }
    eglTerminate(egl_dpy);

    /* ---- Wait for consumer ACK ---- */
    printf("[PRODUCER] Waiting for consumer ACK...\n");
    struct pollfd pfd = {sock_fd, POLLIN, 0};
    int poll_r = poll(&pfd, 1, 30000);
    if (poll_r > 0) {
        char ack_buf[64] = {0};
        ssize_t r = recv(sock_fd, ack_buf, sizeof(ack_buf) - 1, 0);
        if (r > 0) {
            printf("[PRODUCER] Consumer ACK: %.*s\n", (int)r, ack_buf);
        } else {
            printf("[PRODUCER] Consumer ACK recv: r=%zd errno=%d\n", r, errno);
        }
    } else {
        printf("[PRODUCER] No consumer ACK (poll_r=%d, timeout or error).\n", poll_r);
    }

    close(sock_fd);

    printf("\n");
    printf("================================================================================\n");
    printf("  Producer exiting with code %d.\n", exit_code);
    printf("================================================================================\n");
    return exit_code;
}
