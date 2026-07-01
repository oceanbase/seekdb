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

#include "share/schema/ob_catalog_mgr.h"
#include "lib/allocator/ob_mod_define.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_module.h"
#include "share/schema/ob_schema_utils.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
using namespace std;
using namespace common;
using namespace hash;

ObCatalogMgr::ObCatalogMgr()
    : is_inited_(false),
      local_allocator_(SET_USE_500(ObModIds::OB_SCHEMA_GETTER_GUARD, ObCtxIds::SCHEMA_SERVICE)),
      allocator_(local_allocator_),
      schema_infos_(0, NULL, SET_USE_500("SchemaCatalog", ObCtxIds::SCHEMA_SERVICE)),
      name_map_(SET_USE_500("SchemaCatalog", ObCtxIds::SCHEMA_SERVICE)),
      id_map_(SET_USE_500("SchemaCatalog", ObCtxIds::SCHEMA_SERVICE))
{
}

ObCatalogMgr::ObCatalogMgr(ObIAllocator &allocator)
    : is_inited_(false),
      local_allocator_(SET_USE_500(ObModIds::OB_SCHEMA_GETTER_GUARD, ObCtxIds::SCHEMA_SERVICE)),
      allocator_(allocator),
      schema_infos_(0, NULL, SET_USE_500("SchemaCatalog", ObCtxIds::SCHEMA_SERVICE)),
      name_map_(SET_USE_500("SchemaCatalog", ObCtxIds::SCHEMA_SERVICE)),
      id_map_(SET_USE_500("SchemaCatalog", ObCtxIds::SCHEMA_SERVICE))
{
}

ObCatalogMgr::~ObCatalogMgr()
{
}

int ObCatalogMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init private catalog schema manager twice", K(ret));
  } else if (OB_FAIL(name_map_.init())) {
  } else if (OB_FAIL(id_map_.init())) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObCatalogMgr::reset()
{
  if (!is_inited_) {
    LOG_WARN_RET(OB_NOT_INIT, "catalog manger not init");
  } else {
    schema_infos_.clear();
    name_map_.clear();
    id_map_.clear();
  }
}


int ObCatalogMgr::assign(const ObCatalogMgr &other)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema manager not init", K(ret));
  } else if (this != &other) {
    if (OB_FAIL(name_map_.assign(other.name_map_))) {
    } else if (OB_FAIL(id_map_.assign(other.id_map_))) {
    } else if (OB_FAIL(schema_infos_.assign(other.schema_infos_))) {
    }
  }
  return ret;
}

int ObCatalogMgr::deep_copy(const ObCatalogMgr &other)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema manager not init", K(ret));
  } else if (this != &other) {
    reset();
    ObCatalogSchema *schema = NULL;
    for (CatalogIter iter = other.schema_infos_.begin();
         OB_SUCC(ret) && iter != other.schema_infos_.end(); iter++) {
      if (OB_ISNULL(schema = *iter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", KP(schema), K(ret));
      } else if (OB_FAIL(add_catalog(*schema, schema->get_name_case_mode()))) {
      }
    }
  }
  return ret;
}

int ObCatalogMgr::add_catalog(const ObCatalogSchema &schema,
                              const ObNameCaseMode mode)
{
  int ret = OB_SUCCESS;
  ObCatalogSchema *new_schema = NULL;
  ObCatalogSchema *old_schema = NULL;
  CatalogIter iter = NULL;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the schema mgr is not init", K(ret));
  } else if (OB_UNLIKELY(!schema.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema));
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator_,
                                                 schema,
                                                 new_schema))) {
  } else if (OB_ISNULL(new_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("alloc schema is a NULL ptr", K(new_schema), K(ret));
  } else if (OB_FALSE_IT(new_schema->set_name_case_mode(mode))) {
  } else if (OB_FAIL(schema_infos_.replace(new_schema,
                                           iter,
                                           schema_cmp,
                                           schema_equal,
                                           old_schema))) {
  } else {
    int overwrite = 1;
    ObCatalogNameHashKey catalog_name_hash_key(new_schema->get_name_case_mode(),
                                               new_schema->get_catalog_name_str());
    if (OB_FAIL(name_map_.set_refactored(catalog_name_hash_key, new_schema, overwrite))) {
    } else if (OB_FAIL(id_map_.set_refactored(new_schema->get_catalog_id(), new_schema, overwrite))) {
    }
  }
  if (OB_SUCC(ret) && (schema_infos_.count() != name_map_.item_count()
                       || schema_infos_.count() != id_map_.item_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema is inconsistent with its map", K(ret),
             K(schema_infos_.count()),
             K(name_map_.item_count()),
             K(id_map_.item_count()));
  }
  LOG_DEBUG("catalog add", K(schema), K(schema_infos_.count()),
            K(name_map_.item_count()), K(id_map_.item_count()));
  return ret;
}

