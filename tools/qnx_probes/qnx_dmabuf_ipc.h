/*
 * qnx_dmabuf_ipc.h
 *
 * Shared definitions for the DMAbuf producer/consumer probe IPC protocol.
 * Used by qnx_dmabuf_export_producer.c and qnx_dmabuf_import_consumer.c.
 *
 * Protocol: Unix-domain stream socket.
 *   - Producer binds/listens; consumer connects.
 *   - Producer sends one fixed-size control message carrying DMAbuf metadata.
 *   - DMAbuf plane FDs are passed via SCM_RIGHTS ancillary data on the same
 *     sendmsg() call.
 *   - Consumer reads the control message and extracts the FDs from recvmsg().
 *   - Connection is closed after one transfer (simple probe protocol).
 *
 * QNX note: QNX Neutrino supports POSIX sendmsg/recvmsg with SCM_RIGHTS.
 * If this probe reports "SCM_RIGHTS failed" in QEMU virgl, the blocker and
 * smallest next step will be documented in the Phase 1B report.
 */

#ifndef QNX_DMABUF_IPC_H
#define QNX_DMABUF_IPC_H

#include <stdint.h>

#define QNX_DMABUF_PROBE_SOCK_PATH  "/tmp/qnx_dmabuf_probe.sock"
#define QNX_DMABUF_PROBE_MAGIC      0x444d4142  /* "DMAB" */
#define QNX_DMABUF_PROBE_VERSION    1

/*
 * Control message sent from producer → consumer over the Unix socket.
 * Layout is fixed-size (no pointers inside); all fields are host-endian.
 * Plane FDs are NOT embedded — they arrive as SCM_RIGHTS ancillary data.
 */
typedef struct {
    uint32_t magic;        /* QNX_DMABUF_PROBE_MAGIC */
    uint32_t version;      /* QNX_DMABUF_PROBE_VERSION */
    uint32_t width;       /* pixels */
    uint32_t height;      /* pixels */
    uint32_t stride;      /* bytes per scanline (plane 0) */
    uint32_t fourcc;      /* DRM fourcc, e.g. DRM_FORMAT_ARGB8888 = 0x34324241 */
    uint32_t n_planes;    /* number of plane FDs expected (1–4) */
    uint32_t exported;    /*
                             * 0 = export not attempted or failed
                             * 1 = DMAbuf export succeeded (plane FDs valid via SCM_RIGHTS)
                             * 2 = DMAbuf export blocked; raw RGBA pixels sent via socket
                             */
    uint32_t offset0;    /* byte offset for plane 0 (plane 1+ require extended header) */
    uint32_t modifier0_lo; /* modifier lower 32 bits (plane 0; upper bits unused on 32-bit) */
    uint32_t _reserved[4];  /* was [6]; stride/offset/modifier now explicit */
} qnx_dmabuf_ipc_header_t;

/*
 * DRM fourcc values used in this probe.
 * Defined in drm_fourcc.h (upstream kernel headers); reproduced here to
 * keep the probe standalone without kernel headers.
 */
#define DRM_FORMAT_ARGB8888  0x34324241
#define DRM_FORMAT_XRGB8888  0x34324258
#define DRM_FORMAT_ABGR8888  0x34324252
#define DRM_FORMAT_RGB888    0x38424752

/*
 * The ancillary CMSG area holds up to 4 SCM_RIGHTS FDs (one per plane).
 * Control message buffer size.
 */
#define QNX_DMABUF_IPC_CMSG_SIZE 256

#endif /* QNX_DMABUF_IPC_H */
