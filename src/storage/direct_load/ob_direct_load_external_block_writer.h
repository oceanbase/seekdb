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

#include "storage/direct_load/ob_direct_load_data_block.h"
#include "storage/direct_load/ob_direct_load_data_block_writer.h"

namespace oceanbase
{
namespace storage
{

template <typename T>
class ObDirectLoadExternalBlockWriter
  : public ObDirectLoadDataBlockWriter<ObDirectLoadDataBlock::Header, T>
{
  typedef ObDirectLoadDataBlockWriter<ObDirectLoadDataBlock::Header, T> ParentType;
public:
  ObDirectLoadExternalBlockWriter() {}
  virtual ~ObDirectLoadExternalBlockWriter() {}
  int init(int64_t data_block_size, common::ObCompressorType compressor_type, char *extra_buf,
           int64_t extra_buf_size)
  {
    return ParentType::init(data_block_size, compressor_type, extra_buf, extra_buf_size, nullptr);
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadExternalBlockWriter);
};

} // namespace storage
} // namespace oceanbase
