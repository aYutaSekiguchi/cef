/*
 * qnx_egl_extension_probe.c
 *
 * Phase 1A standalone probe: inventory EGL/Screen extensions and
 * optionally resolve function pointers for QNX OOP-GPU feasibility.
 *
 * Build:
 *   source ../out/qnx_release/qnx_env.sh
 *   qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_egl_extension_probe \
 *       tools/qnx_probes/qnx_egl_extension_probe.c -lscreen -lEGL -lGLESv2
 *
 * Run:
 *   ./tools/qnx_run.sh --virgl -- ./qnx_egl_extension_probe
 *
 * Does NOT link against Chromium; no GN, no Ozone backend.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* QNX Screen */
#include <screen/screen.h>

/* EGL */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* ---- Extension presence map ---- */
typedef struct {
    const char *name;
    int present;
    void *func_ptr;
    const char *func_sig;
} ExtEntry;

static ExtEntry KNOWN_EGL_EXTENSIONS[] = {
    /* ---- Core EGL/GLES extensions we care about ---- */
    { "EGL_KHR_stream",                    0, NULL, "EGLStreamKHR eglCreateStreamKHR(EGLDisplay,const EGLint*)" },
    { "EGL_KHR_stream_producer_eglsurface",0, NULL, "EGLSurface eglCreateStreamProducerSurfaceKHR(EGLDisplay,EGLConfig,EGLStreamKHR,const EGLint*)" },
    { "EGL_KHR_stream_cross_process_fd",  0, NULL, "EGLNativeFileDescriptorKHR eglGetStreamFileDescriptorKHR(EGLDisplay,EGLStreamKHR)" },
    { "EGL_KHR_stream_consumer_gltexture",0, NULL, "EGLBoolean eglStreamConsumerGLTextureExternalKHR(EGLDisplay,EGLStreamKHR)" },
    { "EGL_KHR_stream_fifo_sync",         0, NULL, "EGLBoolean eglGetStreamFifoParamsFOO(EGLDisplay,EGLStreamKHR,EGLenum*,EGLint*,EGLTimeKHR*)" },
    /* ---- QNX platform & buffer sharing ---- */
    { "EGL_QNX_platform_screen",          0, NULL, "EGLBoolean eglGetPlatformDisplayQNX(EGLenum,void*,const EGLint*)" },
    { "EGL_QNX_image_native_buffer",      0, NULL, "EGLBoolean eglGetNativeBufferQNX(EGLClientBuffer,EGLint)" },
    /* ---- Cross-platform platform base (alternative to QNX-specific) ---- */
    { "EGL_EXT_platform_base",            0, NULL, "EGLDisplay eglGetPlatformDisplayEXT(EGLenum,EGLNativeDisplayType,const EGLint*)" },
    { "EGL_EXT_platform_device",           0, NULL, "EGLDisplay eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT,...)" },
    /* ---- EGL version / client extension ---- */
    { "EGL_EXT_client_extensions",        0, NULL, "client-string extension marker" },
    /* ---- Surface / swap control ---- */
    { "EGL_KHR_swap_buffers_with_damage", 0, NULL, "EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay,EGLSurface,EGLint*,EGLint)" },
    { "EGL_KHR_create_context",           0, NULL, "EGLContext eglCreateContext(... EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR)" },
    /* ---- Surfaceless (useful for headless) ---- */
    { "EGL_KHR_surfaceless_context",      0, NULL, "surfaceless context behaviour guaranteed" },
    { "EGL_KHR_no_config_context",        0, NULL, "EGL_NO_CONFIG_KHR accepted for eglChooseConfig" },
};
static const int NUM_KNOWN_EGL = (int)(sizeof(KNOWN_EGL_EXTENSIONS)/sizeof(KNOWN_EGL_EXTENSIONS[0]));

