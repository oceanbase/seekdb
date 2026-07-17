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

#define USING_LOG_PREFIX SQL

#include "ob_end_trans_callback.h"
#include "sql/session/ob_sql_session_info.h"
using namespace oceanbase::transaction;
using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{
ObSharedEndTransCallback::ObSharedEndTransCallback()
{
}

ObSharedEndTransCallback::~ObSharedEndTransCallback()
{
}

ObExclusiveEndTransCallback::ObExclusiveEndTransCallback()
{
  reset();
}

ObExclusiveEndTransCallback::~ObExclusiveEndTransCallback()
{
}

/////////////////  Async Callback Impl /////////////

ObEndTransAsyncCallback::ObEndTransAsyncCallback() :
    ObExclusiveEndTransCallback(),
    mysql_end_trans_cb_()
{
}

ObEndTransAsyncCallback::~ObEndTransAsyncCallback()
{
}

void ObEndTransAsyncCallback::callback(int cb_param, const transaction::ObTransID &trans_id)
{
  UNUSED(trans_id);
  callback(cb_param);
}

void ObEndTransAsyncCallback::callback(int cb_param)
{
  bool need_disconnect = false;
  if (OB_UNLIKELY(!has_set_need_rollback_)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "is_need_rollback_ has not been set",
              K(has_set_need_rollback_),
              K(is_need_rollback_));
  } else if (OB_UNLIKELY(ObExclusiveEndTransCallback::END_TRANS_TYPE_INVALID == end_trans_type_)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "end trans type is invalid", K(cb_param), K(end_trans_type_));
  } else {
    ObSQLUtils::check_if_need_disconnect_after_end_trans(
        cb_param, is_need_rollback_,
        ObExclusiveEndTransCallback::END_TRANS_TYPE_EXPLICIT == end_trans_type_,
        need_disconnect);
  }
  mysql_end_trans_cb_.set_need_disconnect(need_disconnect);
  this->handin();
  CHECK_BALANCE("[async callback]");

  if (OB_SUCCESS == this->last_err_) {
    mysql_end_trans_cb_.callback(cb_param);
  } else {
    cb_param = this->last_err_;
    mysql_end_trans_cb_.callback(cb_param);
  }
}

}/* ns sql*/
}/* ns oceanbase */
