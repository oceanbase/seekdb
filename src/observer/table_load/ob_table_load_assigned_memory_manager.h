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


namespace oceanbase
{
namespace observer
{

class ObTableLoadAssignedMemoryManager
{
public:
	// Minimum memory required in sort execution mode
	static const int64_t MIN_SORT_MEMORY_PER_TASK = 128LL * 1024LL * 1024LL;  // 128 MB
  ObTableLoadAssignedMemoryManager();
	~ObTableLoadAssignedMemoryManager();
	int init();
	int assign_memory(bool is_sort, int64_t assign_memory);
	int recycle_memory(bool is_sort, int64_t assign_memory);
	int64_t get_avail_memory();
	int refresh_avail_memory(int64_t avail_memory);
	int get_sort_memory(int64_t &sort_memory);
private:
	int64_t avail_sort_memory_;
	int64_t avail_memory_;
	int64_t chunk_count_;
  mutable lib::ObMutex mutex_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
