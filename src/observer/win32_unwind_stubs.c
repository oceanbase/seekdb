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
 * Itanium ABI _Unwind_* bridges for Windows SEH.
 *
 * On Windows, PL exceptions are dispatched via Windows SEH (RaiseException /
 * RtlDispatchException). This file:
 *   - Implements _Unwind_RaiseException / _Unwind_Resume by calling
 *     RaiseException with OB_PL_SEH_EXCEPTION_CODE.
 *   - Implements _Unwind_Get and _Unwind_Set accessors that read from / write
 *     to the ObWin32UnwindCtx synthetic context built by ob_pl_seh_personality.
 */
#ifdef _WIN32

/* Include <stdint.h> before <windows.h> so that uintptr_t is defined before
 * vcruntime.h (included transitively by windows.h) attempts to use it. */
#include <stdint.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "win32_pl_seh.h"

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

/* -----------------------------------------------------------------------
 * Thread-local state shared with ob_pl_seh_personality
 *
 * §3.5 global-variable exemption: these MUST be file-scope __declspec(thread)
 * because JIT-generated PL code loads them by linker-resolved symbol name
 * (see ob_pl_adt.cpp TLS-model constant accessors). Encapsulating them in a
 * class or TLS-get-helper would break the symbol-name linkage that LLVM IR
 * relies on at JIT link time. Per-thread scope keeps concurrent PL frames
 * on different threads correctly isolated.
 * ----------------------------------------------------------------------- */
__declspec(thread) void *tl_ob_pl_seh_exc_ptr = NULL;
__declspec(thread) uintptr_t tl_ob_pl_seh_selector = 0;

/* -----------------------------------------------------------------------
 * _Unwind_Context accessors — ctx is actually an ObWin32UnwindCtx*
 * ----------------------------------------------------------------------- */

unsigned long long _Unwind_GetLanguageSpecificData(struct _Unwind_Context *ctx) {
  struct ObWin32UnwindCtx *wctx = (struct ObWin32UnwindCtx *)ctx;
  return (unsigned long long)(uintptr_t)wctx->disp_ctx->HandlerData;
}

unsigned long long _Unwind_GetIP(struct _Unwind_Context *ctx) {
  struct ObWin32UnwindCtx *wctx = (struct ObWin32UnwindCtx *)ctx;
  return (unsigned long long)wctx->disp_ctx->ControlPc;
}

unsigned long long _Unwind_GetRegionStart(struct _Unwind_Context *ctx) {
  struct ObWin32UnwindCtx *wctx = (struct ObWin32UnwindCtx *)ctx;
  return (unsigned long long)(wctx->disp_ctx->ImageBase +
                              wctx->disp_ctx->FunctionEntry->BeginAddress);
}

void _Unwind_SetGR(struct _Unwind_Context *ctx, int reg, unsigned long long val) {
  struct ObWin32UnwindCtx *wctx = (struct ObWin32UnwindCtx *)ctx;
  if (reg >= 0 && reg < 2) {
    wctx->gr[reg] = (uintptr_t)val;
  }
}

void _Unwind_SetIP(struct _Unwind_Context *ctx, unsigned long long val) {
  struct ObWin32UnwindCtx *wctx = (struct ObWin32UnwindCtx *)ctx;
  wctx->target_ip = (uintptr_t)val;
}

/* -----------------------------------------------------------------------
 * _Unwind_RaiseException — phase 1+2 via Windows SEH
 *
 * Stores the exception pointer in TLS so that ob_pl_seh_personality can
 * retrieve it during Windows SEH dispatch, then calls RaiseException.
 *
 * Critical design notes:
 *   1. No __try/__except wrapper here.  If we wrapped the call, Windows
 *      would find the __except handler in THIS frame first (before reaching
 *      the JIT PL frame), and ob_pl_seh_personality would never be called.
 *   2. flags = 0 (NOT EXCEPTION_NONCONTINUABLE).  With NONCONTINUABLE,
 *      if no JIT handler is found Windows terminates the process.  With
 *      flags=0, RtlDispatchException returns FALSE and RaiseException
 *      returns normally to this function, so we can return
 *      _URC_FATAL_PHASE1_ERROR to the PL runtime gracefully.
 *   3. When ob_pl_seh_personality finds a handler it calls RtlUnwindEx,
 *      which unwinds the stack past this frame to the landing pad.
 *      In that case RaiseException never returns here.
 * ----------------------------------------------------------------------- */
_Unwind_Reason_Code _Unwind_RaiseException(struct _Unwind_Exception *exc) {
  ULONG_PTR args[OB_PL_SEH_NARGS];
  args[0] = (ULONG_PTR)exc;
  tl_ob_pl_seh_exc_ptr = exc;
  tl_ob_pl_seh_selector = 0;
  /* flags = 0: continuable exception so RaiseException can return when no
   * handler is found, allowing graceful error propagation. */
  RaiseException(OB_PL_SEH_EXCEPTION_CODE,
                 0,
                 OB_PL_SEH_NARGS,
                 args);
  /* Reached only when no handler was found (RtlDispatchException returned
   * FALSE).  The JIT PL runtime will propagate the OB error code. */
  return _URC_FATAL_PHASE1_ERROR;
}

/* -----------------------------------------------------------------------
 * _Unwind_Resume — re-raise during phase 2 cleanup
 *
 * Same design rationale as _Unwind_RaiseException: no __try wrapper, no
 * EXCEPTION_NONCONTINUABLE, so RtlDispatchException can reach JIT frames.
 * ----------------------------------------------------------------------- */
void _Unwind_Resume(struct _Unwind_Exception *exc) {
  ULONG_PTR args[OB_PL_SEH_NARGS];
  args[0] = (ULONG_PTR)exc;
  tl_ob_pl_seh_exc_ptr = exc;
  RaiseException(OB_PL_SEH_EXCEPTION_CODE,
                 0,
                 OB_PL_SEH_NARGS,
                 args);
  /* If RaiseException returns (no handler), nothing we can do. */
}

/*
 * Linux-specific syscall wrappers that are referenced from ob_sql_nio.cpp
 * and ob_futex.h but have no Windows implementation.
 */
struct epoll_event;


#include <stdint.h>
struct timespec;


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
