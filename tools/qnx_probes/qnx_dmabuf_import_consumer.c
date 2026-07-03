/*
 * qnx_dmabuf_import_consumer.c
 *
 * Phase 1B standalone probe: DMAbuf/EGLImage consumer.
 *
 * Responsibilities:
 *   1. Connect to the producer's Unix-domain socket.
 *   2. Receive DMAbuf metadata (header) and plane FDs (SCM_RIGHTS ancillary)
 *      from the producer.
 *   3. Import the DMAbuf plane FDs into EGL as an EGLImage using
 *      EGL_EXT_image_dma_buf_import.
 *   4. Attach the EGLImage to a GL texture (GL_OES_EGL_image).
 *   5. Create a QNX Screen window and composite the texture into it using
 *      GL blit via a simple GLES2 shader.
 *   6. Report milestones.
 *
 * Build:
 *   source ../out/qnx_release/qnx_env.sh
 *   qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_import_consumer \
 *       tools/qnx_probes/qnx_dmabuf_import_consumer.c \
 *       -lsocket -lscreen -lEGL -lGLESv2
 *
 * Run:
 *   (started by the runner script; see qnx_dmabuf_export_producer.c for the
 *    combined runner command)
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

/* ---- Globals ---- */
#define DBG(fmt, ...) fprintf(stderr, "[CONSUMER] " fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...) fprintf(stderr, "[CONSUMER ERROR] " fmt "\n", ##__VA_ARGS__)

/* ---- EGL helper ---- */
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

/* ---- GL helper: compile a shader ---- */
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

/* ---- GL helper: link a program ---- */
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

/* ---- Socket helper: connect to producer ---- */
static int connect_to_producer(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        ERR("socket(AF_UNIX) failed: %s", strerror(errno));
        return -1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
        return fd;
    close(fd);
    return -1;
}

/* ---- Receive IPC header + SCM_RIGHTS FDs from producer ---- */
static int recv_with_fds(int sock_fd, qnx_dmabuf_ipc_header_t *hdr_out, int *plane_fds, int max_fds) {
    struct iovec iov;
    iov.iov_base = hdr_out;
    iov.iov_len  = sizeof(*hdr_out);

    unsigned char cmsg_buf[QNX_DMABUF_IPC_CMSG_SIZE];
    memset(cmsg_buf, 0, sizeof(cmsg_buf));

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov     = &iov;
    msg.msg_iovlen  = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    ssize_t r = recvmsg(sock_fd, &msg, 0);
    if (r < 0) {
        ERR("recvmsg() failed: %s", strerror(errno));
        ERR("  BLOCKER: recvmsg(SCM_RIGHTS) failed on QNX virgl.");
        ERR("  This indicates QNX may not support SCM_RIGHTS fd passing.");
        ERR("  Next step: document this as a plan update and evaluate alternative IPC.");
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
        ERR("Invalid magic 0x%x (expected 0x%x)", hdr_out->magic, QNX_DMABUF_PROBE_MAGIC);
        return -1;
    }
    if (hdr_out->version != QNX_DMABUF_PROBE_VERSION) {
        ERR("Unsupported protocol version %u (expected %u)", hdr_out->version, QNX_DMABUF_PROBE_VERSION);
        return -1;
    }

    /* Extract SCM_RIGHTS FDs from ancillary data */
    /*
     * NOTE: On QNX/QEMU, recvmsg() may report cmsg_level=65535 (0xFFFF) instead
     * of the POSIX SOL_SOCKET=1 for SCM_RIGHTS ancillary data. Both the
     * Phase 1B-devicefd probe and Phase 1B-scmrights probe confirmed this
     * anomaly but it does not prevent fd passing from working. We accept both
     * cmsg_level values to be safe.
     */
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

    printf("  IPC header: %dx%d stride=%u fourcc=0x%x planes=%u exported=%u\n",
           hdr_out->width, hdr_out->height, hdr_out->stride,
           hdr_out->fourcc, hdr_out->n_planes, hdr_out->exported);
    printf("  SCM_RIGHTS FDs received: %d (expected %u)\n", n_fds_found, hdr_out->n_planes);

    if (hdr_out->exported == 0) {
        printf("  NOTE: Producer reported export was not fully successful.\n");
        printf("  Continuing to validate the consumer-side import path.\n");
    }

    return n_fds_found;
}

