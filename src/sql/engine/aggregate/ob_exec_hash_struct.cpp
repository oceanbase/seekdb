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

#include "ob_exec_hash_struct.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

//When there's stored_row_ and reserved_cells_, use store_row's reserved_cells_ for calc hash.
//Other, use row_ for calc hash
uint64_t ObHashCols::inner_hash() const
{
  uint64_t result = 99194853094755497L;
  if (hash_col_idx_ != NULL) {
    int64_t group_col_count = hash_col_idx_->count();
    if (stored_row_ != NULL
        && stored_row_->reserved_cells_count_ > 0) {
      const ObObj *cells = stored_row_->reserved_cells_;
      for (int32_t i = 0; i < group_col_count; ++i) {
        if (hash_col_idx_->at(i).index_ < stored_row_->reserved_cells_count_) {
          const ObObj &cell = cells[hash_col_idx_->at(i).index_];
          if (cell.is_string_type()) {
            result = cell.varchar_murmur_hash(hash_col_idx_->at(i).cs_type_, result);
          } else {
            cell.hash(result, result);
          }
        }
      }
    } else if (row_ != NULL && row_->is_valid()) {
      const ObObj *cells = row_->cells_;
      const int32_t *projector = row_->projector_;
      for (int64_t i = 0; i < group_col_count; ++i) {
        int64_t real_index = row_->projector_size_ > 0 ?
            projector[hash_col_idx_->at(i).index_] : hash_col_idx_->at(i).index_;
        const ObObj &cell = cells[real_index];
        if (cell.is_string_type()) {
          result = cell.varchar_murmur_hash(hash_col_idx_->at(i).cs_type_, result);
        } else {
          cell.hash(result, result);
        }
      }
    }
  }
  return result;
}

//When there is stored_row_ reserved_cells, use stored_row_'s reserved_cells_ for calc equal.
//Other use row_.

}//ns sql
}//ns oceanbase

