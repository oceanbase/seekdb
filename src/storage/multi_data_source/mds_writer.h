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
 
#ifndef SRC_STORAGE_MULTI_DATA_SOURCE_MDS_WRITTER_H
#define SRC_STORAGE_MULTI_DATA_SOURCE_MDS_WRITTER_H

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "runtime_utility/common_define.h"

namespace oceanbase
{
namespace transaction
{
class ObTransID;
}
namespace storage
{
namespace mds
{
struct MdsWriter
{
  OB_UNIS_VERSION(1);
public:
  MdsWriter() : writer_type_(WriterType::UNKNOWN_WRITER), writer_id_(INVALID_VALUE) {}
  MdsWriter(const WriterType writer_type, const int64_t writer_id = DEFAULT_WRITER_ID);
  explicit MdsWriter(const transaction::ObTransID &tx_id);
  bool operator==(const MdsWriter &rhs) const {
    return writer_type_ == rhs.writer_type_ && writer_id_ == rhs.writer_id_;
  }
  bool operator!=(const MdsWriter &rhs) const { return !operator==(rhs); }
  void reset();
  bool is_valid() const;
  TO_STRING_KV("writer_type", obj_to_string(writer_type_), K_(writer_id));
public:
  static constexpr int64_t DEFAULT_WRITER_ID = 10000;

  WriterType writer_type_;
  int64_t writer_id_;
};
OB_SERIALIZE_MEMBER_TEMP(inline, MdsWriter, writer_type_, writer_id_);
}
}
}

#endif
