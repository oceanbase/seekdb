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

#include "ob_pl_interpreter.h"
#include "ob_pl.h"
#include "sql/ob_spi.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "pl/ob_pl_user_type.h"            // ObPlCompiteWrite (obj-access write target)
#include "share/ob_errno.h"
#include "lib/string/ob_sql_string.h"      // ObSqlString (null-terminate PL_INTERFACE entry)
#include "lib/oblog/ob_warning_buffer.h"   // ob_get_tsi_warning_buffer (SIGNAL sqlstate)
#include "parser/parse_stmt_item_type.h"  // SignalCondInfoItem (DIAG_MYSQL_ERRNO/MESSAGE_TEXT)

namespace oceanbase
{
using namespace common;
using namespace sql;
namespace pl
{

// Non-local control flow during a tree walk. A statement that diverts control
// (RETURN / LEAVE / ITERATE) sets this; enclosing blocks and loops inspect it
// to stop iterating or unwind. The tree-walk analogue of codegen's branches.
enum class Ctrl { NORMAL, RETURNING, LEAVING, ITERATING, EXITING };

struct CtrlState
{
  CtrlState() : flow(Ctrl::NORMAL), label(), exit_handler(NULL) {}
  Ctrl flow;
  ObString label;  // target label for LEAVE/ITERATE (empty == innermost loop)
  // EXITING: an EXIT handler fired inside a per-statement CONTINUE wrapper. Unwind
  // until the block whose eh declares this handler (HandlerDesc*) as is_original().
  const void *exit_handler;
};

static int exec_stmt(ObPLExecCtx *ctx, const ObPLStmt *stmt, CtrlState &ctrl);
static int exec_block(ObPLExecCtx *ctx, const ObPLStmtBlock *block, CtrlState &ctrl);
static int exec_interface(ObPLExecCtx *ctx, const ObPLInterfaceStmt *s);

// Does `stmt` carry the label `name`? Labels are matched case-insensitively
// (MySQL). A stmt stores indices into the function-wide label table.
static bool stmt_has_label(const ObPLStmt *stmt, const ObString &name)
{
  bool found = false;
  if (OB_NOT_NULL(stmt) && !name.empty()) {
    const ObPLLabelTable *lt = stmt->get_label_table();
    if (OB_NOT_NULL(lt)) {
      for (int64_t j = 0; !found && j < stmt->get_label_cnt(); ++j) {
        const ObString *l = lt->get_label(stmt->get_label_idx(j));
        if (OB_NOT_NULL(l) && l->case_compare_equal(name)) {
          found = true;
        }
      }
    }
  }
  return found;
}

// After a loop body runs, decide whether the loop stops. Consumes a LEAVE/ITERATE
// that targets this loop (resetting to NORMAL); otherwise the signal propagates.
static bool loop_should_stop(CtrlState &ctrl, const ObPLStmt *loop)
{
  bool stop = false;
  if (Ctrl::NORMAL == ctrl.flow) {
    stop = false;
  } else if (Ctrl::RETURNING == ctrl.flow || Ctrl::EXITING == ctrl.flow) {
    stop = true;  // unwind toward the function top / the EXIT handler's declaring block
  } else {
    const bool matches = ctrl.label.empty() || stmt_has_label(loop, ctrl.label);
    if (Ctrl::LEAVING == ctrl.flow) {
      if (matches) { ctrl.flow = Ctrl::NORMAL; }
      stop = true;  // leave this loop (consumed) or propagate outward
    } else {  // ITERATING
      if (matches) { ctrl.flow = Ctrl::NORMAL; stop = false; }  // next iteration
      else { stop = true; }  // belongs to an outer loop
    }
  }
  return stop;
}

// Classify a sqlstate into its condition class (mirrors ObPLEH::eh_classify_exception):
// '00'->none, '01'->warning, '02'->not found, anything else->exception.
static ObPLConditionType classify_sqlstate(const char *sql_state)
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

// Recover the *effective* raised condition (MySQL errno + SQLSTATE) from a
// propagated OB error code, mirroring ObPLEH::eh_convert_exception (MySQL mode).
// A MySQL SIGNAL/RESIGNAL with a SQLSTATE surfaces as OB_SP_RAISE_APPLICATION_ERROR
// (spi_process_resignal raises that via LOG_MYSQL_USER_ERROR and stashes the real
// errno + literal sqlstate in the TSI warning buffer); for that code the true
// condition lives in the warning buffer, not in ob_mysql_errno/ob_sqlstate of the
// surfaced code (whose sqlstate is the generic "HY000"). For every other code the
// condition derives from the code itself, as before.
static void effective_raised_condition(int err, int &mysql_errno, const char *&sql_state)
{
  if (OB_SP_RAISE_APPLICATION_ERROR == err) {
    ObWarningBuffer *wb = common::ob_get_tsi_warning_buffer();
    if (OB_NOT_NULL(wb)) {
      mysql_errno = wb->get_err_code();
      sql_state = wb->get_sql_state();
      return;
    }
  }
  mysql_errno = (err < 0) ? ob_mysql_errno(err) : err;
  sql_state = ob_sqlstate(err);
}

// OB-internal errors must not be swallowed by SQLEXCEPTION/OTHERS handlers.
static bool is_internal_err(int error_code)
{
  return OB_TRY_LOCK_ROW_CONFLICT == error_code
      || OB_ERR_UNEXPECTED == error_code
      || OB_EAGAIN == error_code
      || OB_NOT_MASTER == error_code
      || OB_SNAPSHOT_DISCARDED == error_code;
}

// Does handler condition `action` match raised condition `exception`? Returns the
// match precedence (the matched ObPLConditionType; INVALID_TYPE == no match).
// Local mirror of ObPLEH::match_action_value (which is private). `exception` carries
// the legacy codegen path's numeric code (MySQL errno in MySQL mode) for the ERROR_CODE
// compare; `ob_err` is the raw OB-internal code, used only to exclude OB-internal
// errors from SQLEXCEPTION/OTHERS (handler conditions never name internal codes, and
// some convert to an unmapped -1, so checking the raw code here is strictly correct).
static int64_t match_condition(const ObPLConditionValue &action, const ObPLConditionValue &exception,
                               int ob_err)
{
  int64_t precedence = INVALID_TYPE;
  switch (action.type_) {
    case ERROR_CODE:
      precedence = (action.error_code_ == exception.error_code_) ? ERROR_CODE : INVALID_TYPE;
      break;
    case SQL_STATE:
      precedence = (exception.str_len_ == action.str_len_
                    && OB_NOT_NULL(action.sql_state_) && OB_NOT_NULL(exception.sql_state_)
                    && 0 == STRNCMP(action.sql_state_, exception.sql_state_, exception.str_len_))
                   ? SQL_STATE : INVALID_TYPE;
      break;
    case SQL_EXCEPTION:
      precedence = (classify_sqlstate(exception.sql_state_) == SQL_EXCEPTION
                    && !is_internal_err(ob_err)) ? SQL_EXCEPTION : INVALID_TYPE;
      break;
    case SQL_WARNING:
      precedence = (classify_sqlstate(exception.sql_state_) == SQL_WARNING) ? SQL_WARNING : INVALID_TYPE;
      break;
    case NOT_FOUND:
      precedence = (classify_sqlstate(exception.sql_state_) == NOT_FOUND) ? NOT_FOUND : INVALID_TYPE;
      break;
    case OTHERS:
      precedence = is_internal_err(ob_err) ? INVALID_TYPE : OTHERS;
      break;
    default:
      precedence = INVALID_TYPE;
      break;
  }
  return precedence;
}

// Look for a handler in `eh` matching the raised error `err`. The most specific
// match wins (ObPLConditionType is ordered high-to-low priority; lower enum value
// == more specific, so we keep the lowest precedence). Reuses ObPLEH's matcher so
// it agrees with the legacy codegen path. If found, runs the handler body and reports whether it
// is an EXIT handler (leave the declaring block) or CONTINUE (resume after).
// Match the raised condition `exception` against `eh`'s handlers (most-specific wins;
// innermost-scope on equal precedence) and run the chosen handler body. `mysql_errno`
// is installed as the SQLCODE on handler entry; `ob_err` is the raw OB code (only for
// the internal-error exclusion in match_condition). Shared by the error path
// (try_handle) and the raised-warning path (try_handle_warning).
static int run_matched_handler(ObPLExecCtx *ctx, const ObPLDeclareHandlerStmt *eh,
                               const ObPLConditionValue &exception, int mysql_errno,
                               int ob_err, CtrlState &ctrl, bool &handled, bool &is_exit)
{
  typedef ObPLDeclareHandlerStmt::DeclareHandler::HandlerDesc HandlerDesc;
  int ret = OB_SUCCESS;
  // Mirror ObPLDeclareHandlerStmt::DeclareHandler::compare_condition: rank candidates by
  // declaring scope level FIRST (higher level == innermost scope wins), breaking ties only
  // at equal level by condition-type specificity (smaller ObPLConditionType enum is more
  // specific). The resolver stacks every active handler into this block's eh tagged with
  // its scope level, so an inner SQLWARNING beats an outer SQLSTATE match.
  int64_t best_type = MAX_TYPE;
  int64_t best_level = OB_INVALID_INDEX;
  const HandlerDesc *chosen = NULL;
  for (int64_t i = 0; i < eh->get_handlers().count(); ++i) {
    const ObPLDeclareHandlerStmt::DeclareHandler &h = eh->get_handler(i);
    const HandlerDesc *desc = h.get_desc();
    if (OB_ISNULL(desc)) { continue; }
    const int64_t level = h.get_level();
    for (int64_t j = 0; j < desc->get_conditions().count(); ++j) {
      const int64_t pre = match_condition(desc->get_condition(j), exception, ob_err);
      if (pre < 0) { continue; }
      const bool better = (NULL == chosen)
          || (level > best_level)
          || (level == best_level && pre < best_type);
      if (better) { best_level = level; best_type = pre; chosen = desc; }
    }
  }
  if (OB_NOT_NULL(chosen)) {
    handled = true;
    is_exit = chosen->is_exit();
    // Mirror the legacy handler bracket: on entry, snapshot the caught condition's warning
    // buffer onto the diagnostic stack (spi_get_pl_exception_code) and make its errno the
    // SQLCODE; on normal exit, restore+pop. A native error carries no sqlstate on the
    // buffer, so stamp the caught sqlstate (e.g. 23000) now -- a bare RESIGNAL recovers it
    // from the snapshot instead of falling back to HY000.
    if (common::ObWarningBuffer *wb = common::ob_get_tsi_warning_buffer()) {
      // A native error's message can be wiped off the buffer by an inner frame before the
      // handler catches it (errno + sqlstate survive); restore the standard message for the
      // caught OB code so a bare RESIGNAL re-raises it with text, not an empty message. Skip
      // user SIGNALs (OB_SP_RAISE_APPLICATION_ERROR), whose message is user-supplied and lives
      // on the buffer.
      const char *live_msg = wb->get_err_msg();
      if ((OB_ISNULL(live_msg) || '\0' == live_msg[0])
          && OB_SUCCESS != ob_err && OB_SP_RAISE_APPLICATION_ERROR != ob_err) {
        wb->set_error(ob_strerror(ob_err), wb->get_err_code());  // keep errno, restore message
      }
      if (OB_NOT_NULL(exception.sql_state_) && '\0' != exception.sql_state_[0]) {
        wb->set_sql_state(exception.sql_state_);
      }
    }
    const int64_t level = eh->get_level();
    int64_t saved_code = OB_SUCCESS;
    OZ (ObSPIService::spi_get_pl_exception_code(ctx, &saved_code));
    OZ (ObSPIService::spi_set_pl_exception_code(ctx, mysql_errno, false /*keep warning buf*/, level));
    // Record the caught condition's severity for a bare RESIGNAL in the body: try_handle (the
    // error search) passes the real error in ob_err, try_handle_warning passes OB_SUCCESS. A bare
    // RESIGNAL re-raises with this severity, so an error-severity warning-class sqlstate (a
    // strict-mode 1265 with sqlstate 01000) re-raises as an error instead of downgrading.
    ObPLSqlCodeInfo *sci = (OB_NOT_NULL(ctx->exec_ctx_) && OB_NOT_NULL(ctx->exec_ctx_->get_my_session()))
        ? ctx->exec_ctx_->get_my_session()->get_pl_sqlcode_info() : NULL;
    const bool saved_is_err = OB_NOT_NULL(sci) ? sci->is_caught_error() : false;
    if (OB_NOT_NULL(sci)) { sci->set_caught_error(OB_SUCCESS != ob_err); }
    if (OB_SUCC(ret)) {
      const int hret = SMART_CALL(exec_block(ctx, chosen->get_body(), ctrl));
      if (OB_SUCCESS == hret) {
        // Body fell through normally: restore the prior SQLCODE and pop the snapshot.
        ret = ObSPIService::spi_set_pl_exception_code(ctx, saved_code, true /*pop warning buf*/, level);
      } else {
        // Body re-raised (SIGNAL / RESIGNAL / fresh error): propagate with its own SQLCODE
        // (the legacy path emits restore+pop only on normal fall-through, not the re-raise path).
        ret = hret;
      }
    }
    if (OB_NOT_NULL(sci)) { sci->set_caught_error(saved_is_err); }
    // An EXIT handler leaves the block that DECLARED it. Mark EXITING toward the chosen
    // handler's desc; each block's end-of-block check stops it at the declaring block. When
    // the handler is native to the current block, that reset fires immediately on return (net
    // same as a plain break). When it was passed down into a per-statement CONTINUE wrapper,
    // or found in an OUTER block during the raised-warning search, EXITING propagates up to
    // the real declaring block. Only on normal fall-through -- a RETURN/re-raise wins.
    if (OB_SUCC(ret) && is_exit && Ctrl::NORMAL == ctrl.flow) {
      ctrl.flow = Ctrl::EXITING;
      ctrl.exit_handler = static_cast<const void *>(chosen);
    }
  }
  return ret;
}

// Look for a handler in `eh` matching the raised error `err` and run it.
static int try_handle(ObPLExecCtx *ctx, const ObPLDeclareHandlerStmt *eh, int err,
                      CtrlState &ctrl, bool &handled, bool &is_exit)
{
  int ret = OB_SUCCESS;
  handled = false;
  is_exit = false;
  if (OB_NOT_NULL(eh)) {
    ObPLConditionValue exception;
    // Build the raised condition as the legacy path does: a numeric handler ("handler for NNNN")
    // matches the MySQL error number, so convert err -> ob_mysql_errno(err); when there is
    // no MySQL mapping (-1) match by SQLSTATE. sql_state stays the OB code's sqlstate (the
    // SQLEXCEPTION/SQLWARNING/NOT FOUND class match).
    int mysql_errno = -1;
    const char *eff_sql_state = NULL;
    effective_raised_condition(err, mysql_errno, eff_sql_state);
    exception.type_ = (-1 == mysql_errno) ? SQL_STATE : ERROR_CODE;
    exception.error_code_ = mysql_errno;
    exception.sql_state_ = eff_sql_state;
    exception.str_len_ = OB_NOT_NULL(exception.sql_state_) ? STRLEN(exception.sql_state_) : 0;
    ret = run_matched_handler(ctx, eh, exception, mysql_errno, err, ctrl, handled, is_exit);
  }
  return ret;
}

// A raised *warning* (a SIGNAL of a 01xxx/02xxx sqlstate, or a statement that produced a
// warning) also fires a matching CONTINUE/EXIT handler -- the interpreter is otherwise
// error-driven and would miss it (the legacy path checks the diagnostic area for warnings after
// every statement). Build the condition from the warning's errno + sqlstate and run the
// same search; is_exit is reported so the caller can unwind.
static int try_handle_warning(ObPLExecCtx *ctx, const ObPLDeclareHandlerStmt *eh,
                              int warn_errno, const char *warn_sql_state,
                              CtrlState &ctrl, bool &handled, bool &is_exit)
{
  int ret = OB_SUCCESS;
  handled = false;
  is_exit = false;
  if (OB_NOT_NULL(eh) && OB_NOT_NULL(warn_sql_state) && '\0' != warn_sql_state[0]) {
    ObPLConditionValue exception;
    exception.type_ = SQL_STATE;
    exception.error_code_ = warn_errno;
    exception.sql_state_ = warn_sql_state;
    exception.str_len_ = STRLEN(warn_sql_state);
    // ob_err == OB_SUCCESS: a warning is never an OB-internal error, so it is not excluded
    // from the SQLWARNING/SQLEXCEPTION class match.
    ret = run_matched_handler(ctx, eh, exception, warn_errno, OB_SUCCESS, ctrl, handled, is_exit);
  }
  return ret;
}

static int exec_block(ObPLExecCtx *ctx, const ObPLStmtBlock *block, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(block)) {
    const ObIArray<ObPLStmt *> &stmts = block->get_stmts();
    // get_eh() has no const overload; the block is logically const here.
    const ObPLDeclareHandlerStmt *eh = static_cast<const ObPLDeclareHandlerStmt *>(
        const_cast<ObPLStmtBlock *>(block)->get_eh());
    for (int64_t i = 0; OB_SUCC(ret) && Ctrl::NORMAL == ctrl.flow && i < stmts.count(); ++i) {
      common::ObWarningBuffer *wb = common::ob_get_tsi_warning_buffer();
      const uint32_t warn_before = OB_NOT_NULL(wb) ? wb->get_total_warning_count() : 0;
      int sret = SMART_CALL(exec_stmt(ctx, stmts.at(i), ctrl));
      if (OB_SUCCESS != sret) {
        // A statement raised a condition: search this block's handlers.
        bool handled = false;
        bool is_exit = false;
        if (OB_FAIL(try_handle(ctx, eh, sret, ctrl, handled, is_exit))) {
          // the handler body itself failed; propagate that
        } else if (handled) {
          if (is_exit) { break; }  // EXIT handler -> leave block; CONTINUE -> next stmt
        } else {
          // No handler. An unhandled NOT FOUND / SQLWARNING raised *by a
          // SELECT..INTO with no rows* is a completion condition: a warning,
          // INTO vars unchanged, execution continues. Everything else propagates
          // -- a cursor FETCH past the end raises a hard error 1329, a NOT FOUND
          // bubbling up through a CALL must keep bubbling, and real exceptions
          // abort. So only swallow when *this* statement is the SELECT..INTO
          // (PL_SQL); matching on "not PL_FETCH" was too broad and swallowed
          // NOT FOUND that propagated up through non-FETCH statements.
          int eff_errno = -1;
          const char *eff_sql_state = NULL;
          effective_raised_condition(sret, eff_errno, eff_sql_state);
          const ObPLConditionType cls = classify_sqlstate(eff_sql_state);
          const bool completion = (NOT_FOUND == cls || SQL_WARNING == cls)
                                  && PL_SQL == stmts.at(i)->get_type();
          if (!completion) {
            ret = sret;  // propagate to the enclosing block
            break;
          }
        }
      } else if (PL_BLOCK != stmts.at(i)->get_type()) {
        // The statement succeeded but may have raised a *warning* (SIGNAL '01000', a
        // truncation, a SELECT..INTO that found nothing). MySQL fires a matching
        // SQLWARNING / NOT FOUND / specific CONTINUE/EXIT handler for it; the interpreter is
        // otherwise error-driven and the legacy path checks the diagnostic area after every statement
        // -- mirror that here. Only a non-block statement triggers the search: a nested block
        // already ran this check for whichever of its own statements actually raised, so
        // gating on PL_BLOCK fires the search exactly once, at the innermost raising statement.
        wb = common::ob_get_tsi_warning_buffer();
        if (OB_NOT_NULL(wb) && wb->get_total_warning_count() > warn_before
            && wb->get_readable_warning_count() > 0) {
          const ObWarningBuffer::WarningItem *item =
              wb->get_warning_item(wb->get_readable_warning_count() - 1);
          if (OB_NOT_NULL(item)) {
            // A warning-class SIGNAL is appended with only an errno (OB_ERR_SIGNAL_WARN) and an
            // empty sqlstate; recover the class from the errno so the SQLWARNING match works.
            const char *wss = item->get_sql_state();
            if (OB_ISNULL(wss) || '\0' == wss[0]) { wss = ob_sqlstate(item->get_code()); }
            const ObPLConditionType wcls = classify_sqlstate(wss);
            if (SQL_WARNING == wcls || NOT_FOUND == wcls) {
              // Search this statement's block and each enclosing block, innermost first. An EXIT
              // handler in an outer scope is not copied into the inner block's eh (only CONTINUE
              // handlers are passed down), so the warning search must climb the block chain the
              // way an error propagates by return code -- and stop at the first match.
              bool whandled = false;
              bool wexit = false;
              for (const ObPLStmtBlock *b = block;
                   OB_SUCC(ret) && !whandled && OB_NOT_NULL(b);
                   b = b->get_block()) {
                const ObPLDeclareHandlerStmt *beh = static_cast<const ObPLDeclareHandlerStmt *>(
                    const_cast<ObPLStmtBlock *>(b)->get_eh());
                if (OB_NOT_NULL(beh)) {
                  ret = try_handle_warning(ctx, beh, item->get_code(), wss, ctrl, whandled, wexit);
                }
              }
              if (OB_SUCC(ret) && whandled && wexit) {
                break;  // EXIT handler: leave this block (EXITING unwinds to the declaring one)
              }
            }
          }
        }
      }
    }
    // LEAVE <label> of a labeled BEGIN..END block ends here.
    if (OB_SUCC(ret) && Ctrl::LEAVING == ctrl.flow && stmt_has_label(block, ctrl.label)) {
      ctrl.flow = Ctrl::NORMAL;
    }
    // A propagating EXIT (Ctrl::EXITING) lands at the block that declares the handler
    // as original; it has left this block, so reset to NORMAL and let the parent run on.
    if (OB_SUCC(ret) && Ctrl::EXITING == ctrl.flow && OB_NOT_NULL(eh)) {
      for (int64_t i = 0; i < eh->get_handlers().count(); ++i) {
        if (eh->get_handler(i).is_original()
            && static_cast<const void *>(eh->get_handler(i).get_desc()) == ctrl.exit_handler) {
          ctrl.flow = Ctrl::NORMAL;
          ctrl.exit_handler = NULL;
          break;
        }
      }
    }
  }
  return ret;
}

// SET into[i] = value[i]. Two target shapes (mirrors codegen visit(ObPLAssignStmt)):
//   - PL-local scalar: the into expr is a const "unknown" whose value is the target
//     variable's slot; evaluating value with that result_idx stores it directly
//     (ob_spi.cpp: ctx->params_->at(result_idx) = computed value).
//   - SET @user_var / @@sys_var / package / subprogram var: the into expr is a sys-func
//     expr (T_OP_GET_USER_VAR / T_OP_GET_SYS_VAR / ...). Evaluate the RHS into a temp
//     objparam (result_idx == OB_INVALID_INDEX, so it is NOT stored into a slot), then
//     hand the into expr + value to spi_set_variable_to_expr, exactly as codegen's
//     generate_set_variable does. is_default carries a `SET x = DEFAULT`.
static int exec_assign(ObPLExecCtx *ctx, const ObPLAssignStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s));
  for (int64_t i = 0; OB_SUCC(ret) && i < s->get_into().count(); ++i) {
    int64_t result_idx = OB_INVALID_INDEX;
    const ObRawExpr *into = s->get_into_expr(i);
    if (OB_NOT_NULL(into) && into->is_const_raw_expr()) {
      const ObConstRawExpr *c = static_cast<const ObConstRawExpr *>(into);
      if (c->get_value().is_unknown()) {
        result_idx = c->get_value().get_unknown();
      }
    }
    if (OB_INVALID_INDEX != result_idx) {
      ObObjParam result;
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_value_index(i), result_idx, &result));
    } else if (OB_NOT_NULL(into) && into->is_sys_func_expr()) {
      ObObjParam result;
      const ObRawExpr *value_expr = s->get_value_expr(i);
      const bool is_default = OB_NOT_NULL(value_expr) && T_DEFAULT == value_expr->get_expr_type();
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_value_index(i), OB_INVALID_INDEX, &result));
      OZ (ObSPIService::spi_set_variable_to_expr(ctx, s->get_into_index(i), &result, is_default));
    } else if (OB_NOT_NULL(into) && into->is_obj_access_expr()) {
      // Obj-access write target (a trigger's SET NEW.col = expr, a record field, a
      // collection element). Mirrors codegen visit(ObPLAssignStmt)'s is_obj_access_expr()
      // branch for the scalar (obj-type) case -- the only obj-access write reachable in
      // MySQL mode (records via %ROWTYPE and collections are Oracle-only / unparseable here).
      //
      // Evaluating the for-write obj-access expr yields an objparam whose extend points to a
      // ObPlCompiteWrite { allocator_, value_addr_ }: value_addr_ is the destination ObObj*,
      // allocator_ the allocator for deep-copying string/number payloads. spi_copy_datum then
      // converts + copies the computed RHS into that destination, exactly the runtime call
      // codegen emits via ObObjType::generate_copy (a NULL allocator falls back to the ctx
      // statement allocator inside spi_copy_datum).
      ObObjParam rhs;
      ObObjParam into_addr;
      ObPLDataType final_type;
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_value_index(i), OB_INVALID_INDEX, &rhs));
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_into_index(i), OB_INVALID_INDEX, &into_addr));
      OZ (static_cast<const ObObjAccessRawExpr *>(into)->get_final_type(final_type));
      if (OB_FAIL(ret)) {
      } else if (!final_type.is_obj_type()) {
        // Composite (record / collection) write targets are Oracle-only and not reachable in
        // MySQL mode; leave them unsupported rather than ship an untested deep-copy path.
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("[pl-interp] composite obj-access assignment not supported yet", K(ret), K(i));
      } else if (OB_UNLIKELY(!into_addr.is_ext() || 0 == into_addr.get_ext())) {
        // The for-write obj-access evaluates to an extend objparam whose value is an
        // ObPlCompiteWrite*; is_ext() (not the stricter is_pl_extend()) is the right guard
        // -- calc_obj_access_expr resets the param meta to the field's result type, so the
        // extend_type no longer satisfies is_pl_extend(), but the value is still the address
        // (mirrors ObSPIService::check_exist_in_into_exprs: CK(into.is_ext()) + get_ext()).
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("[pl-interp] obj-access write target has no address", K(ret));
      } else {
        ObPlCompiteWrite *cw = reinterpret_cast<ObPlCompiteWrite *>(into_addr.get_ext());
        ObObj *dest = OB_NOT_NULL(cw) ? reinterpret_cast<ObObj *>(cw->value_addr_) : NULL;
        ObIAllocator *alloc = OB_NOT_NULL(cw) ? reinterpret_cast<ObIAllocator *>(cw->allocator_) : NULL;
        common::ObDataType *dest_type = final_type.get_data_type();
        CK (OB_NOT_NULL(cw), OB_NOT_NULL(dest), OB_NOT_NULL(dest_type));
        if (OB_SUCC(ret) && final_type.get_not_null() && rhs.is_null()) {
          ret = OB_ERR_NUMERIC_OR_VALUE_ERROR;
          LOG_WARN("[pl-interp] NOT NULL obj-access target assigned NULL", K(ret));
        }
        OZ (ObSPIService::spi_copy_datum(ctx, alloc, &rhs, dest, dest_type));
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("[pl-interp] assignment target is not a simple variable yet", K(ret), K(i));
    }
  }
  return ret;
}

