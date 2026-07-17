/*
 * Copyright (c) 2026 OceanBase.
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

#include "storage/fts/ob_fts_stop_token_check.h"

#include "share/datum/ob_datum_funcs.h"
#include "share/rc/ob_tenant_base.h"
#include "storage/ob_storage_util.h"

namespace oceanbase
{
namespace storage
{

int ObStopTokenChecker::init(const ObCollationType coll,
                             ObStopTokenTable *stop_token_hash_table)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(CS_TYPE_INVALID == coll || coll >= CS_TYPE_PINYIN_BEGIN_MARK
                         || nullptr == stop_token_hash_table)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid stop token checker arguments", K(ret), K(coll), KP(stop_token_hash_table));
  } else {
    collation_type_ = coll;
    stop_token_hash_table_ = stop_token_hash_table;
    is_inited_ = true;
  }
  return ret;
}

int ObStopTokenChecker::check_is_stop_token(const ObFTToken &token, bool &is_stop_token) const
{
  int ret = OB_SUCCESS;
  is_stop_token = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else {
    // 表初始化完成后永不修改，exist_refactored 直接走无锁表并复用 token 的预计算 hash。
    ret = stop_token_hash_table_->exist_refactored(token);
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else if (OB_HASH_EXIST == ret) {
      is_stop_token = true;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to query stop token table", K(ret), K(token));
    }
  }
  return ret;
}

void ObStopTokenCheckerGen::reset()
{
  common::TCWLockGuard guard(lock_);
  is_inited_ = false;
  if (stop_token_hash_tables_.created()) {
    for (StopTokenHashMap::iterator iter = stop_token_hash_tables_.begin();
         iter != stop_token_hash_tables_.end();
         ++iter) {
      if (nullptr != iter->second) {
        iter->second->destroy();
        OB_DELETE(ObStopTokenTable, &allocator_, iter->second);
        iter->second = nullptr;
      }
    }
    stop_token_hash_tables_.destroy();
  }
  allocator_.reset();
}

int ObStopTokenCheckerGen::init()
{
  int ret = OB_SUCCESS;
  common::TCWLockGuard guard(lock_);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else {
    const uint64_t tenant_id = OB_SERVER_TENANT_ID;
    allocator_.set_attr(common::ObMemAttr("tokenCheckerGen"));
    if (OB_FAIL(stop_token_hash_tables_.create(ObCharset::VALID_COLLATION_TYPES,
                                                "st_hash_tables",
                                                "st_hash_tables",
                                                tenant_id))) {
      LOG_WARN("failed to create stop token table map", K(ret));
    } else if (OB_FAIL(generate_stop_token_hash_table_by_coll(CS_TYPE_UTF8MB4_GENERAL_CI))) {
      LOG_WARN("failed to build general-ci stop token table", K(ret));
    } else if (OB_FAIL(generate_stop_token_hash_table_by_coll(CS_TYPE_UTF8MB4_BIN))) {
      LOG_WARN("failed to build binary stop token table", K(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObStopTokenCheckerGen::convert_charset(const ObString &src_string,
                                           const ObCollationType from_collation,
                                           const ObCollationType to_collation,
                                           ObString &converted_string)
{
  int ret = OB_SUCCESS;
  converted_string.reset();
  if (CHARSET_UTF8MB4 == ObCharset::charset_type_by_coll(to_collation)) {
    converted_string = src_string;
  } else if (OB_FAIL(ObCharset::charset_convert(
                         allocator_, src_string, from_collation, to_collation, converted_string))) {
    LOG_WARN("failed to convert stop token charset", K(ret), K(from_collation), K(to_collation));
  }
  return ret;
}

int ObStopTokenCheckerGen::generate_stop_token_hash_table_by_coll(const ObCollationType coll)
{
  int ret = OB_SUCCESS;
  ObStopTokenTable *table = OB_NEWx(ObStopTokenTable, &allocator_);
  if (OB_ISNULL(table)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(table->create(DEFAULT_STOP_TOKEN_TABLE_CAPACITY,
                                    "stop_token_tab",
                                    "stop_token_tab"))) {
    LOG_WARN("failed to create stop token hash set", K(ret), K(coll));
  } else {
    ObObjMeta meta;
    meta.set_varchar();
    meta.set_collation_type(coll);
    sql::ObExprBasicFuncs *basic_funcs = ObDatumFuncs::get_basic_func(meta.get_type(), coll);
    ObDatumCmpFuncType cmp_func = get_datum_cmp_func(meta, meta);
    if (OB_ISNULL(basic_funcs) || OB_ISNULL(basic_funcs->default_hash_) || OB_ISNULL(cmp_func)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get stop token datum functions", K(ret), K(meta), KP(basic_funcs), KP(cmp_func));
    }
    const int64_t token_count =
        sizeof(OB_STOP_TOKEN_TABLE_UTF8) / sizeof(OB_STOP_TOKEN_TABLE_UTF8[0]);
    for (int64_t i = 0; OB_SUCC(ret) && i < token_count; ++i) {
      const ObString src(OB_STOP_TOKEN_TABLE_UTF8[i]);
      ObString converted;
      ObFTToken token;
      uint64_t hash_val = 0;
      if (OB_FAIL(convert_charset(src, CS_TYPE_UTF8MB4_GENERAL_CI, coll, converted))) {
        LOG_WARN("failed to convert stop token", K(ret), K(coll), K(src));
      } else if (OB_FAIL(token.init(converted.ptr(),
                                    converted.length(),
                                    meta,
                                    basic_funcs->default_hash_,
                                    cmp_func))) {
        LOG_WARN("failed to initialize stop token", K(ret), K(converted), K(meta));
      } else if (OB_FAIL(token.hash(hash_val))) {
        LOG_WARN("failed to precompute stop token hash", K(ret), K(token));
      } else if (OB_FAIL(table->set_refactored(token))) {
        LOG_WARN("failed to insert stop token", K(ret), K(token));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(stop_token_hash_tables_.set_refactored(coll, table))) {
      LOG_WARN("failed to publish stop token table", K(ret), K(coll));
    }
  }
  if (OB_FAIL(ret) && nullptr != table) {
    table->destroy();
    OB_DELETE(ObStopTokenTable, &allocator_, table);
  }
  return ret;
}

int ObStopTokenCheckerGen::get_stop_token_checker_by_coll(
    const ObCollationType coll,
    ObStopTokenChecker &stop_token_checker)
{
  int ret = OB_SUCCESS;
  ObStopTokenTable *table = nullptr;
  stop_token_checker.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(CS_TYPE_INVALID == coll || coll >= CS_TYPE_PINYIN_BEGIN_MARK)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    {
      common::TCRLockGuard read_guard(lock_);
      if (OB_FAIL(stop_token_hash_tables_.get_refactored(coll, table))) {
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to find stop token table", K(ret), K(coll));
        }
      }
    }
    if (OB_SUCC(ret) && nullptr == table) {
      // 双检写锁只发生在首个使用该 collation 时，发布后 checker 的逐 token 读取完全无锁。
      common::TCWLockGuard write_guard(lock_);
      if (OB_FAIL(stop_token_hash_tables_.get_refactored(coll, table))) {
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          if (OB_FAIL(generate_stop_token_hash_table_by_coll(coll))) {
            LOG_WARN("failed to lazily build stop token table", K(ret), K(coll));
          } else if (OB_FAIL(stop_token_hash_tables_.get_refactored(coll, table))) {
            LOG_WARN("failed to fetch newly built stop token table", K(ret), K(coll));
          }
        } else {
          LOG_WARN("failed to find stop token table under write lock", K(ret), K(coll));
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(stop_token_checker.init(coll, table))) {
      LOG_WARN("failed to initialize stop token checker view", K(ret), K(coll), KP(table));
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
