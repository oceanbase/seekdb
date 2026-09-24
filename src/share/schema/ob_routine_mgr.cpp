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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_routine_mgr.h"
#include "share/schema/ob_schema_utils.h"
namespace oceanbase
{
using namespace std;
using namespace common;
using namespace common::hash;
namespace share
{
namespace schema
{
ObSimpleRoutineSchema::ObSimpleRoutineSchema()
  : ObSchema()
{
  reset();
}

ObSimpleRoutineSchema::ObSimpleRoutineSchema(ObIAllocator *allocator)
  : ObSchema(allocator)
{
  reset();
}

ObSimpleRoutineSchema::~ObSimpleRoutineSchema()
{
}

int ObSimpleRoutineSchema::assign(const ObSimpleRoutineSchema &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    error_ret_ = other.error_ret_;
    
    database_id_ = other.database_id_;
    package_id_ = other.package_id_;
    routine_id_ = other.routine_id_;
    overload_ = other.overload_;
    schema_version_ = other.schema_version_;
    routine_type_ = other.routine_type_;
    if (OB_FAIL(deep_copy_str(other.routine_name_, routine_name_))) {
    } else if (OB_FAIL(deep_copy_str(other.priv_user_, priv_user_))) {
    }
    if (OB_FAIL(ret)) {
      error_ret_ = ret;
    }
  }
  return ret;
}


int64_t ObSimpleRoutineSchema::get_convert_size() const
{
  int64_t convert_size = 0;

  convert_size += sizeof(ObSimpleRoutineSchema);
  convert_size += routine_name_.length() + 1;
  convert_size += priv_user_.length() + 1;

  return convert_size;
}

ObRoutineMgr::ObRoutineMgr()
    : local_allocator_(lib::ObMemAttr(ObModIds::OB_SCHEMA_GETTER_GUARD, ObCtxIds::SCHEMA_SERVICE)),
      allocator_(local_allocator_),
      routine_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_ROUTINE_INFO_VECTOR, ObCtxIds::SCHEMA_SERVICE)),
      routine_name_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_ROUTINE_INFO_VECTOR, ObCtxIds::SCHEMA_SERVICE)),
      routine_id_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_ROUTINE_ID_MAP, ObCtxIds::SCHEMA_SERVICE)),
      routine_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_ROUTINE_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      is_inited_(false), name_index_valid_(true)
{
}

ObRoutineMgr::ObRoutineMgr(ObIAllocator &allocator)
    : local_allocator_(lib::ObMemAttr(ObModIds::OB_SCHEMA_GETTER_GUARD, ObCtxIds::SCHEMA_SERVICE)),
      allocator_(allocator),
      routine_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_ROUTINE_INFO_VECTOR, ObCtxIds::SCHEMA_SERVICE)),
      routine_name_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_ROUTINE_INFO_VECTOR, ObCtxIds::SCHEMA_SERVICE)),
      routine_id_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_ROUTINE_ID_MAP, ObCtxIds::SCHEMA_SERVICE)),
      routine_name_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_ROUTINE_NAME_MAP, ObCtxIds::SCHEMA_SERVICE)),
      is_inited_(false), name_index_valid_(true)
{
}

ObRoutineMgr::~ObRoutineMgr()
{
}

int ObRoutineMgr::init()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(routine_id_map_.init())) {
  } else if (OB_FAIL(routine_name_map_.init())) {
  } else {
    is_inited_ = true;
  }

  return ret;
}

void ObRoutineMgr::reset()
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else {
    // reset will not release memory for vector, use clear()
    routine_infos_.clear();
    routine_name_infos_.clear();
    routine_id_map_.clear();
    routine_name_map_.clear();
    name_index_valid_ = true;
  }
}