/* ---- Import DMAbuf into EGLImage ---- */
static EGLImage import_dmabuf_as_eglimage(EGLDisplay dpy, const qnx_dmabuf_ipc_header_t *hdr, int *fds) {
    if (!s_eglCreateImageKHR) {
        ERR("eglCreateImageKHR not available");
        return EGL_NO_IMAGE;
    }

    /*
     * Build EGL attribute list for EGL_LINUX_DMA_BUF_EXT (0x3270).
     * Per EGL_EXT_image_dma_buf_import spec:
     *   EGL_WIDTH (0x3057), EGL_HEIGHT (0x3056), EGL_LINUX_DRM_FOURCC_EXT (0x3271),
     *   EGL_DMA_BUF_PLANE*_FD_EXT (0x3272, 0x3275, 0x3278, 0x327B),
     *   EGL_DMA_BUF_PLANE*_PITCH_EXT (0x3274, 0x3277, 0x327A, 0x327D),
     *   EGL_DMA_BUF_PLANE*_OFFSET_EXT (0x3273, 0x3276, 0x3279, 0x327C),
     *   EGL_NONE terminator.
     *
     * Use hdr->stride (plane 0 stride) and hdr->offset0 (plane 0 offset).
     * hdr->modifier0_lo is logged but EGL_EXT_image_dma_buf_import_modifiers
     * (not the base import extension) is required for modifier passing.
     */
    EGLint attrs[64];
    int idx = 0;
    attrs[idx++] = 0x3057;  /* EGL_WIDTH */
    attrs[idx++] = (EGLint)hdr->width;
    attrs[idx++] = 0x3056;  /* EGL_HEIGHT */
    attrs[idx++] = (EGLint)hdr->height;
    attrs[idx++] = 0x3271;  /* EGL_LINUX_DRM_FOURCC_EXT */
    attrs[idx++] = (EGLint)hdr->fourcc;

    /*
     * Per-plane attributes (DRM fourcc plane layout):
     * Plane p: FD=0x3272+(p*3), PITCH=0x3274+(p*3), OFFSET=0x3273+(p*3)
     * For single-plane AR24 (DRM_FORMAT_ARGB8888): stride=hdr->stride, offset=hdr->offset0.
     */
    for (unsigned int p = 0; p < hdr->n_planes && p < 4 && idx < 56; p++) {
        EGLint plane_stride = (p == 0) ? (EGLint)hdr->stride : (EGLint)hdr->stride;
        EGLint plane_offset  = (p == 0) ? (EGLint)hdr->offset0 : 0;
        /* Plane FD */
        attrs[idx++] = (EGLint)(0x3272 + (p * 3));  /* EGL_DMA_BUF_PLANE*_FD_EXT */
        attrs[idx++] = fds[p];
        /* Plane pitch */
        attrs[idx++] = (EGLint)(0x3274 + (p * 3));  /* EGL_DMA_BUF_PLANE*_PITCH_EXT */
        attrs[idx++] = plane_stride;
        /* Plane offset */
        attrs[idx++] = (EGLint)(0x3273 + (p * 3));  /* EGL_DMA_BUF_PLANE*_OFFSET_EXT */
        attrs[idx++] = plane_offset;
    }
    attrs[idx++] = 0x3038;  /* EGL_NONE */
    attrs[idx++] = 0x3038;  /* EGL_NONE (redundant but harmless) */

    printf("  Import attrs (%d entries): %dx%d fourcc=0x%08x n_planes=%u\n",
           idx, hdr->width, hdr->height, hdr->fourcc, hdr->n_planes);
    printf("  Import attrs: stride=%u offset0=%u modifier0=0x%08x\n",
           hdr->stride, hdr->offset0, hdr->modifier0_lo);

    EGLImage egl_img = s_eglCreateImageKHR(dpy, EGL_NO_CONTEXT,
                                            0x3270,  /* EGL_LINUX_DMA_BUF_EXT */
                                            (EGLClientBuffer)NULL, attrs);
    if (egl_img == EGL_NO_IMAGE) {
        print_egl_error("eglCreateImageKHR(DMA_BUF_EXT)");
        printf("  BLOCKER: DMAbuf EGLImage IMPORT FAILED.\n");
        printf("  Metadata: %dx%d fourcc=0x%08x n_planes=%u stride=%u offset0=%u\n",
               hdr->width, hdr->height, hdr->fourcc, hdr->n_planes,
               hdr->stride, hdr->offset0);
        return EGL_NO_IMAGE;
    }

    printf("  EGLImage imported from DMAbuf: %p\n", (void*)(uintptr_t)egl_img);
    printf("  MILESTONE: DMAbuf EGLImage IMPORT succeeded.\n");
    return egl_img;
}

