/*
 * qnx_dmabuf_export_only_probe.c
 *
 * Phase 1B-exportonly microtask: isolate true DMAbuf export.
 *
 * Goal: determine whether QEMU virgl can produce at least one real DMAbuf fd
 * using eglExportDMABUFImageMESA without entering the known Mesa/QNX virgl
 * pbuffer crash path.
 *
 * Export path candidates (tried in order):
 *
 * Path A — EGL_MESA_drm_image (preferred, no GL involved):
 *   1. eglCreateDRMImageMESA creates a Mesa-internal DRM buffer as EGLImage.
 *      Bypasses Screen and GL entirely.
 *   2. eglExportDMABUFImageMESA exports DMAbuf plane fds from the DRM EGLImage.
 *
 * Path B — EGL_GL_TEXTURE_2D_KHR (DISABLED by default):
 *   1. GLES2 context + pbuffer surface (pbuffer is context holder only).
 *   2. GL texture + FBO render into it.
 *   3. eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_GL_TEXTURE_2D_KHR,
 *      tex, NULL) creates EGLImage from the GL texture.
 *   4. eglExportDMABUFImageMESA exports DMAbuf plane fds.
 *   NOTE: Path B crashes with SIGSEGV in Mesa/QNX virgl.  It is skipped
 *   by default.  Pass --allow-pbuffer-risk to attempt it, but this is
 *   NOT authorized by the Phase 1B plan and is for investigation only.
 *
 * If a real DMAbuf fd is obtained via Path A: fstat/fcntl/dump, then
 * close and exit.  No IPC/import/display.
 *
 * Build:
 *   source ../out/qnx_release/qnx_env.sh
 *   export PATH="$QNX_HOST/usr/bin:$PATH"
 *   qcc -Wall -Wextra -Vgcc_ntox86_64 \
 *       -o /tmp/qnx_dmabuf_export_only_probe_safe \
 *       tools/qnx_probes/qnx_dmabuf_export_only_probe.c \
 *       -lscreen -lEGL -lGLESv2
 *
 * Run (default — Path A only, no pbuffer):
 *   ./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_dmabuf_export_only_probe
 *
 * Run (Path B investigation — NOT authorized by plan):
 *   ./tools/qnx_run.sh --virgl --kill-existing -- \
 *       ./qnx_dmabuf_export_only_probe --allow-pbuffer-risk
 *
 * This probe is standalone — no Chromium, no GN, no Ozone backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <setjmp.h>
#include <signal.h>

/* QNX Screen */
#include <screen/screen.h>

/* EGL */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* ---- DRM fourcc (upstream kernel/linux/drm_fourcc.h) ---- */
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888  0x34324241
#endif
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888  0x34324258
#endif

/* ---- Use flags for EGL_MESA_drm_image ---- */
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
    fprintf(stderr, "[ERROR] %s: EGL error 0x%x (%s)\n", where, err, name);
}

static void print_gl_error(const char *where) {
    GLenum err = glGetError();
    const char *name;
    switch (err) {
        case GL_NO_ERROR:           name = "GL_NO_ERROR"; break;
        case GL_INVALID_ENUM:       name = "GL_INVALID_ENUM"; break;
        case GL_INVALID_VALUE:      name = "GL_INVALID_VALUE"; break;
        case GL_INVALID_OPERATION:  name = "GL_INVALID_OPERATION"; break;
        case GL_OUT_OF_MEMORY:      name = "GL_OUT_OF_MEMORY"; break;
        default:                    name = "(unknown)"; break;
    }
    fprintf(stderr, "[ERROR] %s: GL error 0x%x (%s)\n", where, err, name);
}

static const char *fourcc_to_str(uint32_t fourcc) {
    static char buf[5];
    buf[0] = (fourcc >> 0) & 0xff;
    buf[1] = (fourcc >> 8) & 0xff;
    buf[2] = (fourcc >> 16) & 0xff;
    buf[3] = (fourcc >> 24) & 0xff;
    buf[4] = '\0';
    return buf;
}

