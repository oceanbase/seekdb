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

#define USING_LOG_PREFIX SQL_OPT
#include "share/stat/ob_opt_table_stat.h"
namespace oceanbase {
namespace common {
using namespace sql;

OB_DEF_SERIALIZE(ObOptTableStat) {
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              table_id_,
              partition_id_,
              object_type_,
              row_count_,
              avg_row_size_,
              sample_size_
              );
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObOptTableStat) {
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              table_id_,
              partition_id_,
              object_type_,
              row_count_,
              avg_row_size_,
              sample_size_
              );
  return len;
}

OB_DEF_DESERIALIZE(ObOptTableStat) {
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              table_id_,
              partition_id_,
              object_type_,
              row_count_,
              avg_row_size_,
              sample_size_
              );
  return ret; 
}

int ObOptTableStat::merge_table_stat(const ObOptTableStat &other)
{
  int ret = OB_SUCCESS;
  if (table_id_ != other.get_table_id() ||
      partition_id_ != other.get_partition_id()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("two stat do not match", K(ret));
  } else {
    double avg_len = 0;
    other.get_avg_row_size(avg_len); 
    add_avg_row_size(avg_len, other.get_row_count());
    if (sample_size_ == 0) {
      sample_size_ = row_count_;
    }
    row_count_ += other.get_row_count();
    stattype_locked_ = other.get_stattype_locked();
    sample_size_ += (other.sample_size_ == 0 ? other.get_row_count() : other.sample_size_);
  }

  return ret;
}
}
}
