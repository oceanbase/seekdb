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

#ifndef OCEANBASE_LIB_GEO_MVT_
#define OCEANBASE_LIB_GEO_MVT_

#include "ob_geo.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_iarray.h"
#include "lib/allocator/ob_allocator.h"
#include "common/datum/datum_value.h"
#include "rust/cxx.h"

namespace oceanbase
{
namespace mvt { struct LayerBuilder; }
namespace common
{

class ObILobReadService;

class mvt_agg_result
{
public:
  mvt_agg_result(common::ObIAllocator &alloc, ObILobReadService &lob_read_service);
  virtual ~mvt_agg_result();
  inline bool is_inited() { return inited_; }
  inline const common::ObString& get_geom_name() const { return geom_name_; }
  inline common::ObString& get_geom_name() { return geom_name_; } 
  int init_layer();
  int add_key(const ObString &key);
  int generate_feature(const DatumValue *values, uint32_t value_cnt);
  int transform_geom(const ObGeometry &geo);
  int geometry_to_varchar(const DatumValue &value, ObIAllocator &scratch, ObString &result);
  int transform_other_column(const DatumValue *values, uint32_t obj_cnt);
  int transform_json_column(const DatumValue &json);
  int mvt_pack(ObString &blob_res);
  void set_tmp_allocator(common::ObIAllocator *temp_allocator) { temp_allocator_ = temp_allocator;}
  static bool is_upper_char_exist(const ObString &str);
private:
  int get_key_id(ObString col_name, uint32_t &key_id);

public:

  common::ObIAllocator &allocator_;
  // for single row iterate allocator;
  common::ObIAllocator *temp_allocator_;
  ObILobReadService &lob_read_service_;
  bool inited_;
  common::ObString lay_name_;
  common::ObString geom_name_;
  // feature geometry index
  uint32_t geom_idx_;
  common::ObString feature_id_name_;
  // feature id column index
  uint32_t feat_id_idx_;
  rust::Box<mvt::LayerBuilder> builder_;
  uint32_t extent_;
  uint32_t column_offset_;
};

} // namespace common
} // namespace oceanbase

#endif
