/*
 * qnx_dmabuf_restart_consumer.c
 *
 * Phase 1B-crash browser-like consumer: owns one visible QNX Screen window
 * for the entire run, listens on a Unix-domain socket for producer connections,
 * accepts two producer connections sequentially, imports each DMAbuf frame,
 * draws it to the same Screen/EGL window, and swaps.
 *
 * Goals:
 *   - Consumer owns one Screen context, one window, one EGL window surface
 *     for the whole run (stable identity across both producer connections).
 *   - First producer: exits non-zero after consumer imports/displays its frame.
 *     Consumer must remain alive and keep the window visible.
 *   - Second producer: reconnects, sends a second frame, exits zero.
 *     Consumer imports/displays it on the same window and exits cleanly.
 *
 * Architecture:
 *   - Single-process browser-like consumer.
 *   - Screen window + EGL context initialized once at startup.
 *   - accept() loop: accept two connections, handle each, then loop for next.
 *   - Graceful shutdown after second producer completes.
 *
 * Build:
 *   source ../out/qnx_release/qnx_env.sh
 *   export PATH="$QNX_HOST/usr/bin:$PATH"
 *   qcc -Wall -Wextra -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_restart_consumer \
 *       tools/qnx_probes/qnx_dmabuf_restart_consumer.c \
 *       -lsocket -lscreen -lEGL -lGLESv2
 *
 * Run (via runner script — see plan for exact command):
 *   The runner starts this consumer in the background, then starts
 *   two producers sequentially with different --exit-code values.
 *
 * This probe is standalone — no Chromium, no GN, no Ozone backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>

/* Shared IPC definitions */
#include "qnx_dmabuf_ipc.h"

/* QNX Screen */
#include <screen/screen.h>

/* EGL */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* ---- EGL function pointers ---- */
static PFNEGLCREATEIMAGEKHRPROC        s_eglCreateImageKHR = NULL;
static PFNEGLGETPLATFORMDISPLAYEXTPROC s_eglGetPlatformDisplayEXT = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC s_glEGLImageTargetTexture2DOES = NULL;

/* ---- Logging macros ---- */
#define DBG(fmt, ...)  fprintf(stderr, "[CONSUMER] " fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...)   fprintf(stderr, "[CONSUMER ERROR] " fmt "\n", ##__VA_ARGS__)

/* ---- Socket path ---- */
#define RESTART_SOCK_PATH  "/tmp/qnx_dmabuf_restart.sock"

/* ============================================================================
 * EGL helpers
 * ============================================================================ */
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
    ERR("%s: EGL error 0x%x (%s)", where, err, name);
}

/* ============================================================================
 * GL helpers
 * ============================================================================ */
static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        ERR("Shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    if (prog == 0) return 0;
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        ERR("Program link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* ============================================================================
 * Receive IPC header + SCM_RIGHTS FDs from producer
 * ============================================================================ */
static int recv_with_fds(int sock_fd, qnx_dmabuf_ipc_header_t *hdr_out,
                         int *plane_fds, int max_fds) {
    struct iovec iov;
    iov.iov_base = hdr_out;
    iov.iov_len  = sizeof(*hdr_out);

    unsigned char cmsg_buf[QNX_DMABUF_IPC_CMSG_SIZE];
    memset(cmsg_buf, 0, sizeof(cmsg_buf));

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    ssize_t r = recvmsg(sock_fd, &msg, 0);
    if (r < 0) {
        ERR("recvmsg() failed: %s", strerror(errno));
        return -1;
    }
    if (r == 0) {
        ERR("Producer closed connection before sending data.");
        return -1;
    }
    if ((size_t)r < sizeof(*hdr_out)) {
        ERR("recvmsg() returned only %zd bytes (expected %zu)", r, sizeof(*hdr_out));
        return -1;
    }

    /* Validate magic */
    if (hdr_out->magic != QNX_DMABUF_PROBE_MAGIC) {
        ERR("Invalid magic 0x%x (expected 0x%x)",
            hdr_out->magic, QNX_DMABUF_PROBE_MAGIC);
        return -1;
    }
    if (hdr_out->version != QNX_DMABUF_PROBE_VERSION) {
        ERR("Unsupported protocol version %u (expected %u)",
            hdr_out->version, QNX_DMABUF_PROBE_VERSION);
        return -1;
    }

    /* Extract SCM_RIGHTS FDs */
    int n_fds_found = 0;
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
         cmsg != NULL;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        int is_scmrights = (cmsg->cmsg_type == SCM_RIGHTS) &&
            (cmsg->cmsg_level == SOL_SOCKET || cmsg->cmsg_level == 65535);
        if (is_scmrights) {
            int *fds = (int*)CMSG_DATA(cmsg);
            size_t fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            if (fd_count > (size_t)max_fds) fd_count = (size_t)max_fds;
            for (size_t i = 0; i < fd_count; i++) {
                plane_fds[n_fds_found++] = fds[i];
                DBG("  Received plane FD[%zu]: %d (cmsg_level=%u)",
                    i, fds[i], (unsigned)cmsg->cmsg_level);
            }
            break;
        }
    }

    DBG("  IPC header: %dx%d stride=%u fourcc=0x%x planes=%u exported=%u",
        hdr_out->width, hdr_out->height, hdr_out->stride,
        hdr_out->fourcc, hdr_out->n_planes, hdr_out->exported);
    DBG("  SCM_RIGHTS FDs received: %d (expected %u)",
        n_fds_found, hdr_out->n_planes);

    return n_fds_found;
}

/* ============================================================================
 * Import DMAbuf FD as EGLImage
 * ============================================================================ */
static EGLImage import_dmabuf_as_eglimage(EGLDisplay dpy,
                                          const qnx_dmabuf_ipc_header_t *hdr,
                                          int *fds) {
    if (!s_eglCreateImageKHR) {
        ERR("eglCreateImageKHR not available");
        return EGL_NO_IMAGE;
    }

    EGLint attrs[64];
    int idx = 0;
    attrs[idx++] = 0x3057;  /* EGL_WIDTH */
    attrs[idx++] = (EGLint)hdr->width;
    attrs[idx++] = 0x3056;  /* EGL_HEIGHT */
    attrs[idx++] = (EGLint)hdr->height;
    attrs[idx++] = 0x3271;  /* EGL_LINUX_DRM_FOURCC_EXT */
    attrs[idx++] = (EGLint)hdr->fourcc;

    for (unsigned int p = 0; p < hdr->n_planes && p < 4 && idx < 56; p++) {
        attrs[idx++] = (EGLint)(0x3272 + (p * 3));  /* EGL_DMA_BUF_PLANE*_FD_EXT */
        attrs[idx++] = fds[p];
        attrs[idx++] = (EGLint)(0x3274 + (p * 3));  /* EGL_DMA_BUF_PLANE*_PITCH_EXT */
        attrs[idx++] = (EGLint)hdr->stride;
        attrs[idx++] = (EGLint)(0x3273 + (p * 3));  /* EGL_DMA_BUF_PLANE*_OFFSET_EXT */
        attrs[idx++] = (EGLint)hdr->offset0;
    }
    attrs[idx++] = 0x3038;  /* EGL_NONE */
    attrs[idx++] = 0x3038;  /* EGL_NONE (harmless) */

    DBG("  Import attrs: %dx%d fourcc=0x%08x n_planes=%u stride=%u offset0=%u",
        hdr->width, hdr->height, hdr->fourcc, hdr->n_planes,
        hdr->stride, hdr->offset0);

    EGLImage egl_img = s_eglCreateImageKHR(dpy, EGL_NO_CONTEXT,
                                             0x3270,  /* EGL_LINUX_DMA_BUF_EXT */
                                             (EGLClientBuffer)NULL, attrs);
    if (egl_img == EGL_NO_IMAGE) {
        print_egl_error("eglCreateImageKHR(DMA_BUF_EXT)");
        return EGL_NO_IMAGE;
    }

    DBG("  EGLImage imported: %p", (void*)(uintptr_t)egl_img);
    DBG("  MILESTONE: DMAbuf EGLImage IMPORT succeeded.");
    return egl_img;
}

/* ============================================================================
 * Bind EGLImage to GL texture
 * ============================================================================ */
static GLuint bind_eglimage_to_tex(EGLImage egl_img) {
    if (!s_glEGLImageTargetTexture2DOES) {
        ERR("glEGLImageTargetTexture2DOES not found");
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) {
        ERR("glGenTextures failed");
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    s_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_img);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        ERR("glEGLImageTargetTexture2DOES failed: 0x%x", err);
        glDeleteTextures(1, &tex);
        return 0;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    DBG("  GL texture created from EGLImage: %u", tex);
    DBG("  MILESTONE: GL_OES_EGL_image texture binding succeeded.");
    return tex;
}

/* ============================================================================
 * Render imported texture to the pre-created Screen/EGL window surface
 * ============================================================================ */
static int render_to_window(EGLDisplay egl_dpy, EGLSurface win_surf,
                            EGLContext win_ctx,
                            screen_window_t win  __attribute__((unused)),
                            GLuint tex,
                            const qnx_dmabuf_ipc_header_t *hdr) {
    /* ---- Make window surface current ---- */
    if (!eglMakeCurrent(egl_dpy, win_surf, win_surf, win_ctx)) {
        print_egl_error("eglMakeCurrent(win)");
        return 0;
    }
    DBG("  eglMakeCurrent: OK");

    /* Window dimensions (from hdr or default) */
    int win_w = (hdr->width > 0) ? (int)hdr->width : 64;
    int win_h = (hdr->height > 0) ? (int)hdr->height : 64;

    /* ---- GLES2 shader for fullscreen quad ---- */
    const char *vs_src =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_tex;\n"
        "varying vec2 v_tex;\n"
        "void main() {\n"
        "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "  v_tex = a_tex;\n"
        "}\n";
    const char *fs_src =
        "precision mediump float;\n"
        "uniform sampler2D u_tex;\n"
        "varying vec2 v_tex;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(u_tex, v_tex);\n"
        "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = 0;
    if (vs && fs)
        prog = link_program(vs, fs);
    if (!prog) {
        ERR("Shader compile/link failed");
        return 0;
    }

    glUseProgram(prog);
    GLint u_tex_loc = glGetUniformLocation(prog, "u_tex");
    GLint a_pos_loc = glGetAttribLocation(prog, "a_pos");
    GLint a_tex_loc = glGetAttribLocation(prog, "a_tex");
    DBG("  Shader: prog=%u u_tex=%d a_pos=%d a_tex=%d",
        prog, u_tex_loc, a_pos_loc, a_tex_loc);

    /* Fullscreen quad */
    GLfloat quad_data[] = {
        /* x     y     u     v */
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };
    glVertexAttribPointer(a_pos_loc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat),
                          quad_data + 0);
    glVertexAttribPointer(a_tex_loc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat),
                          quad_data + 2);
    glEnableVertexAttribArray(a_pos_loc);
    glEnableVertexAttribArray(a_tex_loc);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (u_tex_loc >= 0) glUniform1i(u_tex_loc, 0);
    GLenum gl_err = glGetError();
    DBG("  glBindTexture(tex=%u): GL error=0x%x", tex, gl_err);
    if (gl_err != GL_NO_ERROR) {
        ERR("glBindTexture failed: 0x%x", gl_err);
        glDeleteProgram(prog);
        glDeleteShader(fs);
        glDeleteShader(vs);
        return 0;
    }

    glViewport(0, 0, win_w, win_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_err = glGetError();
    glFlush();
    glFinish();
    DBG("  glDrawArrays: GL error=0x%x", gl_err);

    int swap_ok = 0;
    if (!eglSwapBuffers(egl_dpy, win_surf)) {
        print_egl_error("eglSwapBuffers");
    } else {
        DBG("  eglSwapBuffers: OK");
        DBG("  MILESTONE: Frame composited to Screen window.");
        swap_ok = 1;
    }

    /* Brief pause to let display compositor process the swap */
    usleep(200000);  /* 200ms */

    glDeleteProgram(prog);
    glDeleteShader(fs);
    glDeleteShader(vs);

    return swap_ok;
}

