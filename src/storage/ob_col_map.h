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

#ifndef OCEANBASE_STORAGE_OB_COL_MAP_
#define OCEANBASE_STORAGE_OB_COL_MAP_

#include "lib/hash/ob_placement_hashmap.h"

namespace oceanbase
{
namespace storage
{
class ObColMap
{
public:
  static const int64_t RP_LOCAL_NUM = 3;
  static const uint64_t FIRST_LEVEL_MAX_COL_NUM = 1024;
  static const uint64_t FIRST_LEVEL_MAP_COL_NUM = 1031;
  static const uint64_t FINAL_LEVEL_MAX_COL_NUM = 65536;
public:
  ObColMap() :
    is_inited_(false),
    col_map_first_(),
    col_map_final_(NULL)
  {
  }

  virtual ~ObColMap()
  {
    destroy();
  }

  void destroy();
  int init(const int64_t col_count);
  int set_refactored(const uint64_t col_id, const int64_t col_idx);
  int get_refactored(const uint64_t col_id, int64_t &col_idx) const;
  int64_t *get(const uint64_t col_id);
  void reset();
  bool is_valid() const { return is_inited_; }
  typedef common::hash::ObPlacementHashMap<uint64_t,
                                           int64_t,
                                           FIRST_LEVEL_MAP_COL_NUM
                                          > ColMapFirst;
  typedef common::hash::ObPlacementHashMap<uint64_t,
                                           int64_t,
                                           FINAL_LEVEL_MAX_COL_NUM
                                          > ColMapFinal;
private:
  bool is_inited_;
  ColMapFirst col_map_first_;
  ColMapFinal *col_map_final_;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObColMap);
};

} // namespace storage
} // namespace storage

#endif // OCEANBASE_STORAGE_OB_COL_MAP_