int ObCatalogMgr::get_schema_by_name(const ObNameCaseMode mode,
                                     const ObString &name,
                                     const ObCatalogSchema *&schema) const
{
  int ret = OB_SUCCESS;
  schema = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(name));
  } else {
    ObCatalogSchema *tmp_schema = NULL;
    ObCatalogNameHashKey hash_wrap(mode, name);
    if (OB_FAIL(name_map_.get_refactored(hash_wrap, tmp_schema))) {
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("schema is not exist", K(name),
                 "map_cnt", name_map_.item_count());
      }
    } else {
      schema = tmp_schema;
    }
  }
  return ret;
}

int ObCatalogMgr::del_catalog(const ObTenantCatalogId &id)
{
  int ret = OB_SUCCESS;
  ObCatalogSchema *schema = NULL;
  if (!id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(id));
  } else if (OB_FAIL(schema_infos_.remove_if(id,
                                             compare_with_tenant_catalog_id,
                                             equal_to_tenant_catalog_id,
                                             schema))) {
  } else if (OB_ISNULL(schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("removed catalog schema return NULL, ",
             "catalog_id",
             id.schema_id_,
             K(ret));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(id_map_.erase_refactored(schema->get_catalog_id()))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(name_map_.erase_refactored(ObCatalogNameHashKey(schema->get_name_case_mode(),
                                                                schema->get_catalog_name_str())))) {
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(schema_infos_.count() != name_map_.item_count()
                                  || schema_infos_.count() != id_map_.item_count())) {
    LOG_WARN("catalog schema is non-consistent",K(id), K(schema_infos_.count()),
             K(name_map_.item_count()), K(id_map_.item_count()));
  }
  LOG_DEBUG("catalog del", K(id), K(schema_infos_.count()),
            K(name_map_.item_count()), K(id_map_.item_count()));
  return ret;
}

bool ObCatalogMgr::compare_with_tenant_catalog_id(const ObCatalogSchema *lhs,
                                                  const ObTenantCatalogId &id)
{
  return NULL != lhs ? (lhs->get_tenant_catalog_id() < id) : false;
}

bool ObCatalogMgr::equal_to_tenant_catalog_id(const ObCatalogSchema *lhs,
                                              const ObTenantCatalogId &id)
{
  return NULL != lhs ? (lhs->get_tenant_catalog_id() == id) : false;
}

int ObCatalogMgr::get_schemas_in_tenant(ObIArray<const ObCatalogSchema *> &schemas) const
{
  int ret = OB_SUCCESS;
  schemas.reset();
  ObTenantCatalogId id(OB_MIN_ID);
  ConstCatalogIter iter_begin =
      schema_infos_.lower_bound(id, compare_with_tenant_catalog_id);
  bool is_stop = false;
  for (ConstCatalogIter iter = iter_begin;
      OB_SUCC(ret) && iter != schema_infos_.end() && !is_stop; ++iter) {
    const ObCatalogSchema *schema = NULL;
    if (OB_ISNULL(schema = *iter)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", K(ret), K(schema));
    } else if (false) {
      is_stop = true;
    } else if (OB_FAIL(schemas.push_back(schema))) {
    }
  }
  return ret;
}

int ObCatalogMgr::get_schema_by_id(const uint64_t catalog_id, const ObCatalogSchema *&schema) const
{
  int ret = OB_SUCCESS;
  schema = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == catalog_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(catalog_id));
  } else {
    ObCatalogSchema *tmp_schema = NULL;
    int hash_ret = id_map_.get_refactored(catalog_id, tmp_schema);
    if (OB_LIKELY(OB_SUCCESS == hash_ret)) {
      if (OB_ISNULL(tmp_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(ret), K(tmp_schema));
      } else {
        schema = tmp_schema;
      }
    }
  }
  return ret;
}

int ObCatalogMgr::get_schema_count(int64_t &schema_count) const
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    schema_count = schema_infos_.count();
  }
  return ret;
}

int ObCatalogMgr::get_schema_statistics(ObSchemaStatisticsInfo &schema_info) const
{
  int ret = OB_SUCCESS;
  schema_info.reset();
  schema_info.schema_type_ = CATALOG_SCHEMA;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    schema_info.count_ = schema_infos_.size();
    for (ConstCatalogIter it = schema_infos_.begin(); OB_SUCC(ret) && it != schema_infos_.end(); it++) {
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

} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase
