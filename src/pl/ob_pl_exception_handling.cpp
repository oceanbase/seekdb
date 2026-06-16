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

#define USING_LOG_PREFIX PL
#include "ob_pl_exception_handling.h"

namespace oceanbase
{
using namespace common;
using namespace jit;

namespace pl
{

_RLOCAL(_Unwind_Exception*, tl_eptr);

#if defined(__APPLE__) && defined(__aarch64__)
// Workaround for Apple libunwind crash in _Unwind_SetIP on macOS ARM64.
// Apple's unw_set_reg for UNW_REG_IP tries to re-lookup unwind info (compact
// unwind) for the new IP, but JIT frames registered via __register_frame only
// have DWARF unwind info. The lookup returns NULL, causing a crash.
// We bypass _Unwind_SetIP by locating the PC register in the cursor's
// Registers_arm64 layout and writing directly.
//
// Registers_arm64 layout:
//   x[0..28] (29 regs * 8 = 232), fp(x29), lr(x30), sp, pc
//   PC is at offset 32*8 = 256 bytes from x0.
static void safe_Unwind_SetIP(struct _Unwind_Context *context, uintptr_t new_ip) {
  // Place a unique magic value in the eh_return_data register (x0) to find it
  const uintptr_t magic = 0xDEAD1234CAFE5678ULL;
  uintptr_t saved = _Unwind_GetGR(context, __builtin_eh_return_data_regno(0));
  _Unwind_SetGR(context, __builtin_eh_return_data_regno(0), magic);

  uint8_t *ctx = (uint8_t *)context;
  bool found = false;
  // Scan first 2KB for the magic value (register state is near the front)
  for (size_t i = 0; i + 256 + 8 <= 2048; i += sizeof(uintptr_t)) {
    if (*(uintptr_t *)(ctx + i) == magic) {
      // Found x0 at offset i. PC is 256 bytes later.
      *(uintptr_t *)(ctx + i + 256) = new_ip;
      found = true;
      break;
    }
  }

  // Restore x0 to the saved value
  _Unwind_SetGR(context, __builtin_eh_return_data_regno(0), saved);

  if (!found) {
    // Fallback to standard API (may crash — shouldn't reach here)
    _Unwind_SetIP(context, new_ip);
  }
}
#endif
ObPLException pre_reserved_e(OB_ALLOCATE_MEMORY_FAILED); // reserved exception space to prevent exceptions from not being thrown when there is no memory

void ObPLEH::eh_debug_int64(const char *name_ptr, int64_t name_len, int64_t object)
{
  LOG_DEBUG(">>>>>>>>>>0", K(ObString(name_len, name_ptr)), K(object));
}

void ObPLEH::eh_debug_int64ptr(const char *name_ptr, int64_t name_len, const int64_t *object)
{
  LOG_DEBUG(">>>>>>>>>>0", K(ObString(name_len, name_ptr)), K(*object));
}

void ObPLEH::eh_debug_int32(const char *name_ptr, int64_t name_len, int32_t object)
{
  LOG_DEBUG(">>>>>>>>>>0", K(ObString(name_len, name_ptr)), K(object));
}

void ObPLEH::eh_debug_int32ptr(const char *name_ptr, int64_t name_len, const int32_t *object)
{
  LOG_DEBUG(">>>>>>>>>>0", K(ObString(name_len, name_ptr)), K(*object));
}

void ObPLEH::eh_debug_int8(const char *name_ptr, int64_t name_len, const int8_t object)
{
  LOG_DEBUG(">>>>>>>>>>0", K(ObString(name_len, name_ptr)), K(object));
}

void ObPLEH::eh_debug_int8ptr(const char *name_ptr, int64_t name_len, const int8_t *object)
{
  LOG_DEBUG(">>>>>>>>>>0", K(ObString(name_len, name_ptr)), K(*object));
}

void ObPLEH::eh_debug_obj(const char *name_ptr, int64_t name_len, const ObObj *object)
{
  LOG_DEBUG(">>>>>>>>>>0", K(ObString(name_len, name_ptr)), K(*object));
}

void ObPLEH::eh_debug_objparam(const char *name_ptr, int64_t name_len, const ObObjParam *object)
{
  LOG_DEBUG(">>>>>>>>>>0", K(ObString(name_len, name_ptr)), K(*object));
}

int ObPLEH::eh_convert_exception(bool oracle_mode, int oberr, ObPLConditionType *type, int64_t *error_code, const char **sql_state, int64_t *str_len)
{
  UNUSED(oracle_mode);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(type) || OB_ISNULL(error_code) || OB_ISNULL(sql_state)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid Argument", K(oberr), K(type), K(error_code), K(sql_state), K(ret));
  } else {
    *sql_state = ob_sqlstate(oberr);
    *str_len = STRLEN(*sql_state);
    // Oracle mode branch removed (MySQL-only project)
    if (OB_SP_RAISE_APPLICATION_ERROR == oberr) {
      ObWarningBuffer *wb = NULL;
      CK (OB_NOT_NULL(wb = common::ob_get_tsi_warning_buffer()));
      OX (*error_code = wb->get_err_code());
      OX (*sql_state = wb->get_sql_state());
      OX (*str_len = STRLEN(*sql_state));
      if (OB_FAIL(ret)) {
      } else if (-1 == *error_code) {
        *type = SQL_STATE;
      } else {
        *type = ERROR_CODE;
      }
    } else {
      if (oberr < 0) {
        *error_code = ob_mysql_errno(oberr);
        if (-1 == *error_code) {
          *type = SQL_STATE;
        } else {
          *type = ERROR_CODE;
        }
      } else {
        *error_code = oberr;
        *type = SQL_STATE;
      }
    }
  }
  return ret;
}

ObPLException::ObPLException(int64_t error_code)
{
  body_.exception_class = ObPLEHService::get_exception_class();
  body_.exception_cleanup = NULL;
  body_.private_1 = 0;
  body_.private_2 = 0;
  new(&type_)ObPLConditionValue(ERROR_CODE, error_code);
}


ObUnwindException *ObPLEH::eh_create_exception(int64_t pl_context,
                                               int64_t pl_function,
                                               int64_t loc,
                                               int64_t allocator,
                                               const ObPLConditionValue *value)
{
  ObUnwindException *unwind = NULL;
  UNUSED (allocator);
  if (NULL != value) {
    int ret = OB_SUCCESS;
    ObPLContext *pl_ctx = reinterpret_cast<ObPLContext *>(pl_context);
    ObPLExecState *frame = NULL;
    ObIAllocator *pl_allocator = NULL;
    CK (OB_NOT_NULL(pl_ctx));
    CK (pl_ctx->get_exec_stack().count() > 0);
    CK (OB_NOT_NULL(frame = pl_ctx->get_exec_stack().at(0)));
    CK (frame->is_top_call());
    CK (OB_NOT_NULL(pl_allocator = &(frame->get_exec_ctx().expr_alloc_)));
    if (OB_FAIL(ret)) {
    } else if (OB_ALLOCATE_MEMORY_FAILED == value->error_code_) {
      unwind = &pre_reserved_e.body_;
    } else {
      ObPLException *exception
        = static_cast<ObPLException *>(pl_allocator->alloc(sizeof(ObPLException)));
      if (NULL != exception) {
        exception = new(exception)ObPLException();
        unwind = &exception->body_;
        unwind->exception_class = ObPLEHService::get_exception_class();
        unwind->exception_cleanup = NULL;
        exception->type_.type_ = value->type_;
        exception->type_.error_code_ = value->error_code_;
        if (NULL == value->sql_state_ || 0 == value->str_len_) {
          exception->type_.sql_state_ = value->sql_state_;
        } else {
          char* str = static_cast<char*>(pl_allocator->alloc(value->str_len_));
          if (NULL != str) {
            STRNCPY(str, value->sql_state_, value->str_len_);
            exception->type_.sql_state_ = str;
          } else {
            pl_allocator->free(exception);
            unwind = &pre_reserved_e.body_;
          }
        }
        exception->type_.str_len_ = value->str_len_;
        exception->type_.stmt_id_ = value->stmt_id_;
        exception->type_.signal_ = value->signal_;
      } else {
        unwind = &pre_reserved_e.body_;
      }
    }
    tl_eptr = unwind;


  }
  return unwind;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////Runtime Library functions///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename Type_>
uintptr_t ObPLEH::ReadType(const uint8_t *&p)
{
  Type_ value;
  memcpy(&value, p, sizeof(Type_));
  p += sizeof(Type_);
  return static_cast<uintptr_t>(value);
}

uintptr_t ObPLEH::readULEB128(const uint8_t **data)
{
  uintptr_t result = 0;
  uintptr_t shift = 0;
  unsigned char byte;
  const uint8_t *p = *data;

  do {
    byte = *p++;
    result |= (byte & 0x7f) << shift;
    shift += 7;
  }
  while (byte & 0x80);

  *data = p;

  return result;
}

uintptr_t ObPLEH::readSLEB128(const uint8_t **data)
{
  uintptr_t result = 0;
  uintptr_t shift = 0;
  unsigned char byte;
  const uint8_t *p = *data;

  do {
    byte = *p++;
    result |= (byte & 0x7f) << shift;
    shift += 7;
  }
  while (byte & 0x80);

  *data = p;

  if ((byte & 0x40) && (shift < (sizeof(result) << 3))) {
    result |= (~0ULL << shift);
  }

  return result;
}

uintptr_t ObPLEH::readEncodedPointer(const uint8_t **data, uint8_t encoding)
{
  uintptr_t result = 0;
  const uint8_t *p = *data;

  if (encoding == DW_EH_PE_omit)
    return(result);

  // first get value
  switch (encoding & 0x0F) {
    case DW_EH_PE_absptr:
    case DW_EH_PE_signed:
      result = ReadType<uintptr_t>(p);
      break;
    case DW_EH_PE_uleb128:
      result = readULEB128(&p);
      break;
      // Note: This case has not been tested
    case DW_EH_PE_sleb128:
      result = readSLEB128(&p);
      break;
    case DW_EH_PE_udata2:
      result = ReadType<uint16_t>(p);
      break;
    case DW_EH_PE_udata4:
      result = ReadType<uint32_t>(p);
      break;
    case DW_EH_PE_udata8:
      result = ReadType<uint64_t>(p);
      break;
    case DW_EH_PE_sdata2:
      result = ReadType<int16_t>(p);
      break;
    case DW_EH_PE_sdata4:
      result = ReadType<int32_t>(p);
      break;
    case DW_EH_PE_sdata8:
      result = ReadType<int64_t>(p);
      break;
    default:
      // not supported
      ob_abort();
      break;
  }

  // then add relative offset
  switch (encoding & 0x70) {
    case DW_EH_PE_absptr:
      // do nothing
      break;
    case DW_EH_PE_pcrel:
      result += (uintptr_t)(*data);
      break;
    case DW_EH_PE_textrel:
    case DW_EH_PE_datarel:
    case DW_EH_PE_funcrel:
    case DW_EH_PE_aligned:
    default:
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "unsupported DWARF relative encoding in readEncodedPointer",
                    K(encoding), "rel_encoding", encoding & 0x70);
      ob_abort();
      break;
  }

  // then apply indirection
  if (0 != (encoding & DW_EH_PE_indirect)) {
    result = *((uintptr_t*)result);
  }

  *data = p;

  return result;
}

unsigned ObPLEH::getEncodingSize(uint8_t Encoding)
{
  if (Encoding == DW_EH_PE_omit)
    return 0;

  switch (Encoding & 0x0F) {
  case DW_EH_PE_absptr:
  case DW_EH_PE_signed:
    return sizeof(uintptr_t);
  case DW_EH_PE_udata2:
    return sizeof(uint16_t);
  case DW_EH_PE_udata4:
    return sizeof(uint32_t);
  case DW_EH_PE_udata8:
    return sizeof(uint64_t);
  case DW_EH_PE_sdata2:
    return sizeof(int16_t);
  case DW_EH_PE_sdata4:
    return sizeof(int32_t);
  case DW_EH_PE_sdata8:
    return sizeof(int64_t);
  default:
    // not supported
    ob_abort();
  }
  return 0;
}

bool ObPLEH::handleActionValue(int64_t *resultAction,
                               uint8_t TTypeEncoding,
                               const uint8_t *ClassInfo,
                               uintptr_t actionEntry,
                               uint64_t exceptionClass,
                               struct _Unwind_Exception *exceptionObject)
{
  bool ret = false;

