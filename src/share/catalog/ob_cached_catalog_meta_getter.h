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

#ifndef __SHARE_OB_CACHED_CATALOG_META_GETTER_H__
#define __SHARE_OB_CACHED_CATALOG_META_GETTER_H__

#include "share/catalog/ob_catalog_meta_getter.h"
#include "share/catalog/ob_external_catalog.h"

namespace oceanbase
{
namespace share
{

class ObCatalogSchemaCacheKey final : public common::ObIKVCacheKey
{
public:
  ObCatalogSchemaCacheKey() : tenant_id_(OB_INVALID), catalog_id_(OB_INVALID) {}
  ~ObCatalogSchemaCacheKey() override {}
  bool operator==(const common::ObIKVCacheKey &other) const override;
  uint64_t hash() const override;
  uint64_t get_tenant_id() const override { return tenant_id_; }
  int64_t size() const override;
  int deep_copy(char *buf, const int64_t buf_len, common::ObIKVCacheKey *&key) const override;
  TO_STRING_KV(K(tenant_id_), K(catalog_id_), K(namespace_name_), K(table_name_));

public:
  uint64_t tenant_id_;
  uint64_t catalog_id_;
  common::ObString namespace_name_;
  common::ObString table_name_;
};

class ObCachedCatalogSchemaMgr
{
public:
  ObCachedCatalogSchemaMgr() = default;
  int init();
  static ObCachedCatalogSchemaMgr &get_instance();
  int get_table_schema(ObCatalogMetaGetter *meta_getter,
                       const uint64_t tenant_id,
                       const uint64_t catalog_id,
                       const common::ObString &ns_name,
                       const common::ObString &tbl_name,
                       const ObNameCaseMode case_mode,
                       share::schema::ObTableSchema &table_schema);

private:
  DISALLOW_COPY_AND_ASSIGN(ObCachedCatalogSchemaMgr);
  int check_table_schema_cache_valid_(ObCatalogMetaGetter *meta_getter,
                                      const int64_t cached_time,
                                      const uint64_t tenant_id,
                                      const uint64_t catalog_id,
                                      const common::ObString &ns_name,
                                      const common::ObString &tbl_name,
                                      const ObNameCaseMode case_mode,
                                      bool &is_valid);
  static constexpr int64_t LOAD_CACHE_LOCK_CNT = 16;
  static const int64_t LOCK_TIMEOUT = 2 * 1000000L;
  common::ObSpinLock fill_cache_locks_[LOAD_CACHE_LOCK_CNT];
  common::ObKVCache<ObCatalogSchemaCacheKey, schema::ObSchemaCacheValue> schema_cache_;
  // common::ObKVCache<ObCatalogSchemaCacheKey, ObExternalTableFiles> file_cache_;
};
// The logic to determine if the Cache is expired should only be implemented in the ObCachingCatalogMetaGetter layer, do not intrude into the internal Catalog's API
class ObCachedCatalogMetaGetter final : public ObICatalogMetaGetter
{
public:
  ObCachedCatalogMetaGetter(ObSchemaGetterGuard &schema_getter_guard, ObIAllocator &allocator)
      : delegate_(ObCatalogMetaGetter{schema_getter_guard, allocator})
  {
  }

  ~ObCachedCatalogMetaGetter() override {}

  int list_namespace_names(const uint64_t tenant_id, const uint64_t catalog_id, common::ObIArray<common::ObString> &ns_names) override;

  int list_table_names(const uint64_t tenant_id,
                       const uint64_t catalog_id,
                       const common::ObString &ns_name,
                       const ObNameCaseMode case_mode,
                       common::ObIArray<common::ObString> &tbl_names) override;

  int fetch_namespace_schema(const uint64_t tenant_id,
                             const uint64_t catalog_id,
                             const common::ObString &ns_name,
                             const ObNameCaseMode case_mode,
                             share::schema::ObDatabaseSchema &database_schema) override;

  int fetch_table_schema(const uint64_t tenant_id,
                         const uint64_t catalog_id,
                         const common::ObString &ns_name,
                         const common::ObString &tbl_name,
                         const ObNameCaseMode case_mode,
                         share::schema::ObTableSchema &table_schema) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObCachedCatalogMetaGetter);
  ObCatalogMetaGetter delegate_;
};

} // namespace share
} // namespace oceanbase

#endif //__SHARE_OB_CACHED_CATALOG_META_GETTER_H__