// DECLARE v ... DEFAULT expr: store the default into each declared variable's slot.
static int exec_declare(ObPLExecCtx *ctx, const ObPLDeclareVarStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s));
  if (OB_SUCC(ret) && OB_INVALID_INDEX != s->get_default()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < s->get_index().count(); ++i) {
      ObObjParam result;
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_default(), s->get_index(i), &result));
    }
  }
  return ret;
}

static int exec_if(ObPLExecCtx *ctx, const ObPLIfStmt *s, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s));
  if (OB_SUCC(ret)) {
    ObObjParam cond;
    OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_cond(), OB_INVALID_INDEX, &cond));
    if (OB_FAIL(ret)) {
    } else if (cond.is_true()) {
      OZ (SMART_CALL(exec_block(ctx, s->get_then(), ctrl)));
    } else if (OB_NOT_NULL(s->get_else())) {
      OZ (SMART_CALL(exec_block(ctx, s->get_else(), ctrl)));
    }
  }
  return ret;
}

// CASE: simple (CASE x WHEN v ...) computes x into the case var first, then each
// WHEN expr is a boolean test against it; searched (CASE WHEN cond ...) tests
// each WHEN expr directly. Either way it is an IF/ELSEIF chain.
static int exec_case(ObPLExecCtx *ctx, const ObPLCaseStmt *s, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s));
  if (OB_SUCC(ret) && OB_INVALID_INDEX != s->get_case_expr() && OB_INVALID_INDEX != s->get_case_var()) {
    ObObjParam tmp;
    OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_case_expr(), s->get_case_var(), &tmp));
  }
  bool matched = false;
  const ObPLCaseStmt::WhenClauses &whens = s->get_when_clauses();
  for (int64_t i = 0; OB_SUCC(ret) && !matched && i < whens.count(); ++i) {
    ObObjParam cond;
    OZ (ObSPIService::spi_calc_expr_at_idx(ctx, whens.at(i).expr_, OB_INVALID_INDEX, &cond));
    if (OB_SUCC(ret) && cond.is_true()) {
      matched = true;
      OZ (SMART_CALL(exec_block(ctx, whens.at(i).body_, ctrl)));
    }
  }
  if (OB_SUCC(ret) && !matched && OB_NOT_NULL(s->get_else_clause())) {
    OZ (SMART_CALL(exec_block(ctx, s->get_else_clause(), ctrl)));
  }
  return ret;
}