  if (!resultAction || !exceptionObject || exceptionClass != ObPLEHService::get_exception_class())
    return(ret);

  ObPLException *excp = (ObPLException*)(((char*) exceptionObject) + ObPLEHService::get_exception_base_offset());
  ObPLConditionValue &condition_value = excp->type_;

  const uint8_t *actionPos = (uint8_t*) actionEntry,
  *tempActionPos;
  int64_t typeOffset = 0;
  int64 actionOffset = 0;

  int64_t precedence = MAX_TYPE;
  for (int i = 0; true; ++i) {
    // Each emitted dwarf action corresponds to a 2 tuple of
    // type info address offset, and action offset to the next
    // emitted action.
    typeOffset = readSLEB128(&actionPos);
    tempActionPos = actionPos;
    actionOffset = readSLEB128(&tempActionPos);

    assert((typeOffset >= 0) && "handleActionValue(...):filters are not supported.");

    // Note: A typeOffset == 0 implies that a cleanup llvm.eh.selector
    //       argument has been matched.
    if (typeOffset > 0) {
      unsigned EncSize = getEncodingSize(TTypeEncoding);
      const uint8_t *EntryP = ClassInfo - typeOffset * EncSize;
      uintptr_t P = readEncodedPointer(&EntryP, TTypeEncoding);
      ObPLConditionValue *ThisClassInfo = reinterpret_cast<ObPLConditionValue*>(P);
      int64_t cur_pre = 0;
      if (OB_SUCCESS !=match_action_value(ThisClassInfo, &condition_value, cur_pre)) {
        LOG_WARN("Bug: Failed to match action value", K(ThisClassInfo), K(condition_value), K(ret));
      } else if (cur_pre < 0) {
        /*do nothing*/
      } else if (cur_pre < precedence) {
        precedence = cur_pre;
        *resultAction = i + 1;
        ret = true;
       break; //Here actually should not break, it should find the one with the highest precedence, but we have already sorted the conditions by precedence in the previous CG phase, so we can break here to improve efficiency
      } else { /*do nothing*/ }
    }

    if (!actionOffset)
      break;

    actionPos += actionOffset;
  }
  return ret;
}

int ObPLEH::match_action_value(const ObPLConditionValue *action, const ObPLConditionValue *exception, int64_t &precedence)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(action) || OB_ISNULL(exception)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parse_tree is NULL", K(action), K(exception), K(ret));
  } else if (ERROR_CODE != exception->type_ && SQL_STATE != exception->type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected exception type", K(exception->type_), K(ret));
  } else {
    switch (action->type_) {
    case ERROR_CODE: {
      precedence = action->error_code_ == exception->error_code_ ? ERROR_CODE : INVALID_TYPE;
      break;
    }
    case SQL_STATE: {
      precedence = exception->str_len_ == action->str_len_ ? (0 == STRNCMP(action->sql_state_, exception->sql_state_, exception->str_len_) ? SQL_STATE : INVALID_TYPE) : INVALID_TYPE;
      break;
    }
    case SQL_EXCEPTION: {
      precedence = (eh_classify_exception(exception->sql_state_) == SQL_EXCEPTION) && !is_internal_error(exception->error_code_) ? SQL_EXCEPTION : INVALID_TYPE;
      break;
    }
    case SQL_WARNING: {
      precedence = eh_classify_exception(exception->sql_state_) == SQL_WARNING ? SQL_WARNING : INVALID_TYPE;
      break;
    }
    case NOT_FOUND: {
      precedence = eh_classify_exception(exception->sql_state_) == NOT_FOUND ? NOT_FOUND : INVALID_TYPE;
      break;
    }
    case OTHERS: {
      if (ERROR_CODE == exception->type_) {
        precedence = is_internal_error(exception->error_code_) ? INVALID_TYPE : OTHERS;
      } else {
        precedence = OTHERS;
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected exception type", K(action->type_), K(ret));
      break;
    }
    }
  }
  return ret;
}

