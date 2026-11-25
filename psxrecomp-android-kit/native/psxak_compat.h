/* Force-included into every C/C++ file of the Android game library
 * (cmake/psxak.cmake). Bionic ships ucontext_t but not the functions that
 * use it; psx_fiber.c (the BIOS thread fibers) needs them. The code is in
 * psxak_ucontext_aarch64.c. */
#ifndef PSXAK_ANDROID_COMPAT_H
#define PSXAK_ANDROID_COMPAT_H
#if defined(__ANDROID__)
#include <ucontext.h>
#ifdef __cplusplus
extern "C" {
#endif
int  getcontext(ucontext_t *ucp);
int  setcontext(const ucontext_t *ucp);
int  swapcontext(ucontext_t *oucp, const ucontext_t *ucp);
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...);
#ifdef __cplusplus
}
#endif
#endif /* __ANDROID__ */
#endif