// KILL QUERY (what the mysql client sends on Ctrl+C) and query / transaction
// timeout only set flags on the session; a running loop must poll them or it can
// never be interrupted. Poll on the first iteration and then every 10000th — the
// cadence the legacy codegen path used (generate_early_exit, EARLY_EXIT_CHECK_CNT) — so
// the per-iteration cost stays negligible.
static const int64_t EARLY_EXIT_CHECK_CNT = 10000;

static int loop_check_early_exit(ObPLExecCtx *ctx, int64_t &count)
{
  int ret = OB_SUCCESS;
  ++count;
  if (1 == count || count >= EARLY_EXIT_CHECK_CNT) {
    count = 1;
    OZ (ObSPIService::spi_check_early_exit(ctx));
  }
  return ret;
}

static int exec_while(ObPLExecCtx *ctx, const ObPLCondLoop *s, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  int64_t check_count = 0;
  CK (OB_NOT_NULL(s));
  while (OB_SUCC(ret)) {
    ObObjParam cond;
    OZ (loop_check_early_exit(ctx, check_count));
    OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_cond(), OB_INVALID_INDEX, &cond));
    if (OB_FAIL(ret) || !cond.is_true()) {
      break;
    }
    OZ (SMART_CALL(exec_block(ctx, s->get_body(), ctrl)));
    if (OB_FAIL(ret) || loop_should_stop(ctrl, s)) {
      break;
    }
  }
  return ret;
}