bool ObPLEH::is_internal_error(int error_code)
{
  // these error code is oceanbase inner error, should not catched by exception handler.
  return OB_TRY_LOCK_ROW_CONFLICT == error_code
    || OB_ERR_UNEXPECTED == error_code
    || OB_ALLOCATE_MEMORY_FAILED == error_code
    || OB_ERR_DEFENSIVE_CHECK == error_code
    || OB_TRANS_XA_BRANCH_FAIL == error_code
    || OB_TRANS_SQL_SEQUENCE_ILLEGAL == error_code
    || OB_ERR_SESSION_INTERRUPTED == error_code
    || OB_ERR_QUERY_INTERRUPTED == error_code
    || OB_TIMEOUT == error_code
    || OB_TRANS_TIMEOUT == error_code
    || OB_TRANS_STMT_TIMEOUT == error_code
    || OB_EAGAIN == error_code
    || OB_NOT_MASTER == error_code
    || OB_SNAPSHOT_DISCARDED == error_code;
}

ObPLConditionType ObPLEH::eh_classify_exception(const char *sql_state)
{
  ObPLConditionType type = INVALID_TYPE;
  if (NULL != sql_state) {
    if ('0' == sql_state[0] && '0' == sql_state[1]) {
      type = INVALID_TYPE;
    } else if ('0' == sql_state[0] && '1' == sql_state[1]) {
      type = SQL_WARNING;
    } else if ('0' == sql_state[0] && '2' == sql_state[1]) {
      type = NOT_FOUND;
    } else {
      type = SQL_EXCEPTION;
    }
  }
  return type;
}

