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

#include "storage/direct_load/ob_direct_load_compare.h"
#include "storage/direct_load/ob_direct_load_external_multi_partition_row.h"
#include "storage/direct_load/ob_direct_load_mem_chunk.h"

namespace oceanbase
{
namespace storage
{

struct ObDirectLoadExternalMultiPartitionRowRange
{
public:
  ObDirectLoadExternalMultiPartitionRowRange() : start_(nullptr), end_(nullptr) {}
  ObDirectLoadExternalMultiPartitionRowRange(ObDirectLoadConstExternalMultiPartitionRow *start,
                                             ObDirectLoadConstExternalMultiPartitionRow *end)
    : start_(start), end_(end)
  {
  }
  TO_STRING_KV(KP_(start), KP_(end));

public:
  ObDirectLoadConstExternalMultiPartitionRow *start_;
  ObDirectLoadConstExternalMultiPartitionRow *end_;
};

typedef ObDirectLoadMemChunk<ObDirectLoadConstExternalMultiPartitionRow,
                             ObDirectLoadExternalMultiPartitionRowCompare>
  ObDirectLoadExternalMultiPartitionRowChunk;

} // namespace storage
} // namespace oceanbase
