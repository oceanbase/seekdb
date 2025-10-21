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

#ifndef OCEANBASE_STORAGE_DICT_OB_GEN_DIC_LOADER_H_
#define OCEANBASE_STORAGE_DICT_OB_GEN_DIC_LOADER_H_

#include "lib/lock/ob_tc_rwlock.h"
#include "storage/fts/dict/ob_dic_loader.h"
#include "share/ob_plugin_helper.h"

namespace oceanbase
{
namespace storage
{
class ObGenDicLoader
{
public:
  class ObGenDicLoaderKey final
  {
  public:
    ObGenDicLoaderKey() : tenant_id_(OB_INVALID_ID), charset_(CHARSET_INVALID) 
    {
      MEMSET(parser_name_, '\0', share::OB_PLUGIN_NAME_LENGTH);
    }
    ~ObGenDicLoaderKey() = default;
    int init(const uint64_t tenant_id, const ObString &parser_name, const ObCharsetType charset);
    int assign(const ObGenDicLoaderKey &other);
    bool operator==(const ObGenDicLoaderKey &other) const
    {
      return tenant_id_ == other.tenant_id_ 
             && 0 == STRCMP(parser_name_, other.parser_name_)
             && charset_ == other.charset_;
    }
    bool is_valid() const
    {
      return is_valid_tenant_id(tenant_id_) && 0 != STRLEN(parser_name_) && CHARSET_INVALID != charset_;
    }
    int hash(uint64_t &hash_val) const;
    OB_INLINE uint64_t get_tenant_id() const { return tenant_id_; }
    OB_INLINE const char *get_parser_name() const { return parser_name_; }
    OB_INLINE ObCharsetType get_charset() const { return charset_; }
    TO_STRING_KV(K_(tenant_id), KCSTRING_(parser_name), K_(charset));

  private:
    uint64_t hash() const;
    int set_parser_name(const char *parser_name);
    int set_parser_name(const ObString &parser_name);

  private:
    uint64_t tenant_id_;
    char parser_name_[share::OB_PLUGIN_NAME_LENGTH];
    ObCharsetType charset_;
  };

  class ObNeedDeleteDicLoadersFn final
  {
  public:
    ObNeedDeleteDicLoadersFn() = default;
    ~ObNeedDeleteDicLoadersFn() = default;
    int operator() (hash::HashMapPair<ObGenDicLoaderKey, ObTenantDicLoader*> &entry);
    
  public:
    ObArray<ObGenDicLoaderKey> need_delete_loaders_;
  };

public:
  static ObGenDicLoader& get_instance()
  {
    static ObGenDicLoader ins;
    return ins;
  }
  int init();
  int get_dic_loader(const uint64_t tenant_id, 
                     const ObString &parser_name, 
                     const ObCharsetType charset, 
                     ObTenantDicLoaderHandle &loader_handle);
  int destroy_dic_loader_for_tenant();

private:
  ObGenDicLoader() 
      : is_inited_(false), lock_(), dic_loader_map_() { }
  ~ObGenDicLoader() { dic_loader_map_.destroy(); }
  int gen_dic_loader(const ObGenDicLoaderKey &dic_loader_key, 
                     ObTenantDicLoader *&dic_loader);

private:
  bool is_inited_;
  common::TCRWLock lock_;
  hash::ObHashMap<ObGenDicLoaderKey, ObTenantDicLoader*> dic_loader_map_;
  DISALLOW_COPY_AND_ASSIGN(ObGenDicLoader);
};
} //end storage
} // end oceanbase
#endif //OCEANBASE_STORAGE_DICT_OB_GEN_DIC_LOADER_H_