_Unwind_Reason_Code ObPLEH::handleLsda(int version,
                                       const uint8_t *lsda,
                                       _Unwind_Action actions,
                                       _Unwind_Exception_Class exceptionClass,
                                       struct _Unwind_Exception *exceptionObject,
                                       struct _Unwind_Context *context)
{
  UNUSED(version);
  _Unwind_Reason_Code ret = _URC_CONTINUE_UNWIND;

  if (NULL != lsda) {
    uintptr_t pc = _Unwind_GetIP(context)-1;

    uintptr_t funcStart = _Unwind_GetRegionStart(context);
    uintptr_t pcOffset = pc - funcStart;
    const uint8_t *ClassInfo = NULL;

    uint8_t lpStartEncoding = *lsda++;

    if (lpStartEncoding != DW_EH_PE_omit) {
      if (0 != (lpStartEncoding & DW_EH_PE_indirect)) {
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "unexpected DW_EH_PE_indirect in lpStart encoding, "
                     "LSDA likely unrelocated, skip frame",
                     K(lpStartEncoding), K(pc), K(funcStart));
        return _URC_CONTINUE_UNWIND;
      }
      uint8_t relEnc = lpStartEncoding & 0x70;
      if (relEnc != DW_EH_PE_absptr && relEnc != DW_EH_PE_pcrel) {
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "unsupported lpStart encoding in LSDA, skip frame",
                     K(lpStartEncoding), K(pc), K(funcStart));
        return _URC_CONTINUE_UNWIND;
      }
      readEncodedPointer(&lsda, lpStartEncoding);
    }

    uint8_t ttypeEncoding = *lsda++;
    uintptr_t classInfoOffset;

    if (ttypeEncoding != DW_EH_PE_omit) {
      classInfoOffset = readULEB128(&lsda);
      ClassInfo = lsda + classInfoOffset;
    }

    uint8_t         callSiteEncoding = *lsda++;

    if (callSiteEncoding != DW_EH_PE_omit) {
      // Call site entries are function-relative offsets and should never use
      // DW_EH_PE_indirect. If the indirect flag is set, the LSDA pointer for
      // this frame is likely unrelocated (known issue with RuntimeDyld on
      // macOS ARM64 for certain JIT-compiled trigger packages). Skip this
      // frame and let the exception propagate to the caller.
      if (0 != (callSiteEncoding & DW_EH_PE_indirect)) {
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "unexpected DW_EH_PE_indirect in call site encoding, "
                     "LSDA likely unrelocated, skip frame",
                     K(callSiteEncoding), K(pc), K(funcStart));
        return _URC_CONTINUE_UNWIND;
      }
      uint8_t relEnc = callSiteEncoding & 0x70;
      if (relEnc != DW_EH_PE_absptr && relEnc != DW_EH_PE_pcrel) {
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "unsupported call site encoding in LSDA, skip frame",
                     K(callSiteEncoding), K(pc), K(funcStart));
        return _URC_CONTINUE_UNWIND;
      }
    }

    uint32_t        callSiteTableLength = static_cast<uint32_t>(readULEB128(&lsda));
    const uint8_t   *callSiteTableStart = lsda;
    const uint8_t   *callSiteTableEnd = callSiteTableStart + callSiteTableLength;
    const uint8_t   *actionTableStart = callSiteTableEnd;
    const uint8_t   *callSitePtr = callSiteTableStart;

    while (callSitePtr < callSiteTableEnd) {
      uintptr_t start = readEncodedPointer(&callSitePtr, callSiteEncoding);
      uintptr_t length = readEncodedPointer(&callSitePtr, callSiteEncoding);
      uintptr_t landingPad = readEncodedPointer(&callSitePtr, callSiteEncoding);

      // Note: Action value
      uintptr_t actionEntry = readULEB128(&callSitePtr);

      if (exceptionClass != ObPLEHService::get_exception_class()) {
        // We have been notified of a foreign exception being thrown,
        // and we therefore need to execute cleanup landing pads
        actionEntry = 0;
      }

      if (0 == landingPad) {
        continue; // no landing pad for this entry
      }

      if (0 != actionEntry) {
        actionEntry += ((uintptr_t) actionTableStart) - 1;
      }

      bool exceptionMatched = false;

      if ((start <= pcOffset) && (pcOffset < (start + length))) {
        int64_t actionValue = 0;

        if (0 != actionEntry) {
          exceptionMatched = handleActionValue(&actionValue,
                                               ttypeEncoding,
                                               ClassInfo,
                                               actionEntry,
                                               exceptionClass,
                                               exceptionObject);
        }

        if (!(actions & _UA_SEARCH_PHASE)) {

          // Found landing pad for the PC.
          // Set Instruction Pointer to so we re-enter function
          // at landing pad. The landing pad is created by the
          // compiler to take two parameters in registers.
          _Unwind_SetGR(context, __builtin_eh_return_data_regno(0), (uintptr_t)exceptionObject);

          // Note: this virtual register directly corresponds
          //       to the return of the llvm.eh.selector intrinsic
          if (!actionEntry || !exceptionMatched) {
            // We indicate cleanup only
            _Unwind_SetGR(context, __builtin_eh_return_data_regno(1), 0);
          } else {
            // Matched type info index of llvm.eh.selector intrinsic
            // passed here.
            _Unwind_SetGR(context, __builtin_eh_return_data_regno(1), actionValue);
          }

          // To execute landing pad set here
#if defined(__APPLE__) && defined(__aarch64__)
          safe_Unwind_SetIP(context, funcStart + landingPad);
#else
          _Unwind_SetIP(context, funcStart + landingPad);
#endif
          ret = _URC_INSTALL_CONTEXT;
        } else if (exceptionMatched) {
          ret = _URC_HANDLER_FOUND;
        } else {
          // Note: Only non-clean up handlers are marked as
          //       found. Otherwise the clean up handlers will be
          //       re-found and executed during the clean up
          //       phase.
        }

        break;
      }
    }
  }
  return ret;
}