/* ============================================================================
 * Main: browser-like consumer with one persistent Screen window
 * ============================================================================ */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("\n");
    printf("================================================================================\n");
    printf("  QNX OOP-GPU Phase 1B-crash: DMAbuf Restart Consumer\n");
    printf("  Built: " __DATE__ " " __TIME__ "\n");
    printf("================================================================================\n\n");

    /* ---- Resolve EGL extension function pointers ---- */
    s_eglCreateImageKHR =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    s_glEGLImageTargetTexture2DOES =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    s_eglGetPlatformDisplayEXT =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

    /* =====================================================================
     * PHASE A: Create persistent Screen context and window once
     * This is the "browser-owned" resource that must survive producer crash.
     * ===================================================================== */
    printf("[SETUP A] Creating persistent Screen context and window...\n");

    screen_context_t screen_ctx = NULL;
    if (screen_create_context(&screen_ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        ERR("screen_create_context failed: %s", strerror(errno));
        return 1;
    }
    DBG("  Screen context created: %p", (void*)(uintptr_t)screen_ctx);

    screen_window_t screen_win = NULL;
    if (screen_create_window(&screen_win, screen_ctx) != 0) {
        ERR("screen_create_window failed: %s", strerror(errno));
        screen_destroy_context(screen_ctx);
        return 1;
    }
    DBG("  Screen window created: %p", (void*)(uintptr_t)screen_win);

    /* Configure window */
    int win_size[2] = { 64, 64 };
    int win_pos[2]  = { 64, 64 };
    int usage       = SCREEN_USAGE_OPENGL_ES2;
    int visible     = 1;

    screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_SIZE, win_size);
    screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_POSITION, win_pos);
    screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_USAGE, &usage);
    screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_VISIBLE, &visible);

    if (screen_create_window_buffers(screen_win, 1) != 0) {
        ERR("screen_create_window_buffers failed: %s", strerror(errno));
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }
    printf("[SETUP A]   Screen window: size=%dx%d pos=%d,%d usage=SCREEN_USAGE_OPENGL_ES2 visible=1\n",
           win_size[0], win_size[1], win_pos[0], win_pos[1]);
    printf("[SETUP A]   Window identity: screen_win=%p  (stable across producer connections)\n",
           (void*)(uintptr_t)screen_win);
    DBG("  Screen window buffers allocated.");

    /* =====================================================================
     * PHASE B: EGL initialization (uses same EGL display for import + display)
     * ===================================================================== */
    printf("[SETUP B] EGL initialization...\n");

    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    if (s_eglGetPlatformDisplayEXT) {
        /* Try platform display first */
        EGLDisplay d = s_eglGetPlatformDisplayEXT(0x3237,  /* EGL_DRM_DEVICE_FILE_EXT */
                                                   EGL_DEFAULT_DISPLAY, NULL);
        if (d != EGL_NO_DISPLAY) egl_dpy = d;
    }
    if (egl_dpy == EGL_NO_DISPLAY)
        egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, NULL, NULL)) {
        print_egl_error("consumer eglInitialize");
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }
    DBG("  EGL initialized: display=%p", (void*)(uintptr_t)egl_dpy);
    DBG("  EGL vendor: %s", eglQueryString(egl_dpy, EGL_VENDOR));
    DBG("  EGL version: %s", eglQueryString(egl_dpy, EGL_VERSION));

    /* ---- EGL config: window-compatible (for Screen window surface) ---- */
    EGLConfig win_cfg = NULL;
    EGLint n_win_cfg = 0;
    EGLint win_cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };
    if (!eglChooseConfig(egl_dpy, win_cfg_attrs, &win_cfg, 1, &n_win_cfg) ||
        n_win_cfg == 0) {
        print_egl_error("consumer eglChooseConfig(for win)");
        eglTerminate(egl_dpy);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }
    DBG("  eglChooseConfig (window): found %d config(s), cfg=%p",
        n_win_cfg, (void*)(uintptr_t)win_cfg);

    /* ---- EGL config: pbuffer-compatible (for import context) ---- */
    EGLConfig pbuf_cfg = NULL;
    EGLint n_pbuf_cfg = 0;
    EGLint pbuf_cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };
    if (!eglChooseConfig(egl_dpy, pbuf_cfg_attrs, &pbuf_cfg, 1, &n_pbuf_cfg) ||
        n_pbuf_cfg == 0) {
        print_egl_error("consumer eglChooseConfig(pbuf)");
        eglTerminate(egl_dpy);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }
    DBG("  eglChooseConfig (pbuf): found %d config(s), cfg=%p",
        n_pbuf_cfg, (void*)(uintptr_t)pbuf_cfg);

    /* ---- EGL window surface from the persistent Screen window ---- */
    EGLSurface win_surf = eglCreateWindowSurface(egl_dpy, win_cfg,
                                                  (EGLNativeWindowType)(uintptr_t)screen_win,
                                                  NULL);
    if (win_surf == EGL_NO_SURFACE) {
        print_egl_error("eglCreateWindowSurface(screen_win)");
        ERR("  BLOCKER: eglCreateWindowSurface FAILED.");
        ERR("  Cannot composite frames to Screen window.");
        eglTerminate(egl_dpy);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }
    printf("[SETUP B]   eglCreateWindowSurface: SUCCESS surf=%p\n",
           (void*)(uintptr_t)win_surf);
    printf("[SETUP B]   Window surface identity: surf=%p  (stable across producer connections)\n",
           (void*)(uintptr_t)win_surf);

    /* ---- Pbuffer surface for import context ---- */
    EGLint pbuf_attrs[] = { EGL_WIDTH, 4, EGL_HEIGHT, 4, EGL_NONE };
    EGLSurface pbuf_surf = eglCreatePbufferSurface(egl_dpy, pbuf_cfg, pbuf_attrs);
    if (pbuf_surf == EGL_NO_SURFACE) {
        print_egl_error("consumer eglCreatePbufferSurface");
        eglDestroySurface(egl_dpy, win_surf);
        eglTerminate(egl_dpy);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }

    /* ---- Import context (pbuffer surface, for eglCreateImageKHR) ---- */
    EGLContext import_ctx = eglCreateContext(egl_dpy, pbuf_cfg, EGL_NO_CONTEXT,
                                              (EGLint[]){ EGL_CONTEXT_MAJOR_VERSION_KHR, 2,
                                                          EGL_NONE });
    if (import_ctx == EGL_NO_CONTEXT) {
        print_egl_error("consumer eglCreateContext(import)");
        eglDestroySurface(egl_dpy, pbuf_surf);
        eglDestroySurface(egl_dpy, win_surf);
        eglTerminate(egl_dpy);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }
    DBG("  Import context: %p", (void*)(uintptr_t)import_ctx);

    /* ---- Window rendering context ---- */
    EGLContext win_ctx = eglCreateContext(egl_dpy, win_cfg, EGL_NO_CONTEXT,
                                           (EGLint[]){ EGL_CONTEXT_MAJOR_VERSION_KHR, 2,
                                                       EGL_NONE });
    if (win_ctx == EGL_NO_CONTEXT) {
        print_egl_error("consumer eglCreateContext(win)");
        eglDestroyContext(egl_dpy, import_ctx);
        eglDestroySurface(egl_dpy, pbuf_surf);
        eglDestroySurface(egl_dpy, win_surf);
        eglTerminate(egl_dpy);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }
    DBG("  Window context: %p", (void*)(uintptr_t)win_ctx);

    /* Make import context current for EGLImage import */
    if (!eglMakeCurrent(egl_dpy, pbuf_surf, pbuf_surf, import_ctx)) {
        print_egl_error("consumer eglMakeCurrent(import)");
        eglDestroyContext(egl_dpy, win_ctx);
        eglDestroyContext(egl_dpy, import_ctx);
        eglDestroySurface(egl_dpy, pbuf_surf);
        eglDestroySurface(egl_dpy, win_surf);
        eglTerminate(egl_dpy);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        return 1;
    }
    DBG("  Import context current (pbuf surface).");

    /* =====================================================================
     * PHASE C: Create listening socket and accept two producers sequentially
     * ===================================================================== */
    printf("[SETUP C] Creating listening socket at %s...\n", RESTART_SOCK_PATH);

    unlink(RESTART_SOCK_PATH);
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        ERR("socket(AF_UNIX) failed: %s", strerror(errno));
        goto cleanup;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, RESTART_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ERR("bind(%s) failed: %s", RESTART_SOCK_PATH, strerror(errno));
        close(listen_fd);
        goto cleanup;
    }
    if (listen(listen_fd, 5) < 0) {
        ERR("listen() failed: %s", strerror(errno));
        close(listen_fd);
        goto cleanup;
    }
    printf("[SETUP C]   Listening on %s\n", RESTART_SOCK_PATH);

    /* Accept and handle two producers */
    int frame_ok[2] = { 0, 0 };

    for (int frame = 0; frame < 2; frame++) {
        printf("\n");
        printf("################################################################################\n");
        printf("  ACCEPTING PRODUCER CONNECTION %d/2\n", frame + 1);
        printf("  Window identity check: screen_win=%p  surf=%p\n",
               (void*)(uintptr_t)screen_win, (void*)(uintptr_t)win_surf);
        printf("################################################################################\n\n");

        /* Poll for incoming connection (60s timeout per producer) */
        struct pollfd lpfd = {listen_fd, POLLIN, 0};
        int poll_r = poll(&lpfd, 1, 60000);
        if (poll_r <= 0) {
            ERR("poll/accept timeout for producer %d (poll_r=%d)", frame + 1, poll_r);
            ERR("  Stop condition: producer cannot reconnect to consumer socket.");
            goto cleanup;
        }

        struct sockaddr_un from;
        socklen_t fromlen = sizeof(from);
        int prod_fd = accept(listen_fd, (struct sockaddr*)&from, &fromlen);
        if (prod_fd < 0) {
            ERR("accept() failed for producer %d: %s", frame + 1, strerror(errno));
            goto cleanup;
        }
        printf("[FRAME %d] Producer connected (prod_fd=%d).\n", frame + 1, prod_fd);

        /* ---- Receive DMAbuf header + FDs ---- */
        printf("[FRAME %d] Receiving DMAbuf frame...\n", frame + 1);
        qnx_dmabuf_ipc_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        int plane_fds[4] = { -1, -1, -1, -1 };

        int n_fds = recv_with_fds(prod_fd, &hdr, plane_fds, 4);
        if (n_fds < 0) {
            ERR("  BLOCKER: SCM_RIGHTS fd receive failed for producer %d.", frame + 1);
            close(prod_fd);
            goto cleanup;
        }
        printf("[FRAME %d] IPC MILESTONE: SCM_RIGHTS fd passing succeeded.\n", frame + 1);

        /* ---- Import DMAbuf as EGLImage ---- */
        if (hdr.exported != 1 || n_fds == 0) {
            ERR("  Producer %d did not export real DMAbuf FDs (exported=%u, n_fds=%d).",
                frame + 1, hdr.exported, n_fds);
            close(prod_fd);
            goto cleanup;
        }

        EGLImage egl_img = import_dmabuf_as_eglimage(egl_dpy, &hdr, plane_fds);
        if (egl_img == EGL_NO_IMAGE) {
            ERR("  BLOCKER: DMAbuf EGLImage import failed for producer %d.", frame + 1);
            for (int i = 0; i < 4; i++) {
                if (plane_fds[i] >= 0) close(plane_fds[i]);
            }
            close(prod_fd);
            goto cleanup;
        }

        /* ---- Bind EGLImage to GL texture ---- */
        GLuint tex = bind_eglimage_to_tex(egl_img);
        PFNEGLDESTROYIMAGEKHRPROC destroy_img =
            (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        if (destroy_img && egl_img != EGL_NO_IMAGE)
            destroy_img(egl_dpy, egl_img);

        if (tex == 0) {
            ERR("  BLOCKER: EGLImage texture binding failed for producer %d.", frame + 1);
            for (int i = 0; i < 4; i++) {
                if (plane_fds[i] >= 0) close(plane_fds[i]);
            }
            close(prod_fd);
            goto cleanup;
        }

        /* ---- Render imported texture to the persistent Screen window ---- */
        printf("[FRAME %d] Rendering to persistent Screen window...\n", frame + 1);
        printf("[FRAME %d]   Window identity: screen_win=%p  surf=%p (SAME window as frame 1)\n",
               frame + 1, (void*)(uintptr_t)screen_win, (void*)(uintptr_t)win_surf);

        int render_ok = render_to_window(egl_dpy, win_surf, win_ctx,
                                         screen_win, tex, &hdr);
        glDeleteTextures(1, &tex);

        for (int i = 0; i < 4; i++) {
            if (plane_fds[i] >= 0) close(plane_fds[i]);
        }

        if (!render_ok) {
            ERR("  BLOCKER: Screen composition failed for producer %d.", frame + 1);
            close(prod_fd);
            goto cleanup;
        }
        printf("[FRAME %d] Frame %d composited to Screen window.\n", frame + 1, frame + 1);

        /* ---- Send ACK to producer ---- */
        {
            struct pollfd wpfd = {prod_fd, POLLOUT, 0};
            if (poll(&wpfd, 1, 5000) > 0) {
                const char *ack = "CONSUMER_OK";
                send(prod_fd, ack, strlen(ack), MSG_NOSIGNAL);
                DBG("  Sent ACK to producer.");
            }
        }
        close(prod_fd);

        frame_ok[frame] = 1;

        printf("\n[FRAME %d] Producer %d handled successfully.\n", frame + 1, frame + 1);
        printf("[FRAME %d] Window still alive: screen_win=%p  surf=%p\n",
               frame + 1, (void*)(uintptr_t)screen_win, (void*)(uintptr_t)win_surf);
    }

    printf("\n");
    printf("================================================================================\n");
    printf("  Both producers processed.\n");
    printf("  Frame 1: %s\n", frame_ok[0] ? "PASS" : "FAIL");
    printf("  Frame 2: %s\n", frame_ok[1] ? "PASS" : "FAIL");
    printf("  Window persistence: screen_win=%p surf=%p\n",
           (void*)(uintptr_t)screen_win, (void*)(uintptr_t)win_surf);
    printf("================================================================================\n");

cleanup:
    close(listen_fd);
    unlink(RESTART_SOCK_PATH);

    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(egl_dpy, import_ctx);
    eglDestroyContext(egl_dpy, win_ctx);
    eglDestroySurface(egl_dpy, pbuf_surf);
    eglDestroySurface(egl_dpy, win_surf);
    eglTerminate(egl_dpy);

    if (screen_win)  screen_destroy_window(screen_win);
    if (screen_ctx)  screen_destroy_context(screen_ctx);

    int ret = (frame_ok[0] && frame_ok[1]) ? 0 : 1;
    printf("\n================================================================================\n");
    printf("  Consumer exiting with code %d.\n", ret);
    printf("  Reason: frame_ok[0]=%d frame_ok[1]=%d\n", frame_ok[0], frame_ok[1]);
    printf("================================================================================\n");
    return ret;
}
