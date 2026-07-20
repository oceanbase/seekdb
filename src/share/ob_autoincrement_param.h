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

#ifndef OCEANBASE_SHARE_OB_AUTOINCREMENT_PARAM_H_
#define OCEANBASE_SHARE_OB_AUTOINCREMENT_PARAM_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/hash_func/murmur_hash.h"
#include "common/object/ob_obj_type.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{

static const uint64_t DEFAULT_INCREMENT_CACHE_SIZE = 1000000;       // 1 million
static const uint64_t MAX_INCREMENT_CACHE_SIZE = 100000000;         // 100 million
struct AutoincKey
{
  OB_UNIS_VERSION(1);

public:
  AutoincKey(uint64_t table_id, uint64_t column_id) :
      table_id_(table_id), column_id_(column_id) {}
  AutoincKey() : table_id_(0), column_id_(0) {}
  void reset()
  {
    table_id_ = 0;
    column_id_ = 0;
  }
  bool operator==(const AutoincKey &other) const
  {
    return true
           && other.table_id_  == table_id_
           && other.column_id_ == column_id_;
  }

  int compare(const AutoincKey &other) {
    return (table_id_  < other.table_id_ ) ? -1 :
           (table_id_  > other.table_id_ ) ?  1 :
           (column_id_ < other.column_id_) ? -1 :
           (column_id_ > other.column_id_) ?  1 :
           0;
  }

  uint64_t hash() const
  {
    uint64_t hash_val = 0;
    hash_val = common::murmurhash(&table_id_, sizeof(table_id_), hash_val);
    hash_val = common::murmurhash(&column_id_, sizeof(column_id_), hash_val);
    return hash_val;
  }

  int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }

  TO_STRING_KV(K_(table_id), K_(column_id));

  uint64_t table_id_;
  uint64_t column_id_;
};

struct CacheHandle;
struct AutoincParam
{
  AutoincParam()
    : autoinc_table_id_(0),
      autoinc_first_part_num_(0),
      autoinc_table_part_num_(0),
      autoinc_col_id_(0),
      autoinc_col_type_(common::ObNullType),
      total_value_count_(0),
      autoinc_desired_count_(0),
      autoinc_old_value_index_(-1),
      autoinc_increment_(0),
      autoinc_offset_(0),
      cache_handle_(NULL),
      curr_value_count_(0),
      global_value_to_sync_(0),
      value_to_sync_(0),
      sync_flag_(false),
      is_ignore_(false),
      part_value_no_order_(false),
      autoinc_intervals_count_(0),
      part_level_(schema::PARTITION_LEVEL_ZERO),
      auto_increment_cache_size_(DEFAULT_INCREMENT_CACHE_SIZE),
      autoinc_mode_is_order_(true),
      autoinc_version_(OB_INVALID_VERSION),
      autoinc_auto_increment_(1)
  {}

  TO_STRING_KV("autoinc_table_id"        , autoinc_table_id_,
               "autoinc_first_part_num"  , autoinc_first_part_num_,
               "autoinc_table_part_num"  , autoinc_table_part_num_,
               "autoinc_col_id"          , autoinc_col_id_,
               "autoinc_col_type"        , autoinc_col_type_,
               "total_value_count_"      , total_value_count_,
               "autoinc_desired_count"   , autoinc_desired_count_,
               "autoinc_old_value_index" , autoinc_old_value_index_,
               "autoinc_increment"       , autoinc_increment_,
               "autoinc_offset"          , autoinc_offset_,
               "curr_value_count"        , curr_value_count_,
               "global_value_to_sync"    , global_value_to_sync_,
               "value_to_sync"           , value_to_sync_,
               "sync_flag"               , sync_flag_,
               "is_ignore"               , is_ignore_,
               "autoinc_intervals_count" , autoinc_intervals_count_,
               "part_level"              , part_level_,
               "auto_increment_cache_size"  , auto_increment_cache_size_,
               "part_value_no_order"     , part_value_no_order_,
               "autoinc_mode_is_order"   , autoinc_mode_is_order_,
               "autoinc_version"         , autoinc_version_,
               "autoinc_auto_increment"  , autoinc_auto_increment_);

  inline bool with_order() const { return !part_value_no_order_; }
  // pay attention to schema changes
  
  uint64_t          autoinc_table_id_;
  int64_t           autoinc_first_part_num_;
  int64_t           autoinc_table_part_num_;
  uint64_t          autoinc_col_id_;
  common::ObObjType autoinc_col_type_;
  uint64_t          total_value_count_;
  uint64_t          autoinc_desired_count_;
  int64_t           autoinc_old_value_index_; //used in insert on duplicate key
  // need to refresh param below when ObSQL get plan from pc
  // session variable may be refreshed already
  uint64_t          autoinc_increment_;
  uint64_t          autoinc_offset_;
  // do not serialize
  CacheHandle       *cache_handle_;
  uint64_t          curr_value_count_;
  uint64_t          global_value_to_sync_;
  uint64_t          value_to_sync_;
  bool              sync_flag_;
  bool              is_ignore_;
  // in order to support partitioning with auto increment pkey,
  // we have to **loose the restriction** that generated number must be
  // in intra-partition ascending order.
  // 
  // If part_value_no_order_ flag = true, we can break this ordering restriction.
  // for compatibility consideration, part_value_no_order_ defaults to false
  bool              part_value_no_order_;

  // count for cache handle allocated already
  uint64_t          autoinc_intervals_count_;
  schema::ObPartitionLevel part_level_;
  int64_t auto_increment_cache_size_;
  bool              autoinc_mode_is_order_;
  int64_t           autoinc_version_;
  uint64_t          autoinc_auto_increment_; // auto increment value of table schema
  OB_UNIS_VERSION(1);
};

OB_INLINE int64_t get_auto_increment_cache_size(const int64_t table_cache_size,
                                                const int64_t tenant_cache_size)
{
  return table_cache_size == 0 ? tenant_cache_size : table_cache_size;
}

}//end namespace share
}//end namespace oceanbase
#endif