_Unwind_Reason_Code ObPLEH::eh_personality(int version, _Unwind_Action actions,
                                   _Unwind_Exception_Class exceptionClass,
                                   ObUnwindException *exceptionObject,
                                   struct _Unwind_Context *context)
{
  const uint8_t *lsda = reinterpret_cast<const uint8_t *>(_Unwind_GetLanguageSpecificData(context));
  _Unwind_Reason_Code ret = handleLsda(version, lsda, actions, exceptionClass, exceptionObject, context);
  LOG_DEBUG(">>>>>>>>>>0", K(version), K(actions), K(exceptionClass), K(lsda));
  return ret;
}

} // pl
} // oceanbase

// ======================================================================
// Windows SEH personality adapter — ob_pl_seh_personality
//
// LLVM encodes the JIT symbol "eh_personality" into UNWIND_INFO.ExceptionHandler
// for every JIT-compiled PL function that contains a landingpad. On Windows,
// RtlDispatchException calls that function with the 4-parameter Windows SEH
// calling convention (not the 5-parameter Itanium ABI). This adapter bridges
// between the two:
//
//   Search phase (no EXCEPTION_UNWINDING in ExceptionFlags):
//     1. Build ObWin32UnwindCtx from DispatcherContext.
//     2. Call ObPLEH::eh_personality(... _UA_SEARCH_PHASE ...) to find a handler.
//     3. If _URC_HANDLER_FOUND: call again with _UA_CLEANUP_PHASE|_UA_HANDLER_FRAME
//        to obtain the landing-pad address (via _Unwind_SetIP) and data-registers
//        (via _Unwind_SetGR).
//     4. Call RtlUnwindEx to initiate stack unwinding to the landing pad.
//
//   Target-frame phase (EXCEPTION_TARGET_UNWIND set in ExceptionFlags):
//     Called by RtlUnwindEx for the frame that claimed the exception.
//     Call personality with _UA_CLEANUP_PHASE|_UA_HANDLER_FRAME, then install
//     the exception-data registers (RAX = exc ptr, RDX = selector) into the
//     CONTEXT so they are restored when execution resumes at the landing pad.
//
//   Cleanup/unwind phase (only EXCEPTION_UNWINDING set):
//     Our JIT frames have no C++ destructors; nothing to do.
// ======================================================================
#ifdef _WIN32

