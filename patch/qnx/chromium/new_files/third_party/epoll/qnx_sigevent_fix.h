#ifndef THIRD_PARTY_EPOLL_QNX_SIGEVENT_FIX_H_
#define THIRD_PARTY_EPOLL_QNX_SIGEVENT_FIX_H_

// The pinned qnx-ports/epoll revision checks `__QNX__ < 800` in epoll-mgr.c,
// but Chromium's QNX toolchain currently defines `__QNX__` without a numeric
// value. Force the epoll translation unit onto the QNX 8 code path that is
// already validated in the working tree.
#if defined(__QNXNTO__)
#ifdef __QNX__
#undef __QNX__
#endif
#define __QNX__ 800
#endif

#endif  // THIRD_PARTY_EPOLL_QNX_SIGEVENT_FIX_H_
