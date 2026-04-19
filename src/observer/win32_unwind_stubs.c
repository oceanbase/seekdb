/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Stubs for Itanium ABI _Unwind_* functions on Windows.
 * Windows uses SEH for exception handling; PL exception handling via
 * the Itanium unwinder is not functional in this build.
 */
#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <string.h>

void win32_trace(const char *msg) {
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  DWORD written;
  WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
}

typedef int _Unwind_Reason_Code;
typedef int _Unwind_Action;
typedef unsigned long long _Unwind_Exception_Class;

struct _Unwind_Exception;
struct _Unwind_Context;

#define _URC_FATAL_PHASE1_ERROR 3
#define _URC_FATAL_PHASE2_ERROR 2

unsigned long long _Unwind_GetLanguageSpecificData(struct _Unwind_Context *ctx) {
  (void)ctx;
  return 0;
}

unsigned long long _Unwind_GetIP(struct _Unwind_Context *ctx) {
  (void)ctx;
  return 0;
}

unsigned long long _Unwind_GetRegionStart(struct _Unwind_Context *ctx) {
  (void)ctx;
  return 0;
}

void _Unwind_SetGR(struct _Unwind_Context *ctx, int reg, unsigned long long val) {
  (void)ctx; (void)reg; (void)val;
}

void _Unwind_SetIP(struct _Unwind_Context *ctx, unsigned long long val) {
  (void)ctx; (void)val;
}

_Unwind_Reason_Code _Unwind_RaiseException(struct _Unwind_Exception *exc) {
  (void)exc;
  return _URC_FATAL_PHASE1_ERROR;
}

void _Unwind_Resume(struct _Unwind_Exception *exc) {
  (void)exc;
}

/*
 * Linux-specific syscall wrappers that are referenced from ob_sql_nio.cpp
 * and ob_futex.h but have no Windows implementation.
 */
struct epoll_event;

int ob_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
  (void)epfd; (void)events; (void)maxevents; (void)timeout;
  return -1;
}

#include <stdint.h>
struct timespec;

int futex_hook(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec *timeout) {
  (void)uaddr; (void)futex_op; (void)val; (void)timeout;
  return -1;
}

/*
 * Override abort() to get diagnostics during static initialization crashes.
 * MSVC CRT abort() exits with code 3 after resetting the SIGABRT handler,
 * making it impossible to catch via signal(). This override captures a stack
 * trace before terminating.
 */
#include <windows.h>
#include <stdio.h>

/*
 * Intercept ExitProcess to trace what's calling exit(3) during static init.
 * Since CRT abort/exit are in DLLs and can't be overridden via linking,
 * we hook at the Windows API level.
 */
typedef VOID (WINAPI *ExitProcessFunc)(UINT);
static ExitProcessFunc real_ExitProcess = NULL;

static VOID WINAPI hooked_ExitProcess(UINT code) {
  HANDLE h = GetStdHandle((DWORD)-12);
  DWORD written;
  char buf[2048];
  int n = snprintf(buf, sizeof(buf), "[WIN32-EXIT] ExitProcess(%u) called! Stack:\r\n", code);
  WriteFile(h, buf, (DWORD)n, &written, NULL);

  void *stack[48];
  USHORT frames = RtlCaptureStackBackTrace(0, 48, stack, NULL);
  for (USHORT i = 0; i < frames; i++) {
    n = snprintf(buf, sizeof(buf), "  [%d] 0x%p\r\n", i, stack[i]);
    WriteFile(h, buf, (DWORD)n, &written, NULL);
  }
  if (real_ExitProcess) real_ExitProcess(code);
  _exit((int)code);
}

void win32_hook_exit_process(void) {
  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  if (!kernel32) return;
  real_ExitProcess = (ExitProcessFunc)GetProcAddress(kernel32, "ExitProcess");

  /* Patch IAT of current module */
  HMODULE exe = GetModuleHandleA(NULL);
  IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)exe;
  IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)((char*)exe + dos->e_lfanew);
  IMAGE_IMPORT_DESCRIPTOR *imports = (IMAGE_IMPORT_DESCRIPTOR*)((char*)exe +
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

  for (; imports->Name; imports++) {
    IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA*)((char*)exe + imports->FirstThunk);
    for (; thunk->u1.Function; thunk++) {
      if ((void*)(uintptr_t)thunk->u1.Function == (void*)real_ExitProcess) {
        DWORD old_protect;
        VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old_protect);
        thunk->u1.Function = (uintptr_t)hooked_ExitProcess;
        VirtualProtect(&thunk->u1.Function, sizeof(void*), old_protect, &old_protect);
        return;
      }
    }
  }
}

#endif /* _WIN32 */