// Plain LOOP ... END LOOP: runs until a LEAVE (or RETURN) breaks out.
static int exec_loop(ObPLExecCtx *ctx, const ObPLLoopStmt *s, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  int64_t check_count = 0;
  CK (OB_NOT_NULL(s));
  while (OB_SUCC(ret)) {
    OZ (loop_check_early_exit(ctx, check_count));
    OZ (SMART_CALL(exec_block(ctx, s->get_body(), ctrl)));
    if (OB_FAIL(ret) || loop_should_stop(ctrl, s)) {
      break;
    }
  }
  return ret;
}

// REPEAT ... UNTIL cond END REPEAT: body first, then stop once cond is true.
static int exec_repeat(ObPLExecCtx *ctx, const ObPLRepeatStmt *s, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  int64_t check_count = 0;
  CK (OB_NOT_NULL(s));
  while (OB_SUCC(ret)) {
    OZ (loop_check_early_exit(ctx, check_count));
    OZ (SMART_CALL(exec_block(ctx, s->get_body(), ctrl)));
    if (OB_FAIL(ret) || loop_should_stop(ctrl, s)) {
      break;
    }
    ObObjParam cond;
    OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_cond(), OB_INVALID_INDEX, &cond));
    if (OB_FAIL(ret) || cond.is_true()) {
      break;
    }
  }
  return ret;
}