int ObRoutineMgr::assign(const ObRoutineMgr &other)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else if (!other.check_inner_stat() || !other.name_index_valid_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (this != &other) {
    reset();
    name_index_valid_ = false;
    #define ASSIGN_FIELD(x)                        \
      if (OB_SUCC(ret)) {                          \
        if (OB_FAIL(x.assign(other.x))) {          \
          LOG_WARN("assign " #x "failed", K(ret)); \
        }                                          \
      }
    ASSIGN_FIELD(routine_infos_);
    ASSIGN_FIELD(routine_name_infos_);
    ASSIGN_FIELD(routine_id_map_);
    ASSIGN_FIELD(routine_name_map_);
    #undef ASSIGN_FIELD
    name_index_valid_ = OB_SUCC(ret);
  }

  return ret;
}

int ObRoutineMgr::deep_copy(const ObRoutineMgr &other)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else if (!other.check_inner_stat() || !other.name_index_valid_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (this != &other) {
    reset();
    for (RoutineIter iter = other.routine_infos_.begin();
       OB_SUCC(ret) && iter != other.routine_infos_.end(); iter++) {
      ObSimpleRoutineSchema *routine = *iter;
      if (OB_ISNULL(routine)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(add_routine(*routine))) {
      }
    }
    if (OB_FAIL(ret)) name_index_valid_ = false;
  }
  return ret;
}

bool ObRoutineMgr::check_inner_stat() const
{
  return is_inited_;
}

bool ObRoutineMgr::compare_routine(const ObSimpleRoutineSchema *lhs, const ObSimpleRoutineSchema *rhs)
{
  return lhs->get_routine_key() < rhs->get_routine_key();
}

bool ObRoutineMgr::equal_routine(const ObSimpleRoutineSchema *lhs, const ObSimpleRoutineSchema *rhs)
{
  return lhs->get_routine_key() == rhs->get_routine_key();
}

int ObRoutineMgr::compare_routine_name(const ObSimpleRoutineSchema *lhs,
                                      const ObRoutineNameHashWrapper &rhs)
{
  int cmp = 0;
  if (lhs->get_database_id() != rhs.get_database_id()) {
    cmp = lhs->get_database_id() < rhs.get_database_id() ? -1 : 1;
  } else if (lhs->get_package_id() != rhs.get_package_id()) {
    cmp = lhs->get_package_id() < rhs.get_package_id() ? -1 : 1;
  } else if (lhs->get_routine_type() != rhs.get_routine_type()) {
    cmp = lhs->get_routine_type() < rhs.get_routine_type() ? -1 : 1;
  } else if (0 != (cmp = ObSchemaNameComparator().compare(lhs->get_routine_name(), rhs.get_routine_name()))) {
  } else if (lhs->get_overload() != rhs.get_overload()) {
    cmp = lhs->get_overload() < rhs.get_overload() ? -1 : 1;
  }
  return cmp;
}

bool ObRoutineMgr::compare_name_key(const ObSimpleRoutineSchema *lhs,
                                   const ObRoutineNameHashWrapper &rhs)
{
  return compare_routine_name(lhs, rhs) < 0;
}

bool ObRoutineMgr::compare_names(const ObSimpleRoutineSchema *lhs, const ObSimpleRoutineSchema *rhs)
{
  return compare_name_key(lhs, ObGetRoutineKey<ObRoutineNameHashWrapper, ObSimpleRoutineSchema *>()(rhs));
}

bool ObRoutineMgr::equal_names(const ObSimpleRoutineSchema *lhs, const ObSimpleRoutineSchema *rhs)
{
  return 0 == compare_routine_name(lhs, ObGetRoutineKey<ObRoutineNameHashWrapper, ObSimpleRoutineSchema *>()(rhs));
}

int ObRoutineMgr::get_standalone_function_schemas(uint64_t database_id, const ObString &name,
    ObIArray<const ObSimpleRoutineSchema *> &schemas) const
{
  int ret = OB_SUCCESS;
  schemas.reset();
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else if (OB_INVALID_ID == database_id || name.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!name_index_valid_) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    ObRoutineNameHashWrapper first(database_id, OB_INVALID_ID, name, 0, ROUTINE_FUNCTION_TYPE);
    for (ConstRoutineIter it = routine_name_infos_.lower_bound(first, compare_name_key);
         OB_SUCC(ret) && it != routine_name_infos_.end(); ++it) {
      const ObSimpleRoutineSchema *routine = *it;
      if (OB_ISNULL(routine)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (routine->get_database_id() != database_id || routine->get_package_id() != OB_INVALID_ID
                 || routine->get_routine_type() != ROUTINE_FUNCTION_TYPE
                 || ObSchemaNameComparator().compare(routine->get_routine_name(), name) != 0) {
        break;
      } else if (OB_FAIL(schemas.push_back(routine))) {
      }
    }
  }
  if (OB_FAIL(ret)) schemas.reset();
  return ret;
}

bool ObRoutineMgr::compare_with_routine_id(const ObSimpleRoutineSchema *lhs,
                                                 const ObRoutineId &routine_id)
{
  return NULL != lhs ? (lhs->get_routine_key() < routine_id) : false;
}

bool ObRoutineMgr::equal_with_routine_id(const ObSimpleRoutineSchema *lhs,
                                                    const ObRoutineId &routine_id)
{
  return NULL != lhs ? (lhs->get_routine_key() == routine_id) : false;
}

int ObRoutineMgr::add_routines(const ObIArray<ObSimpleRoutineSchema> &routine_schemas)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else {
    FOREACH_CNT_X(routine_schema, routine_schemas, OB_SUCC(ret)) {
      if (OB_FAIL(add_routine(*routine_schema))) {
      }
    }
  }

  return ret;
}


