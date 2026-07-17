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

#ifndef OCEANBASE_UNITTEST_MOCK_OB_END_TRANS_CALLBACK_H_
#define OCEANBASE_UNITTEST_MOCK_OB_END_TRANS_CALLBACK_H_

#include "sql/ob_i_end_trans_callback.h"
#include "storage/tx/ob_trans_result.h"

namespace oceanbase
{
namespace unittest
{
class MockObEndTransCallback : public sql::ObExclusiveEndTransCallback
{
public:
  virtual int wait()
  {
    int res = OB_SUCCESS;
    int ret = cond_.wait(WAIT_US, res);
    return (OB_SUCCESS == ret) ? res : ret;
  }
  virtual void callback(int cb_param, const transaction::ObTransID &trans_id)
  {
    UNUSED(trans_id);
    callback(cb_param);
  }
  virtual void callback(int cb_param)
  {
    cond_.notify(cb_param);
  }
  virtual const char *get_type() const { return "MockObEndTransCallback"; }
  virtual sql::ObEndTransCallbackType get_callback_type() const { return sql::MOCK_CALLBACK_TYPE; }
private:
  static const int64_t WAIT_US = 1000 * 1000 * 1000;
private:
  transaction::ObTransCond cond_;
};

} // namespace unittest
} // namespace oceanbase
#endif // OCEANBASE_UNITTEST_MOCK_OB_END_TRANS_CALLBACK_H_