// LEAVE / ITERATE <label>. ObPLLoopControl may carry a guard cond (Oracle EXIT
// WHEN); in MySQL it is unconditional. Sets the control state for loops to act on.
static int exec_loop_control(ObPLExecCtx *ctx, const ObPLLoopControl *s, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s));
  bool fire = true;
  if (OB_SUCC(ret) && OB_INVALID_INDEX != s->get_cond()) {
    ObObjParam cond;
    OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_cond(), OB_INVALID_INDEX, &cond));
    OX (fire = cond.is_true());
  }
  if (OB_SUCC(ret) && fire) {
    ctrl.flow = (PL_LEAVE == s->get_type()) ? Ctrl::LEAVING : Ctrl::ITERATING;
    ctrl.label = s->get_next_label();
  }
  return ret;
}

// RETURN [expr]: store the return value into ctx->result_ (functions) and unwind.
static int exec_return(ObPLExecCtx *ctx, const ObPLReturnStmt *s, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s), OB_NOT_NULL(ctx));
  if (OB_SUCC(ret) && OB_INVALID_INDEX != s->get_ret()) {
    ObObjParam value;
    OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_ret(), OB_INVALID_INDEX, &value));
    if (OB_SUCC(ret) && OB_NOT_NULL(ctx->result_) && OB_NOT_NULL(ctx->allocator_)) {
      // Deep-copy onto the function's exec allocator: a NUMBER/DECIMAL/string
      // value carries data on the transient per-expr allocator, so a shallow
      // assign would dangle once that allocator is reset (INT is inline, hence
      // it worked). exec allocator outlives statement evaluation.
      OZ (ob_write_obj(*ctx->allocator_, value, *ctx->result_));
    }
  }
  OX (ctrl.flow = Ctrl::RETURNING);
  return ret;
}

// Make a null-terminated C string copy of `src` on the exec allocator (spi takes char*).
static int dup_cstr(ObPLExecCtx *ctx, const ObString &src, const char *&out)
{
  int ret = OB_SUCCESS;
  char *buf = static_cast<char *>(ctx->allocator_->alloc(src.length() + 1));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("[pl-interp] failed to allocate sql buffer", K(ret), K(src.length()));
  } else {
    MEMCPY(buf, src.ptr(), src.length());
    buf[src.length()] = '\0';
    out = buf;
  }
  return ret;
}

// Embedded SQL (SELECT..INTO / INSERT / UPDATE / DELETE). Mirrors codegen's
// generate_sql: spi_query_into_expr_idx when there are no PL params, else
// spi_execute_with_expr_idx with the prepared (ps) statement. All param/into
// arrays are expr indices into func->get_expressions(); spi resolves them.
static int exec_sql(ObPLExecCtx *ctx, const ObPLSqlStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s), OB_NOT_NULL(ctx), OB_NOT_NULL(ctx->allocator_));
  if (OB_SUCC(ret)) {
    const int64_t type = static_cast<int64_t>(s->get_stmt_type());
    const int64_t into_count = s->get_into().count();
    const int64_t *into_idx = into_count > 0 ? &s->get_into().at(0) : NULL;
    const int64_t type_count = s->get_data_type().count();
    const ObDataType *types = type_count > 0 ? &s->get_data_type().at(0) : NULL;
    const bool *not_null = s->get_not_null_flags().count() > 0 ? &s->get_not_null_flags().at(0) : NULL;
    const int64_t *ranges = s->get_pl_integer_ranges().count() > 0 ? &s->get_pl_integer_ranges().at(0) : NULL;
    const bool is_bulk = s->is_bulk();
    const bool is_type_record = s->is_type_record();
    const bool for_update = s->is_for_update();
    if (s->get_params().empty()) {
      const char *sql = NULL;
      OZ (dup_cstr(ctx, s->get_sql(), sql));
      OZ (ObSPIService::spi_query_into_expr_idx(ctx, sql, type, into_idx, into_count,
            types, type_count, not_null, ranges, is_bulk, is_type_record, for_update));
    } else {
      const char *ps_sql = NULL;
      OZ (dup_cstr(ctx, s->get_ps_sql(), ps_sql));
      const int64_t param_count = s->get_params().count();
      const int64_t *param_idx = param_count > 0 ? &s->get_params().at(0) : NULL;
      OZ (ObSPIService::spi_execute_with_expr_idx(ctx, ps_sql, type, param_idx, param_count,
            into_idx, into_count, types, type_count, not_null, ranges,
            is_bulk, s->is_forall_sql(), is_type_record, for_update));
    }
  }
  return ret;
}

