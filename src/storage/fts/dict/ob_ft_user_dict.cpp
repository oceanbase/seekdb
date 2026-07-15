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

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/dict/ob_ft_user_dict.h"

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_smart_var.h"
#include "share/ob_server_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"
#include "storage/fts/dict/ob_ft_dat_dict.h"
#include "storage/fts/dict/ob_ft_trie.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace share::schema;

namespace
{
int append_quoted_identifier(const ObString &identifier, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  if (identifier.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(sql.append("`"))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < identifier.length(); ++i) {
      if ('`' == identifier[i] && OB_FAIL(sql.append("``"))) {
      } else if ('`' != identifier[i] && OB_FAIL(sql.append(identifier.ptr() + i, 1))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(sql.append("`"))) {
    }
  }
  return ret;
}
} // namespace

ObFTUserDict::ObFTUserDict()
    : allocator_(ObMemAttr("FTUserDict")),
      reader_(nullptr),
      ref_count_(1),
      table_id_(OB_INVALID_ID),
      word_count_(0)
{
}

ObFTUserDict::~ObFTUserDict()
{
  if (nullptr != reader_) {
    reader_->~ObFTDATReader<void>();
    reader_ = nullptr;
  }
  allocator_.reset();
}

int ObFTUserDict::build_query(const ObString &database_name,
                              const ObString &table_name,
                              ObSqlString &sql) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql.append("SELECT word FROM "))) {
  } else if (OB_FAIL(append_quoted_identifier(database_name, sql))) {
  } else if (OB_FAIL(sql.append("."))) {
  } else if (OB_FAIL(append_quoted_identifier(table_name, sql))) {
  } else if (OB_FAIL(sql.append(" ORDER BY word"))) {
  }
  return ret;
}

int ObFTUserDict::build(const ObString &database_name,
                        const ObString &table_name,
                        const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  ObArenaAllocator trie_allocator(ObMemAttr("FTUserTrie"));
  ObFTTrie<void> trie(trie_allocator, CS_TYPE_UTF8MB4_BIN);
  ObSqlString sql;
  if (database_name.empty() || table_name.empty() || OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid user dictionary identity", K(ret), K(database_name), K(table_name), K(table_id));
  } else if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else if (OB_FAIL(build_query(database_name, table_name, sql))) {
    LOG_WARN("fail to build dictionary query", K(ret), K(database_name), K(table_name));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, result) {
      sqlclient::ObMySQLResult *mysql_result = nullptr;
      if (OB_FAIL(sql_proxy->read(result, sql.ptr()))) {
        LOG_WARN("fail to read user dictionary", K(ret), K(database_name), K(table_name));
      } else if (OB_ISNULL(mysql_result = result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("dictionary query returned null result", K(ret));
      } else {
        while (OB_SUCC(ret)) {
          ObString word;
          if (OB_FAIL(mysql_result->next())) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
            } else {
              LOG_WARN("fail to read dictionary row", K(ret));
            }
            break;
          } else if (OB_FAIL(mysql_result->get_varchar("word", word))) {
            LOG_WARN("fail to read dictionary word", K(ret));
          } else if (word.empty()) {
            LOG_DEBUG("ignore empty dictionary word", K(table_id));
          } else if (OB_FAIL(trie.insert(word, {}))) {
            LOG_WARN("fail to insert dictionary word", K(ret), K(word));
          } else {
            ++word_count_;
          }
        }
      }
    }
  }

  if (OB_SUCC(ret) && word_count_ > 0) {
    ObFTDATBuilder<void> builder(allocator_);
    ObFTDAT *dat = nullptr;
    size_t dat_size = 0;
    if (OB_FAIL(builder.init(trie))) {
      LOG_WARN("fail to initialize user dictionary DAT", K(ret));
    } else if (OB_FAIL(builder.build_from_trie(trie))) {
      LOG_WARN("fail to build user dictionary DAT", K(ret));
    } else if (OB_FAIL(builder.get_mem_block(dat, dat_size))) {
      LOG_WARN("fail to get user dictionary DAT", K(ret));
    } else if (OB_ISNULL(dat) || 0 == dat_size) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("empty DAT for nonempty dictionary", K(ret), K(word_count_));
    } else if (OB_ISNULL(reader_ = OB_NEWx(ObFTDATReader<void>, &allocator_, dat))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate user dictionary reader", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    table_id_ = table_id;
    LOG_INFO("loaded user fulltext dictionary", K(database_name), K(table_name), K(table_id_), K(word_count_));
  }
  return ret;
}

int ObFTUserDict::match(const ObString &single_word, ObDATrieHit &hit) const
{
  return match_with_hit(single_word, hit, hit);
}

