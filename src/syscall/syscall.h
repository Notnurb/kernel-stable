/*
 * syscall.h - System call interface
 *
 * ABI (follows System V AMD64 calling convention for syscalls):
 *   RAX = syscall number
 *   RDI = arg0
 *   RSI = arg1
 *   RDX = arg2
 *   R10 = arg3 (RCX is clobbered by SYSCALL)
 *   R8  = arg4
 *   R9  = arg5
 *
 *   Return: RAX = result (0 = success, negative = error)
 *
 * Syscall numbers:
 *   0 = SYSCALL_EXIT      — terminate current process
 *   1 = SYSCALL_WRITE     — write to I/O (fd, buf, len) → serial for now
 *   2 = SYSCALL_GETPID    — get current process PID
 *   3 = SYSCALL_YIELD     — yield CPU to next task
 *   4 = SYSCALL_SBRK      — adjust program break (not implemented)
 *   5 = SYSCALL_TEST      — test syscall that returns 0xDEAD
 */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* Syscall numbers */
#define SYSCALL_EXIT    0
#define SYSCALL_WRITE   1
#define SYSCALL_GETPID  2
#define SYSCALL_YIELD   3
#define SYSCALL_SBRK    4
#define SYSCALL_TEST    5

#define SYSCALL_COUNT   6

/* Error codes */
#define SYSCALL_ERR_NOSYS   (-1)
#define SYSCALL_ERR_INVAL   (-2)
#define SYSCALL_ERR_FAULT   (-3)

/* Initialize syscall MSRs (STAR, LSTAR, FMASK) */
void syscall_init(void);

/* C dispatch handler — called from assembly entry point */
uint64_t syscall_dispatch(uint64_t number, uint64_t arg0, uint64_t arg1,
                          uint64_t arg2, uint64_t arg3, uint64_t arg4,
                          uint64_t arg5);

#endif /* SYSCALL_H */
