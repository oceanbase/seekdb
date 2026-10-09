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

#ifndef OCEANBASE_ROOTSERVER_NAMESPACE_MAINTENANCE_H_
#define OCEANBASE_ROOTSERVER_NAMESPACE_MAINTENANCE_H_
#include "lib/task/ob_timer.h"
#include <map>
#include <string>

namespace oceanbase {
namespace share { class ObISharedTimer; }
namespace storage { class InstanceMetaStore; }
namespace rootserver {
// Owns Namespace maintenance and its disposable scan positions. Scheduling uses
// the process timer; the task never starts a Namespace SQL Runtime.
class NamespaceMaintenance final : public common::ObTimerTask
{
public:
  int start(share::ObISharedTimer &timer, storage::InstanceMetaStore &store);
  // Cancel and drain before the composition root stops storage dependencies.
  void stop();
  void runTimerTask() override;
private:
  int materialize_inherited_tablets();
  share::ObISharedTimer *timer_ = nullptr;
  storage::InstanceMetaStore *store_ = nullptr;
  uint64_t last_namespace_ = 0;
  uint64_t physical_cursor_ = 0;
  std::map<uint64_t, std::string> positions_;
};
}
}
#endif