int ObFTUserDict::match(const ObString &words, bool &is_match) const
{
  int ret = OB_SUCCESS;
  int64_t char_len = 0;
  is_match = false;
  ObDATrieHit hit(this, 0);
  if (nullptr != reader_) {
    for (int64_t offset = 0; OB_SUCC(ret) && offset < words.length(); offset += char_len) {
      if (OB_FAIL(ObCharset::first_valid_char(CS_TYPE_UTF8MB4_BIN,
                                              words.ptr() + offset,
                                              words.length() - offset,
                                              char_len))) {
        LOG_WARN("invalid dictionary lookup string", K(ret), K(words));
      } else if (OB_FAIL(match_with_hit(ObString(char_len, words.ptr() + offset), hit, hit))) {
        LOG_WARN("fail to match dictionary word", K(ret));
      } else if (hit.is_match() && offset + char_len == words.length()) {
        is_match = true;
        break;
      } else if (hit.is_unmatch()) {
        break;
      }
    }
  }
  return ret;
}

int ObFTUserDict::match_with_hit(const ObString &single_word,
                                 const ObDATrieHit &last_hit,
                                 ObDATrieHit &hit) const
{
  int ret = OB_SUCCESS;
  if (nullptr == reader_) {
    if (&last_hit != &hit) {
      hit = last_hit;
    }
    hit.set_unmatch();
  } else if (OB_UNLIKELY(last_hit.dict_ != this)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dictionary hit belongs to another dictionary", K(ret), KP(last_hit.dict_), KP(this));
  } else if (OB_FAIL(reader_->match_with_hit(single_word, last_hit, hit))) {
    LOG_WARN("fail to match user dictionary", K(ret));
  }
  return ret;
}

ObFTUserDictHandle &ObFTUserDictHandle::operator=(const ObFTUserDictHandle &other)
{
  if (this != &other) {
    reset();
    if (nullptr != other.dict_) {
      dict_ = other.dict_;
      dict_->inc_ref();
    }
  }
  return *this;
}

int ObFTUserDictHandle::set_dict(ObFTUserDict *dict)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(dict)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    reset();
    dict_ = dict;
    dict_->inc_ref();
  }
  return ret;
}

void ObFTUserDictHandle::reset()
{
  if (nullptr != dict_) {
    ObFTUserDict *dict = dict_;
    dict_ = nullptr;
    if (0 == dict->dec_ref()) {
      OB_DELETE(ObFTUserDict, ObMemAttr("FTUserDict"), dict);
    }
  }
}

int ObFTUserDictManager::init()
{
  int ret = OB_SUCCESS;
  static constexpr int64_t BUCKET_COUNT = 128;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(dict_map_.create(BUCKET_COUNT, "FTUserMap"))) {
    LOG_WARN("fail to create user dictionary map", K(ret));
  } else if (OB_FAIL(dict_lock_.init(BUCKET_COUNT))) {
    LOG_WARN("fail to initialize user dictionary lock", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObFTUserDictManager::release_dict(ObFTUserDict *dict)
{
  if (nullptr != dict && 0 == dict->dec_ref()) {
    OB_DELETE(ObFTUserDict, ObMemAttr("FTUserDict"), dict);
  }
}

void ObFTUserDictManager::destroy()
{
  if (dict_map_.created()) {
    for (auto iter = dict_map_.begin(); iter != dict_map_.end(); ++iter) {
      release_dict(iter->second);
    }
    dict_map_.destroy();
  }
  is_inited_ = false;
}

int ObFTUserDictManager::resolve_table(const ObString &full_table_name,
                                       ObString &database_name,
                                       ObString &table_name,
                                       uint64_t &table_id) const
{
  int ret = OB_SUCCESS;
  const char *dot = full_table_name.find('.');
  const ObDatabaseSchema *database_schema = nullptr;
  const ObTableSchema *table_schema = nullptr;
  ObSchemaGetterGuard schema_guard;
  table_id = OB_INVALID_ID;
  if (OB_ISNULL(dot) || dot == full_table_name.ptr()
      || dot == full_table_name.ptr() + full_table_name.length() - 1
      || nullptr != ObString(full_table_name.length() - (dot - full_table_name.ptr()) - 1,
                            dot + 1).find('.')) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dictionary table name must be database.table", K(ret), K(full_table_name));
  } else {
    database_name.assign_ptr(full_table_name.ptr(), dot - full_table_name.ptr());
    table_name.assign_ptr(dot + 1, full_table_name.length() - (dot - full_table_name.ptr()) - 1);
    if (OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("fail to get schema guard", K(ret));
    } else if (OB_FAIL(schema_guard.get_database_schema(database_name, database_schema))) {
      LOG_WARN("fail to get dictionary database", K(ret), K(database_name));
    } else if (OB_ISNULL(database_schema)) {
      ret = OB_ERR_BAD_DATABASE;
    } else if (OB_FAIL(schema_guard.get_table_schema(database_schema->get_database_id(),
                                                     table_name,
                                                     false,
                                                     table_schema))) {
      LOG_WARN("fail to get dictionary table", K(ret), K(database_name), K(table_name));
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
    } else if (!table_schema->is_fulltext_dict()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "referenced table is not a FULLTEXT_DICT table");
    } else if (OB_FAIL(table_schema->check_fulltext_dict_structure())) {
      LOG_WARN("invalid fulltext dictionary structure", K(ret), KPC(table_schema));
    } else {
      table_id = table_schema->get_table_id();
    }
  }
  return ret;
}

