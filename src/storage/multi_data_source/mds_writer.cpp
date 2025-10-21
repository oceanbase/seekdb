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
 
#include "mds_writer.h"
#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{

MdsWriter::MdsWriter(const WriterType writer_type, const int64_t writer_id) : writer_type_(writer_type), writer_id_(writer_id) {}

MdsWriter::MdsWriter(const transaction::ObTransID &tx_id) : writer_type_(WriterType::TRANSACTION), writer_id_(tx_id.get_id()) {}

void MdsWriter::reset()
{
  writer_type_ = WriterType::UNKNOWN_WRITER;
  writer_id_ = DEFAULT_WRITER_ID;
}

bool MdsWriter::is_valid() const
{
  return writer_type_ != WriterType::UNKNOWN_WRITER && writer_type_ < WriterType::END;
}

}
}
}