/* ---- EGL function pointers ---- */
static PFNEGLCREATEIMAGEKHRPROC        s_eglCreateImageKHR        = NULL;
static PFNEGLDESTROYIMAGEKHRPROC       s_eglDestroyImageKHR       = NULL;
static PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC s_eglExportDMABUFImageQueryMESA = NULL;
static PFNEGLEXPORTDMABUFIMAGEMESAPROC s_eglExportDMABUFImageMESA = NULL;
static PFNEGLCREATEDRMIMAGEMESAPROC   s_eglCreateDRMImageMESA   = NULL;

/* ---- Extension presence flags ---- */
static int s_has_egl_ext_image_dma_buf_import      = 0;
static int s_has_egl_ext_image_dma_buf_import_mods = 0;
static int s_has_egl_mesa_image_dma_buf_export     = 0;
static int s_has_egl_mesa_drm_image               = 0;
static int s_has_khr_gl_texture_2d                = 0;
static int s_has_khr_surfaceless_context          = 0;

/* ---- Command-line flags ---- */
static int s_allow_pbuffer_risk = 0;

/* ---- Signal guard for eglCreateImageKHR crash detection ---- */
static sigjmp_buf s_jump_buf;
static volatile sig_atomic_t s_sigbus_received = 0;

static void sigbus_handler(int sig) {
    (void)sig;
    s_sigbus_received = 1;
    siglongjmp(s_jump_buf, 1);
}

/* ---- Export helpers ---- */
static int do_export_and_report(EGLDisplay egl_dpy, EGLImageKHR egl_img) {
    /* Query DMAbuf metadata */
    printf("  [Export] Querying DMAbuf metadata...\n");
    int query_n_planes = 0;
    int query_fourcc = 0;
    EGLuint64KHR query_modifier = 0;

    if (s_eglExportDMABUFImageQueryMESA) {
        EGLBoolean ok = s_eglExportDMABUFImageQueryMESA(
            egl_dpy, egl_img, &query_fourcc, &query_n_planes, &query_modifier);
        if (ok) {
            printf("  [Export] Query OK: fourcc=0x%08x (%s), planes=%d, modifier=0x%llx\n",
                   (unsigned)query_fourcc,
                   fourcc_to_str((uint32_t)query_fourcc),
                   query_n_planes,
                   (unsigned long long)query_modifier);
        } else {
            print_egl_error("eglExportDMABUFImageQueryMESA");
            return 0;
        }
    } else {
        printf("  [Export] SKIPPED: eglExportDMABUFImageQueryMESA not resolved.\n");
    }

    /* Export DMAbuf */
    printf("  [Export] Calling eglExportDMABUFImageMESA...\n");

    int export_fds[4] = { -1, -1, -1, -1 };
    EGLint export_strides[4] = { 0, 0, 0, 0 };
    EGLint export_offsets[4] = { 0, 0, 0, 0 };

    EGLBoolean export_ok = s_eglExportDMABUFImageMESA(
        egl_dpy, egl_img,
        export_fds, export_strides, export_offsets);

    if (!export_ok) {
        print_egl_error("eglExportDMABUFImageMESA");
        EGLint err = eglGetError();
        printf("  [Export] Export failed with EGL error 0x%x.\n", err);
        return 0;
    }

    printf("  [Export] eglExportDMABUFImageMESA: SUCCESS\n");

    int total_fds = 0;
    for (int i = 0; i < 4; i++) {
        if (export_fds[i] >= 0) total_fds++;
    }
    printf("  [Export] Valid fds found: %d\n", total_fds);

    for (int i = 0; i < 4; i++) {
        if (export_fds[i] < 0) continue;
        printf("\n  --- Plane %d ---\n", i);
        printf("  fd      = %d\n", export_fds[i]);
        printf("  stride  = %d bytes\n", export_strides[i]);
        printf("  offset  = %d bytes\n", export_offsets[i]);

        struct stat st;
        if (fstat(export_fds[i], &st) == 0) {
            printf("  fstat   = OK\n");
            printf("    st_dev  = 0x%lx\n", (unsigned long)st.st_dev);
            printf("    st_ino  = %lu\n",   (unsigned long)st.st_ino);
            printf("    st_mode = 0%o (S_ISCHR=%d S_ISBLK=%d S_ISREG=%d)\n",
                   st.st_mode,
                   S_ISCHR(st.st_mode) ? 1 : 0,
                   S_ISBLK(st.st_mode) ? 1 : 0,
                   S_ISREG(st.st_mode) ? 1 : 0);
            printf("    st_size = %ld\n",  (long)st.st_size);
        } else {
            printf("  fstat   = FAIL (%s)\n", strerror(errno));
        }

        int flags = fcntl(export_fds[i], F_GETFL, 0);
        if (flags >= 0) {
            printf("  fcntl   = 0x%x (O_RDWR=%d O_WRONLY=%d O_NONBLOCK=%d)\n",
                   flags,
                   (flags & O_RDWR) ? 1 : 0,
                   (flags & O_WRONLY) && !(flags & O_RDWR) ? 1 : 0,
                   (flags & O_NONBLOCK) ? 1 : 0);
        } else {
            printf("  fcntl   = FAIL (%s)\n", strerror(errno));
        }
    }

    /* Close fds before reporting */
    for (int i = 0; i < 4; i++) {
        if (export_fds[i] >= 0) {
            close(export_fds[i]);
            export_fds[i] = -1;
        }
    }

    if (total_fds > 0) {
        printf("\n================================================================================\n");
        printf("  MILESTONE: TRUE DMAbuf EXPORT SUCCEEDED\n");
        printf("================================================================================\n");
        printf("  fourcc=0x%08x (%s), planes=%d, modifier=0x%llx\n",
               (unsigned)query_fourcc,
               fourcc_to_str((uint32_t)query_fourcc),
               total_fds,
               (unsigned long long)query_modifier);
        printf("  Closed exported fds (export-only, no IPC/import/display).\n");
        return 1; /* success */
    }
    return 0;
}

