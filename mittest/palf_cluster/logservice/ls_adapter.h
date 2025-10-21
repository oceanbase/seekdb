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

#ifndef OCEANBASE_PALF_CLUSTER_LS_ADAPTER_H_
#define OCEANBASE_PALF_CLUSTER_LS_ADAPTER_H_

#include <stdint.h>
#include "share/ob_ls_id.h"
#include "logservice/ob_ls_adapter.h"

namespace oceanbase
{
namespace logservice
{
class ObLogReplayTask;
};

namespace palfcluster
{

class MockLSAdapter : public logservice::ObLSAdapter
{
public:
  MockLSAdapter();
  ~MockLSAdapter();
  int init();
  void destroy();
public:
  int replay(logservice::ObLogReplayTask *replay_task) override final;
  int wait_append_sync(const share::ObLSID &ls_id) override final;
private:
  bool is_inited_;
};

} // logservice
} // oceanbase

#endif