#include "observer/win32_pl_seh.h"

// tl_ob_pl_seh_exc_ptr and tl_ob_pl_seh_selector are declared in win32_pl_seh.h
// (via the extern __declspec(thread) declarations in its extern "C" block)
// and defined in win32_unwind_stubs.c.

using namespace oceanbase::pl;

// Named namespace (not anonymous) per §3.1.
namespace detail {

// Helper: call ObPLEH::eh_personality using a fresh ObWin32UnwindCtx built
// from disp_ctx and return the GR values and target IP in the out-params.
//
// NB: Returns _Unwind_Reason_Code (not OB ret) because the Itanium ABI
// personality contract mandates that return type. §5.2 exemption applies.
static _Unwind_Reason_Code call_personality_with_ctx(
    _Unwind_Action          actions,
    struct _Unwind_Exception *exc,
    DISPATCHER_CONTEXT      *disp_ctx,
    CONTEXT                 *ctx_record,
    EXCEPTION_RECORD        *exc_record,
    uintptr_t               &out_gr0,
    uintptr_t               &out_gr1,
    uintptr_t               &out_ip)
{
  _Unwind_Reason_Code rc = _URC_FATAL_PHASE1_ERROR;
  if (OB_ISNULL(exc) || OB_ISNULL(disp_ctx) || OB_ISNULL(ctx_record) || OB_ISNULL(exc_record)) {
    // Cannot call personality without a complete context. Return a fatal
    // reason so the caller aborts the dispatch for this frame.
    out_gr0 = 0;
    out_gr1 = 0;
    out_ip  = 0;
  } else {
    ObWin32UnwindCtx wctx;
    wctx.disp_ctx   = disp_ctx;
    wctx.ctx_record = ctx_record;
    wctx.exc_record = exc_record;
    wctx.gr[0]      = 0;
    wctx.gr[1]      = 0;
    wctx.target_ip  = 0;

    rc = ObPLEH::eh_personality(
        1, actions,
        exc->exception_class, exc,
        reinterpret_cast<struct _Unwind_Context *>(&wctx));

    out_gr0 = wctx.gr[0];
    out_gr1 = wctx.gr[1];
    out_ip  = wctx.target_ip;
  }
  return rc;
}

} // namespace detail