/* ---- Path A: EGL_MESA_drm_image (no GL, no Screen) ---- */
static int try_path_a(EGLDisplay egl_dpy, EGLImageKHR *out_img) {
    *out_img = EGL_NO_IMAGE_KHR;

    if (!s_has_egl_mesa_drm_image) {
        printf("  [Path A] EGL_MESA_drm_image extension: ABSENT\n");
        return 0;
    }
    if (!s_eglCreateDRMImageMESA) {
        printf("  [Path A] eglCreateDRMImageMESA: NOT RESOLVED\n");
        return 0;
    }

    printf("  [Path A] Attempting eglCreateDRMImageMESA...\n");
    printf("  [Path A] Attribute list:\n");

    /*
     * Corrected attribute list for EGL_MESA_drm_image.
     * Format: key, value, key, value, ..., EGL_NONE
     *
     * QNX EGL header constants (from eglext.h):
     *   EGL_DRM_BUFFER_FORMAT_MESA        0x31D0  <- KEY
     *   EGL_DRM_BUFFER_FORMAT_ARGB32_MESA 0x31D2 <- VALUE
     *   EGL_DRM_BUFFER_USE_MESA           0x31D1  <- KEY
     *   EGL_DRM_BUFFER_USE_SCANOUT_MESA   0x0001  <- VALUE
     *   EGL_DRM_BUFFER_USE_SHARE_MESA     0x0002  <- VALUE
     *   EGL_WIDTH  (EGL 1.5)              0x3057
     *   EGL_HEIGHT (EGL 1.5)              0x3058
     */
    EGLint drm_attrs[] = {
        /* key */ EGL_DRM_BUFFER_FORMAT_MESA,
        /* val */ EGL_DRM_BUFFER_FORMAT_ARGB32_MESA,
        /* key */ EGL_DRM_BUFFER_USE_MESA,
        /* val */ (EGL_DRM_BUFFER_USE_SCANOUT_MESA |
                  EGL_DRM_BUFFER_USE_SHARE_MESA),
        /* key */ EGL_WIDTH,
        /* val */ 64,
        /* key */ EGL_HEIGHT,
        /* val */ 64,
        EGL_NONE
    };

    for (const EGLint *p = drm_attrs; *p != EGL_NONE; p += 2) {
        EGLint key = p[0];
        EGLint val = p[1];
        const char *key_name = "???";
        char val_buf[64];

        if (key == EGL_DRM_BUFFER_FORMAT_MESA) {
            key_name = "EGL_DRM_BUFFER_FORMAT_MESA";
            if (val == 0x31D2) snprintf(val_buf, sizeof(val_buf),
                "0x%04x (= EGL_DRM_BUFFER_FORMAT_ARGB32_MESA)", val);
            else snprintf(val_buf, sizeof(val_buf), "0x%04x", val);
        } else if (key == EGL_DRM_BUFFER_USE_MESA) {
            key_name = "EGL_DRM_BUFFER_USE_MESA";
            snprintf(val_buf, sizeof(val_buf), "0x%04x (SCANOUT|SHARE)", val);
        } else if (key == EGL_WIDTH) {
            key_name = "EGL_WIDTH";
            snprintf(val_buf, sizeof(val_buf), "%d", val);
        } else if (key == EGL_HEIGHT) {
            key_name = "EGL_HEIGHT";
            snprintf(val_buf, sizeof(val_buf), "%d", val);
        } else {
            snprintf(val_buf, sizeof(val_buf), "0x%04x", key);
        }
        printf("    %-45s %s\n", key_name, val_buf);
    }

    *out_img = s_eglCreateDRMImageMESA(egl_dpy, drm_attrs);

    EGLint err = eglGetError();
    if (*out_img == EGL_NO_IMAGE_KHR) {
        printf("  [Path A] eglCreateDRMImageMESA returned EGL_NO_IMAGE_KHR\n");
        if (err != EGL_SUCCESS && err != 0x3000) {
            print_egl_error("eglCreateDRMImageMESA");
        }
        return 0;
    }

    if (err != EGL_SUCCESS && err != 0x3000) {
        print_egl_error("eglCreateDRMImageMESA");
        /* Non-fatal warning; still proceed to export attempt */
    }

    printf("  [Path A] eglCreateDRMImageMESA: created %p\n",
           (void*)(uintptr_t)(*out_img));
    printf("  [Path A] Source: Mesa-internal DRM buffer (no GL, no Screen).\n");
    return 1; /* proceed to export */
}