int ObRoutineMgr::add_routine(const ObSimpleRoutineSchema &routine_schema)
{
  int ret = OB_SUCCESS;

  ObSimpleRoutineSchema *new_routine_schema = NULL;
  RoutineIter iter = NULL;
  ObSimpleRoutineSchema *replaced_routine = NULL;
  const ObSimpleRoutineSchema *name_collision = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else if (!routine_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!name_index_valid_ && OB_FAIL(rebuild_routine_hashmap())) {
  } else if (OB_FAIL(get_routine_schema(routine_schema.get_database_id(), routine_schema.get_package_id(),
      routine_schema.get_routine_name(), routine_schema.get_overload(), routine_schema.get_routine_type(),
      name_collision))) {
  } else if (name_collision != NULL && name_collision->get_routine_id() != routine_schema.get_routine_id()) {
    // A new ID cannot silently replace an existing name/slot in only one index.
    ret = OB_STATE_NOT_MATCH;
  } else if (routine_name_infos_.capacity() < routine_infos_.count() + 1
             && OB_FAIL(routine_name_infos_.reserve(MAX(16L, 2 * (routine_infos_.count() + 1))))) {
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_,
                                                 routine_schema,
                                                 new_routine_schema))) {
  } else if (OB_ISNULL(new_routine_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(routine_infos_.replace(new_routine_schema,
                                            iter,
                                            compare_routine,
                                            equal_routine,
                                            replaced_routine))) {
  } else {
    name_index_valid_ = false;
    if (NULL != replaced_routine) {
      ret = routine_name_infos_.remove_if(replaced_routine, compare_names, equal_names);
      if (OB_SUCC(ret)) {
        const auto old_key = ObGetRoutineKey<ObRoutineNameHashWrapper, ObSimpleRoutineSchema *>()(replaced_routine);
        ret = routine_name_map_.erase_refactored(old_key);
      }
    }
    if (OB_SUCC(ret)) {
      ret = routine_name_infos_.insert_unique(new_routine_schema, iter, compare_names, equal_names);
    }
    int over_write = 1;
    int hash_ret = routine_id_map_.set_refactored(new_routine_schema->get_routine_id(),
                                                  new_routine_schema, over_write);
    if (OB_SUCCESS != hash_ret && OB_HASH_EXIST != hash_ret) {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCC(ret)) {
      ObRoutineNameHashWrapper name_wrapper(new_routine_schema->get_database_id(),
                                            new_routine_schema->get_package_id(),
                                            new_routine_schema->get_routine_name(),
                                            new_routine_schema->get_overload(),
                                            new_routine_schema->get_routine_type());
      hash_ret = routine_name_map_.set_refactored(name_wrapper, new_routine_schema, over_write);
      if (OB_SUCCESS != hash_ret && OB_HASH_EXIST != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    name_index_valid_ = OB_SUCC(ret);
  }
  // ignore ret
  if (!name_index_valid_ || routine_infos_.count() != routine_name_infos_.count() ||
      routine_infos_.count() != routine_id_map_.item_count() ||
      routine_infos_.count() != routine_name_map_.item_count()) {
    LOG_WARN("routine info is non-consistent",
             "routine_infos_count", routine_infos_.count(),
             "routine_id_map_item_count", routine_id_map_.item_count(),
             "routine_name_map_item_count", routine_name_map_.item_count(),
             "routine_id", routine_schema.get_routine_id(),
             "routine_name", routine_schema.get_routine_name());
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = rebuild_routine_hashmap())) {
      if (OB_SUCC(ret)) ret = tmp_ret;
    }
  }
  return ret;
}