static ExtEntry KNOWN_GL_EXTENSIONS[] = {
    { "GL_OES_EGL_image",                 0, NULL, "void glEGLImageTargetTexture2DOES(GLenum,void*)" },
    { "GL_OES_EGL_image_external",        0, NULL, "external oes sampler binding" },
    { "GL_EXT_texture_sRGB_decode",       0, NULL, "GL_TEXTURE_SRGB_DECODE_EXT / GL_DECODE_EXT" },
    { "GL_OES_rgb8_rgba8",                0, NULL, "RGBA8 / RGB8 internal formats" },
};
static const int NUM_KNOWN_GL = (int)(sizeof(KNOWN_GL_EXTENSIONS)/sizeof(KNOWN_GL_EXTENSIONS[0]));

/* ---- Helpers ---- */

static int contains_ext(const char *list, const char *ext) {
    if (!list || !ext) return 0;
    size_t elen = strlen(ext);
    const char *p = list;
    while ((p = strstr(p, ext)) != NULL) {
        /* must be a word boundary */
        if ((p == list || p[-1] == ' ') && (p[elen] == '\0' || p[elen] == ' '))
            return 1;
        p += elen;
    }
    return 0;
}

static void print_sep(void) {
    printf("--------------------------------------------------------------------------------\n");
}

/* ---- Function pointer resolution ---- */
static void resolve_egl_funcptrs(void) {
    printf("\n=== EGL function pointer resolution ===\n");

    /* These are all EGL 1.5 / KHR_stream function pointers;
       they may still be available at runtime in the virgl driver. */
    void *func = NULL;
#define RESOLVE(_name) do {                   \
        func = (void*)eglGetProcAddress(#_name); \
        printf("  %-50s %s\n", #_name,         \
               func ? "RESOLVED" : "NOT FOUND"); \
    } while(0)

    RESOLVE(eglGetPlatformDisplayEXT);
    RESOLVE(eglGetPlatformDisplayQNX);
    RESOLVE(eglCreateStreamKHR);
    RESOLVE(eglDestroyStreamKHR);
    RESOLVE(eglQueryStreamKHR);
    RESOLVE(eglQueryStreamAttribKHR);
    RESOLVE(eglStreamAttribKHR);
    RESOLVE(eglCreateStreamProducerSurfaceKHR);
    RESOLVE(eglGetStreamFileDescriptorKHR);
    RESOLVE(eglCreateStreamFromFileDescriptorKHR);
    RESOLVE(eglStreamConsumerGLTextureExternalKHR);
    RESOLVE(eglStreamConsumerAcquireKHR);
    RESOLVE(eglStreamConsumerReleaseKHR);
    RESOLVE(eglGetStreamFifoParamsKHR);
#undef RESOLVE
}

