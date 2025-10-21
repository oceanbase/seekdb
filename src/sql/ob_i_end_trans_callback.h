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

#ifndef __OB_SQL_I_END_TRANS_CALLBACK_H__
#define __OB_SQL_I_END_TRANS_CALLBACK_H__

#include "share/ob_define.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/utility/utility.h"
#include "storage/tx/ob_trans_end_trans_callback.h"
#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{

namespace transaction {
  class ObTransID;
  class ObITxCallback;
}

namespace sql
{

enum ObEndTransCallbackType
{
  SYNC_CALLBACK_TYPE = 0, /* Synchronous wait, such as DDL statements */
  ASYNC_CALLBACK_TYPE, /* Asynchronously execute operations after transaction commit, such as DML statements, COMMIT/ROLLBACK statements sending execution results to the client */
  // NO_CALLBACK_TYPE, /* When the user actively disconnects the link mid-process, the transaction will be rolled back, and this situation is also more convenient to handle with SYNC mode */
  NULL_CALLBACK_TYPE,
  //SQL_CALLBACK_TYPE,
  WRITE_FROZEN_STAT_CALLBACK_TYPE,
  MOCK_CALLBACK_TYPE,
  MAX_CALLBACK_TYPE
};


/* ObIEndTransCallback lifecycle:
 * Approach 1. Create at StartTrans, terminate after EndTrans call ends
 *   - In disconnect mode, use synchronous waiting. This action cannot be predicted at StartTrans
 * Approach 2. Create before calling EndTrans, terminate after calling EndTrans
 *   - Need to know the current operation type (ac=0/1, commit/rollback, dml, disconnect)
 *
 * explicit_end_trans (commit/rollback), on_plan_end (ac=1 dml) cases require ObIEndTransCallback to be passed from outside,
 * other cases use sync mode, internally constructing and releasing ObIEndTransCallback
 *
 * After calling end_trans but before callback occurs, can operations that [may cause errors] still be performed?
 *  - Operations that [may cause errors] refer to calling a function and obtaining an error code that affects SQL error output.
 *  - If such an operation is performed and an error code is generated, this error code must be saved, otherwise it will be dropped.
 *    It can only be saved in the Callback. However, this poses a multithreading issue: what if the Callback has already been called when saving the error code?
 *  - A relatively simple approach: do nothing after end_trans, and perform all other operations inside the callback.
 *
 */
class ObIEndTransCallback : public transaction::ObITxCallback
{
public:
  ObIEndTransCallback() { reset(); }
  virtual ~ObIEndTransCallback() {};
  /*
   * In some scenarios (such as cmd triggering implicit commit) it is necessary to synchronously wait for the callback to be successfully called before
   * continuing with subsequent operations, so the wait() operation is introduced
   */
  virtual int wait() { return common::OB_NOT_IMPLEMENT; }
  /*
   * Called after the transaction is completed, specific actions are defined by the object itself
   * e.g.: wake up wait(); respond to the client; respond to the RPC caller, etc.
   */
  virtual void callback(int cb_param) = 0;
  virtual void callback(int cb_param, const transaction::ObTransID &trans_id) = 0;
  virtual const char *get_type() const = 0;
  virtual ObEndTransCallbackType get_callback_type() const = 0;
  // Indicates whether it is possible for the same callback object to be shared by multiple end trans functions, which may concurrently call the callback function of the same callback object
  virtual bool is_shared() const = 0;

  /*
   * Set external error code, refer to the error code specified by cb_param in the callback(callback(cb_param))
   * after the transaction is completed, along with this external error code, return the first occurring error code
   */
  void set_last_error(int last_err) { last_err_ = last_err; }

  /*
   * Call after successfully calling ps->end_trans
   */
  inline void handout() { ATOMIC_INC(&call_counter_); }
  /*
   * Transaction layer triggers cb.callback() and immediately calls the callback in the callback
   */
  inline void handin() { ATOMIC_INC(&callback_counter_);}
  inline void reset()
  {
    last_err_ = common::OB_SUCCESS;
    call_counter_ = 0;
    callback_counter_ = 0;
  }
  inline bool is_called()
  {
    return callback_counter_ == call_counter_;
  }
protected:
  // To check for duplicate callback calls, or missed callback calls
  inline void CHECK_BALANCE(const char *type) const
  {
    if (OB_UNLIKELY(callback_counter_ != call_counter_)) {
      SQL_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "Callback times mismatch. bug!!!",
              K(type), K(this), K(common::lbt()), K_(callback_counter), K_(call_counter));
    }
  }
protected:
  int last_err_;
  volatile uint64_t call_counter_; // Number of successful calls to ps->end_trans
  volatile uint64_t callback_counter_; // Number of times the callback is called
};

} /*ns*/
}/*ns */

#endif /* __OB_SQL_I_END_TRANS_CALLBACK_H__ */
//// end of header file
