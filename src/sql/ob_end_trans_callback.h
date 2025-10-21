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

#ifndef __OB_SQL_END_TRANS_CALLBACK_H__
#define __OB_SQL_END_TRANS_CALLBACK_H__

#include "share/ob_define.h"
#include "lib/utility/utility.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/objectpool/ob_tc_factory.h"
#include "lib/allocator/ob_mod_define.h"
#include "sql/ob_i_end_trans_callback.h"
#include "storage/tx/ob_trans_result.h"
#include "storage/tx/ob_trans_define.h"
#include "observer/mysql/ob_mysql_end_trans_cb.h"

namespace oceanbase
{
namespace sql
{
// The same object may be shared by multiple end trans functions at the same time, which may result in concurrent calls to the same object's callback function.
// deprecated, only used in NullEndTransCallback and will be removed in 4.0
class ObSharedEndTransCallback : public ObIEndTransCallback
{
public:
  ObSharedEndTransCallback();
  virtual ~ObSharedEndTransCallback();

  virtual bool is_shared() const override final { return true; }
};
// An object can only be exclusively owned by one end trans function at a time, and the callback function of the same object can only be called serially
class ObExclusiveEndTransCallback : public ObIEndTransCallback
{
public:
  enum EndTransType
  {
    END_TRANS_TYPE_INVALID,
    END_TRANS_TYPE_EXPLICIT, // explicit commit
    END_TRANS_TYPE_IMPLICIT, // implicit commit
  };
public:
  ObExclusiveEndTransCallback();
  virtual ~ObExclusiveEndTransCallback();

  virtual bool is_shared() const override final { return false; }

  void set_is_need_rollback(bool is_need_rollback)
  {
    has_set_need_rollback_ = true;
    is_need_rollback_ = is_need_rollback;
  }
  void set_end_trans_type(ObExclusiveEndTransCallback::EndTransType end_trans_type)
  {
    end_trans_type_ = end_trans_type;
  }
  void set_is_txs_end_trans_called(bool is_txs_end_trans_called)
  {
    is_txs_end_trans_called_ = is_txs_end_trans_called;
  }
  bool is_txs_end_trans_called() const { return is_txs_end_trans_called_; }
  void reset()
  {
    ObIEndTransCallback::reset();
    end_trans_type_ = END_TRANS_TYPE_INVALID;
    has_set_need_rollback_ = false;
    is_need_rollback_ = false;
    is_txs_end_trans_called_ = false;
  }
protected:
  ObExclusiveEndTransCallback::EndTransType end_trans_type_; // indicates whether it is an explicit or implicit commit or rollback
  bool has_set_need_rollback_;
  bool is_need_rollback_;
  bool is_txs_end_trans_called_; // Has the end trans interface of the transaction module been called (considered called if it is in the process of being called, and the parameters passed are not necessarily this callback object)
};

class ObEndTransAsyncCallback : public ObExclusiveEndTransCallback
{
public:
  ObEndTransAsyncCallback();
  virtual ~ObEndTransAsyncCallback();
  virtual void callback(int cb_param);
  virtual void callback(int cb_param, const transaction::ObTransID &trans_id);
  virtual const char *get_type() const { return "ObEndTransAsyncCallback"; }
  virtual ObEndTransCallbackType get_callback_type() const { return ASYNC_CALLBACK_TYPE; }
  observer::ObSqlEndTransCb &get_mysql_end_trans_cb() { return mysql_end_trans_cb_; }
  void reset()
  {
    ObExclusiveEndTransCallback::reset();
    mysql_end_trans_cb_.reset();
    reset_diagnostic_info();
  }
  void set_diagnostic_info(common::ObDiagnosticInfo *diagnostic_info);
  void reset_diagnostic_info();
private:
  /* macro */
  observer::ObSqlEndTransCb mysql_end_trans_cb_;
  common::ObDiagnosticInfo *diagnostic_info_;
  DISALLOW_COPY_AND_ASSIGN(ObEndTransAsyncCallback);
};

}
}


#endif /* __OB_SQL_END_TRANS_CALLBACK_H__ */
//// end of header file
