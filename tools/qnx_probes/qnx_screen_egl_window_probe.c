/*
 * qnx_screen_egl_window_probe.c
 *
 * Phase 1B-display-isolation microtask: isolate whether a minimal QNX Screen
 * window can be used as an EGL window surface under QEMU virgl, and whether
 * SCREEN_PROPERTY_EGL_HANDLE is required or avoidable.
 *
 * Goals:
 *  1. Create a Screen context and visible Screen window with GLES-compatible
 *     usage flags (SCREEN_USAGE_OPENGL_ES2).
 *  2. Report whether screen_get_window_property_iv(SCREEN_PROPERTY_EGL_HANDLE)
 *     succeeds or fails (including errno).
 *  3. Test whether eglCreateWindowSurface(display, config,
 *     (EGLNativeWindowType)screen_window_t, NULL) works DIRECTLY without
 *     querying SCREEN_PROPERTY_EGL_HANDLE.
 *  4. If surface created: make current, clear to known color, call
 *     eglSwapBuffers(), report success/failure.
 *
 * Non-goals (do NOT attempt):
 *  - DMAbuf import/display
 *  - Screen texture composition via blit
 *  - Crash/restart simulation
 *
 * Build:
 *   cd /home/yuta/chromium/src/cef
 *   source ../out/qnx_release/qnx_env.sh
 *   export PATH="$QNX_HOST/usr/bin:$PATH"
 *   qcc -Wall -Wextra -Vgcc_ntox86_64 \
 *       -o ../out/qnx_release/qnx_screen_egl_window_probe \
 *       tools/qnx_probes/qnx_screen_egl_window_probe.c \
 *       -lscreen -lEGL -lGLESv2
 *
 * Run (compare the legacy and explicit QNX Screen platform paths):
 *   ./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_screen_egl_window_probe
 *   ./tools/qnx_run.sh --virgl --kill-existing -- \
 *       ./qnx_screen_egl_window_probe --platform-screen
 *
 * This probe is standalone — no Chromium, no GN, no Ozone backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/* QNX Screen */
#include <screen/screen.h>

/* EGL */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* ---- Helpers ---- */
static const char *egl_err_name(EGLint err) {
    switch (err) {
        case 0x3000: return "EGL_SUCCESS";
        case 0x3001: return "EGL_NOT_INITIALIZED";
        case 0x3002: return "EGL_BAD_ACCESS";
        case 0x3003: return "EGL_BAD_ALLOC";
        case 0x3004: return "EGL_BAD_ATTRIBUTE";
        case 0x3005: return "EGL_BAD_CONFIG";
        case 0x3006: return "EGL_BAD_CONTEXT";
        case 0x3007: return "EGL_BAD_CURRENT_SURFACE";
        case 0x3008: return "EGL_BAD_DISPLAY";
        case 0x3009: return "EGL_BAD_MATCH";
        case 0x300A: return "EGL_BAD_NATIVE_PIXMAP";
        case 0x300B: return "EGL_BAD_NATIVE_WINDOW";
        case 0x300C: return "EGL_BAD_PARAMETER";
        case 0x300D: return "EGL_BAD_SURFACE";
        case 0x300E: return "EGL_CONTEXT_LOST";
        default:     return "(unknown)";
    }
}

static const char *gl_err_name(GLenum err) {
    switch (err) {
        case 0:      return "GL_NO_ERROR";
        case 0x0500: return "GL_INVALID_ENUM";
        case 0x0501: return "GL_INVALID_VALUE";
        case 0x0502: return "GL_INVALID_OPERATION";
        case 0x0505: return "GL_OUT_OF_MEMORY";
        default:     return "(unknown)";
    }
}