// CALL proc(args): build the ObObjParam* argv, then ObPL::execute_proc loads and
// runs the callee (recursing back into this interpreter for an interpreted callee).
//
// Every actual -- IN, OUT and INOUT alike -- is passed through an independent temp
// objparam (storage[i]), never a direct alias to the caller's variable slot. This
// mirrors codegen's visit(ObPLCallStmt), which evaluates each actual into a fresh
// argv buffer and only copies the result back afterwards (generate_out_params).
// Aliasing the caller slot is what crashed: execute_proc shallow-copies *argv[i]
// into the callee's param store, and the callee's final()/destruct_objparam then
// frees that string through the callee allocator -- aborting when the buffer is
// really owned by the caller (ObVSliceAlloc::free's abort_unless). It happened to
// survive for INT (inline, nothing to free) but not for VARCHAR.
//
// For OUT/INOUT with a local target (out_idx_ valid):
//   - IN value: pure-OUT starts empty (default objparam, like codegen's empty
//     buffer); INOUT pre-evaluates the actual so the callee sees the input.
//   - copy-back: after the call, spi_convert_objparam(src=storage[i], out_idx_)
//     converts to the target type and deep-copies onto the caller's exec allocator
//     (storage[i] points into the callee mem_context, which execute_proc has by
//     then destroyed -- so the deep copy must happen here, exactly as the legacy path does).
// IN actuals (and out-to-external targets, which this path does not yet copy back)
// keep the evaluate-into-temp behavior.
//
static int exec_call(ObPLExecCtx *ctx, const ObPLCallStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s), OB_NOT_NULL(ctx), OB_NOT_NULL(ctx->allocator_), OB_NOT_NULL(ctx->params_));
  const int64_t argc = OB_SUCC(ret) ? s->get_params().count() : 0;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObObjParam **argv = NULL;
  ObObjParam *storage = NULL;  // independent temps backing every actual
  int64_t *nocopy = NULL;
  if (OB_SUCC(ret) && argc > 0) {
    argv = static_cast<ObObjParam **>(tmp_alloc.alloc(sizeof(ObObjParam *) * argc));
    storage = static_cast<ObObjParam *>(tmp_alloc.alloc(sizeof(ObObjParam) * argc));
    nocopy = static_cast<int64_t *>(tmp_alloc.alloc(sizeof(int64_t) * argc));
    if (OB_ISNULL(argv) || OB_ISNULL(storage) || OB_ISNULL(nocopy)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("[pl-interp] failed to allocate call argv", K(ret), K(argc));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < argc; ++i) {
    const InOutParam &p = s->get_params().at(i);
    nocopy[i] = (s->get_nocopy_params().count() == argc) ? s->get_nocopy_params().at(i) : OB_INVALID_INDEX;
    new (&storage[i]) ObObjParam();
    const bool local_out = p.is_out() && OB_INVALID_INDEX != p.out_idx_;
    const sql::ObRawExpr *act_expr = s->get_param_expr(i);
    const bool obj_access_out = p.is_out() && OB_INVALID_INDEX == p.out_idx_
        && OB_NOT_NULL(act_expr) && act_expr->is_obj_access_expr();
    if (obj_access_out) {
      // OUT/INOUT actual whose target is an obj-access write (a trigger NEW.col): evaluating it
      // yields a for-write address (ObPlCompiteWrite*), not a value, so seed the callee from the
      // destination's current value (INOUT) and write the result back after the call.
      if (!p.is_pure_out()) {
        ObObjParam addr;
        OZ (ObSPIService::spi_calc_expr_at_idx(ctx, p.param_, OB_INVALID_INDEX, &addr));
        if (OB_SUCC(ret) && addr.is_ext() && 0 != addr.get_ext()) {
          ObPlCompiteWrite *cw = reinterpret_cast<ObPlCompiteWrite *>(addr.get_ext());
          ObObj *src = OB_NOT_NULL(cw) ? reinterpret_cast<ObObj *>(cw->value_addr_) : NULL;
          ObIArray<common::ObString> *src_ti = NULL;
          if (OB_NOT_NULL(src) && ObEnumType == src->get_type()) {
            // The actual column ENUM and the callee param ENUM may be *differently valued*
            // (tab1.c2 '21'..'25' vs param '1','2','21'..). A raw index copy would mis-map, so
            // pass the column's *string* value (looked up via its own type_info); the callee
            // then resolves the string into its own ENUM.
            ObPLDataType src_type;
            OZ (static_cast<const sql::ObObjAccessRawExpr *>(act_expr)->get_final_type(src_type));
            if (OB_SUCC(ret) && OB_INVALID_ID != src_type.get_type_info_id()) {
              OZ (ctx->func_->get_enum_set_ctx().get_enum_type_info(src_type.get_type_info_id(), src_ti));
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_NOT_NULL(src_ti)) {
            const uint64_t idx = src->get_uint64();  // 1-based ENUM index; 0 == '' (no value)
            common::ObString s;
            if (OB_SUCC(ret) && idx >= 1 && idx <= static_cast<uint64_t>(src_ti->count())) {
              OZ (ob_write_string(tmp_alloc, src_ti->at(idx - 1), s));
            }
            OX (storage[i].set_varchar(s));
            OX (storage[i].set_collation_type(src->get_collation_type()));
            OX (storage[i].set_collation_level(common::CS_LEVEL_IMPLICIT));
            OX (storage[i].set_param_meta());
          } else if (OB_NOT_NULL(src)) {
            OZ (ob_write_obj(tmp_alloc, *src, storage[i]));
            OX (storage[i].set_param_meta());
          }
        }
      }  // pure-OUT: leave storage[i] default-constructed.
    } else if (!local_out) {
      // IN actual, or an OUT to an external target (user/sys/pkg var): evaluate it.
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, p.param_, OB_INVALID_INDEX, &storage[i]));
    } else if (!p.is_pure_out()) {
      // INOUT local: seed the temp with the actual's current value as the input.
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, p.param_, OB_INVALID_INDEX, &storage[i]));
    }  // pure-OUT local: leave storage[i] default-constructed.
    OX (argv[i] = &storage[i]);
  }
  if (OB_SUCC(ret)) {
    const ObIArray<int64_t> &subpath = s->get_subprogram_path();
    int64_t *path = subpath.count() > 0 ? const_cast<int64_t *>(&subpath.at(0)) : NULL;
    OZ (ObPL::execute_proc(*ctx, s->get_package_id(), s->get_proc_id(), path,
          subpath.count(), 0 /*line_num*/, argc, argv, nocopy));
  }
  // Copy each OUT/INOUT result from its temp back into the caller's local slot.
  for (int64_t i = 0; OB_SUCC(ret) && i < argc; ++i) {
    const InOutParam &p = s->get_params().at(i);
    const sql::ObRawExpr *act_expr = s->get_param_expr(i);
    if (p.is_out() && OB_INVALID_INDEX != p.out_idx_) {
      OZ (ObSPIService::spi_convert_objparam(ctx, &storage[i], p.out_idx_, NULL /*result*/, true /*need_set*/));
    } else if (p.is_out() && OB_INVALID_INDEX == p.out_idx_
               && OB_NOT_NULL(act_expr) && act_expr->is_obj_access_expr()) {
      // Write the callee's OUT result back through the obj-access address (trigger NEW.col),
      // converting to the target column type -- mirrors exec_assign's obj-access write.
      ObPLDataType final_type;
      ObObjParam addr;
      OZ (static_cast<const sql::ObObjAccessRawExpr *>(act_expr)->get_final_type(final_type));
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, p.param_, OB_INVALID_INDEX, &addr));
      if (OB_SUCC(ret) && final_type.is_obj_type() && addr.is_ext() && 0 != addr.get_ext()) {
        ObPlCompiteWrite *cw = reinterpret_cast<ObPlCompiteWrite *>(addr.get_ext());
        ObObj *dest = OB_NOT_NULL(cw) ? reinterpret_cast<ObObj *>(cw->value_addr_) : NULL;
        ObIAllocator *alloc = OB_NOT_NULL(cw) ? reinterpret_cast<ObIAllocator *>(cw->allocator_) : NULL;
        common::ObDataType *dest_type = final_type.get_data_type();
        if (OB_NOT_NULL(dest) && OB_NOT_NULL(dest_type)) {
          // Pass the destination column's enum/set type_info id so spi_copy_datum can map a
          // string result (e.g. "12") back to the ENUM value; without it the VARCHAR->ENUM
          // convert fails OB_INVALID_ARGUMENT. (get_type_info_id is OB_INVALID_ID for non-enum.)
          OZ (ObSPIService::spi_copy_datum(ctx, alloc, &storage[i], dest, dest_type,
                                           OB_INVALID_ID /*package_id*/, final_type.get_type_info_id()));
        }
      }
    }
  }
  return ret;
}

// DECLARE cur CURSOR FOR <select>: allocate the runtime cursor slot
// (spi_cursor_init). The SELECT text lives on the cursor; OPEN runs it.
static int exec_cursor_decl(ObPLExecCtx *ctx, const ObPLDeclareCursorStmt *s)
{
  int ret = OB_SUCCESS;
  const ObPLCursor *cursor = NULL;
  CK (OB_NOT_NULL(s));
  OX (cursor = s->get_cursor());
  if (OB_SUCC(ret) && OB_NOT_NULL(cursor) && ObPLCursor::DUP_DECL != cursor->get_state()) {
    OZ (ObSPIService::spi_cursor_init(ctx, s->get_index()));
  }
  return ret;
}

// OPEN cur [ ( args ) ]: run the cursor's SELECT. Mirrors codegen's generate_open
// via the expr-idx spi variant. sql params are PL-var refs inside the SELECT;
// formal/actual params are the parameterized-cursor arguments (empty for simple).
static int exec_open(ObPLExecCtx *ctx, const ObPLOpenStmt *s)
{
  int ret = OB_SUCCESS;
  const ObPLCursor *cursor = NULL;
  CK (OB_NOT_NULL(s), OB_NOT_NULL(ctx), OB_NOT_NULL(ctx->allocator_));
  OX (cursor = s->get_cursor());
  CK (OB_NOT_NULL(cursor));
  if (OB_SUCC(ret)) {
    const ObIArray<int64_t> &sql_params = cursor->get_sql_params();
    const int64_t sql_pcount = sql_params.count();
    const int64_t *sql_pidx = sql_pcount > 0 ? &sql_params.at(0) : NULL;
    // spi_cursor_open requires: with params -> sql must be NULL (run via ps_sql);
    // without params -> sql must be non-NULL (the raw text).
    const char *sql = NULL;
    const char *ps_sql = NULL;
    OZ (dup_cstr(ctx, cursor->get_ps_sql(), ps_sql));
    if (OB_SUCC(ret) && 0 == sql_pcount) {
      OZ (dup_cstr(ctx, cursor->get_sql(), sql));
    }
    const ObIArray<int64_t> &actual = s->get_params();
    const int64_t cur_pcount = actual.count();
    const int64_t *actual_idx = cur_pcount > 0 ? &actual.at(0) : NULL;
    const ObIArray<int64_t> &formal = cursor->get_formal_params();
    const int64_t *formal_idx = formal.count() > 0 ? &formal.at(0) : NULL;
    OZ (ObSPIService::spi_cursor_open_with_param_idx(ctx, sql, ps_sql,
          static_cast<int64_t>(cursor->get_stmt_type()),
          cursor->is_for_update(), cursor->has_hidden_rowid(),
          sql_pidx, sql_pcount,
          s->get_package_id(), s->get_routine_id(), s->get_index(),
          formal_idx, actual_idx, cur_pcount, false /*skip_locked*/));
  }
  return ret;
}

