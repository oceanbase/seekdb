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

#ifndef OCEANBASE_SCHEMA_OB_SCHEMA_STORE_H_
#define OCEANBASE_SCHEMA_OB_SCHEMA_STORE_H_

#include "lib/hash/ob_ptr_array_map.h"
#include "share/schema/ob_schema_mgr_cache.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObSchemaStore
{
public:
  static const int64_t MAX_VERSION_COUNT = 64;
  ObSchemaStore()
    : refreshed_version_(0),
      received_version_(0),
      checked_sys_version_(0),
      baseline_schema_version_(common::OB_INVALID_VERSION) {}
  ~ObSchemaStore() {}
  int init(const int64_t init_version_count);
  void reset_version();
  void update_refreshed_version(int64_t version);
  void update_received_version(int64_t version);
  void update_checked_sys_version(int64_t version);
  void update_baseline_schema_version(int64_t version);
  int64_t get_refreshed_version() const { return ATOMIC_LOAD(&refreshed_version_); }
  int64_t get_received_version() const { return ATOMIC_LOAD(&received_version_); }
  int64_t get_checked_sys_version() const { return ATOMIC_LOAD(&checked_sys_version_); }
  int64_t get_baseline_schema_version() const { return ATOMIC_LOAD(&baseline_schema_version_); }

  
  int64_t refreshed_version_;
  int64_t received_version_;
  int64_t checked_sys_version_;
  int64_t baseline_schema_version_;
  ObSchemaMgrCache schema_mgr_cache_;
};


}; // end namespace schema
}; // end namespace share
}; // end namespace oceanbase

#endif /* OCEANBASE_SCHEMA_OB_SCHEMA_STORE_H_ */
