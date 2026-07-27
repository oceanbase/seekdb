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
#include "ob_sys_variable_mgr.h"
#include "share/schema/ob_schema_utils.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
using namespace std;
using namespace common;
using namespace common::hash;

ObSimpleSysVariableSchema::ObSimpleSysVariableSchema()
  : ObSchema(), schema_version_(common::OB_INVALID_VERSION),
    name_case_mode_(OB_NAME_CASE_INVALID), read_only_(false)
{
  reset();
}

ObSimpleSysVariableSchema::ObSimpleSysVariableSchema(ObIAllocator *allocator)
  : ObSchema(allocator), schema_version_(common::OB_INVALID_VERSION),
    name_case_mode_(OB_NAME_CASE_INVALID), read_only_(false)
{
  reset();
}

ObSimpleSysVariableSchema::ObSimpleSysVariableSchema(const ObSimpleSysVariableSchema &other)
  : ObSchema(), schema_version_(common::OB_INVALID_VERSION)
{
  reset();
  *this = other;
}

ObSimpleSysVariableSchema::~ObSimpleSysVariableSchema()
{
}

void ObSimpleSysVariableSchema::reset()
{
  ObSchema::reset();
  
  schema_version_ = OB_INVALID_VERSION;
  name_case_mode_ = OB_NAME_CASE_INVALID;
  read_only_ = false;
}

bool ObSimpleSysVariableSchema::is_valid() const
{
  bool ret = true;
  if (schema_version_ < 0 ||
      OB_NAME_CASE_INVALID == name_case_mode_) {
    ret = false;
  }
  return ret;
}

int64_t ObSimpleSysVariableSchema::get_convert_size() const
{
  int64_t convert_size = 0;
  convert_size += sizeof(ObSimpleSysVariableSchema);
  return convert_size;
}

ObSimpleSysVariableSchema &ObSimpleSysVariableSchema::operator =(const ObSimpleSysVariableSchema &other)
{
  if (this != &other) {
    reset();
    error_ret_ = other.error_ret_;
    
    schema_version_ = other.schema_version_;
    name_case_mode_ = other.name_case_mode_;
    read_only_ = other.read_only_;
  }
  return *this;
}

ObSysVariableMgr::ObSysVariableMgr() :
    is_inited_(false),
    local_allocator_(lib::ObMemAttr(ObModIds::OB_SCHEMA_GETTER_GUARD, ObCtxIds::SCHEMA_SERVICE)),
    allocator_(local_allocator_),
    sys_variable_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_SYS_VARIABLE, ObCtxIds::SCHEMA_SERVICE)),
    sys_variable_map_(lib::ObMemAttr(ObModIds::OB_SCHEMA_SYS_VARIABLE, ObCtxIds::SCHEMA_SERVICE))
  {
  }

ObSysVariableMgr::ObSysVariableMgr(common::ObIAllocator &allocator) :
    is_inited_(false),
    local_allocator_(lib::ObMemAttr(ObModIds::OB_SCHEMA_GETTER_GUARD, ObCtxIds::SCHEMA_SERVICE)),
    allocator_(allocator),
    sys_variable_infos_(0, NULL, lib::ObMemAttr(ObModIds::OB_SCHEMA_SYS_VARIABLE, ObCtxIds::SCHEMA_SERVICE)),
    sys_variable_map_(ObMemAttr(ObModIds::OB_SCHEMA_SYS_VARIABLE, ObCtxIds::SCHEMA_SERVICE))
{
}

ObSysVariableMgr::~ObSysVariableMgr()
{
}

int ObSysVariableMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init private sys_variable manager twice", K(ret));
  } else if (OB_FAIL(sys_variable_map_.init())) {
    LOG_WARN("init private sys_variable map failed", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObSysVariableMgr::reset()
{
  if (!is_inited_) {
    LOG_WARN_RET(OB_NOT_INIT, "sys_variable manger not init");
  } else {
    sys_variable_infos_.clear();
    sys_variable_map_.clear();
  }
}


int ObSysVariableMgr::assign(const ObSysVariableMgr &other)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys_variable manager not init", K(ret));
  } else if (this != &other) {
    if (OB_FAIL(sys_variable_map_.assign(other.sys_variable_map_))) {
      LOG_WARN("assign sys_variable map failed", K(ret));
    } else if (OB_FAIL(sys_variable_infos_.assign(other.sys_variable_infos_))) {
      LOG_WARN("assign sys_variable infos vector failed", K(ret));
    }
  }
  return ret;
}

