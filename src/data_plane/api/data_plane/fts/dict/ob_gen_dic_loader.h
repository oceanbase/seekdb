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
#include "data_plane/fts/dict/ob_dic_loader.h"
#include "data_plane/fts/ob_fts_parser_name.h"

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
    ObGenDicLoaderKey() : charset_(CHARSET_INVALID)
    {
      MEMSET(parser_name_, '\0', OB_FT_PARSER_NAME_LENGTH);
    }
    ~ObGenDicLoaderKey() = default;
    int init(const ObString &parser_name, const ObCharsetType charset);
    int assign(const ObGenDicLoaderKey &other);
    bool operator==(const ObGenDicLoaderKey &other) const
    {
      return true
             && 0 == STRCMP(parser_name_, other.parser_name_)
             && charset_ == other.charset_;
    }
    bool is_valid() const
    {
      return true && 0 != STRLEN(parser_name_) && CHARSET_INVALID != charset_;
    }
    int hash(uint64_t &hash_val) const;

    OB_INLINE const char *get_parser_name() const { return parser_name_; }
    OB_INLINE ObCharsetType get_charset() const { return charset_; }
    TO_STRING_KV(KCSTRING_(parser_name), K_(charset));

  private:
    uint64_t hash() const;
    int set_parser_name(const char *parser_name);
    int set_parser_name(const ObString &parser_name);

  private:
    char parser_name_[OB_FT_PARSER_NAME_LENGTH];
    ObCharsetType charset_;
  };

  static ObGenDicLoader& get_instance()
  {
    static ObGenDicLoader ins;
    return ins;
  }
  int init();
  int get_dic_loader(const ObString &parser_name,
                     const ObCharsetType charset,
                     ObDicLoaderHandle &loader_handle);

private:
  ObGenDicLoader()
      : is_inited_(false), lock_(), dic_loader_map_() { }
  ~ObGenDicLoader() { dic_loader_map_.destroy(); }
  int gen_dic_loader(const ObGenDicLoaderKey &dic_loader_key,
                     ObDicLoader *&dic_loader);

private:
  bool is_inited_;
  common::TCRWLock lock_;
  hash::ObHashMap<ObGenDicLoaderKey, ObDicLoader*> dic_loader_map_;
  DISALLOW_COPY_AND_ASSIGN(ObGenDicLoader);
};
} //end storage
} // end oceanbase
#endif //OCEANBASE_STORAGE_DICT_OB_GEN_DIC_LOADER_H_
