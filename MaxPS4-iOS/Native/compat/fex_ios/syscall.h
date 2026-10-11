#pragma once
// FEXCore's ARM64 MiscOps.cpp includes the Linux-style <syscall.h>.
// The Apple iOS SDK exposes syscall constants in <sys/syscall.h>.
// Forward to the real platform header; do not emulate syscalls here.
#if defined(__APPLE__)
#include <sys/syscall.h>
#else
#include_next <syscall.h>
#endif