/* ---- Path B: EGL_GL_TEXTURE_2D_KHR (requires GL; disabled by default) ---- */
static int try_path_b(EGLDisplay egl_dpy, EGLImageKHR *out_img) {
    *out_img = EGL_NO_IMAGE_KHR;

    printf("  [Path B] EGL_GL_TEXTURE_2D_KHR path requested.\n");
    printf("  [Path B] NOTE: This path is NOT authorized by the Phase 1B plan.\n");
    printf("  [Path B] It is included here only for investigation of why\n");
    printf("  [Path B] eglCreateImageKHR crashes with SIGSEGV in Mesa/QNX virgl.\n\n");

    /* Set up GLES2 context with pbuffer */
    EGLint ncfg = 0;
    EGLConfig gl_cfg = NULL;
    EGLSurface gl_surf = EGL_NO_SURFACE;
    EGLContext gl_ctx = EGL_NO_CONTEXT;

    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };

    if (!s_has_khr_gl_texture_2d) {
        printf("  [Path B] EGL_KHR_gl_texture_2d: ABSENT\n");
        return 0;
    }
    if (!s_eglCreateImageKHR) {
        printf("  [Path B] eglCreateImageKHR: NOT RESOLVED\n");
        return 0;
    }

    if (!eglChooseConfig(egl_dpy, cfg_attrs, &gl_cfg, 1, &ncfg) || ncfg == 0) {
        print_egl_error("eglChooseConfig");
        printf("  [Path B] Cannot choose EGL config. Aborting Path B.\n");
        return 0;
    }
    printf("  [Path B] EGL config chosen.\n");

    EGLint pbuf_attrs[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    gl_surf = eglCreatePbufferSurface(egl_dpy, gl_cfg, pbuf_attrs);
    if (gl_surf == EGL_NO_SURFACE) {
        print_egl_error("eglCreatePbufferSurface");
        printf("  [Path B] Cannot create pbuffer. Aborting Path B.\n");
        return 0;
    }
    printf("  [Path B] pbuffer surface: %p\n", (void*)(uintptr_t)gl_surf);

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 2,
        EGL_CONTEXT_MINOR_VERSION_KHR, 0,
        EGL_NONE
    };
    gl_ctx = eglCreateContext(egl_dpy, gl_cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (gl_ctx == EGL_NO_CONTEXT) {
        print_egl_error("eglCreateContext");
        eglDestroySurface(egl_dpy, gl_surf);
        printf("  [Path B] Cannot create GLES2 context. Aborting Path B.\n");
        return 0;
    }
    printf("  [Path B] GLES2 context: %p\n", (void*)gl_ctx);

    if (!eglMakeCurrent(egl_dpy, gl_surf, gl_surf, gl_ctx)) {
        print_egl_error("eglMakeCurrent");
        eglDestroyContext(egl_dpy, gl_ctx);
        eglDestroySurface(egl_dpy, gl_surf);
        printf("  [Path B] Cannot make context current. Aborting Path B.\n");
        return 0;
    }
    printf("  [Path B] GLES2 context active.\n");

    /* Check GL_OES_EGL_image */
    const char *gl_exts = (const char*)glGetString(GL_EXTENSIONS);
    int gl_oes_egl_image = gl_exts && strstr(gl_exts, "GL_OES_EGL_image") != NULL;
    printf("  [Path B] GL_OES_EGL_image: %s\n",
           gl_oes_egl_image ? "PRESENT" : "ABSENT");

    /* Create GL texture and render to it */
    printf("  [Path B] Creating GL texture + FBO...\n");
    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    if (glGetError() != GL_NO_ERROR || tex == 0) {
        print_gl_error("glGenTextures");
        goto path_b_cleanup;
    }

    glBindTexture(GL_TEXTURE_2D, tex);
    uint8_t tex_data[64 * 64 * 4];
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            tex_data[(y * 64 + x) * 4 + 0] = 0;
            tex_data[(y * 64 + x) * 4 + 1] = (uint8_t)((y * 255) / 63);
            tex_data[(y * 64 + x) * 4 + 2] = (uint8_t)((x * 255) / 63);
            tex_data[(y * 64 + x) * 4 + 3] = 255;
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, tex_data);
    if (glGetError() != GL_NO_ERROR) {
        print_gl_error("glTexImage2D");
        goto path_b_cleanup;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fbo);
    if (glGetError() != GL_NO_ERROR || fbo == 0) {
        print_gl_error("glGenFramebuffers");
        goto path_b_cleanup;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, tex, 0);
    GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[ERROR] FBO incomplete 0x%x\n", fb_status);
        goto path_b_cleanup;
    }

    glViewport(0, 0, 64, 64);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    glFinish();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    printf("  [Path B] GL texture + FBO render: OK (tex=%u, fbo=%u)\n", tex, fbo);

    /* Attempt eglCreateImageKHR with SIGSEGV guard */
    printf("  [Path B] Attempting eglCreateImageKHR(display, EGL_NO_CONTEXT,\n");
    printf("           EGL_GL_TEXTURE_2D_KHR, tex=%u, NULL)...\n", tex);
    printf("  [Path B] WARNING: Known to crash with SIGSEGV in Mesa/QNX virgl.\n");

    struct sigaction sa, old_sa_sigsegv, old_sa_sigbus;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigbus_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &old_sa_sigsegv);
    sigaction(SIGBUS,  &sa, &old_sa_sigbus);

    s_sigbus_received = 0;
    if (sigsetjmp(s_jump_buf, 1) == 0) {
        /* Normal path */
        *out_img = s_eglCreateImageKHR(
            egl_dpy,
            EGL_NO_CONTEXT,
            (EGLenum)0x30B1,  /* EGL_GL_TEXTURE_2D_KHR */
            (EGLClientBuffer)(uintptr_t)tex,
            NULL);
        EGLint err2 = eglGetError();
        if (err2 != EGL_SUCCESS && err2 != 0x3000) {
            print_egl_error("eglCreateImageKHR(GL_TEXTURE_2D)");
        }
        if (*out_img == EGL_NO_IMAGE_KHR) {
            fprintf(stderr, "[ERROR] eglCreateImageKHR returned EGL_NO_IMAGE_KHR\n");
        }
    } else {
        /* Crash caught */
        fprintf(stderr, "[ERROR] eglCreateImageKHR(GL_TEXTURE_2D_KHR): "
                        "SIGSEGV/SIGBUS caught — Mesa crashed.\n");
        *out_img = EGL_NO_IMAGE_KHR;
    }

    sigaction(SIGSEGV, &old_sa_sigsegv, NULL);
    sigaction(SIGBUS,  &old_sa_sigbus,  NULL);

    if (*out_img != EGL_NO_IMAGE_KHR) {
        printf("  [Path B] eglCreateImageKHR: created %p\n",
               (void*)(uintptr_t)(*out_img));
        printf("  [Path B] Source: GL texture (FBO-rendered) via EGL_GL_TEXTURE_2D_KHR.\n");
        /* Success: caller will handle export and cleanup */
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        return 1;
    }

    printf("  [Path B] eglCreateImageKHR: FAILED or crashed.\n");
    printf("  [Path B] Path B: BLOCKED in Mesa/QNX virgl.\n");