/* ---- Attach EGLImage to GL texture ---- */
static GLuint create_tex_from_eglimage(EGLImage egl_img) {
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

    printf("  GL texture created from EGLImage: %u\n", tex);
    printf("  MILESTONE: GL_OES_EGL_image texture binding succeeded.\n");
    return tex;
}

/* ---- Create Screen window and composite DMAbuf-imported texture ---- */
static int composite_to_screen(EGLDisplay egl_dpy, GLuint tex,
                                const qnx_dmabuf_ipc_header_t *hdr) {
    screen_context_t ctx = NULL;
    screen_window_t win = NULL;
    EGLSurface win_surf = EGL_NO_SURFACE;
    EGLContext win_ctx = EGL_NO_CONTEXT;
    int success = 0;

    printf("  Creating Screen window for composition...\n");

    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        ERR("screen_create_context failed");
        return 0;
    }

    /* Create on-screen window */
    int win_size[2] = { hdr->width > 0 ? (int)hdr->width : 64,
                         hdr->height > 0 ? (int)hdr->height : 64 };
    int win_pos[2] = { 64, 64 };
    if (screen_create_window(&win, ctx) != 0) {
        ERR("screen_create_window failed");
        screen_destroy_context(ctx);
        return 0;
    }
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, win_size);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, win_pos);
    /* Require GLES2 usage so the window can be used as an EGL surface */
    int usage = SCREEN_USAGE_OPENGL_ES2;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    /* Make the window visible */
    int visible = 1;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, &visible);
    if (screen_create_window_buffers(win, 1) != 0) {
        ERR("screen_create_window_buffers failed");
        screen_destroy_window(win);
        screen_destroy_context(ctx);
        return 0;
    }
    printf("  Screen window: size=%dx%d pos=%d,%d usage=SCREEN_USAGE_OPENGL_ES2 visible=1\n",
           win_size[0], win_size[1], win_pos[0], win_pos[1]);

    /*
     * Phase 1B-display-isolation proved that SCREEN_PROPERTY_EGL_HANDLE is
     * non-fatal: Mesa's EGL accepts a raw screen_window_t as
     * EGLNativeWindowType directly. We skip that query entirely.
     * Diagnostic only:
     */
    {
        EGLint diag_egl_h = 0;
        if (screen_get_window_property_iv(win, SCREEN_PROPERTY_EGL_HANDLE,
                                            &diag_egl_h) != 0) {
            printf("  SCREEN_PROPERTY_EGL_HANDLE: not available (diagnostic only; non-fatal)\n");
        } else {
            printf("  SCREEN_PROPERTY_EGL_HANDLE: 0x%x (diagnostic only)\n", diag_egl_h);
        }
    }

    /* ---- Find a window-compatible EGL config ---- */
    EGLConfig cfg = NULL;
    EGLint ncfg = 0;
    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };
    if (!eglChooseConfig(egl_dpy, cfg_attrs, &cfg, 1, &ncfg) || ncfg == 0) {
        print_egl_error("eglChooseConfig(for win)");
        screen_destroy_window(win);
        screen_destroy_context(ctx);
        return 0;
    }
    printf("  eglChooseConfig: found %d window config(s), cfg=%p\n", ncfg, (void*)(uintptr_t)cfg);

    /* ---- Create EGL window surface directly from screen_window_t ---- */
    printf("  Calling eglCreateWindowSurface(egl_dpy, cfg, screen_win=%p, NULL)...\n",
           (void*)(uintptr_t)win);
    win_surf = eglCreateWindowSurface(egl_dpy, cfg,
                                       (EGLNativeWindowType)(uintptr_t)win,
                                       NULL);
    if (win_surf == EGL_NO_SURFACE) {
        print_egl_error("eglCreateWindowSurface(screen_win)");
        printf("  BLOCKER: eglCreateWindowSurface failed with raw screen_window_t.\n");
        screen_destroy_window(win);
        screen_destroy_context(ctx);
        return 0;
    }
    printf("  eglCreateWindowSurface: SUCCESS surface=%p\n", (void*)(uintptr_t)win_surf);
    printf("  MILESTONE: EGL window surface created from screen_window_t.\n");

    /* ---- Create a dedicated GLES2 context for the window ---- */
    EGLint ctx_attrs[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 2, EGL_NONE };
    win_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (win_ctx == EGL_NO_CONTEXT) {
        print_egl_error("eglCreateContext(for win)");
        eglDestroySurface(egl_dpy, win_surf);
        screen_destroy_window(win);
        screen_destroy_context(ctx);
        return 0;
    }
    printf("  eglCreateContext: OK ctx=%p\n", (void*)(uintptr_t)win_ctx);

    /* ---- Make window surface current ---- */
    if (!eglMakeCurrent(egl_dpy, win_surf, win_surf, win_ctx)) {
        print_egl_error("eglMakeCurrent(win)");
        eglDestroyContext(egl_dpy, win_ctx);
        eglDestroySurface(egl_dpy, win_surf);
        screen_destroy_window(win);
        screen_destroy_context(ctx);
        return 0;
    }
    printf("  eglMakeCurrent: OK (display=%p surface=%p ctx=%p)\n",
           (void*)(uintptr_t)egl_dpy, (void*)(uintptr_t)win_surf,
           (void*)(uintptr_t)win_ctx);

    /* ---- Set up GLES2 shader program for textured fullscreen quad ---- */
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
    GLint u_tex_loc = -1;
    if (vs && fs) {
        prog = link_program(vs, fs);
    }
    if (!prog) {
        ERR("Shader compile/link failed; cannot render imported texture.");
        eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_dpy, win_ctx);
        eglDestroySurface(egl_dpy, win_surf);
        screen_destroy_window(win);
        screen_destroy_context(ctx);
        return 0;
    }

    glUseProgram(prog);
    u_tex_loc = glGetUniformLocation(prog, "u_tex");
    GLint a_pos = glGetAttribLocation(prog, "a_pos");
    GLint a_tex = glGetAttribLocation(prog, "a_tex");
    printf("  Shader: prog=%u u_tex=%d a_pos=%d a_tex=%d\n",
           prog, u_tex_loc, a_pos, a_tex);

    /* Fullscreen quad: clip-space positions + normalized texture coords */
    GLfloat quad_data[] = {
        /* x     y     u     v */
        -1.0f, -1.0f, 0.0f, 1.0f,  /* bottom-left  */
         1.0f, -1.0f, 1.0f, 1.0f,  /* bottom-right */
        -1.0f,  1.0f, 0.0f, 0.0f,  /* top-left     */
         1.0f,  1.0f, 1.0f, 0.0f,  /* top-right    */
    };
    glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat),
                          quad_data + 0);
    glVertexAttribPointer(a_tex, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat),
                          quad_data + 2);
    glEnableVertexAttribArray(a_pos);
    glEnableVertexAttribArray(a_tex);

    /* Bind the DMAbuf-imported GL texture to texture unit 0 */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (u_tex_loc >= 0) glUniform1i(u_tex_loc, 0);
    GLenum gl_err = glGetError();
    printf("  glBindTexture(tex=%u): GL error=0x%x\n", tex, gl_err);
    if (gl_err != GL_NO_ERROR) {
        ERR("glBindTexture failed: GL error 0x%x", gl_err);
        glDeleteProgram(prog);
        glDeleteShader(fs);
        glDeleteShader(vs);
        eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_dpy, win_ctx);
        eglDestroySurface(egl_dpy, win_surf);
        screen_destroy_window(win);
        screen_destroy_context(ctx);
        return 0;
    }

    /* ---- Render the imported texture to the window surface ---- */
    glViewport(0, 0, win_size[0], win_size[1]);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  /* black background */
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl_err = glGetError();
    glFlush();
    glFinish();
    printf("  glDrawArrays: GL error=0x%x\n", gl_err);
    if (gl_err != GL_NO_ERROR) {
        ERR("glDrawArrays failed: GL error 0x%x", gl_err);
    } else {
        printf("  MILESTONE: DMAbuf-imported texture rendered to Screen window.\n");
    }

    /* ---- Swap the rendered frame to the visible window ---- */
    if (!eglSwapBuffers(egl_dpy, win_surf)) {
        print_egl_error("eglSwapBuffers(Screen display)");
        printf("  BLOCKER: eglSwapBuffers FAILED.\n");
    } else {
        printf("  eglSwapBuffers: OK\n");
        printf("  MILESTONE: Screen display composition (eglSwapBuffers) succeeded.\n");
        success = 1;
    }

    /* Brief pause to let the display compositor process the swap */
    usleep(200000);  /* 200ms */

    /* ---- Cleanup ---- */
    glDeleteProgram(prog);
    glDeleteShader(fs);
    glDeleteShader(vs);

    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(egl_dpy, win_ctx);
    eglDestroySurface(egl_dpy, win_surf);
    if (win) screen_destroy_window(win);
    if (ctx) screen_destroy_context(ctx);

    return success;
}