int ObSysVariableMgr::deep_copy(const ObSysVariableMgr &other)
{
  int ret = OB_SUCCESS;
  UNUSED(other);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys_variable manager not init", K(ret));
  } else if (this != &other) {
    reset();
    for (SysVariableIter iter = other.sys_variable_infos_.begin();
       OB_SUCC(ret) && iter != other.sys_variable_infos_.end(); iter++) {
      ObSimpleSysVariableSchema *sys_variable_info = *iter;
      if (OB_ISNULL(sys_variable_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(sys_variable_info), K(ret));
      } else if (OB_FAIL(add_sys_variable(*sys_variable_info))) {
        LOG_WARN("add sys variable failed", K(*sys_variable_info), K(ret));
      }
    }
  }
  return ret;
}

int ObSysVariableMgr::get_sys_variable_schema(
    const ObSimpleSysVariableSchema *&sys_variable_schema) const
{
  int ret = OB_SUCCESS;
  sys_variable_schema = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSimpleSysVariableSchema *tmp_schema = NULL;
    ObSysVariableHashWrapper hash_wrap;
    if (OB_FAIL(sys_variable_map_.get_refactored(hash_wrap, tmp_schema))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        LOG_DEBUG("sys_variable is not exist");
      }
    } else {
      sys_variable_schema = tmp_schema;
    }
  }
  return ret;
}

int ObSysVariableMgr::add_sys_variable(const ObSimpleSysVariableSchema &sys_variable_schema)
{
  int ret = OB_SUCCESS;
  int overwrite = 1;
  ObSimpleSysVariableSchema *new_sys_variable_schema = NULL;
  SysVariableIter iter = NULL;
  ObSimpleSysVariableSchema *replaced_sys_variable = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys_variable manager not init", K(ret));
  } else if (OB_UNLIKELY(!sys_variable_schema.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(sys_variable_schema));
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_,
                                                 sys_variable_schema,
                                                 new_sys_variable_schema))) {
    LOG_WARN("alloca sys_variable schema failed", K(ret));
  } else if (OB_ISNULL(new_sys_variable_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(new_sys_variable_schema), K(ret));
  } else if (OB_FAIL(sys_variable_infos_.replace(new_sys_variable_schema,
                                        iter,
                                        compare_sys_variable,
                                        equal_sys_variable,
                                        replaced_sys_variable))) {
      LOG_WARN("failed to add sys_variable schema", K(ret));
  } else {
    ObSysVariableHashWrapper hash_wrapper;
    if (OB_FAIL(sys_variable_map_.set_refactored(hash_wrapper, new_sys_variable_schema, overwrite))) {
      LOG_WARN("build sys_variable hash map failed", K(ret));
    }
  }
  if (sys_variable_infos_.count() != sys_variable_map_.item_count()) {
    LOG_WARN("sys_variable info is non-consistent",
             "sys_variable infos count", sys_variable_infos_.count(),
             "sys_variable map item count", sys_variable_map_.item_count());
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = ObSysVariableMgr::rebuild_sys_variable_hashmap(sys_variable_infos_, sys_variable_map_))) {
      LOG_WARN("rebuild sys_variable hashmap failed", K(tmp_ret));
    }
  }
  if (OB_SUCC(ret)) { //for debug
    const ObSimpleSysVariableSchema *tmp_schema = NULL;
    if (OB_FAIL(get_sys_variable_schema( tmp_schema))) {
      LOG_WARN("fail to get sys variable schema", K(ret), K(sys_variable_schema));
    } else if (OB_ISNULL(tmp_schema)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      LOG_INFO("sys variable schema", K(*tmp_schema));
    }
  }
  return ret;
}

