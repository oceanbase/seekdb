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

#define USING_LOG_PREFIX STORAGE

#include "storage/ddl/ob_ddl_fixed_length_vector.h"
#include "sql/engine/vector/ob_uniform_vector.h"

namespace oceanbase
{
namespace storage
{
template class ObDDLFixedLengthVector<int8_t>;
template class ObDDLFixedLengthVector<int16_t>;
template class ObDDLFixedLengthVector<int32_t>;
template class ObDDLFixedLengthVector<int64_t>;
template class ObDDLFixedLengthVector<int128_t>;
template class ObDDLFixedLengthVector<int256_t>;
template class ObDDLFixedLengthVector<int512_t>;

template class ObDDLFixedLengthVector<uint8_t>;
template class ObDDLFixedLengthVector<uint16_t>;
template class ObDDLFixedLengthVector<uint32_t>;
template class ObDDLFixedLengthVector<uint64_t>;

template class ObDDLFixedLengthVector<float>;
template class ObDDLFixedLengthVector<double>;

template class ObDDLFixedLengthVector<ObOTimestampData>;
template class ObDDLFixedLengthVector<ObOTimestampTinyData>;

} // namespace storage
} // namespace oceanbase