/* ---- Main ---- */
int main(void) {
    int ret = 0;

    printf("\n");
    print_sep();
    printf("  QNX OOP-GPU Phase 1A: EGL / Screen Extension Probe\n");
    printf("  Built: " __DATE__ " " __TIME__ "\n");
    print_sep();
    printf("\n");

    /* ---- Phase 1: Screen context (native handle source) ---- */
    printf("=== Phase 1: Screen context ===\n");
    screen_context_t screen_ctx = NULL;
    int screen_err = screen_create_context(&screen_ctx, SCREEN_APPLICATION_CONTEXT);
    if (screen_err != 0) {
        printf("  screen_create_context FAILED: err=%d (%s)\n",
               screen_err, strerror(-screen_err));
        printf("  NOTE: non-fatal; continuing with EGL default display.\n");
        ret = 1;
    } else {
        printf("  screen_create_context OK: ctx=%p\n", (void*)screen_ctx);

        /* Dump context type property for diagnostics */
        int ctx_type;
        if (screen_get_context_property_iv(screen_ctx, SCREEN_PROPERTY_TYPE, &ctx_type) == 0) {
            printf("  SCREEN_PROPERTY_TYPE=%d\n", ctx_type);
        }
    }
    printf("\n");

    /* ---- Phase 2: EGL initialization ---- */
    printf("=== Phase 2: EGL initialization ===\n");

    /* First try the standard default display */
    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    EGLint egl_major = 0, egl_minor = 0;
    const char *init_method = "none";

    egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy != EGL_NO_DISPLAY) {
        init_method = "eglGetDisplay(EGL_DEFAULT_DISPLAY)";
    }

    /* QNX does not expose a context-level EGL handle; the screen window
       provides one via screen_get_window_property_egld(). For extension probing,
       EGL_DEFAULT_DISPLAY is sufficient and portable across QEMU and real hw. */
    (void)screen_ctx;   /* keep screen context alive for diagnostics */
    (void)screen_err;

    if (egl_dpy == EGL_NO_DISPLAY) {
        printf("  FATAL: could not obtain an EGLDisplay.\n");
        if (screen_ctx) screen_destroy_context(screen_ctx);
        return 2;
    }

    if (!eglInitialize(egl_dpy, &egl_major, &egl_minor)) {
        printf("  FATAL: eglInitialize failed: 0x%x\n", eglGetError());
        if (screen_ctx) screen_destroy_context(screen_ctx);
        return 2;
    }
    printf("  EGL initialized via %s\n", init_method);
    printf("  EGL version: %d.%d\n", egl_major, egl_minor);
    printf("\n");

    /* ---- Phase 3: EGL vendor / client info ---- */
    printf("=== Phase 3: EGL vendor / client info ===\n");
    const char *vendor = eglQueryString(egl_dpy, EGL_VENDOR);
    const char *version = eglQueryString(egl_dpy, EGL_VERSION);
    const char *client_apis = eglQueryString(egl_dpy, EGL_CLIENT_APIS);
    printf("  EGL_VENDOR      = %s\n", vendor   ? vendor   : "(null)");
    printf("  EGL_VERSION     = %s\n", version  ? version  : "(null)");
    printf("  EGL_CLIENT_APIS = %s\n", client_apis ? client_apis : "(null)");
    printf("\n");

    /* ---- Phase 4: EGL extension strings ---- */
    printf("=== Phase 4: EGL extension strings ===\n");

    /* Client extensions (not display-specific) */
    const char *client_ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    printf("  EGL_EXTENSIONS (client):\n");
    if (client_ext && strlen(client_ext) > 0) {
        printf("    [%zu chars]\n", strlen(client_ext));
        /* print in lines of ~120 chars */
        const char *c = client_ext;
        printf("    ");
        int col = 4;
        while (*c) {
            const char *sp = strchr(c, ' ');
            int len = sp ? (sp - c) : (int)strlen(c);
            if (col + len > 118) { printf("\n    "); col = 4; }
            printf("%.*s ", len, c);
            col += len + 1;
            c = sp ? sp + 1 : c + len;
            if (*c == '\0') break;
        }
        printf("\n");
    } else {
        printf("    (none / empty)\n");
    }

    /* Display extensions */
    const char *disp_ext = eglQueryString(egl_dpy, EGL_EXTENSIONS);
    printf("\n  EGL_EXTENSIONS (display 0x%p):\n", (void*)egl_dpy);
    if (disp_ext && strlen(disp_ext) > 0) {
        printf("    [%zu chars]\n", strlen(disp_ext));
        const char *c = disp_ext;
        printf("    ");
        int col = 4;
        while (*c) {
            const char *sp = strchr(c, ' ');
            int len = sp ? (sp - c) : (int)strlen(c);
            if (col + len > 118) { printf("\n    "); col = 4; }
            printf("%.*s ", len, c);
            col += len + 1;
            c = sp ? sp + 1 : c + len;
            if (*c == '\0') break;
        }
        printf("\n");
    } else {
        printf("    (none / empty)\n");
    }

    /* ---- Phase 5: Check known extensions (EGL) ---- */
    printf("\n=== Phase 5: Known EGL extension check ===\n");
    /* Combine both client and display extension strings for matching */
    size_t combined_len = 1
        + (client_ext ? strlen(client_ext) : 0)
        + (disp_ext   ? strlen(disp_ext)   : 0);
    char *combined = malloc(combined_len);
    combined[0] = ' ';
    combined[1] = '\0';
    if (client_ext) strcat(combined, client_ext);
    strcat(combined, " ");
    if (disp_ext) strcat(combined, disp_ext);

    /* We also look at EGL_EXTENSIONS directly on the combined string */
    for (int i = 0; i < NUM_KNOWN_EGL; i++) {
        ExtEntry *e = &KNOWN_EGL_EXTENSIONS[i];
        e->present = contains_ext(combined, e->name);
        printf("  %-45s %s\n", e->name, e->present ? "[PRESENT]" : "[ABSENT]");
    }
    free(combined);
    printf("\n");

    /* ---- Phase 6: Resolve function pointers for present extensions ---- */
    /* We always attempt resolution; eglGetProcAddress returns NULL on failure */
    resolve_egl_funcptrs();

    /* ---- Phase 7: Create GLES2 context (needed for glGetString) ---- */
    printf("\n=== Phase 7: GLES2 context creation ===\n");
    EGLContext gl_ctx = EGL_NO_CONTEXT;
    EGLSurface gl_surf = EGL_NO_SURFACE;
    EGLConfig  gl_cfg  = NULL;
    EGLint ctx_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 2,
        EGL_CONTEXT_MINOR_VERSION_KHR, 0,
        EGL_NONE
    };
    EGLint surf_attrs[] = {
        EGL_WIDTH,  16,
        EGL_HEIGHT, 16,
        EGL_NONE
    };
    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_NONE
    };

    EGLint ncfg = 0;
    if (!eglChooseConfig(egl_dpy, cfg_attrs, &gl_cfg, 1, &ncfg) || ncfg == 0) {
        printf("  eglChooseConfig (pbuffer) FAILED: 0x%x\n", eglGetError());
    } else {
        gl_surf = eglCreatePbufferSurface(egl_dpy, gl_cfg, surf_attrs);
        if (gl_surf == EGL_NO_SURFACE) {
            printf("  eglCreatePbufferSurface FAILED: 0x%x\n", eglGetError());
        } else {
            gl_ctx = eglCreateContext(egl_dpy, gl_cfg, EGL_NO_CONTEXT, ctx_attrs);
            if (gl_ctx == EGL_NO_CONTEXT) {
                printf("  eglCreateContext (GLES2) FAILED: 0x%x\n", eglGetError());
            } else {
                if (!eglMakeCurrent(egl_dpy, gl_surf, gl_surf, gl_ctx)) {
                    printf("  eglMakeCurrent FAILED: 0x%x\n", eglGetError());
                    eglDestroyContext(egl_dpy, gl_ctx);
                    gl_ctx = EGL_NO_CONTEXT;
                } else {
                    printf("  GLES2 context created and made current.\n");
                }
            }
        }
    }

    /* ---- Phase 8: GLES extension string ---- */
    printf("\n=== Phase 8: GLES extension check ===\n");
    const char *gl_ext = NULL;
    if (gl_ctx != EGL_NO_CONTEXT) {
        gl_ext = (const char*)glGetString(GL_EXTENSIONS);
    } else {
        printf("  SKIPPED: no valid GLES2 context (glGetString not safe).\n");
    }
    printf("  GL_EXTENSIONS:\n");
    if (gl_ext && strlen(gl_ext) > 0) {
        printf("    [%zu chars]\n", strlen(gl_ext));
        const char *c = gl_ext;
        printf("    ");
        int col = 4;
        while (*c) {
            const char *sp = strchr(c, ' ');
            int len = sp ? (sp - c) : (int)strlen(c);
            if (col + len > 118) { printf("\n    "); col = 4; }
            printf("%.*s ", len, c);
            col += len + 1;
            c = sp ? sp + 1 : c + len;
            if (*c == '\0') break;
        }
        printf("\n");
    } else {
        printf("    (none / empty)\n");
    }

    printf("\n  Known GLES extension checks:\n");
    if (gl_ext) {
        for (int i = 0; i < NUM_KNOWN_GL; i++) {
            ExtEntry *e = &KNOWN_GL_EXTENSIONS[i];
            e->present = contains_ext(gl_ext, e->name);
            printf("    %-45s %s\n", e->name, e->present ? "[PRESENT]" : "[ABSENT]");
        }
    } else {
        for (int i = 0; i < NUM_KNOWN_GL; i++) {
            KNOWN_GL_EXTENSIONS[i].present = 0;
            printf("    %-45s [UNKNOWN - glGetString returned NULL]\n",
                   KNOWN_GL_EXTENSIONS[i].name);
        }
    }

    /* Phase 8 (EGL config sanity) is covered by Phase 7's eglChooseConfig
       call used to create the GLES2 pbuffer surface. */
    (void)gl_cfg;  /* suppress unused warning */

    /* ---- Summary table ---- */
    print_sep();
    printf("  PHASE 1A SUMMARY: Extension Availability\n");
    print_sep();
    printf("  %-45s %-10s %s\n", "Extension", "Status", "Notes");
    printf("  %-45s %-10s %s\n", "-------", "------", "-----");

    struct { const char *name; const char *label; int *pval; const char *note; } rows[] = {
        { "EGL_KHR_stream",                    "STREAM",  &KNOWN_EGL_EXTENSIONS[0].present, "core stream API" },
        { "EGL_KHR_stream_producer_eglsurface","PROD_SURF",&KNOWN_EGL_EXTENSIONS[1].present, "producer surface" },
        { "EGL_KHR_stream_cross_process_fd",  "XPROC_FD", &KNOWN_EGL_EXTENSIONS[2].present, "FD-based sharing" },
        { "EGL_KHR_stream_consumer_gltexture","CONSUMER", &KNOWN_EGL_EXTENSIONS[3].present, "GL tex consumer" },
        { "EGL_QNX_platform_screen",           "QNX_PLAT", &KNOWN_EGL_EXTENSIONS[5].present, "QNX screen platform" },
        { "EGL_QNX_image_native_buffer",       "QNX_NBUF", &KNOWN_EGL_EXTENSIONS[6].present, "native buffer share" },
        { "EGL_EXT_platform_base",             "EXT_BASE", &KNOWN_EGL_EXTENSIONS[7].present, "generic platform dispatch" },
        { "GL_OES_EGL_image",                  "OES_IMG",  &KNOWN_GL_EXTENSIONS[0].present,  "EGL image to GL tex" },
    };
    const int NUM_ROWS = (int)(sizeof(rows)/sizeof(rows[0]));
    for (int i = 0; i < NUM_ROWS; i++) {
        printf("  %-45s %-10s %s\n",
               rows[i].name,
               *rows[i].pval ? "PRESENT" : "ABSENT",
               rows[i].note);
    }
    print_sep();

    /* ---- Phase gate decision ---- */
    int khr_stream = KNOWN_EGL_EXTENSIONS[0].present;
    int khr_xproc  = KNOWN_EGL_EXTENSIONS[2].present;
    int qnx_nbuf   = KNOWN_EGL_EXTENSIONS[6].present;
    int oes_img    = KNOWN_GL_EXTENSIONS[0].present;

    printf("\n=== Phase gate decision ===\n");
    if (khr_stream && (khr_xproc || qnx_nbuf)) {
        printf("  DECISION: EGL_KHR_stream + sharing primitive AVAILABLE.\n");
        printf("  Phase 1B (producer/consumer) is VIABLE.\n");
    } else if (!khr_stream) {
        printf("  DECISION: EGL_KHR_stream ABSENT — Phase 1B (stream producer/consumer) is NOT viable.\n");
        printf("  Must evaluate alternate QNX sharing primitives (e.g. DMAbuf, shared memory buffers).\n");
    } else {
        printf("  DECISION: Partial stream support — sharing primitive unclear.\n");
        printf("  Requires manual investigation before Phase 1B.\n");
    }
    printf("  GL_OES_EGL_image: %s (needed for texture-external stream consumer).\n",
           oes_img ? "AVAILABLE" : "ABSENT");
    printf("\n");

    /* ---- Cleanup ---- */
    if (gl_ctx != EGL_NO_CONTEXT) {
        eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_dpy, gl_ctx);
    }
    if (gl_surf != EGL_NO_SURFACE)
        eglDestroySurface(egl_dpy, gl_surf);
    eglTerminate(egl_dpy);
    if (screen_ctx)
        screen_destroy_context(screen_ctx);

    return ret;
}
