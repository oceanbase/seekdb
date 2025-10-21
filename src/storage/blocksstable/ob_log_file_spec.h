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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_LOG_FILE_SPEC_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_LOG_FILE_SPEC_H_

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace blocksstable
{
struct ObLogFileSpec
{
  const char *retry_write_policy_;
  const char *log_create_policy_;
  const char *log_write_policy_;

  ObLogFileSpec()
    : retry_write_policy_(nullptr),
      log_create_policy_(nullptr),
      log_write_policy_(nullptr)
  {
  }

  void reset()
  {
    retry_write_policy_ = nullptr;
    log_create_policy_ = nullptr;
    log_write_policy_ = nullptr;
  }

  bool is_valid() const
  {
    return nullptr != retry_write_policy_
        && nullptr != log_create_policy_
        && nullptr != log_write_policy_;
  }

  TO_STRING_KV(K_(retry_write_policy), K_(log_create_policy), K_(log_write_policy));
};
} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_BLOCKSSTABLE_OB_LOG_FILE_SPEC_H_