path_b_cleanup:
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (tex) glDeleteTextures(1, &tex);
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (gl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, gl_ctx);
    if (gl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, gl_surf);
    return 0;
}

/* ============================================================================
 * Main
 * ============================================================================ */
int main(int argc, char *argv[]) {
    int exit_code = 1;

    /* Parse --allow-pbuffer-risk flag */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--allow-pbuffer-risk") == 0) {
            s_allow_pbuffer_risk = 1;
            printf("[SETUP] --allow-pbuffer-risk: Path B (pbuffer fallback) is ENABLED.\n");
            printf("[SETUP] WARNING: Path B is NOT authorized by the Phase 1B plan.\n");
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--allow-pbuffer-risk]\n", argv[0]);
            printf("  --allow-pbuffer-risk  Enable Path B (EGL_GL_TEXTURE_2D_KHR)\n");
            printf("                        NOT authorized by Phase 1B plan.\n");
            return 0;
        }
    }

    printf("\n");
    printf("================================================================================\n");
    printf("  QNX OOP-GPU Phase 1B-exportonly: True DMAbuf Export Probe\n");
    printf("  Built: " __DATE__ " " __TIME__ "\n");
    printf("  Path B fallback: %s\n",
           s_allow_pbuffer_risk ? "ENABLED (--allow-pbuffer-risk)" : "DISABLED (default)");
    printf("================================================================================\n\n");

    /* Suppress Mesa fatal errors */
    {
        const char *env = "MESA_NO_FATAL_ERROR=1";
        if (putenv((char*)env) == 0)
            printf("[SETUP] Mesa suppression: %s\n", env);
    }

    /* -------------------------------------------------------------------------
     * Phase 1: EGL initialization (no Screen/GL needed for Path A)
     * -------------------------------------------------------------------------- */
    printf("=== Phase 1: EGL initialization ===\n");
    EGLDisplay egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "[FATAL] Cannot get EGLDisplay\n");
        return 2;
    }

    EGLint maj = 0, min = 0;
    if (!eglInitialize(egl_dpy, &maj, &min)) {
        print_egl_error("eglInitialize");
        return 2;
    }
    printf("  EGL %d.%d initialized\n", maj, min);
    printf("  EGL_VENDOR      = %s\n",
           eglQueryString(egl_dpy, EGL_VENDOR) ? : "(null)");
    printf("  EGL_VERSION     = %s\n",
           eglQueryString(egl_dpy, EGL_VERSION) ? : "(null)");
    printf("  EGL_CLIENT_APIS = %s\n",
           eglQueryString(egl_dpy, EGL_CLIENT_APIS) ? : "(null)");
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 2: EGL extension inventory
     * -------------------------------------------------------------------------- */
    printf("=== Phase 2: EGL extension inventory ===\n");
    const char *egl_exts = eglQueryString(egl_dpy, EGL_EXTENSIONS);
    printf("  EGL_EXTENSIONS (%zu chars):\n", egl_exts ? strlen(egl_exts) : 0);

