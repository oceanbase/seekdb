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
#include "share/compaction/ob_new_micro_info.h"

namespace oceanbase
{
namespace compaction
{
OB_SERIALIZE_MEMBER_SIMPLE(ObNewMicroInfo, info_, meta_micro_size_, data_micro_size_);

void ObNewMicroInfo::add(const ObNewMicroInfo &input_info)
{
  meta_micro_size_ += input_info.meta_micro_size_;
  data_micro_size_ += input_info.data_micro_size_;
}

} // namespace compaction
} // namespace oceanbase