int ObFTUserDictManager::build_dict(const ObString &database_name,
                                    const ObString &table_name,
                                    const uint64_t table_id,
                                    ObFTUserDict *&dict) const
{
  int ret = OB_SUCCESS;
  dict = OB_NEW(ObFTUserDict, ObMemAttr("FTUserDict"));
  if (OB_ISNULL(dict)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(dict->build(database_name, table_name, table_id))) {
    LOG_WARN("fail to build user dictionary", K(ret), K(database_name), K(table_name), K(table_id));
    release_dict(dict);
    dict = nullptr;
  }
  return ret;
}

int ObFTUserDictManager::get_dict(const ObString &full_table_name, ObFTUserDictHandle &handle)
{
  int ret = OB_SUCCESS;
  ObString database_name;
  ObString table_name;
  uint64_t table_id = OB_INVALID_ID;
  ObFTUserDict *dict = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(resolve_table(full_table_name, database_name, table_name, table_id))) {
    LOG_WARN("fail to resolve user dictionary", K(ret), K(full_table_name));
  } else {
    {
      ObBucketHashRLockGuard guard(dict_lock_, table_id);
      ret = dict_map_.get_refactored(table_id, dict);
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else if (OB_SUCC(ret) && OB_FAIL(handle.set_dict(dict))) {
        LOG_WARN("fail to pin user dictionary", K(ret), K(table_id));
      }
    }
    if (OB_SUCC(ret) && !handle.is_valid()) {
      ObFTUserDict *new_dict = nullptr;
      if (OB_FAIL(build_dict(database_name, table_name, table_id, new_dict))) {
      } else {
        ObBucketHashWLockGuard guard(dict_lock_, table_id);
        if (OB_FAIL(dict_map_.get_refactored(table_id, dict))) {
          if (OB_HASH_NOT_EXIST == ret) {
            ret = dict_map_.set_refactored(table_id, new_dict);
            dict = new_dict;
            new_dict = nullptr;
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(handle.set_dict(dict))) {
          LOG_WARN("fail to pin loaded user dictionary", K(ret), K(table_id));
        }
      }
      release_dict(new_dict);
    }
  }
  return ret;
}

int ObFTUserDictManager::refresh(const ObString &full_table_name)
{
  int ret = OB_SUCCESS;
  ObString database_name;
  ObString table_name;
  uint64_t table_id = OB_INVALID_ID;
  if (OB_FAIL(resolve_table(full_table_name, database_name, table_name, table_id))) {
    LOG_WARN("fail to resolve dictionary for refresh", K(ret), K(full_table_name));
  } else if (OB_FAIL(refresh(database_name, table_name, table_id))) {
    LOG_WARN("fail to refresh dictionary", K(ret), K(full_table_name), K(table_id));
  }
  return ret;
}

int ObFTUserDictManager::refresh(const ObString &database_name,
                                 const ObString &table_name,
                                 const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObFTUserDict *new_dict = nullptr;
  ObFTUserDict *old_dict = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(build_dict(database_name, table_name, table_id, new_dict))) {
  } else {
    ObBucketHashWLockGuard guard(dict_lock_, table_id);
    int get_ret = dict_map_.get_refactored(table_id, old_dict);
    if (OB_SUCCESS != get_ret && OB_HASH_NOT_EXIST != get_ret) {
      ret = get_ret;
      old_dict = nullptr;
    } else if (OB_FAIL(dict_map_.set_refactored(table_id, new_dict, 1))) {
      LOG_WARN("fail to publish refreshed dictionary", K(ret), K(table_id));
      old_dict = nullptr;
    } else {
      new_dict = nullptr;
    }
  }
  release_dict(old_dict);
  release_dict(new_dict);
  return ret;
}

} // namespace storage
} // namespace oceanbase
