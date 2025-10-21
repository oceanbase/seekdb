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

#pragma once

#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_mutex.h"
#include "observer/table_load/ob_table_load_struct.h"
#include "observer/table_load/resource/ob_table_load_resource_rpc_struct.h"


namespace oceanbase
{
namespace observer
{

class ObTableLoadAssignedTaskManager
{
public:
	ObTableLoadAssignedTaskManager();
	~ObTableLoadAssignedTaskManager();
	int init();
	int add_assigned_task(ObDirectLoadResourceApplyArg &arg);
	int delete_assigned_task(ObTableLoadUniqueKey &task_key);
	int get_assigned_tasks(common::ObSArray<ObDirectLoadResourceApplyArg> &assigned_tasks);
private:
  typedef common::hash::ObHashMap<ObTableLoadUniqueKey, 
																	ObDirectLoadResourceApplyArg,
                                  common::hash::NoPthreadDefendMode>
		ResourceApplyMap;
	ResourceApplyMap assigned_tasks_map_;
	mutable lib::ObMutex mutex_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
