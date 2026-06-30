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

#include "storage/fts/dict/ob_gen_dic_loader.h"

#include "rootserver/ob_root_service.h"
#include "share/ob_server_struct.h"
#include "storage/fts/dict/ob_ik_utf8_dic_loader.h"
#include "storage/fts/ob_fts_literal.h"
#define USING_LOG_PREFIX STORAGE_FTS

namespace oceanbase
{
namespace storage
{
/**
 * -----------------------------------ObDicLoaderID-----------------------------------
 */
int ObGenDicLoader::ObGenDicLoaderKey::init(
    const ObString &parser_name, 
    const ObCharsetType charset)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || parser_name.empty() || CHARSET_INVALID == charset)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(parser_name), K(charset));
  } else if (OB_FAIL(set_parser_name(parser_name))) {
  } else {
    charset_ = charset;
  }
  return ret;
}
int ObGenDicLoader::ObGenDicLoaderKey::assign(const ObGenDicLoaderKey &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the other is invalid", K(ret), K(other));
  } else if (OB_FAIL(set_parser_name(other.parser_name_))) {
  } else {
    charset_ = other.charset_;
  }
  return ret;
}
int ObGenDicLoader::ObGenDicLoaderKey::hash(uint64_t &hash_val) const
{
  hash_val = hash();
  return OB_SUCCESS;
}

uint64_t ObGenDicLoader::ObGenDicLoaderKey::hash() const
{
  uint64_t hash_val = 0;
  hash_val = murmurhash(&parser_name_, sizeof(parser_name_), 0);
  hash_val = murmurhash(&charset_, sizeof(charset_), hash_val);
  return hash_val;
}

int ObGenDicLoader::ObGenDicLoaderKey::set_parser_name(const char *parser_name)
{
  int ret = OB_SUCCESS;
  uint64_t len = STRLEN(parser_name);
  if (OB_ISNULL(parser_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("The parser name is nullptr", K(ret), KP(parser_name));
  } else if (OB_UNLIKELY(len >= OB_PLUGIN_NAME_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("The parser name is too long", K(ret), KCSTRING(parser_name));
  } else {
    MEMSET(parser_name_, '\0', OB_PLUGIN_NAME_LENGTH);
    MEMCPY(parser_name_, parser_name, len);
  }
  return ret;
}

int ObGenDicLoader::ObGenDicLoaderKey::set_parser_name(const ObString &parser_name)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(parser_name.empty() || (parser_name.length() >= OB_PLUGIN_NAME_LENGTH))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parser name is not valid", K(ret), K(parser_name));
  } else {
    MEMSET(parser_name_, '\0', OB_PLUGIN_NAME_LENGTH);
    MEMCPY(parser_name_, parser_name.ptr(), parser_name.length());
  }
  return ret;
}

int ObGenDicLoader::ObNeedDeleteDicLoadersFn::operator() (hash::HashMapPair<ObGenDicLoaderKey, ObTenantDicLoader*> &entry)
{
  int ret = OB_SUCCESS;
  const ObGenDicLoaderKey &dic_loader_key = entry.first;
  if (OB_UNLIKELY(!dic_loader_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid dic loader key", K(ret), K(dic_loader_key));
  } else {
    ObSchemaGetterGuard schema_guard;
    if (OB_ISNULL(GCTX.root_service_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("root service is null", K(ret));
    } else if (OB_FAIL(GCTX.root_service_->get_schema_service().get_tenant_schema_guard(schema_guard))) {
    } else {
      // single tenant: only sys tenant exists, so is_delete is always false (no orphan loader to delete)
    }
  }
  return ret;
}

/**
 * -----------------------------------ObDicLoader-----------------------------------
 */
int ObGenDicLoader::init()
{
  int ret = OB_SUCCESS;
  const uint64_t cap = 16;
  TCWLockGuard guard(lock_);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("gen dic loader initialize twice", K(ret));
  } else if (!dic_loader_map_.created() 
      && OB_FAIL(dic_loader_map_.create(cap, ObMemAttr("dic_loader_map")))) {
    LOG_WARN("fail to create dic loader map", K(ret), K(cap));
  } else {
    is_inited_ = true;
  }
  
  return ret;
}

int ObGenDicLoader::get_dic_loader(const ObString &parser_name, 
                                   const ObCharsetType charset, 
                                   ObTenantDicLoaderHandle &loader_handle)
{
  int ret = OB_SUCCESS;
  ObGenDicLoaderKey dic_loader_key;
  ObTenantDicLoader *dic_loader = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("gen dic loader is not inited", K(ret));
  } else if (!true 
             || parser_name.empty() 
             || charset == CHARSET_INVALID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(parser_name), K(charset));
  } else if (OB_FAIL(dic_loader_key.init(parser_name, charset))) {
  } else {
    TCWLockGuard guard(lock_);
    if (OB_FAIL(dic_loader_map_.get_refactored(dic_loader_key, dic_loader))) {
      if (OB_HASH_NOT_EXIST == ret) {
        if (OB_FAIL(gen_dic_loader(dic_loader_key, dic_loader))) {
        } else if (OB_ISNULL(dic_loader)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("the dic loader handle is not valid", K(ret), K(dic_loader_key));
        } else if (OB_FAIL(dic_loader_map_.set_refactored(dic_loader_key, dic_loader))) {
        } else if (OB_FALSE_IT(dic_loader->inc_ref())) {
        } else if (OB_FAIL(loader_handle.set_loader(dic_loader))) {
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get dic loader", K(ret), K(dic_loader_key));
      }
    } else if (OB_FAIL(loader_handle.set_loader(dic_loader))) {
    }
  }
  return ret;
}

