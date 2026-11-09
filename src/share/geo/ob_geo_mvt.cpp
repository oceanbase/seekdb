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


#define USING_LOG_PREFIX LIB
#include "ob_geo_mvt.h"
#include "rust/mvt.h"
#include "share/ob_lob_access_utils.h"
#include "share/datum/datum_value_cast.h"
#include "common/ob_hex_utils_base.h"
#include "lib/geometry/ob_geo_wkb_define.h"
#include "share/rc/ob_server_runtime.h"
#include "share/geo/ob_geo_utils.h"
#include "share/geo/ob_geo_mvt_encode_visitor.h"
#include "common/json_type/ob_json_bin.h"
#include <limits>

namespace oceanbase {
namespace common {
namespace {
rust::Slice<const uint8_t> bytes(const ObString &value)
{
  return {reinterpret_cast<const uint8_t *>(value.ptr()), static_cast<size_t>(value.length())};
}
}

mvt_agg_result::mvt_agg_result(ObIAllocator &alloc, ObILobReadService &lob_read_service)
    : allocator_(alloc), temp_allocator_(nullptr), lob_read_service_(lob_read_service),
      inited_(false), lay_name_("default"), geom_name_(), geom_idx_(UINT32_MAX),
      feature_id_name_(), feat_id_idx_(UINT32_MAX), builder_(mvt::new_builder()),
      extent_(4096), column_offset_(0)
{}

mvt_agg_result::~mvt_agg_result() = default;

int mvt_agg_result::init_layer()
{
  builder_->configure(bytes(lay_name_), extent_);
  return builder_->error_code();
}

int mvt_agg_result::add_key(const ObString &key)
{
  builder_->seed_key(bytes(key));
  return builder_->error_code();
}

int mvt_agg_result::generate_feature(const DatumValue *values, uint32_t value_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(builder_->error_code())) {
  } else if (geom_idx_ >= value_cnt) {
    ret = OB_ERR_GIS_UNSUPPORTED_ARGUMENT;
  } else if (values[geom_idx_].is_null()) {
  } else if (!values[geom_idx_].get_meta().is_geometry()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const SqlTypeMeta &meta = values[geom_idx_].get_meta();
    ObString str = values[geom_idx_].get_string();
    ObGeometry *geo = nullptr;
    bool is_outrow = false;
    if (is_lob_storage(meta.get_type())) {
      ObTextStringIter str_iter(meta.get_type(), meta.get_collation_type(), str,
                               values[geom_idx_].has_lob_header());
      const ObLobReadOptions read_options(lob_read_service_);
      if (OB_FAIL(str_iter.init(0, &read_options, temp_allocator_))) {
      } else if (OB_FAIL(str_iter.get_full_data(str))) {
      } else {
        is_outrow = str_iter.is_outrow_lob();
      }
    }
    if (OB_SUCC(ret)
        && OB_FAIL(ObGeoTypeUtil::construct_geometry(*temp_allocator_, str, nullptr,
                                                    geo, true, !is_outrow))) {
    } else if (OB_SUCC(ret) && ObGeoTypeUtil::is_3d_geo_type(geo->type())
               && OB_FAIL(ObGeoTypeUtil::convert_geometry_3D_to_2D(
                   nullptr, allocator_, geo, ObGeoBuildFlag::GEO_ALL_DISABLE, geo))) {
    } else if (OB_SUCC(ret) && OB_FAIL(transform_geom(*geo))) {
    } else if (OB_SUCC(ret) && OB_FAIL(transform_other_column(values, value_cnt))) {
    } else if (OB_SUCC(ret)) {
      builder_->commit_feature();
      ret = builder_->error_code();
    }
  }
  return ret;
}

int mvt_agg_result::transform_geom(const ObGeometry &geo)
{
  int ret = OB_SUCCESS;
  uint32_t kind = 0;
  switch (geo.type()) {
    case ObGeoType::POINT:
    case ObGeoType::MULTIPOINT:
      kind = 1;
      break;
    case ObGeoType::LINESTRING:
    case ObGeoType::MULTILINESTRING:
      kind = 2;
      break;
    case ObGeoType::POLYGON:
    case ObGeoType::MULTIPOLYGON:
      kind = 3;
      break;
    default:
      ret = OB_ERR_UNEXPECTED_GEOMETRY_TYPE;
      LOG_USER_ERROR(OB_ERR_UNEXPECTED_GEOMETRY_TYPE, ObGeoTypeUtil::get_geo_name_by_type(geo.type()),
          ObGeoTypeUtil::get_geo_name_by_type(geo.type()), "_st_asmvt");
  }
  if (OB_SUCC(ret)) {
    ObGeoMvtEncodeVisitor visitor;
    if (OB_FAIL(const_cast<ObGeometry &>(geo).do_visit(visitor))) {
    } else {
      ObVector<uint32_t> &buf = visitor.get_encode_buffer();
      builder_->begin_feature(kind, {buf.size() == 0 ? nullptr : &buf.at(0), static_cast<size_t>(buf.size())});
      ret = builder_->error_code();
    }
  }
  return ret;
}

int mvt_agg_result::geometry_to_varchar(const DatumValue &value, ObIAllocator &scratch, ObString &result)
{
  int ret = OB_SUCCESS;
  ObString wkb = value.get_string();
  if (value.is_null() || !value.get_meta().is_geometry()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_lob_storage(value.get_type())) {
    ObTextStringIter str_iter(value.get_type(), value.get_collation_type(), wkb, value.has_lob_header());
    const ObLobReadOptions read_options(lob_read_service_);
    if (OB_FAIL(str_iter.init(0, &read_options, &scratch))) {
    } else if (OB_FAIL(str_iter.get_full_data(wkb))) {
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t src_len = wkb.length();
    uint32_t offset = WKB_GEO_SRID_SIZE;
    int64_t result_len = src_len;
    if (src_len <= WKB_OFFSET) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const uint8_t version = static_cast<uint8_t>(wkb.ptr()[WKB_GEO_SRID_SIZE]);
      if (IS_GEO_VERSION(version)) {
        result_len -= WKB_VERSION_SIZE;
        offset += WKB_VERSION_SIZE;
      }
      char *buffer = nullptr;
      if (OB_ISNULL(buffer = static_cast<char *>(allocator_.alloc(result_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(buffer, wkb.ptr(), WKB_GEO_SRID_SIZE);
        MEMCPY(buffer + WKB_GEO_SRID_SIZE, wkb.ptr() + offset, result_len - WKB_GEO_SRID_SIZE);
        result.assign_ptr(buffer, static_cast<int32_t>(result_len));
      }
    }
  }
  return ret;
}

int mvt_agg_result::get_key_id(ObString col_name, uint32_t &key_id)
{
  key_id = builder_->find_key(bytes(col_name));
  const int ret = builder_->error_code();
  return ret != OB_SUCCESS ? ret : (key_id == UINT32_MAX ? OB_ERR_UNEXPECTED : OB_SUCCESS);
}

int mvt_agg_result::transform_json_column(const DatumValue &json)
{
  int ret = OB_SUCCESS;
  ObString str;
  ObTextStringIter str_iter(json.get_meta().get_type(), json.get_meta().get_collation_type(), json.get_string(), json.has_lob_header());
  const ObLobReadOptions read_options(lob_read_service_);
  if (OB_FAIL(str_iter.init(0, &read_options, temp_allocator_))) {
  } else if (OB_FAIL(str_iter.get_full_data(str))) {
  } else {
    ObArenaAllocator temp_allocator;
    ObJsonBin j_bin(str.ptr(), str.length(), &temp_allocator);
    ObIJsonBase *j_base = &j_bin;
    if (OB_FAIL(j_bin.reset_iter())) {
    } else if (j_base->json_type() == ObJsonNodeType::J_OBJECT) {
      const uint64_t count = j_base->element_count();
      for (uint64_t i = 0; OB_SUCC(ret) && builder_->error_code() == OB_SUCCESS && i < count; ++i) {
        ObString key;
        if (OB_FAIL(j_base->get_key(i, key))) {
        } else {
          const uint32_t key_id = builder_->intern_key(bytes(key));
          ObJsonBin j_val(temp_allocator_);
          ObIJsonBase *value = &j_val;
          if (OB_FAIL(j_base->get_object_value(i, value))) {
          } else {
            switch (value->json_type()) {
              case ObJsonNodeType::J_BOOLEAN:
                builder_->add_bool(key_id, value->get_boolean());
                break;
              case ObJsonNodeType::J_DOUBLE:
                builder_->add_double(key_id, value->get_double());
                break;
              case ObJsonNodeType::J_INT: {
                const int64_t v = value->get_int();
                if (v >= 0) builder_->add_uint(key_id, static_cast<uint64_t>(v));
                else builder_->add_sint(key_id, v);
                break;
              }
              case ObJsonNodeType::J_UINT:
                builder_->add_uint(key_id, value->get_uint());
                break;
              case ObJsonNodeType::J_STRING:
                builder_->add_string(key_id, {reinterpret_cast<const uint8_t *>(value->get_data()),
                    static_cast<size_t>(value->get_data_length())});
                break;
              default:
                break;
            }
          }
        }
      }
    }
  }
  return ret == OB_SUCCESS ? builder_->error_code() : ret;
}

int mvt_agg_result::transform_other_column(const DatumValue *values, uint32_t value_cnt)
{
  int ret = OB_SUCCESS;
  for (uint32_t i = column_offset_;
       i < value_cnt && OB_SUCC(ret) && builder_->error_code() == OB_SUCCESS;
       i += 2) {
    const SqlValueType type = values[i].get_type();
    uint32_t key_id = UINT32_MAX;
    if (i == geom_idx_) {
    } else if (i == feat_id_idx_) {
      if (values[i].is_null()) {
      } else if (!ob_is_int_tc(type) && !ob_is_uint_tc(type)) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
      } else {
        const int64_t v = values[i].get_int();
        if (v >= 0) builder_->set_id(static_cast<uint64_t>(v));
      }
    } else if (!values[i].is_null() && ob_is_json(type)) {
      ret = transform_json_column(values[i]);
    } else if (values[i].is_null() || ob_is_enum_or_set_type(type)) {
    } else if (i + 1 >= value_cnt) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(get_key_id(values[i + 1].get_string(), key_id))) {
    } else if (type == ObTinyIntType) {
      builder_->add_bool(key_id, values[i].get_int() != 0);
    } else if (ob_is_int_tc(type)) {
      const int64_t v = values[i].get_int();
      if (v >= 0) builder_->add_uint(key_id, static_cast<uint64_t>(v));
      else builder_->add_sint(key_id, v);
    } else if (ob_is_uint_tc(type)) {
      builder_->add_uint(key_id, values[i].get_uint64());
    } else if (ob_is_float_tc(type)) {
      builder_->add_float(key_id, values[i].get_float());
    } else if (ob_is_double_tc(type)) {
      builder_->add_double(key_id, values[i].get_double());
    } else {
      ObString str;
      if (ob_is_geometry(type)) {
        ObString cast_string;
        if (OB_FAIL(geometry_to_varchar(values[i], *temp_allocator_, cast_string))) {
        } else if (OB_FAIL(ObHexUtilsBase::hex(str, allocator_, cast_string.ptr(),
                                             cast_string.length()))) {
        }
      } else {
        DatumCastParams cast_ctx(temp_allocator_, nullptr, CM_NONE,
                                 ObCharset::get_system_collation());
        DatumValue varchar_value;
        if (OB_FAIL(DatumValueCaster::to_type(ObVarcharType, cast_ctx, values[i],
                                             allocator_, varchar_value))) {
        } else {
          str = varchar_value.get_string();
        }
      }
      if (OB_SUCC(ret)) {
        builder_->add_string(key_id, bytes(str));
      }
    }
  }
  return ret == OB_SUCCESS ? builder_->error_code() : ret;
}

int mvt_agg_result::mvt_pack(ObString &blob_res)
{
  int ret = OB_SUCCESS;
  const size_t encoded_size = builder_->encoded_len();
  if (OB_FAIL(builder_->error_code())) {
  } else if (encoded_size > static_cast<size_t>(std::numeric_limits<ObString::obstr_size_t>::max())) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    char *buf = static_cast<char *>(allocator_.alloc(encoded_size));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (!builder_->encode_to({reinterpret_cast<uint8_t *>(buf), encoded_size})) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      blob_res.assign_ptr(buf, static_cast<ObString::obstr_size_t>(encoded_size));
    }
  }
  return ret;
}

bool mvt_agg_result::is_upper_char_exist(const ObString &str)
{
  bool res = false;
  const char *ptr = str.ptr();
  for (int32_t i = 0; i < str.length() && !res; i++) {
    if (isupper(static_cast<unsigned char>(ptr[i]))) {
      res = true;
    } 
  }
  return res;
}

} // namespace common
} // namespace oceanbase
