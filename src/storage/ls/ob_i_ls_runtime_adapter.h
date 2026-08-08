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

#ifndef OCEANBASE_STORAGE_LS_OB_I_LS_RUNTIME_ADAPTER_H_
#define OCEANBASE_STORAGE_LS_OB_I_LS_RUNTIME_ADAPTER_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObTimer;
}
namespace data_plane
{
struct ObLogServiceHandler;
class ObIVectorIndexScheduler;
}
namespace storage
{
class ObLS;

// Behaviour implemented above Storage and installed by the Observer
// composition root.  The seam is deliberately limited to behaviours whose
// concrete implementations cannot live in Storage.
class ObILSRuntimeAdapter
{
public:
  virtual ~ObILSRuntimeAdapter() = default;
  virtual int resolve_log_handler(
      int64_t log_type,
      data_plane::ObLogServiceHandler &handler) = 0;
  virtual int create_vector_index_scheduler(
      ObLS &ls,
      common::ObTimer &timer,
      data_plane::ObIVectorIndexScheduler *&scheduler) = 0;
  virtual void destroy_vector_index_scheduler(
      data_plane::ObIVectorIndexScheduler *&scheduler) = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_LS_OB_I_LS_RUNTIME_ADAPTER_H_