int ObGenDicLoader::destroy_dic_loader_for_tenant()
{
  int ret = OB_SUCCESS;
  ObNeedDeleteDicLoadersFn need_del_dic_loader_fn;
  TCWLockGuard guard(lock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("gen dic loader is not inited", K(ret));
  } else if (OB_FAIL(dic_loader_map_.foreach_refactored(need_del_dic_loader_fn))) {
  } else {
    const ObIArray<ObGenDicLoaderKey> &need_delete_loaders = need_del_dic_loader_fn.need_delete_loaders_;
    for (int64_t i = 0; i < need_delete_loaders.count(); i++) { // ignore ret to delete other tenant's dic loader
      const ObGenDicLoaderKey &dic_loader_key = need_delete_loaders.at(i);
      ObTenantDicLoader *dic_loader = nullptr;
      // overwrite ret
      if (OB_UNLIKELY(!dic_loader_key.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the dic loader key is not valid", K(ret), K(dic_loader_key));
      } else if (OB_FAIL(dic_loader_map_.get_refactored(dic_loader_key, dic_loader))) {
      } else if (OB_FAIL(dic_loader_map_.erase_refactored(dic_loader_key))) {
      } else if (OB_ISNULL(dic_loader)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the dic loader is null", K(ret), K(dic_loader_key));
      } else if (0 == dic_loader->dec_ref()) {
        ObMemAttr attr("dic_loader");
        OB_DELETE(ObTenantDicLoader, attr, dic_loader);
      }
    }
  }
  return ret;
}

int ObGenDicLoader::gen_dic_loader(
    const ObGenDicLoaderKey &dic_loader_key, 
    ObTenantDicLoader *&dic_loader)
{
  int ret = OB_SUCCESS;
  ObString parser_name = dic_loader_key.get_parser_name();
  ObCharsetType charset = dic_loader_key.get_charset();
  dic_loader = nullptr;
  if (nullptr != parser_name.find('.')) {
    parser_name = parser_name.split_on('.');
  }
  if (0 == parser_name.case_compare(ObFTSLiteral::PARSER_NAME_IK)) {
    ObMemAttr attr("dic_loader");
    switch (charset)
    {
      case ObCharsetType::CHARSET_UTF8MB4: {
        dic_loader = OB_NEW(ObTenantIKUTF8DicLoader, attr);
        if (OB_ISNULL(dic_loader)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to allocate memory for the loader", K(ret), K(dic_loader_key));
        } else if (OB_FAIL(dic_loader->init())) {
        }
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
        ObSqlString message;
        message.append_fmt("%s with the %s charset is",
                           ObFTSLiteral::PARSER_NAME_IK, ObCharset::charset_name(charset));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, message.ptr());
        LOG_WARN("not support the charset", K(ret), K(charset), KCSTRING(lbt()));
        break;
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "the parser is");
    LOG_WARN("not support the parser", K(ret), K(parser_name));
  }
  return ret;
}
} // end storage
} // end oceanbase