int ObRoutineMgr::check_user_reffered_by_definer(const ObString &user_name, bool &ref) const
{
  int ret = OB_SUCCESS;
  ref = false;
  for (ConstRoutineIter iter = routine_infos_.begin(); OB_SUCC(ret) && !ref && iter != routine_infos_.end(); iter++) {
    const ObSimpleRoutineSchema *routine = NULL;
    if (OB_ISNULL(routine = *iter)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (0 == user_name.compare(routine->get_priv_user())) {
      ref = true;
    } 
  }
  return ret;
}

int ObRoutineMgr::del_routine(const ObRoutineId &routine_id)
{
  int ret = OB_SUCCESS;

  ObSimpleRoutineSchema *schema_to_del = NULL;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else if (!routine_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!name_index_valid_ && OB_FAIL(rebuild_routine_hashmap())) {
  } else if (OB_FAIL(routine_infos_.remove_if(routine_id, compare_with_routine_id,
                                              equal_with_routine_id,
                                              schema_to_del))) {
  } else if (OB_ISNULL(schema_to_del)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    name_index_valid_ = false;
    ret = routine_name_infos_.remove_if(schema_to_del, compare_names, equal_names);
    int hash_ret = routine_id_map_.erase_refactored(schema_to_del->get_routine_id());
    if (OB_SUCCESS != hash_ret) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed delete routine from procedure id hashmap, ",
               "hash_ret", hash_ret, "routine_id", schema_to_del->get_routine_id());
    }
    if (OB_SUCC(ret)) {
      ObRoutineNameHashWrapper name_wrapper(schema_to_del->get_database_id(),
                                              schema_to_del->get_package_id(),
                                              schema_to_del->get_routine_name(),
                                              schema_to_del->get_overload(),
                                              schema_to_del->get_routine_type());
      hash_ret = routine_name_map_.erase_refactored(name_wrapper);
      if (OB_SUCCESS != hash_ret) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    name_index_valid_ = OB_SUCC(ret);
  }
  // ignore ret
  if (!name_index_valid_ || routine_infos_.count() != routine_name_infos_.count() ||
      routine_infos_.count() != routine_id_map_.item_count() ||
      routine_infos_.count() != routine_name_map_.item_count()) {
    LOG_WARN("routine info is non-consistent",
             "routine_infos_count", routine_infos_.count(),
             "routine_id_map_item_count", routine_id_map_.item_count(),
             "routine_name_map_item_count", routine_name_map_.item_count(),
             "routine_id", routine_id.get_routine_id());
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = rebuild_routine_hashmap())){
      if (OB_SUCC(ret)) ret = tmp_ret;
    }
  }

  return ret;
}

int ObRoutineMgr::get_routine_schema(uint64_t routine_id, const ObSimpleRoutineSchema *&routine_schema) const
{
  int ret = OB_SUCCESS;
  routine_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else if (OB_INVALID_ID == routine_id) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObSimpleRoutineSchema *tmp_schema = NULL;
    int hash_ret = routine_id_map_.get_refactored(routine_id, tmp_schema);
    if (OB_SUCCESS == hash_ret) {
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        routine_schema = tmp_schema;
      }
    }
  }

  return ret;
}

int ObRoutineMgr::get_routine_schema( uint64_t database_id, uint64_t package_id,
    const common::ObString &routine_name, uint64_t overload,
    ObRoutineType routine_type, const ObSimpleRoutineSchema *&routine_schema) const
{
  int ret = OB_SUCCESS;
  routine_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else if (OB_INVALID_ID == database_id || routine_name.empty()
             || OB_INVALID_INDEX == overload || INVALID_ROUTINE_TYPE == routine_type) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObSimpleRoutineSchema *tmp_schema = NULL;
    ObRoutineNameHashWrapper name_wrapper(database_id, package_id, routine_name, overload, routine_type);
    int hash_ret = routine_name_map_.get_refactored(name_wrapper, tmp_schema);
    if (OB_SUCCESS == hash_ret) {
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        routine_schema = tmp_schema;
      }
    }
  }

  return ret;
}

int ObRoutineMgr::get_routine_schemas_in_runtime(ObIArray<const ObSimpleRoutineSchema *> &routine_schemas) const
{
  int ret = OB_SUCCESS;
  routine_schemas.reset();

  ObRoutineId routine_id_lower(OB_MIN_ID);
  ConstRoutineIter routine_begin =
      routine_infos_.lower_bound(routine_id_lower, compare_with_routine_id);
  for (ConstRoutineIter iter = routine_begin; OB_SUCC(ret) && iter != routine_infos_.end(); ++iter) {
    const ObSimpleRoutineSchema *routine = NULL;
    if (OB_ISNULL(routine = *iter)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(routine_schemas.push_back(routine))) {
    }
  }

  return ret;
}

