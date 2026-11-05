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
 * Windows SEH support for OceanBase PL exception handling.
 *
 * Background:
 *   On Linux/macOS, PL exceptions propagate via the Itanium ABI two-phase
 *   unwinder (_Unwind_RaiseException / _Unwind_Resume). On Windows, the OS
 *   uses Structured Exception Handling (SEH). The two mechanisms are
 *   incompatible, so we bridge them here.
 *
 * Mechanism:
 *   1. _Unwind_RaiseException stores the _Unwind_Exception* in TLS then calls
 *      RaiseException(OB_PL_SEH_EXCEPTION_CODE, ...).
 *   2. Windows SEH dispatch walks the call stack using registered .pdata tables
 *      (registered by ObJitMemoryManager::register_windows_pdata).
 *   3. For each JIT frame with a handler, Windows calls ob_pl_seh_personality
 *      (registered as the "eh_personality" JIT symbol on Windows).
 *   4. ob_pl_seh_personality adapts Windows SEH arguments to the Itanium ABI
 *      signature expected by ObPLEH::eh_personality.
 *   5. When a matching handler is found, ob_pl_seh_personality calls RtlUnwindEx
 *      to unwind the stack to that frame and resume at the landing pad.
 */

#ifndef OCEANBASE_OBSERVER_WIN32_PL_SEH_H_
#define OCEANBASE_OBSERVER_WIN32_PL_SEH_H_

#ifdef _WIN32

/* Include <stdint.h> before <windows.h>: vcruntime.h (pulled in by windows.h)
 * uses uintptr_t at namespace scope, which must be defined first. */
#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * OB PL SEH exception code
 *   Bit 29 = 1 (user-defined), bit 28 = 0 (not hardware), severity = error
 *   0xE0 | 'B' | 'L' | 'D'  (OceanBase PL Dispatch)
 * ----------------------------------------------------------------------- */
#define OB_PL_SEH_EXCEPTION_CODE  0xE0424C44UL

/* Number of ULONG_PTR arguments embedded in the SEH exception:
 *   [0] = pointer to _Unwind_Exception (the OB PL exception object)        */
#define OB_PL_SEH_NARGS           1U

/* -----------------------------------------------------------------------
 * ObWin32UnwindCtx  –  synthetic _Unwind_Context for Windows SEH dispatch
 *
 * When Windows calls the personality function it passes DISPATCHER_CONTEXT*
 * instead of an Itanium _Unwind_Context*. We allocate an ObWin32UnwindCtx
 * on the stack, point our _Unwind_* accessor stubs at it, and pass it as the
 * opaque _Unwind_Context* parameter to ObPLEH::eh_personality.
 * ----------------------------------------------------------------------- */
struct ObWin32UnwindCtx {
    PDISPATCHER_CONTEXT disp_ctx;   /* Windows context (for LSDA, PC, etc.) */
    PCONTEXT            ctx_record; /* CPU register state                    */
    PEXCEPTION_RECORD   exc_record; /* exception record                      */
    uintptr_t           gr[2];      /* saved values from _Unwind_SetGR       */
    uintptr_t           target_ip;  /* saved value from _Unwind_SetIP        */
};

/* -----------------------------------------------------------------------
 * Thread-local state shared between _Unwind_RaiseException and
 * ob_pl_seh_personality.
 * ----------------------------------------------------------------------- */
/* Exception object pointer passed by _Unwind_RaiseException to SEH. */
extern __declspec(thread) void *tl_ob_pl_seh_exc_ptr;

/* Landing-pad selector value saved during the search phase so that the
 * EXCEPTION_TARGET_UNWIND call can install it into the target frame. */
extern __declspec(thread) uintptr_t tl_ob_pl_seh_selector;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _WIN32 */

#endif /* OCEANBASE_OBSERVER_WIN32_PL_SEH_H_ */