/* ---- Screen API wrapper ---- */
static const char *screen_err_str(int rc) {
    if (rc == 0) return "SUCCESS (0)";
    /* QNX Screen uses positive errno values */
    switch (rc) {
        case 1:  return "ERROR (1, EPERM)";
        case 2:  return "ERROR (2, ENOENT)";
        case 3:  return "ERROR (3, ESRCH)";
        case 4:  return "ERROR (4, EINTR)";
        case 5:  return "ERROR (5, EIO)";
        case 6:  return "ERROR (6, ENXIO)";
        case 7:  return "ERROR (7, E2BIG)";
        case 8:  return "ERROR (8, ENOEXEC)";
        case 9:  return "ERROR (9, EBADF)";
        case 10: return "ERROR (10, ECHILD)";
        case 11: return "ERROR (11, EAGAIN)";
        case 12: return "ERROR (12, ENOMEM)";
        case 13: return "ERROR (13, EACCES)";
        case 14: return "ERROR (14, EFAULT)";
        case 15: return "ERROR (15, ENOTBLK)";
        case 16: return "ERROR (16, EBUSY)";
        case 17: return "ERROR (17, EEXIST)";
        case 18: return "ERROR (18, EXDEV)";
        case 19: return "ERROR (19, ENODEV)";
        case 20: return "ERROR (20, ENOTDIR)";
        case 21: return "ERROR (21, EISDIR)";
        case 22: return "ERROR (22, EINVAL)";
        case 23: return "ERROR (23, ENFILE)";
        case 24: return "ERROR (24, EMFILE)";
        case 25: return "ERROR (25, ENOTTY)";
        case 26: return "ERROR (26, ETXTBSY)";
        case 27: return "ERROR (27, EFBIG)";
        case 28: return "ERROR (28, ENOSPC)";
        case 29: return "ERROR (29, ESPIPE)";
        case 30: return "ERROR (30, EROFS)";
        case 31: return "ERROR (31, EMLINK)";
        case 32: return "ERROR (32, EPIPE)";
        case 33: return "ERROR (33, EDOM)";
        case 34: return "ERROR (34, ERANGE)";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "ERROR (%d)", rc);
            return buf;
        }
    }
}

/* ============================================================================
 * Main
 * ============================================================================ */