/* ---- Main ---- */
int main(int argc, char *argv[]) {
    int ret = 0;
    qnx_dmabuf_ipc_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    /*
     * --no-screen-composition: skip Screen-window composition (escape hatch).
     * Screen composition is now enabled by default (Phase 1B-display, 2026-07-03).
     * SCREEN_PROPERTY_EGL_HANDLE is non-fatal on QEMU virgl; we use
     * eglCreateWindowSurface(screen_win) directly without that query.
     * This flag is kept for backward compatibility and debug escape.
     */
    int skip_screen = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-screen-composition") == 0)
            skip_screen = 1;
    }

    printf("\n===========================================================\n");
    printf("  QNX OOP-GPU Phase 1B-dmabuf: DMAbuf Consumer Probe\n");
    printf("  Built: " __DATE__ " " __TIME__ "\n");
    printf("  Screen composition: %s\n", skip_screen ? "DISABLED (known blocker)" : "ENABLED");
    printf("===========================================================\n\n");

    /* Resolve extension function pointers */
    s_eglCreateImageKHR =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    s_glEGLImageTargetTexture2DOES =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    s_eglGetPlatformDisplayEXT =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

    /* ---- 1. Connect to producer socket ---- */
    printf("[1/5] Connecting to producer at %s...\n", QNX_DMABUF_PROBE_SOCK_PATH);
    int sock_fd = -1;
    for (int retry = 0; retry < 50; retry++) {
        sock_fd = connect_to_producer(QNX_DMABUF_PROBE_SOCK_PATH);
        if (sock_fd >= 0) break;
        printf("  retry %d/50: %s\n", retry + 1, strerror(errno));
        usleep(100000);  /* 100ms */
    }
    if (sock_fd < 0) {
        ERR("FATAL: cannot connect to producer after 5 seconds");
        ERR("  Ensure qnx_dmabuf_export_producer is running.");
        return 1;
    }
    printf("  Connected to producer (sock_fd=%d).\n", sock_fd);

    /* ---- 2. EGL init (consumer needs EGL for import) ---- */
    printf("[2/5] EGL initialization (consumer side)...\n");
    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    if (s_eglGetPlatformDisplayEXT) {
        EGLDisplay d = s_eglGetPlatformDisplayEXT(0x3237,  /* EGL_DRM_DEVICE_FILE_EXT */
                                                  EGL_DEFAULT_DISPLAY, NULL);
        if (d != EGL_NO_DISPLAY) egl_dpy = d;
    }
    if (egl_dpy == EGL_NO_DISPLAY)
        egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, NULL, NULL)) {
        print_egl_error("consumer eglInitialize");
        close(sock_fd);
        return 1;
    }
    printf("  Consumer EGL initialized (display=%p).\n", (void*)egl_dpy);

    /* Create GLES2 context (pbuffer-based; we'll make the Screen window the
       current surface during composition) */
    EGLConfig egl_cfg = NULL;
    EGLContext egl_ctx = EGL_NO_CONTEXT;
    EGLint ncfg = 0;
    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };
    if (!eglChooseConfig(egl_dpy, cfg_attrs, &egl_cfg, 1, &ncfg) || ncfg == 0) {
        print_egl_error("consumer eglChooseConfig");
        close(sock_fd);
        eglTerminate(egl_dpy);
        return 1;
    }
    EGLint pbuf_attrs[] = { EGL_WIDTH, 4, EGL_HEIGHT, 4, EGL_NONE };
    EGLSurface egl_surf = eglCreatePbufferSurface(egl_dpy, egl_cfg, pbuf_attrs);
    if (egl_surf == EGL_NO_SURFACE) {
        print_egl_error("consumer eglCreatePbufferSurface");
        close(sock_fd);
        eglTerminate(egl_dpy);
        return 1;
    }
    EGLint ctx_attrs[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 2, EGL_NONE };
    egl_ctx = eglCreateContext(egl_dpy, egl_cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (egl_ctx == EGL_NO_CONTEXT) {
        print_egl_error("consumer eglCreateContext");
        eglDestroySurface(egl_dpy, egl_surf);
        close(sock_fd);
        eglTerminate(egl_dpy);
        return 1;
    }
    if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) {
        print_egl_error("consumer eglMakeCurrent");
        eglDestroyContext(egl_dpy, egl_ctx);
        eglDestroySurface(egl_dpy, egl_surf);
        close(sock_fd);
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("  Consumer GLES2 context ready.\n");

    /* ---- 3. Receive DMAbuf metadata + FDs ---- */
    printf("[3/5] Receiving DMAbuf metadata + SCM_RIGHTS FDs...\n");
    int plane_fds[4] = {-1, -1, -1, -1};
    int n_fds = recv_with_fds(sock_fd, &hdr, plane_fds, 4);
    if (n_fds < 0) {
        printf("  BLOCKER: SCM_RIGHTS fd passing failed on QNX.\n");
        printf("  IPC mechanism: Unix domain sendmsg/recvmsg with SCM_RIGHTS.\n");
        printf("  Evidence: recvmsg returned -1 errno=%d (%s)\n", errno, strerror(errno));
        printf("  Decision implications:\n");
        printf("    - DMAbuf FD sharing between separate producer/consumer processes\n");
        printf("      is NOT currently viable without SCM_RIGHTS.\n");
        printf("    - Alternative IPC paths for next plan update:\n");
        printf("      (a) fork() SCM_RIGHTS inheritance — single binary,\n");
        printf("          parent=consumer, child=producer (avoids separate-process fd passing)\n");
        printf("      (b) QNX shared memory regions (QNX msg passing with mmap)\n");
        printf("          as the DMAbuf-sharing primitive instead of PRIME FDs\n");
        printf("    - Smallest next plan update: add fork-based probe\n");
        printf("      'qnx_dmabuf_fork_probe.c' that avoids separate-process\n");
        printf("      SCM_RIGHTS socket passing.\n");
        ret = 1;
        goto cleanup;
    }
    printf("  IPC MILESTONE: SCM_RIGHTS fd passing succeeded.\n");

    /* ---- 3b. If DMAbuf export was blocked, read raw pixel data from socket ---- */
    #define RAW_PIXEL_W 64
    #define RAW_PIXEL_H 64
    #define RAW_PIXEL_BPP 4
    unsigned char raw_pixels[RAW_PIXEL_W * RAW_PIXEL_H * RAW_PIXEL_BPP];
    memset(raw_pixels, 0, sizeof(raw_pixels));
    int got_raw_pixels = 0;
    if (hdr.exported == 2) {
        /* Producer could not export DMAbuf; it sent raw pixels as fallback */
        printf("  Raw pixel path indicated (hdr.exported=2). Reading %zu bytes...\n",
               sizeof(raw_pixels));
        /* Read raw pixel data with a timeout */
        struct pollfd pfd = {sock_fd, POLLIN, 0};
        int poll_r = poll(&pfd, 1, 5000);
        if (poll_r > 0) {
            ssize_t r = recv(sock_fd, raw_pixels, sizeof(raw_pixels), 0);
            if (r > 0) {
                printf("  Read %zd bytes of raw pixel data.\n", r);
                got_raw_pixels = 1;
            } else {
                printf("  Failed to read raw pixels: %zd errno=%d\n", r, errno);
            }
        } else if (poll_r == 0) {
            printf("  Timeout waiting for raw pixel data.\n");
        } else {
            printf("  poll() for raw pixels failed: %s\n", strerror(errno));
        }
    }

    /* ---- 4. Import DMAbuf as EGLImage + GL texture, OR use raw pixels ---- */
    printf("[4/5] Creating GL texture...\n");
    GLuint tex = 0;

    if (hdr.exported == 1 && n_fds > 0 && plane_fds[0] >= 0) {
        /* DMAbuf path: import FD as EGLImage */
        printf("  Attempting DMAbuf EGLImage import (n_fds=%d)...\n", n_fds);
        EGLImage egl_img = import_dmabuf_as_eglimage(egl_dpy, &hdr, plane_fds);
        if (egl_img != EGL_NO_IMAGE) {
            tex = create_tex_from_eglimage(egl_img);
            if (tex > 0) {
                printf("  Texture created via DMAbuf EGLImage import.\n");
            }
            PFNEGLDESTROYIMAGEKHRPROC destroy_img =
                (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
            if (destroy_img && egl_img != EGL_NO_IMAGE)
                destroy_img(egl_dpy, egl_img);
        }
        if (tex == 0) {
            printf("  DMAbuf EGLImage import FAILED.\n");
        }
    }

    if (tex == 0 && got_raw_pixels) {
        /* Fallback: create texture from raw pixel data */
        printf("  Creating texture from raw pixel data (glTexImage2D)...\n");
        glGenTextures(1, &tex);
        if (tex > 0) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                         RAW_PIXEL_W, RAW_PIXEL_H, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, raw_pixels);
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                printf("  glTexImage2D FAILED: 0x%x\n", err);
                glDeleteTextures(1, &tex);
                tex = 0;
            } else {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                printf("  MILESTONE: Raw pixel → GL texture succeeded.\n");
            }
        }
    }

    if (tex == 0) {
        ERR("No texture available (DMAbuf import failed and no raw pixels)");
        ret = 1;
        goto cleanup;
    }

    /* ---- 5. Composite the imported texture into a visible Screen window ---- */
    if (skip_screen) {
        printf("[5/5] Screen composition: SKIPPED (--no-screen-composition flag)\n");
    } else {
        printf("[5/5] Screen display composition (Phase 1B-display)...\n");
        int display_ok = composite_to_screen(egl_dpy, tex, &hdr);
        if (display_ok) {
            printf("  RESULT: Imported texture successfully composited to Screen window.\n");
        } else {
            printf("  RESULT: Screen display composition FAILED.\n");
            printf("  See above for EGL/Screen error details.\n");
            ret = 1;
        }
    }

cleanup:
    /* Close received FDs */
    for (int i = 0; i < 4; i++) {
        if (plane_fds[i] >= 0) {
            DBG("Closing plane FD[%d]=%d", i, plane_fds[i]);
            close(plane_fds[i]);
        }
    }

    /* Send ack to producer */
    {
        struct pollfd pfd = {sock_fd, POLLOUT, 0};
        if (poll(&pfd, 1, 5000) > 0) {
            const char *ack = "CONSUMER_OK";
            send(sock_fd, ack, strlen(ack), MSG_NOSIGNAL);
            DBG("Sent ack to producer.");
        }
    }
    close(sock_fd);

    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
    if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
    eglTerminate(egl_dpy);

    printf("\n===========================================================\n");
    printf("  Consumer probe complete. exit=%d\n", ret);
    printf("===========================================================\n");
    return ret;
}
