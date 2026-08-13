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

#ifndef OCEANBASE_SHARE_OB_DDL_TASK_SERIALIZE_FIELD_H_
#define OCEANBASE_SHARE_OB_DDL_TASK_SERIALIZE_FIELD_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace share
{

// Stable prefix persisted in every DDL task message.  Runtime task classes
// append their private payload after this value object.
struct ObDDLTaskSerializeField final
{
  OB_UNIS_VERSION(1);
public:
  ObDDLTaskSerializeField();
  ObDDLTaskSerializeField(const int64_t task_version,
                          const int64_t parallelism,
                          const uint64_t data_format_version,
                          const bool is_abort,
                          const int32_t sub_task_trace_id,
                          const bool is_unique_index,
                          const bool is_global_index,
                          const bool is_no_logging);
  ~ObDDLTaskSerializeField() = default;

  void reset();

  TO_STRING_KV(K_(task_version), K_(parallelism), K_(data_format_version),
               K_(is_abort), K_(sub_task_trace_id), K_(is_unique_index),
               K_(is_global_index), K_(is_no_logging));

  int64_t task_version_;
  int64_t parallelism_;
  uint64_t data_format_version_;
  bool is_abort_;
  int32_t sub_task_trace_id_;
  bool is_unique_index_;
  bool is_global_index_;
  bool is_no_logging_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_DDL_TASK_SERIALIZE_FIELD_H_
