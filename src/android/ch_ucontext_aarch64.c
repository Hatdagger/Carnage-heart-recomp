/* getcontext / setcontext / swapcontext / makecontext for Android arm64.
 *
 * Bionic has no ucontext functions, and the runtime's BIOS threads
 * (psxrecomp runtime/src/psx_fiber.c) are cooperative fibers built on them.
 * The switch is cooperative (a normal function call), so only the registers
 * the AArch64 calling convention says a callee must preserve are saved:
 * x19-x30, sp and d8-d15. They live in the kernel's mcontext layout
 * (regs[], sp, pc) and, for d8-d15, in the start of mcontext.__reserved,
 * which only this file reads. Signal masks are not touched (the runtime never
 * relies on them). */
#if defined(__ANDROID__) && defined(__aarch64__)

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>

/* Offsets used by the assembly below (checked against the real headers). */
#define CH_UC_REGS 184
#define CH_UC_SP   432
#define CH_UC_PC   440
#define CH_UC_FP   464
_Static_assert(offsetof(ucontext_t, uc_mcontext.regs) == CH_UC_REGS, "regs");
_Static_assert(offsetof(ucontext_t, uc_mcontext.sp) == CH_UC_SP, "sp");
_Static_assert(offsetof(ucontext_t, uc_mcontext.pc) == CH_UC_PC, "pc");
_Static_assert(offsetof(ucontext_t, uc_mcontext.__reserved) == CH_UC_FP, "fpsimd");
_Static_assert(sizeof(((ucontext_t *)0)->uc_mcontext.__reserved) >= 64, "room for d8-d15");

#define S_(x) #x
#define S(x) S_(x)