// Windows SEH personality adapter. Refactored for single-entry single-exit
// per coding standard §5.1. All control flow funnels into the final return
// of `disposition`; side-effects (CtxRecord writes, TLS store, RtlUnwindEx)
// happen only after all branches have settled.
//
// NB: Return type EXCEPTION_DISPOSITION is mandated by the Windows SEH
// personality contract; §5.2 (int ret) exemption applies.
extern "C" EXCEPTION_DISPOSITION ob_pl_seh_personality(
    EXCEPTION_RECORD    *exc_record,
    void                *establisher_frame,
    CONTEXT             *ctx_record,
    DISPATCHER_CONTEXT  *disp_ctx)
{
  // §7.1 type-choice exemption: uintptr_t is used for gr0/gr1/target_ip
  // because they hold raw register/IP values passed through the Itanium
  // _Unwind_SetGR / _Unwind_SetIP ABI (which takes uintptr_t), and must be
  // width-matched to the CPU word on both x86-64 and ARM64 for later
  // assignment into CONTEXT::Rax/Rdx/Rip.
  EXCEPTION_DISPOSITION disposition = ExceptionContinueSearch;
  struct _Unwind_Exception *exc = NULL;
  uintptr_t gr0 = 0;
  uintptr_t gr1 = 0;
  uintptr_t target_ip = 0;
  bool dispatchable = true;

  // §5.7: check input parameters. Windows guarantees non-null, but be
  // defensive so we never dereference a bad pointer.
  if (OB_ISNULL(exc_record) || OB_ISNULL(establisher_frame)
      || OB_ISNULL(ctx_record) || OB_ISNULL(disp_ctx)) {
    dispatchable = false;
  } else if (OB_PL_SEH_EXCEPTION_CODE != exc_record->ExceptionCode) {
    // Only intercept OB PL exceptions.
    dispatchable = false;
  } else {
    // Extract _Unwind_Exception* stored in ExceptionInformation[0].
    if (exc_record->NumberParameters >= OB_PL_SEH_NARGS) {
      exc = reinterpret_cast<struct _Unwind_Exception *>(
                exc_record->ExceptionInformation[0]);
    }
    if (OB_ISNULL(exc)) {
      // Fallback to TLS (set by _Unwind_RaiseException).
      exc = reinterpret_cast<struct _Unwind_Exception *>(tl_ob_pl_seh_exc_ptr);
    }
    if (OB_ISNULL(exc)) {
      dispatchable = false;
    }
  }

  if (dispatchable && (0 != (exc_record->ExceptionFlags & EXCEPTION_TARGET_UNWIND))) {
    // ----------------------------------------------------------------
    // Target-frame phase: RtlUnwindEx has unwound the stack to this frame.
    // Call personality with _UA_HANDLER_FRAME|_UA_CLEANUP_PHASE to trigger
    // _Unwind_SetGR / _Unwind_SetIP, then write the results into ctx_record
    // so the CPU restores RAX/RDX when it resumes at the landing pad.
    // ----------------------------------------------------------------
    _Unwind_Action actions = static_cast<_Unwind_Action>(
        _UA_CLEANUP_PHASE | _UA_HANDLER_FRAME);
    detail::call_personality_with_ctx(actions, exc, disp_ctx, ctx_record, exc_record,
                              gr0, gr1, target_ip);
    // x86-64: eh_return_data_regno(0) = 0 (RAX), (1) = 1 (RDX).
    ctx_record->Rax = gr0;  // exception object pointer
    ctx_record->Rdx = gr1;  // selector
    // disposition stays ExceptionContinueSearch (return to caller of personality)
  } else if (dispatchable
             && 0 != (exc_record->ExceptionFlags
                      & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))) {
    // Cleanup unwind for non-target frames: our JIT frames have no destructors.
    // disposition stays ExceptionContinueSearch.
  } else if (dispatchable) {
    // ----------------------------------------------------------------
    // Search phase: determine whether this frame has a matching handler.
    // ----------------------------------------------------------------
    _Unwind_Reason_Code reason =
        detail::call_personality_with_ctx(_UA_SEARCH_PHASE, exc, disp_ctx, ctx_record, exc_record,
                                  gr0, gr1, target_ip);

    bool found_match = (_URC_HANDLER_FOUND == reason);
    bool cleanup_only = false;

    if (!found_match) {
      // No matching handler in this frame's LSDA action chain. On Linux,
      // Itanium phase-2 still drives the unwinder into cleanup-only landing
      // pads (OB's codegen uses these to chain inner-block handlers to the
      // parent block via `invoke eh_resume_`). Windows SEH has no phase-2,
      // so we simulate it: probe with _UA_CLEANUP_PHASE; if the personality
      // returns a landing pad, treat this frame as the unwind target so
      // RtlUnwindEx will jump to that landing pad. The landing pad itself
      // will invoke eh_resume_, which triggers a fresh RaiseException whose
      // dispatch lands on the parent block's call site (and its action chain
      // has the outer handler).
      _Unwind_Action probe_actions = static_cast<_Unwind_Action>(_UA_CLEANUP_PHASE);
      _Unwind_Reason_Code probe_rc =
          detail::call_personality_with_ctx(probe_actions, exc, disp_ctx, ctx_record, exc_record,
                                    gr0, gr1, target_ip);
      if (_URC_INSTALL_CONTEXT == probe_rc && 0 != target_ip) {
        cleanup_only = true;
      }
    }

    bool ready_to_unwind = false;
    if (found_match) {
      // Handler matched. Call personality again with cleanup+handler-frame
      // flags to obtain the landing-pad address (target_ip) and register values.
      _Unwind_Action cleanup_actions = static_cast<_Unwind_Action>(
          _UA_CLEANUP_PHASE | _UA_HANDLER_FRAME);
      _Unwind_Reason_Code install_rc =
          detail::call_personality_with_ctx(cleanup_actions, exc, disp_ctx, ctx_record, exc_record,
                                    gr0, gr1, target_ip);
      // Personality didn't provide a landing pad → skip unwind.
      if (_URC_INSTALL_CONTEXT == install_rc && 0 != target_ip) {
        ready_to_unwind = true;
      }
    } else if (cleanup_only) {
      // target_ip already populated by the probe call above;
      // gr0/gr1 hold (exc, 0 selector) appropriate for a cleanup landing pad.
      ready_to_unwind = true;
    }

    if (ready_to_unwind) {
      // Save selector in TLS in case EH_TARGET_UNWIND re-derives it differently.
      tl_ob_pl_seh_selector = gr1;

      // Initiate stack unwind to the landing pad.
      // RtlUnwindEx will:
      //   - Unwind all frames between the raise site and establisher_frame.
      //   - Call ob_pl_seh_personality again with EXCEPTION_TARGET_UNWIND
      //     for this frame.
      //   - Place ReturnValue (= exc) into a register (RAX) at the landing pad.
      //   - Resume execution at target_ip with the restored
      //     (and EH_TARGET_UNWIND-patched) CONTEXT.
      // RtlUnwindEx does not return when unwinding succeeds; if it returns,
      // the unwind failed and we fall through to ExceptionContinueSearch.
      RtlUnwindEx(establisher_frame,
                  reinterpret_cast<PVOID>(target_ip),
                  exc_record,
                  reinterpret_cast<PVOID>(exc),   // ReturnValue → RAX at landing pad
                  ctx_record,
                  disp_ctx->HistoryTable);
    }
  }

  return disposition;
}

#endif /* _WIN32 */
