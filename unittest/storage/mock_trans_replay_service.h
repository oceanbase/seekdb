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

#ifndef OCEANBASE_UNITTEST_MOCK_TRANS_REPLAY_SERVICE_H_
#define OCEANBASE_UNITTEST_MOCK_TRANS_REPLAY_SERVICE_H_

#include "share/ob_define.h"
#include "storage/ob_i_trans_replay_service.h"
#include "storage/ob_trans_log_mock.h"

namespace oceanbase
{
namespace unittest
{
class MockTransReplayService: public transaction::ObITransReplayService
{
public:
  MockTransReplayService() {}
  virtual ~MockTransReplayService() {}
public:
  virtual int submit_trans_log(transaction::ObTransLog *trans_log)
  {
    TRANS_LOG(INFO, "submit_trans_log", "log_type", trans_log->get_type());
    return common::OB_SUCCESS;
  }
  virtual void finish_replay()
  {
    TRANS_LOG(INFO, "finish replay");
  }
};

} // namespace unittest
} // namespace oceanbase

#endif // OCEANBASE_TRANSACTION_MOCK_TRANS_REPLAY_SERVICE_H_
