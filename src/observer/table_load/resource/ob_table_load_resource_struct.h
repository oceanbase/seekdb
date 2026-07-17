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

#include "lib/container/ob_array.h"
#include "lib/utility/ob_print_utils.h"
#include "observer/table_load/ob_table_load_struct.h"

namespace oceanbase
{
namespace observer
{

struct ObDirectLoadResourceUnit
{
  ObDirectLoadResourceUnit() : thread_count_(0), memory_size_(0) {}
  TO_STRING_KV(K_(addr), K_(thread_count), K_(memory_size));

  common::ObAddr addr_;
  int64_t thread_count_;
  int64_t memory_size_;
};

struct ObDirectLoadResourceApplyArg
{
  bool is_valid() const
  {
    bool valid = task_key_.is_valid();
    for (int64_t i = 0; valid && i < apply_array_.count(); ++i) {
      const ObDirectLoadResourceUnit &unit = apply_array_[i];
      valid = unit.addr_.is_valid() && unit.thread_count_ >= 0 && unit.memory_size_ >= 0;
    }
    return valid;
  }
  TO_STRING_KV(K_(task_key), K_(apply_array));

  ObTableLoadUniqueKey task_key_;
  common::ObSArray<ObDirectLoadResourceUnit> apply_array_;
};

struct ObDirectLoadResourceReleaseArg
{
  bool is_valid() const { return task_key_.is_valid(); }
  TO_STRING_KV(K_(task_key));

  ObTableLoadUniqueKey task_key_;
};

} // namespace observer
} // namespace oceanbase