#define CHECK_EXT(_name, _flag) do { \
        int has = egl_exts && strstr(egl_exts, _name) != NULL; \
        (_flag) = has; \
        printf("    %-55s %s\n", _name, has ? "[PRESENT]" : "[ABSENT]"); \
    } while(0)

    CHECK_EXT("EGL_KHR_gl_texture_2D",                    s_has_khr_gl_texture_2d);
    CHECK_EXT("EGL_KHR_surfaceless_context",              s_has_khr_surfaceless_context);
    CHECK_EXT("EGL_EXT_image_dma_buf_import",              s_has_egl_ext_image_dma_buf_import);
    CHECK_EXT("EGL_EXT_image_dma_buf_import_modifiers",   s_has_egl_ext_image_dma_buf_import_mods);
    CHECK_EXT("EGL_MESA_image_dma_buf_export",             s_has_egl_mesa_image_dma_buf_export);
    CHECK_EXT("EGL_MESA_drm_image",                       s_has_egl_mesa_drm_image);
#undef CHECK_EXT
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 3: Resolve EGL function pointers
     * -------------------------------------------------------------------------- */
    printf("=== Phase 3: EGL function pointer resolution ===\n");

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

    printf("  %-55s %s\n", "eglCreateImageKHR",
           s_eglCreateImageKHR ? "[RESOLVED]" : "[NOT FOUND]");
    printf("  %-55s %s\n", "eglDestroyImageKHR",
           s_eglDestroyImageKHR ? "[RESOLVED]" : "[NOT FOUND]");
    printf("  %-55s %s\n", "eglExportDMABUFImageQueryMESA",
           s_eglExportDMABUFImageQueryMESA ? "[RESOLVED]" : "[NOT FOUND]");
    printf("  %-55s %s\n", "eglExportDMABUFImageMESA",
           s_eglExportDMABUFImageMESA ? "[RESOLVED]" : "[NOT FOUND]");
    printf("  %-55s %s\n", "eglCreateDRMImageMESA",
           s_eglCreateDRMImageMESA ? "[RESOLVED]" : "[NOT FOUND]");
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 4: STOP GATE — check minimum required functions for Path A
     * -------------------------------------------------------------------------- */
    printf("=== Phase 4: Export path gate ===\n");

    if (!s_eglExportDMABUFImageMESA) {
        printf("  [Gate] eglExportDMABUFImageMESA NOT RESOLVED.\n");
        printf("  [Gate] DMAbuf export is not available. Stopping.\n");
        goto cleanup_and_exit;
    }
    printf("  [Gate] eglExportDMABUFImageMESA: available.\n");

    if (!s_has_egl_mesa_drm_image) {
        printf("  [Gate] EGL_MESA_drm_image: ABSENT.\n");
        printf("  [Gate] Path A (EGL_MESA_drm_image) is unavailable.\n");
        if (!s_allow_pbuffer_risk) {
            printf("  [Gate] Path B (pbuffer fallback) is DISABLED.\n");
            printf("  [Gate] No export path available. Stopping.\n");
            goto cleanup_and_exit;
        }
        printf("  [Gate] Falling through to Path B (--allow-pbuffer-risk is set).\n");
    } else if (!s_eglCreateDRMImageMESA) {
        printf("  [Gate] eglCreateDRMImageMESA NOT RESOLVED.\n");
        printf("  [Gate] Path A (EGL_MESA_drm_image) is unavailable.\n");
        if (!s_allow_pbuffer_risk) {
            printf("  [Gate] Path B (pbuffer fallback) is DISABLED.\n");
            printf("  [Gate] No export path available. Stopping.\n");
            goto cleanup_and_exit;
        }
        printf("  [Gate] Falling through to Path B (--allow-pbuffer-risk is set).\n");
    } else {
        printf("  [Gate] Path A (EGL_MESA_drm_image): AVAILABLE.\n");
    }
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 5: Attempt export paths
     * -------------------------------------------------------------------------- */
    printf("=== Phase 5: Export source candidates ===\n");
    printf("  Path A — EGL_MESA_drm_image: %s\n",
           (s_has_egl_mesa_drm_image && s_eglCreateDRMImageMESA)
               ? "AVAILABLE" : "UNAVAILABLE");
    printf("  Path B — EGL_GL_TEXTURE_2D_KHR: %s\n",
           s_allow_pbuffer_risk ? "ENABLED (--allow-pbuffer-risk)" : "DISABLED (default)");
    printf("\n");

    EGLImageKHR egl_img = EGL_NO_IMAGE_KHR;
    const char *source_label = NULL;
    int export_succeeded = 0;

    /* ---- Path A: EGL_MESA_drm_image (preferred, no GL) ---- */
    if (s_has_egl_mesa_drm_image && s_eglCreateDRMImageMESA) {
        printf("--- Path A: EGL_MESA_drm_image ---\n");
        if (try_path_a(egl_dpy, &egl_img)) {
            source_label = "EGL_MESA_drm_image";
            export_succeeded = do_export_and_report(egl_dpy, egl_img);
        }
    }

    /* ---- Path B: EGL_GL_TEXTURE_2D_KHR (DISABLED by default) ---- */
    if (!export_succeeded) {
        if (!s_allow_pbuffer_risk) {
            printf("--- Path B: EGL_GL_TEXTURE_2D_KHR ---\n");
            printf("  NOT ATTEMPTED: Path B is DISABLED by default.\n");
            printf("  To investigate, run with --allow-pbuffer-risk.\n");
            printf("  NOTE: --allow-pbuffer-risk is NOT authorized by Phase 1B plan.\n");
        } else {
            printf("--- Path B: EGL_GL_TEXTURE_2D_KHR ---\n");
            if (try_path_b(egl_dpy, &egl_img)) {
                source_label = "EGL_GL_TEXTURE_2D_KHR";
                export_succeeded = do_export_and_report(egl_dpy, egl_img);
            }
        }
    }

    /* -------------------------------------------------------------------------
     * Final report
     * -------------------------------------------------------------------------- */
    printf("\n=== Final report ===\n");
    if (export_succeeded) {
        printf("  RESULT: TRUE DMAbuf EXPORT SUCCEEDED\n");
        printf("  Export source: %s\n", source_label);
        printf("  Milestone: eglExportDMABUFImageMESA returned >= 1 valid DMAbuf fd.\n");
        exit_code = 0;
    } else {
        printf("  RESULT: TRUE DMAbuf EXPORT NOT ACHIEVED\n");
        if (s_has_egl_mesa_drm_image && s_eglCreateDRMImageMESA) {
            printf("  Path A (EGL_MESA_drm_image): did not produce a valid DMAbuf fd.\n");
        }
        if (s_allow_pbuffer_risk && s_has_khr_gl_texture_2d && s_eglCreateImageKHR) {
            printf("  Path B (EGL_GL_TEXTURE_2D_KHR): eglCreateImageKHR crashed or returned error.\n");
        } else if (!s_allow_pbuffer_risk) {
            printf("  Path B: not attempted (DISABLED by default).\n");
        }
        printf("  Stop condition: no export path produced a valid DMAbuf fd.\n");
        exit_code = 1;
    }
    printf("\n");

cleanup_and_exit:
    if (s_eglDestroyImageKHR && egl_img != EGL_NO_IMAGE_KHR)
        s_eglDestroyImageKHR(egl_dpy, egl_img);
    eglTerminate(egl_dpy);

    printf("\n================================================================================\n");
    printf("  Phase 1B-exportonly probe complete. exit=%d\n", exit_code);
    printf("================================================================================\n");
    return exit_code;
}