int ObRoutineMgr::get_routine_schemas_in_database(uint64_t database_id,
                                                  ObIArray<const ObSimpleRoutineSchema *> &routine_schemas) const
{
  int ret = OB_SUCCESS;
  routine_schemas.reset();

  ObRoutineId routine_id_lower(OB_MIN_ID);
  ConstRoutineIter routine_begin =
      routine_infos_.lower_bound(routine_id_lower, compare_with_routine_id);
  for (ConstRoutineIter iter = routine_begin;
      OB_SUCC(ret) && iter != routine_infos_.end(); ++iter) {
    const ObSimpleRoutineSchema *routine = NULL;
    if (OB_ISNULL(routine = *iter)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (routine->get_database_id() != database_id) {
      // do-nothing
    } else if (OB_FAIL(routine_schemas.push_back(routine))) {
    }
  }

  return ret;
}

int ObRoutineMgr::get_routine_schemas_in_package(uint64_t package_id,
                                                 ObIArray<const ObSimpleRoutineSchema *> &routine_schemas) const
{
  int ret = OB_SUCCESS;
  routine_schemas.reset();

  ObRoutineId routine_id_lower(OB_MIN_ID);
  ConstRoutineIter routine_begin =
      routine_infos_.lower_bound(routine_id_lower, compare_with_routine_id);
  for (ConstRoutineIter iter = routine_begin;
      OB_SUCC(ret) && iter != routine_infos_.end(); ++iter) {
    const ObSimpleRoutineSchema *routine = NULL;
    if (OB_ISNULL(routine = *iter)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (routine->get_package_id() != package_id
               || routine->get_routine_type() != ROUTINE_PACKAGE_TYPE) {
      // do nothing
    } else if (OB_FAIL(routine_schemas.push_back(routine))) {
    }
  }

  return ret;
}



int ObRoutineMgr::rebuild_routine_hashmap()
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else {
    name_index_valid_ = false;
    routine_name_infos_.reset();
    routine_id_map_.clear();
    routine_name_map_.clear();
    for (ConstRoutineIter iter = routine_infos_.begin();
        iter != routine_infos_.end() && OB_SUCC(ret); ++iter) {
      ObSimpleRoutineSchema *routine_schema = *iter;
      if (OB_ISNULL(routine_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(routine_name_infos_.push_back(routine_schema))) {
      } else {
        int over_write = 1;
        int hash_ret = routine_id_map_.set_refactored(routine_schema->get_routine_id(),
                                                      routine_schema, over_write);
        if (OB_SUCCESS != hash_ret) {
          ret = OB_ERR_UNEXPECTED;
        }
        if (OB_SUCC(ret)) {
          ObRoutineNameHashWrapper name_wrapper(routine_schema->get_database_id(),
                                                routine_schema->get_package_id(),
                                                routine_schema->get_routine_name(),
                                                routine_schema->get_overload(),
                                                routine_schema->get_routine_type());
          hash_ret = routine_name_map_.set_refactored(name_wrapper, routine_schema,
                                                      over_write);
          if (OB_SUCCESS != hash_ret) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      routine_name_infos_.sort(compare_names);
      for (int64_t i = 1; OB_SUCC(ret) && i < routine_name_infos_.count(); ++i) {
        if (equal_names(routine_name_infos_.at(i - 1), routine_name_infos_.at(i))) {
          ret = OB_STATE_NOT_MATCH;
        }
      }
    }
    name_index_valid_ = OB_SUCC(ret);
  }

  return ret;
}

int ObRoutineMgr::get_routine_schema_count(int64_t &routine_schema_count) const
{
  int ret = OB_SUCCESS;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else {
    routine_schema_count = routine_infos_.size();
  }
  return ret;
}

int ObRoutineMgr::get_schema_statistics(ObSchemaStatisticsInfo &schema_info) const
{
  int ret = OB_SUCCESS;
  schema_info.reset();
  schema_info.schema_type_ = ROUTINE_SCHEMA;
  if (!check_inner_stat()) {
    ret = OB_NOT_INIT;
  } else {
    schema_info.count_ = routine_infos_.size();
    for (ConstRoutineIter it = routine_infos_.begin(); OB_SUCC(ret) && it != routine_infos_.end(); it++) {
      if (OB_ISNULL(*it)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        schema_info.size_ += (*it)->get_convert_size();
      }
    }
  }
  return ret;
}

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