int main(int argc, char **argv) {
    int exit_code = 1;
    int use_platform_screen = 0;

    if (argc == 2 && strcmp(argv[1], "--platform-screen") == 0) {
        use_platform_screen = 1;
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [--platform-screen]\n", argv[0]);
        return 2;
    }

    printf("\n");
    printf("================================================================================\n");
    printf("  QNX OOP-GPU Phase 1B-display-isolation:\n");
    printf("  Screen window + EGL window surface feasibility under QEMU virgl\n");
    printf("  Built: " __DATE__ " " __TIME__ "\n");
    printf("================================================================================\n\n");

    /* -------------------------------------------------------------------------
     * Phase 1: EGL initialization.  The explicit path mirrors the qnx-ports
     * Weston backend: EGL_PLATFORM_SCREEN_QNX plus EGL_DEFAULT_DISPLAY as the
     * native display.  Keep each path in a separate process when comparing.
     * -------------------------------------------------------------------------- */
    printf("=== Phase 1: EGL initialization ===\n");
    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    const char *display_method = "eglGetDisplay(EGL_DEFAULT_DISPLAY)";

    if (use_platform_screen) {
        PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
            (PFNEGLGETPLATFORMDISPLAYEXTPROC)
                eglGetProcAddress("eglGetPlatformDisplayEXT");
        display_method =
            "eglGetPlatformDisplayEXT(EGL_PLATFORM_SCREEN_QNX, "
            "EGL_DEFAULT_DISPLAY)";
        if (get_platform_display == NULL) {
            fprintf(stderr,
                    "[FATAL] eglGetPlatformDisplayEXT is not available\n");
            return 2;
        }
        egl_dpy = get_platform_display(
            EGL_PLATFORM_SCREEN_QNX, EGL_DEFAULT_DISPLAY, NULL);
    } else {
        egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (egl_dpy == EGL_NO_DISPLAY) {
        EGLint err = eglGetError();
        fprintf(stderr, "[FATAL] %s failed: 0x%x (%s)\n",
                display_method, err, egl_err_name(err));
        return 2;
    }

    EGLint maj = 0, min = 0;
    if (!eglInitialize(egl_dpy, &maj, &min)) {
        fprintf(stderr, "[FATAL] eglInitialize failed: 0x%x (%s)\n",
                eglGetError(), egl_err_name(eglGetError()));
        return 2;
    }
    printf("  Display method  = %s\n", display_method);
    printf("  EGLDisplay      = %p\n", (void*)egl_dpy);
    printf("  EGL %d.%d initialized\n", maj, min);
    printf("  EGL_VENDOR      = %s\n",
           eglQueryString(egl_dpy, EGL_VENDOR)   ? : "(null)");
    printf("  EGL_VERSION     = %s\n",
           eglQueryString(egl_dpy, EGL_VERSION)  ? : "(null)");
    printf("  EGL_CLIENT_APIS = %s\n",
           eglQueryString(egl_dpy, EGL_CLIENT_APIS) ? : "(null)");
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 2: EGL config for window surface (GLES2)
     * -------------------------------------------------------------------------- */
    printf("=== Phase 2: EGL config for window surface ===\n");
    EGLConfig egl_cfg = NULL;
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

    if (!eglChooseConfig(egl_dpy, cfg_attrs, &egl_cfg, 1, &ncfg) || ncfg == 0) {
        fprintf(stderr, "[FATAL] eglChooseConfig(window) failed: 0x%x (%s)\n",
                eglGetError(), egl_err_name(eglGetError()));
        eglTerminate(egl_dpy);
        return 2;
    }
    printf("  eglChooseConfig: found %d matching config(s)\n", ncfg);
    printf("  Selected EGLConfig: %p\n", (void*)(uintptr_t)egl_cfg);
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 3: Screen context + window creation
     * -------------------------------------------------------------------------- */
    printf("=== Phase 3: Screen context + window creation ===\n");
    screen_context_t screen_ctx = NULL;
    screen_window_t  screen_win = NULL;
    int sc_rc = 0;

    sc_rc = screen_create_context(&screen_ctx, SCREEN_APPLICATION_CONTEXT);
    if (sc_rc != 0) {
        fprintf(stderr, "[FATAL] screen_create_context failed: %s\n",
                screen_err_str(sc_rc));
        eglTerminate(egl_dpy);
        return 2;
    }
    printf("  screen_create_context: OK (ctx=%p)\n", (void*)screen_ctx);

    /* Create window with GLES2 usage flags */
    sc_rc = screen_create_window(&screen_win, screen_ctx);
    if (sc_rc != 0) {
        fprintf(stderr, "[FATAL] screen_create_window failed: %s\n",
                screen_err_str(sc_rc));
        screen_destroy_context(screen_ctx);
        eglTerminate(egl_dpy);
        return 2;
    }
    printf("  screen_create_window: OK (win=%p)\n", (void*)screen_win);

    /* Set window size (small for probe — 64x64) */
    {
        int size[2] = { 64, 64 };
        sc_rc = screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_SIZE, size);
        if (sc_rc != 0) {
            fprintf(stderr, "[FATAL] screen_set_window_property_iv(SIZE) failed: %s\n",
                    screen_err_str(sc_rc));
            screen_destroy_window(screen_win);
            screen_destroy_context(screen_ctx);
            eglTerminate(egl_dpy);
            return 2;
        }
        printf("  SCREEN_PROPERTY_SIZE: set to 64x64\n");
    }

    /* Set GLES2 usage flags — required for EGL window surface */
    {
        int usage = SCREEN_USAGE_OPENGL_ES2;
        sc_rc = screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_USAGE, &usage);
        if (sc_rc != 0) {
            fprintf(stderr, "[ERROR] screen_set_window_property_iv(USAGE) failed: %s\n",
                    screen_err_str(sc_rc));
            /* Non-fatal: continue */
        } else {
            printf("  SCREEN_PROPERTY_USAGE: SCREEN_USAGE_OPENGL_ES2 (0x%08x)\n", usage);
        }
    }

    /* Set window position */
    {
        int pos[2] = { 64, 64 };
        sc_rc = screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_POSITION, pos);
        if (sc_rc != 0) {
            fprintf(stderr, "[ERROR] screen_set_window_property_iv(POSITION) failed: %s\n",
                    screen_err_str(sc_rc));
        } else {
            printf("  SCREEN_PROPERTY_POSITION: set to 64,64\n");
        }
    }

    /* Set visible */
    {
        int visible = 1;
        sc_rc = screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_VISIBLE, &visible);
        if (sc_rc != 0) {
            fprintf(stderr, "[ERROR] screen_set_window_property_iv(VISIBLE) failed: %s\n",
                    screen_err_str(sc_rc));
        } else {
            printf("  SCREEN_PROPERTY_VISIBLE: set to 1\n");
        }
    }

    /* Create window buffer(s) */
    sc_rc = screen_create_window_buffers(screen_win, 1);
    if (sc_rc != 0) {
        fprintf(stderr, "[FATAL] screen_create_window_buffers failed: %s\n",
                screen_err_str(sc_rc));
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        eglTerminate(egl_dpy);
        return 2;
    }
    printf("  screen_create_window_buffers: OK (1 buffer)\n");
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 4: SCREEN_PROPERTY_EGL_HANDLE test
     * -------------------------------------------------------------------------- */
    printf("=== Phase 4: SCREEN_PROPERTY_EGL_HANDLE test ===\n");
    EGLint egl_handle = 0;
    sc_rc = screen_get_window_property_iv(screen_win, SCREEN_PROPERTY_EGL_HANDLE, &egl_handle);
    if (sc_rc != 0) {
        printf("  screen_get_window_property_iv(SCREEN_PROPERTY_EGL_HANDLE): FAILED\n");
        printf("    screen_rc    = %d (%s)\n", sc_rc, screen_err_str(sc_rc));
        printf("    egl_handle   = 0x%x (unchanged)\n", (unsigned)egl_handle);
        printf("  NOTE: SCREEN_PROPERTY_EGL_HANDLE query fails in QEMU virgl.\n");
        printf("  This is a known blocker from Phase 1B-smoke. Proceeding to direct\n");
        printf("  eglCreateWindowSurface test (bypass the EGL_HANDLE query).\n");
    } else {
        printf("  screen_get_window_property_iv(SCREEN_PROPERTY_EGL_HANDLE): SUCCESS\n");
        printf("    egl_handle = 0x%x\n", (unsigned)egl_handle);
    }
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 5: Direct eglCreateWindowSurface test (bypass SCREEN_PROPERTY_EGL_HANDLE)
     * -------------------------------------------------------------------------- */
    printf("=== Phase 5: Direct eglCreateWindowSurface test ===\n");
    printf("  Passing (EGLNativeWindowType)screen_win=%p directly to\n", (void*)screen_win);
    printf("  eglCreateWindowSurface (bypassing SCREEN_PROPERTY_EGL_HANDLE query).\n");

    EGLSurface win_surf = eglCreateWindowSurface(
        egl_dpy, egl_cfg,
        (EGLNativeWindowType)(uintptr_t)screen_win,
        NULL);  /* no attributes */

    EGLint surf_err = eglGetError();
    if (win_surf == EGL_NO_SURFACE) {
        printf("  eglCreateWindowSurface: FAILED\n");
        printf("    EGL error: 0x%x (%s)\n", surf_err, egl_err_name(surf_err));
        if (surf_err == EGL_BAD_NATIVE_WINDOW) {
            printf("  RESULT: EGL_BAD_NATIVE_WINDOW — EGL rejects the screen_window_t\n");
            printf("  as a valid EGLNativeWindowType. This is a blocker for the\n");
            printf("  display-isolation path under QEMU virgl.\n");
        } else if (surf_err == EGL_BAD_MATCH) {
            printf("  RESULT: EGL_BAD_MATCH — EGL config/window type mismatch.\n");
        } else if (surf_err == EGL_BAD_ALLOC) {
            printf("  RESULT: EGL_BAD_ALLOC — Could not allocate surface resources.\n");
        }
        printf("\n  === STOP CONDITION: eglCreateWindowSurface FAILED ===\n");
        printf("  Cannot create EGL window surface. Screen window setup succeeded,\n");
        printf("  but EGL surface creation is blocked. See Phase 1B-display next\n");
        printf("  steps for implications.\n");

        /* Clean up Screen and exit */
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        eglTerminate(egl_dpy);
        return 1;
    }

    printf("  eglCreateWindowSurface: SUCCESS\n");
    printf("    Surface: %p\n", (void*)(uintptr_t)win_surf);
    printf("  RESULT: EGL window surface created from screen_window_t.\n");
    printf("  SCREEN_PROPERTY_EGL_HANDLE query is NOT required for surface creation.\n");
    printf("\n");

    /* -------------------------------------------------------------------------
     * Phase 6: GLES2 context + make current + clear + swap
     * -------------------------------------------------------------------------- */
    printf("=== Phase 6: GLES2 context + make current + clear + swap ===\n");
    EGLContext gl_ctx = EGL_NO_CONTEXT;

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 2,
        EGL_CONTEXT_MINOR_VERSION_KHR, 0,
        EGL_NONE
    };
    gl_ctx = eglCreateContext(egl_dpy, egl_cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (gl_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "[ERROR] eglCreateContext failed: 0x%x (%s)\n",
                eglGetError(), egl_err_name(eglGetError()));
        printf("\n  === STOP CONDITION: eglCreateContext FAILED ===\n");
        eglDestroySurface(egl_dpy, win_surf);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("  eglCreateContext: OK (ctx=%p)\n", (void*)(uintptr_t)gl_ctx);

    if (!eglMakeCurrent(egl_dpy, win_surf, win_surf, gl_ctx)) {
        fprintf(stderr, "[ERROR] eglMakeCurrent failed: 0x%x (%s)\n",
                eglGetError(), egl_err_name(eglGetError()));
        printf("\n  === STOP CONDITION: eglMakeCurrent FAILED ===\n");
        eglDestroyContext(egl_dpy, gl_ctx);
        eglDestroySurface(egl_dpy, win_surf);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("  eglMakeCurrent: OK (display=%p surface=%p ctx=%p)\n",
           (void*)egl_dpy, (void*)(uintptr_t)win_surf, (void*)(uintptr_t)gl_ctx);

    /* Clear to a known color: bright red (0xFF0000FF in GL_RGBA float) */
    printf("  glClearColor(1.0f, 0.0f, 0.0f, 1.0f) — red\n");
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLenum gl_err = glGetError();
    if (gl_err != GL_NO_ERROR) {
        fprintf(stderr, "[ERROR] glClear failed: 0x%x (%s)\n",
                gl_err, gl_err_name(gl_err));
        printf("\n  === STOP CONDITION: glClear FAILED ===\n");
        eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_dpy, gl_ctx);
        eglDestroySurface(egl_dpy, win_surf);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("  glClear: OK (GL error = 0x%x)\n", gl_err);

    glFlush();
    glFinish();
    printf("  glFlush + glFinish: OK\n");

    /* Swap buffers */
    if (!eglSwapBuffers(egl_dpy, win_surf)) {
        fprintf(stderr, "[ERROR] eglSwapBuffers failed: 0x%x (%s)\n",
                eglGetError(), egl_err_name(eglGetError()));
        printf("\n  === STOP CONDITION: eglSwapBuffers FAILED ===\n");
        eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_dpy, gl_ctx);
        eglDestroySurface(egl_dpy, win_surf);
        screen_destroy_window(screen_win);
        screen_destroy_context(screen_ctx);
        eglTerminate(egl_dpy);
        return 1;
    }
    printf("  eglSwapBuffers: OK\n");
    printf("\n");

    /* -------------------------------------------------------------------------
     * Success — all phases passed
     * -------------------------------------------------------------------------- */
    printf("================================================================================\n");
    printf("  PHASE 1B-DISPLAY-ISOLATION: ALL MILESTONES PASSED\n");
    printf("================================================================================\n");
    printf("  Milestones:\n");
    printf("  [1] Screen context + window creation:  PASS\n");
    printf("  [2] SCREEN_PROPERTY_EGL_HANDLE query: %s\n",
           (sc_rc == 0) ? "PASS (handle=0x%x)" : "FAIL (non-fatal — not required)");
    printf("  [3] eglCreateWindowSurface (direct):   PASS\n");
    printf("  [4] eglMakeCurrent + glClear:         PASS\n");
    printf("  [5] eglSwapBuffers:                   PASS\n");
    printf("\n");
    printf("  KEY FINDING:\n");
    printf("  eglCreateWindowSurface accepts a raw screen_window_t directly.\n");
    printf("  SCREEN_PROPERTY_EGL_HANDLE is NOT required for surface creation.\n");
    printf("  The EGL window surface path is viable in QEMU virgl.\n");
    printf("\n");
    printf("  IMPLICATION:\n");
    printf("  Phase 1B-display composition is NOT blocked by SCREEN_PROPERTY_EGL_HANDLE.\n");
    printf("  The DMAbuf consumer can use eglCreateWindowSurface(screen_win, ...)\n");
    printf("  directly to display GPU-produced content on the visible Screen window.\n");
    printf("================================================================================\n");

    exit_code = 0;

    /* -------------------------------------------------------------------------
     * Cleanup
     * -------------------------------------------------------------------------- */
    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (gl_ctx != EGL_NO_CONTEXT)
        eglDestroyContext(egl_dpy, gl_ctx);
    if (win_surf != EGL_NO_SURFACE)
        eglDestroySurface(egl_dpy, win_surf);
    if (screen_win)
        screen_destroy_window(screen_win);
    if (screen_ctx)
        screen_destroy_context(screen_ctx);
    eglTerminate(egl_dpy);

    return exit_code;
}