// FETCH cur INTO vars: pull one row into the into variables (or BULK ... LIMIT).
// On no more rows, spi returns OB_READ_NOTHING -> a NOT FOUND handler catches it.
static int exec_fetch(ObPLExecCtx *ctx, const ObPLFetchStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s));
  if (OB_SUCC(ret)) {
    const int64_t into_count = s->get_into().count();
    const int64_t *into_idx = into_count > 0 ? &s->get_into().at(0) : NULL;
    const int64_t type_count = s->get_data_type().count();
    const ObDataType *types = type_count > 0 ? &s->get_data_type().at(0) : NULL;
    const bool *not_null = s->get_not_null_flags().count() > 0 ? &s->get_not_null_flags().at(0) : NULL;
    const int64_t *ranges = s->get_pl_integer_ranges().count() > 0 ? &s->get_pl_integer_ranges().at(0) : NULL;
    OZ (ObSPIService::spi_cursor_fetch(ctx, s->get_package_id(), s->get_routine_id(),
          s->get_index(), into_idx, into_count, types, type_count, not_null, ranges,
          s->is_bulk(), s->get_limit(), NULL /*return_types*/, 0 /*return_type_count*/,
          s->is_type_record()));
  }
  return ret;
}

// CLOSE cur: release the runtime cursor.
static int exec_close(ObPLExecCtx *ctx, const ObPLCloseStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s));
  OZ (ObSPIService::spi_cursor_close(ctx, s->get_package_id(), s->get_routine_id(),
        s->get_index(), false /*ignore*/));
  return ret;
}

// SIGNAL: raise a condition. The interpreter has no stack unwinder, so the
// raise is modeled as the statement returning an error code, which the enclosing
// block's handler search (try_handle) then matches and runs.
//
// MySQL mode only permits SIGNAL with a SQLSTATE (the resolver rejects an
// error-code condition: OB_ERR_SP_BAD_CONDITION_TYPE), so the live path is
// always cond_type != ERROR_CODE. Codegen routes that through
// spi_process_resignal (visit(ObPLSignalStmt): is_mysql_mode && cond_type !=
// ERROR_CODE -> spi_process_resignal_error_), so mirror it exactly: it derives
// the condition class from the literal sqlstate and
//   - SQLWARNING ('01xxx'): appends a warning, leaves the error code at success
//     -> the SIGNAL does NOT abort, execution continues (the codegen pre-stores
//     OB_SUCCESS into err_code for the signal path, and spi leaves it untouched);
//   - NOT FOUND ('02xxx') / EXCEPTION: sets the sqlcode (ER_SIGNAL_NOT_FOUND /
//     ER_SIGNAL_EXCEPTION when no MYSQL_ERRNO item) and writes it back through
//     error_code, which we then raise as the statement's error.
// The DIAG_MYSQL_ERRNO / DIAG_MESSAGE_TEXT expr indices come off the stmt (==
// OB_INVALID_ID when the SIGNAL has no SET items), exactly as codegen passes them.
static int exec_signal(ObPLExecCtx *ctx, const ObPLSignalStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s), OB_NOT_NULL(ctx));
  if (OB_SUCC(ret) && ERROR_CODE != s->get_cond_type()) {
    const int64_t *err_idx = s->get_expr_idx(static_cast<int64_t>(SignalCondInfoItem::DIAG_MYSQL_ERRNO));
    const int64_t *msg_idx = s->get_expr_idx(static_cast<int64_t>(SignalCondInfoItem::DIAG_MESSAGE_TEXT));
    int error_code = OB_SUCCESS;  // codegen pre-stores OB_SUCCESS for the SIGNAL path
    const char *sql_state = s->get_sql_state();
    const char *resignal_sql_state = NULL;  // caught condition's sqlstate, re-raised by a bare RESIGNAL
    if (s->is_resignal_stmt()) {
      // RESIGNAL re-raises the condition currently being handled. Codegen extracts the caught
      // code + sqlstate from the saved exception and passes a NULL signal-sqlstate so
      // spi_process_resignal reuses them. Here the caught code is the current SQLCODE (set on
      // handler entry in try_handle), and the caught sqlstate is the diagnostic-stack top (the
      // warning buffer pushed on handler entry) -- the same place spi_process_resignal reads the
      // re-raised message. Without it, the re-raise falls back to ob_sqlstate(0) == "HY000"
      // instead of the original (e.g. "23000").
      if (OB_NOT_NULL(ctx->exec_ctx_) && OB_NOT_NULL(ctx->exec_ctx_->get_my_session())
          && OB_NOT_NULL(ctx->exec_ctx_->get_my_session()->get_pl_sqlcode_info())) {
        error_code = static_cast<int>(
            ctx->exec_ctx_->get_my_session()->get_pl_sqlcode_info()->get_sqlcode());
        const int64_t cnt = ctx->exec_ctx_->get_my_session()
            ->get_pl_sqlcode_info()->get_stack_warning_buf().count();
        if (cnt > 0) {
          const char *caught = ctx->exec_ctx_->get_my_session()->get_pl_sqlcode_info()
              ->get_stack_warning_buf().at(cnt - 1).get_sql_state();
          if (OB_NOT_NULL(caught) && '\0' != caught[0]) {
            resignal_sql_state = caught;
          }
        }
      }
      if (OB_NOT_NULL(sql_state) && '\0' == sql_state[0]) {
        sql_state = NULL;  // bare RESIGNAL: no explicit sqlstate -> reuse the caught one
      }
    }
    OZ (ObSPIService::spi_process_resignal(ctx,
          NULL != err_idx ? *err_idx : OB_INVALID_ID,
          NULL != msg_idx ? *msg_idx : OB_INVALID_ID,
          sql_state, &error_code, resignal_sql_state, !s->is_resignal_stmt()));
    if (OB_SUCC(ret) && OB_SUCCESS != error_code) {
      // Mirror codegen: spi_process_resignal stashed the literal sqlstate + the
      // SIGNAL's errno (ER_SIGNAL_NOT_FOUND / ER_SIGNAL_EXCEPTION or a SET MYSQL_ERRNO)
      // into the TSI warning buffer and raised OB_SP_RAISE_APPLICATION_ERROR via
      // LOG_MYSQL_USER_ERROR. Surface *that* OB code so an unhandled SIGNAL reports the
      // literal sqlstate (send_error_packet/eh_convert_exception read the warning buffer
      // for OB_SP_RAISE_APPLICATION_ERROR); the handler search recovers the real errno +
      // sqlstate from the warning buffer too (see effective_raised_condition).
      ret = OB_SP_RAISE_APPLICATION_ERROR;  // a non-warning SIGNAL raises
    }
    LOG_WARN("[pl-interp] SIGNAL processed", K(ret), K(error_code),
             "sql_state", ObString(s->get_sql_state()));
  } else if (OB_SUCC(ret)) {
    // Oracle-mode / error-code SIGNAL: raise the resolved OB error code directly.
    ret = s->get_ob_error_code();
    if (OB_SUCCESS == ret) {
      ret = OB_ERROR;  // a SIGNAL must raise something the handler search can see
    }
    LOG_WARN("[pl-interp] SIGNAL raised", K(ret), "sql_state", ObString(s->get_sql_state()));
  }
  return ret;
}

