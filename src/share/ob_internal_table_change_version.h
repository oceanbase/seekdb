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

#include "share/scn.h"

namespace oceanbase
{
namespace share
{

struct InternalTableChangeVersion {
  uint64_t change_seq_ = 0;
  SCN commit_version_ = SCN::min_scn();

  bool operator==(const InternalTableChangeVersion &other) const
  {
    return change_seq_ == other.change_seq_
        && commit_version_ == other.commit_version_;
  }
  bool operator!=(const InternalTableChangeVersion &other) const
  {
    return !(*this == other);
  }
  TO_STRING_KV(K_(change_seq), K_(commit_version));
};

} // namespace share
} // namespace oceanbase
