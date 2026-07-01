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

#include "storage/direct_load/ob_direct_load_fixed_length_vector.h"
#include "share/vector/ob_uniform_vector.h"

namespace oceanbase
{
namespace storage
{
template class ObDirectLoadFixedLengthVector<int8_t>;
template class ObDirectLoadFixedLengthVector<int16_t>;
template class ObDirectLoadFixedLengthVector<int32_t>;
template class ObDirectLoadFixedLengthVector<int64_t>;
template class ObDirectLoadFixedLengthVector<int128_t>;
template class ObDirectLoadFixedLengthVector<int256_t>;
template class ObDirectLoadFixedLengthVector<int512_t>;

template class ObDirectLoadFixedLengthVector<uint8_t>;
template class ObDirectLoadFixedLengthVector<uint16_t>;
template class ObDirectLoadFixedLengthVector<uint32_t>;
template class ObDirectLoadFixedLengthVector<uint64_t>;

template class ObDirectLoadFixedLengthVector<float>;
template class ObDirectLoadFixedLengthVector<double>;

template class ObDirectLoadFixedLengthVector<ObOTimestampData>;
template class ObDirectLoadFixedLengthVector<ObOTimestampTinyData>;

} // namespace storage
} // namespace oceanbase