__asm__(
    ".text\n"
    ".p2align 2\n"

    /* int getcontext(ucontext_t *x0): returns 0 now, and 0 again when resumed. */
    ".globl getcontext\n"
    ".type getcontext, %function\n"
    "getcontext:\n"
    "  stp x19, x20, [x0, #(" S(CH_UC_REGS) " + 8*19)]\n"
    "  stp x21, x22, [x0, #(" S(CH_UC_REGS) " + 8*21)]\n"
    "  stp x23, x24, [x0, #(" S(CH_UC_REGS) " + 8*23)]\n"
    "  stp x25, x26, [x0, #(" S(CH_UC_REGS) " + 8*25)]\n"
    "  stp x27, x28, [x0, #(" S(CH_UC_REGS) " + 8*27)]\n"
    "  stp x29, x30, [x0, #(" S(CH_UC_REGS) " + 8*29)]\n"
    "  str xzr, [x0, #(" S(CH_UC_REGS) ")]\n"
    "  mov x9, sp\n"
    "  str x9, [x0, #" S(CH_UC_SP) "]\n"
    "  str x30, [x0, #" S(CH_UC_PC) "]\n"
    "  add x10, x0, #" S(CH_UC_FP) "\n"
    "  stp d8, d9, [x10, #0]\n"
    "  stp d10, d11, [x10, #16]\n"
    "  stp d12, d13, [x10, #32]\n"
    "  stp d14, d15, [x10, #48]\n"
    "  mov x0, #0\n"
    "  ret\n"
    ".size getcontext, .-getcontext\n"

    /* int setcontext(const ucontext_t *x0): does not return. Loads x0-x7 too,
     * so a context made by makecontext starts with its arguments. */
    ".globl setcontext\n"
    ".type setcontext, %function\n"
    "setcontext:\n"
    "  add x10, x0, #" S(CH_UC_FP) "\n"
    "  ldp d8, d9, [x10, #0]\n"
    "  ldp d10, d11, [x10, #16]\n"
    "  ldp d12, d13, [x10, #32]\n"
    "  ldp d14, d15, [x10, #48]\n"
    "  ldp x19, x20, [x0, #(" S(CH_UC_REGS) " + 8*19)]\n"
    "  ldp x21, x22, [x0, #(" S(CH_UC_REGS) " + 8*21)]\n"
    "  ldp x23, x24, [x0, #(" S(CH_UC_REGS) " + 8*23)]\n"
    "  ldp x25, x26, [x0, #(" S(CH_UC_REGS) " + 8*25)]\n"
    "  ldp x27, x28, [x0, #(" S(CH_UC_REGS) " + 8*27)]\n"
    "  ldp x29, x30, [x0, #(" S(CH_UC_REGS) " + 8*29)]\n"
    "  ldr x9, [x0, #" S(CH_UC_SP) "]\n"
    "  mov sp, x9\n"
    "  ldr x16, [x0, #" S(CH_UC_PC) "]\n"
    "  ldp x2, x3, [x0, #(" S(CH_UC_REGS) " + 8*2)]\n"
    "  ldp x4, x5, [x0, #(" S(CH_UC_REGS) " + 8*4)]\n"
    "  ldp x6, x7, [x0, #(" S(CH_UC_REGS) " + 8*6)]\n"
    "  ldr x1, [x0, #(" S(CH_UC_REGS) " + 8*1)]\n"
    "  ldr x0, [x0, #(" S(CH_UC_REGS) ")]\n"
    "  br x16\n"
    ".size setcontext, .-setcontext\n"

    /* int swapcontext(ucontext_t *x0, const ucontext_t *x1) */
    ".globl swapcontext\n"
    ".type swapcontext, %function\n"
    "swapcontext:\n"
    "  stp x19, x20, [x0, #(" S(CH_UC_REGS) " + 8*19)]\n"
    "  stp x21, x22, [x0, #(" S(CH_UC_REGS) " + 8*21)]\n"
    "  stp x23, x24, [x0, #(" S(CH_UC_REGS) " + 8*23)]\n"
    "  stp x25, x26, [x0, #(" S(CH_UC_REGS) " + 8*25)]\n"
    "  stp x27, x28, [x0, #(" S(CH_UC_REGS) " + 8*27)]\n"
    "  stp x29, x30, [x0, #(" S(CH_UC_REGS) " + 8*29)]\n"
    "  str xzr, [x0, #(" S(CH_UC_REGS) ")]\n"
    "  mov x9, sp\n"
    "  str x9, [x0, #" S(CH_UC_SP) "]\n"
    "  str x30, [x0, #" S(CH_UC_PC) "]\n"
    "  add x10, x0, #" S(CH_UC_FP) "\n"
    "  stp d8, d9, [x10, #0]\n"
    "  stp d10, d11, [x10, #16]\n"
    "  stp d12, d13, [x10, #32]\n"
    "  stp d14, d15, [x10, #48]\n"
    "  mov x0, x1\n"
    "  b setcontext\n"
    ".size swapcontext, .-swapcontext\n"

    /* A makecontext'd function returned: x19 holds uc_link. */
    ".type ch_uc_return, %function\n"
    "ch_uc_return:\n"
    "  cbz x19, 1f\n"
    "  mov x0, x19\n"
    "  b setcontext\n"
    "1:\n"
    "  mov x0, #0\n"
    "  bl exit\n"
    "  brk #1\n"
    ".size ch_uc_return, .-ch_uc_return\n"
);

extern void ch_uc_return(void);

void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...)
{
    uintptr_t sp = (uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;
    sp &= ~(uintptr_t)15;
    va_list ap;
    va_start(ap, argc);
    for (int i = 0; i < argc && i < 8; i++)
        ucp->uc_mcontext.regs[i] = (uint64_t)(unsigned int)va_arg(ap, int);
    va_end(ap);
    ucp->uc_mcontext.regs[19] = (uint64_t)(uintptr_t)ucp->uc_link;
    ucp->uc_mcontext.regs[29] = 0;
    ucp->uc_mcontext.regs[30] = (uint64_t)(uintptr_t)&ch_uc_return;
    ucp->uc_mcontext.sp = sp;
    ucp->uc_mcontext.pc = (uint64_t)(uintptr_t)func;
}

#endif /* __ANDROID__ && __aarch64__ */