// EXECUTE IMMEDIATE / dynamic SQL. The resolver also lowers session-variable
// assignments such as `SET @uservar = expr` (and `SET @@sysvar = ...`) to this.
// Mirrors codegen visit(ObPLExecuteStmt): evaluate USING actuals into ObObjParams,
// then call spi_execute_immediate, which computes the SQL-text expr and runs it.
static int exec_execute(ObPLExecCtx *ctx, const ObPLExecuteStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s), OB_NOT_NULL(ctx), OB_NOT_NULL(ctx->allocator_));
  const int64_t param_count = OB_SUCC(ret) ? s->get_using().count() : 0;
  const int64_t into_count = OB_SUCC(ret) ? s->get_into().count() : 0;
  const int64_t *into_idx = into_count > 0 ? &s->get_into().at(0) : NULL;
  const int64_t type_count = OB_SUCC(ret) ? s->get_data_type().count() : 0;
  const ObDataType *types = type_count > 0 ? &s->get_data_type().at(0) : NULL;
  const bool *not_null = s->get_not_null_flags().count() > 0 ? &s->get_not_null_flags().at(0) : NULL;
  const int64_t *ranges = s->get_pl_integer_ranges().count() > 0 ? &s->get_pl_integer_ranges().at(0) : NULL;
  ObArenaAllocator tmp_alloc(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObObjParam **params = NULL;
  ObObjParam *storage = NULL;  // independent temps backing every USING actual
  int64_t *params_mode = NULL;
  if (OB_SUCC(ret) && param_count > 0) {
    params = static_cast<ObObjParam **>(tmp_alloc.alloc(sizeof(ObObjParam *) * param_count));
    storage = static_cast<ObObjParam *>(tmp_alloc.alloc(sizeof(ObObjParam) * param_count));
    params_mode = static_cast<int64_t *>(tmp_alloc.alloc(sizeof(int64_t) * param_count));
    if (OB_ISNULL(params) || OB_ISNULL(storage) || OB_ISNULL(params_mode)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("[pl-interp] failed to allocate USING params", K(ret), K(param_count));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < param_count; ++i) {
    const InOutParam &u = s->get_using().at(i);
    new (&storage[i]) ObObjParam();
    params_mode[i] = static_cast<int64_t>(u.mode_);
    if (!u.is_pure_out()) {  // IN/INOUT: evaluate the actual; pure-OUT filled by execute
      OZ (ObSPIService::spi_calc_expr_at_idx(ctx, u.param_, OB_INVALID_INDEX, &storage[i]));
    }
    OX (params[i] = &storage[i]);
  }
  // spi_execute_immediate resolves the INTO targets from into_exprs_idx itself
  // (a `SET @uservar = expr` lowers to dynamic SQL with the variable as the INTO
  // target); result types come from the dynamic statement, so column_types stays NULL.
  OZ (ObSPIService::spi_execute_immediate(ctx, s->get_sql(),
        params, params_mode, param_count,
        into_idx, into_count,
        types, type_count,
        not_null, ranges,
        s->is_bulk(), s->get_is_returning(), s->is_type_record()));
  return ret;
}

// PRAGMA INTERFACE(C, <entry>) routine bodies lower to a single PL_INTERFACE stmt.
// Dispatch to the native C implementation registered under the entry name, mirroring
// the legacy codegen path (ObPLCodeGenerateVisitor::visit(ObPLInterfaceStmt) -> spi_interface_impl)
// and ObPL::interface_execute. The native impl reads its arguments from ctx->params_.
static int exec_interface(ObPLExecCtx *ctx, const ObPLInterfaceStmt *s)
{
  int ret = OB_SUCCESS;
  ObSqlString interface_name;
  CK (OB_NOT_NULL(ctx), OB_NOT_NULL(s));
  CK (!s->get_entry().empty());
  // spi_interface_impl takes a C string; get_entry() returns a (possibly non
  // null-terminated) ObString, so copy it through ObSqlString to terminate it.
  OZ (interface_name.assign(s->get_entry()));
  OZ (ObSPIService::spi_interface_impl(ctx, interface_name.string().ptr()));
  return ret;
}

// DO expr [, expr ...]: evaluate each value expression for its side effects and discard
// the result. Mirrors codegen visit(ObPLDoStmt), which generates each value expr with
// result_idx == OB_INVALID_INDEX (evaluate-and-discard, as exec_case does for WHEN tests).
static int exec_do(ObPLExecCtx *ctx, const ObPLDoStmt *s)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(s));
  for (int64_t i = 0; OB_SUCC(ret) && i < s->get_value().count(); ++i) {
    ObObjParam result;
    OZ (ObSPIService::spi_calc_expr_at_idx(ctx, s->get_value_index(i), OB_INVALID_INDEX, &result));
  }
  return ret;
}

static int exec_stmt(ObPLExecCtx *ctx, const ObPLStmt *stmt, CtrlState &ctrl)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(stmt)) {
    switch (stmt->get_type()) {
      case PL_BLOCK:
        OZ (exec_block(ctx, static_cast<const ObPLStmtBlock *>(stmt), ctrl));
        break;
      case PL_VAR:
        OZ (exec_declare(ctx, static_cast<const ObPLDeclareVarStmt *>(stmt)));
        break;
      case PL_ASSIGN:
        OZ (exec_assign(ctx, static_cast<const ObPLAssignStmt *>(stmt)));
        break;
      case PL_IF:
        OZ (exec_if(ctx, static_cast<const ObPLIfStmt *>(stmt), ctrl));
        break;
      case PL_CASE:
        OZ (exec_case(ctx, static_cast<const ObPLCaseStmt *>(stmt), ctrl));
        break;
      case PL_WHILE:
        OZ (exec_while(ctx, static_cast<const ObPLCondLoop *>(stmt), ctrl));
        break;
      case PL_LOOP:
        OZ (exec_loop(ctx, static_cast<const ObPLLoopStmt *>(stmt), ctrl));
        break;
      case PL_REPEAT:
        OZ (exec_repeat(ctx, static_cast<const ObPLRepeatStmt *>(stmt), ctrl));
        break;
      case PL_LEAVE:
      case PL_ITERATE:
        OZ (exec_loop_control(ctx, static_cast<const ObPLLoopControl *>(stmt), ctrl));
        break;
      case PL_RETURN:
        OZ (exec_return(ctx, static_cast<const ObPLReturnStmt *>(stmt), ctrl));
        break;
      case PL_SQL:
        OZ (exec_sql(ctx, static_cast<const ObPLSqlStmt *>(stmt)));
        break;
      case PL_CALL:
        OZ (exec_call(ctx, static_cast<const ObPLCallStmt *>(stmt)));
        break;
      case PL_SIGNAL:
        OZ (exec_signal(ctx, static_cast<const ObPLSignalStmt *>(stmt)));
        break;
      case PL_CURSOR:
        OZ (exec_cursor_decl(ctx, static_cast<const ObPLDeclareCursorStmt *>(stmt)));
        break;
      case PL_OPEN:
        OZ (exec_open(ctx, static_cast<const ObPLOpenStmt *>(stmt)));
        break;
      case PL_FETCH:
        OZ (exec_fetch(ctx, static_cast<const ObPLFetchStmt *>(stmt)));
        break;
      case PL_CLOSE:
        OZ (exec_close(ctx, static_cast<const ObPLCloseStmt *>(stmt)));
        break;
      case PL_HANDLER:
        // Handler declarations are captured on the block (block->get_eh()) and
        // run from try_handle on error; nothing to execute inline.
        break;
      case PL_COND:
        // DECLARE ... CONDITION FOR ...: a compile-time name binding; no-op here.
        break;
      case PL_EXECUTE:
        OZ (exec_execute(ctx, static_cast<const ObPLExecuteStmt *>(stmt)));
        break;
      case PL_DO:
        OZ (exec_do(ctx, static_cast<const ObPLDoStmt *>(stmt)));
        break;
      case PL_INTERFACE:
        OZ (exec_interface(ctx, static_cast<const ObPLInterfaceStmt *>(stmt)));
        break;
      default:
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("[pl-interp] statement type not implemented yet",
                 K(ret), "stmt_type", static_cast<int64_t>(stmt->get_type()));
        break;
    }
  }
  return ret;
}

int ObPLInterpreter::execute()
{
  int ret = OB_SUCCESS;
  ObPLFunction &func = state_.get_function();
  ObPLExecCtx &ctx = state_.get_exec_ctx();
  ObPLFunctionAST *ast = func.get_ast();
  if (OB_ISNULL(ast)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("[pl-interp] no retained AST on function", K(ret), K(func.get_routine_id()));
  } else {
    CtrlState ctrl;
    OZ (exec_block(&ctx, ast->get_body(), ctrl));
    // A function that runs to completion without a RETURN that set a value falls
    // off the end: MySQL raises ER_SP_NORETURNEND (1321). Codegen detects this in
    // ObPL::execute by passing a local_result seeded to ObMaxType and checking
    // local_result.is_valid_type() afterward; the result_ obj here is that same
    // seed, so an invalid type means no RETURN value was produced. RETURN NULL
    // stores ObNullType (a valid type), so it does not trip this.
    if (OB_SUCC(ret) && func.is_function()
        && OB_NOT_NULL(ctx.result_) && !ctx.result_->is_valid_type()) {
      // Just set the error code; do NOT LOG_USER_ERROR the formatted message.
      // The canonical .result reports the bare static text "FUNCTION ended
      // without RETURN" (the %s in the *user* message is not substituted in the
      // reference), so leaving the default error message is what mysqltest wants.
      ret = OB_ER_SP_NORETURNEND;
      LOG_WARN("[pl-interp] function ended without RETURN", K(ret),
               K(func.get_function_name()));
    }
  }
  return ret;
}

} // namespace pl
} // namespace oceanbase
