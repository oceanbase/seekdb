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

#include "lib/net/ob_addr.h"
#include "share/table/ob_table_load_row_array.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace common
{
class ObObj;
}  // namespace common
namespace observer
{

class ObTableLoadBucket
{
public:
  ObTableLoadBucket() : is_inited_(false), row_size_(0), sequence_no_(0) {}

  int add_row(const common::ObTabletID &tablet_id,
              const table::ObTableLoadObjRow &obj_row,
              int64_t batch_size,
              int64_t row_size,
              bool &flag);

  void reset() {
    is_inited_ = false;
    leader_addr_.reset();
    row_array_.reset();
    row_size_ = 0;
  }

  void clear_data() {
    row_array_.reset();
    row_size_ = 0;
  }

  bool is_inited() const {
    return is_inited_;
  }
  int init(const common::ObAddr &leader_addr);

  TO_STRING_KV(K_(leader_addr), K_(sequence_no));

public:
  // data members
  bool is_inited_;
  common::ObAddr leader_addr_;
  table::ObTableLoadTabletObjRowArray row_array_;
  int64_t row_size_;
  uint64_t sequence_no_;
};

}  // namespace observer
}  // namespace oceanbase