int ObSysVariableMgr::rebuild_sys_variable_hashmap(const SysVariableInfos &sys_variable_infos, ObSysVariableMap& sys_variable_map)
{
  int ret = OB_SUCCESS;
  sys_variable_map.clear();
  ConstSysVariableIter iter = sys_variable_infos.begin();
  for (; iter != sys_variable_infos.end() && OB_SUCC(ret); ++iter) {
    ObSimpleSysVariableSchema *sys_variable_schema = *iter;
    if (OB_ISNULL(sys_variable_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sys_variable schema is NULL", K(sys_variable_schema), K(ret));
    } else {
      bool overwrite = true;
      ObSysVariableHashWrapper hash_wrapper;
      if (OB_FAIL(sys_variable_map.set_refactored(hash_wrapper, sys_variable_schema, overwrite))) {
        LOG_WARN("build sys_variable hash map failed", K(ret));
      }
    }
  }
  return ret;
}

int ObSysVariableMgr::add_sys_variables(const common::ObIArray<ObSimpleSysVariableSchema> &sys_variable_schemas)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < sys_variable_schemas.count() && OB_SUCC(ret); ++i) {
    if (OB_FAIL(add_sys_variable(sys_variable_schemas.at(i)))) {
      LOG_WARN("push sys_variable failed", K(ret));
    }
  }
  return ret;
}

int ObSysVariableMgr::del_sys_variable()
{
  int ret = OB_SUCCESS;
  int hash_ret = OB_SUCCESS;
  ObSimpleSysVariableSchema *schema_to_del = NULL;
  const ObSimpleSysVariableSchema *tmp_schema = NULL;
  if (OB_FAIL(get_sys_variable_schema( tmp_schema))) {
    LOG_WARN("fail to get sys variable schema", K(ret));
  } else if (OB_ISNULL(tmp_schema)) {
    // sys variable schema is null, no need to del
  } else if (FALSE_IT(schema_to_del = sys_variable_infos_.begin() == sys_variable_infos_.end() ? NULL : *sys_variable_infos_.begin())) {
  } else if (OB_NOT_NULL(schema_to_del) && OB_FAIL(sys_variable_infos_.remove(sys_variable_infos_.begin()))) {
    LOG_ERROR("failed to remove sys_variable schema, ", K(ret));
  } else if (OB_ISNULL(schema_to_del)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("removed sys_variable schema return NULL, ", K(ret));
  } else {
    ObSysVariableHashWrapper sys_variable_wrapper;
    hash_ret = sys_variable_map_.erase_refactored(sys_variable_wrapper);
    if (OB_SUCCESS != hash_ret) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed delete sys_variable from sys_variable hashmap, ",
               K(ret), K(hash_ret));
    }
  }
  if (sys_variable_infos_.count() != sys_variable_map_.item_count()) {
    LOG_WARN("sys_variable info is non-consistent",
             "sys_variable infos count", sys_variable_infos_.count(),
             "sys_variable map item count", sys_variable_map_.item_count());
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = ObSysVariableMgr::rebuild_sys_variable_hashmap(sys_variable_infos_, sys_variable_map_))) {
      LOG_WARN("rebuild sys_variable hashmap failed", K(tmp_ret));
    }
  }
  return ret;
}



int ObSysVariableMgr::get_sys_variable_schema_count(int64_t &sys_variable_schema_count) const
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("sys_variable manager not init", K(ret));
  } else {
    sys_variable_schema_count = sys_variable_infos_.size();
  }
  return ret;
}

int ObSysVariableMgr::get_schema_statistics(ObSchemaStatisticsInfo &schema_info) const
{
  int ret = OB_SUCCESS;
  schema_info.reset();
  schema_info.schema_type_ = SYS_VARIABLE_SCHEMA;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    schema_info.count_ = sys_variable_infos_.size();
    for (ConstSysVariableIter it = sys_variable_infos_.begin(); OB_SUCC(ret) && it != sys_variable_infos_.end(); it++) {
      if (OB_ISNULL(*it)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema is null", K(ret));
      } else {
        schema_info.size_ += (*it)->get_convert_size();
      }
    }
  }
  return ret;
}

}// end schema
}// end share
}// end oceanbase
