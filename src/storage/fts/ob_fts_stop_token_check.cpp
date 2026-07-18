/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ob_fts_stop_token_check.h"

#include "share/datum/ob_datum_funcs.h"

namespace oceanbase
{
namespace storage
{

int ObStopTokenChecker::init(const ObCollationType coll, ObStopTokenTable *stop_token_table)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("stop token checker initialized twice", K(ret));
  } else if (OB_UNLIKELY(CS_TYPE_INVALID == coll || coll >= CS_TYPE_PINYIN_BEGIN_MARK)
      || OB_ISNULL(stop_token_table)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid stop token checker arguments", K(ret), K(coll), KP(stop_token_table));
  } else {
    collation_type_ = coll;
    stop_token_table_ = stop_token_table;
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
    LOG_WARN("stop token checker is not initialized", K(ret));
  } else if (OB_UNLIKELY(token.get_collation_type() != collation_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("stop token collation does not match checker", K(ret),
        K(collation_type_), K(token));
  } else {
    ret = stop_token_table_->exist_refactored(token);
    if (OB_HASH_EXIST == ret) {
      is_stop_token = true;
      ret = OB_SUCCESS;
    } else if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to check stop token", K(ret), K(token));
    }
  }
  return ret;
}

int ObStopTokenCheckerGen::init()
{
  int ret = OB_SUCCESS;
  common::TCWLockGuard guard(lock_);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("stop token checker generator initialized twice", K(ret));
  } else if (OB_FAIL(stop_token_tables_.create(ObCharset::VALID_COLLATION_TYPES,
                                               "FTStopTokenMap"))) {
    LOG_WARN("failed to create stop token table map", K(ret));
  } else if (OB_FAIL(generate_stop_token_table(CS_TYPE_UTF8MB4_GENERAL_CI))) {
    LOG_WARN("failed to generate general-ci stop token table", K(ret));
  } else if (OB_FAIL(generate_stop_token_table(CS_TYPE_UTF8MB4_BIN))) {
    LOG_WARN("failed to generate binary stop token table", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObStopTokenCheckerGen::reset()
{
  common::TCWLockGuard guard(lock_);
  if (stop_token_tables_.created()) {
    for (StopTokenHashMap::iterator iter = stop_token_tables_.begin();
         iter != stop_token_tables_.end(); ++iter) {
      if (OB_NOT_NULL(iter->second)) {
        iter->second->destroy();
        OB_DELETE(ObStopTokenTable, &allocator_, iter->second);
        iter->second = nullptr;
      }
    }
    stop_token_tables_.destroy();
  }
  allocator_.reset();
  is_inited_ = false;
}

int ObStopTokenCheckerGen::convert_charset(
    const ObString &src,
    const ObCollationType from_coll,
    const ObCollationType to_coll,
    ObString &converted)
{
  int ret = OB_SUCCESS;
  converted.reset();
  if (CHARSET_UTF8MB4 == ObCharset::charset_type_by_coll(to_coll)) {
    converted = src;
  } else if (OB_FAIL(ObCharset::charset_convert(allocator_, src, from_coll, to_coll, converted))) {
    LOG_WARN("failed to convert stop token charset", K(ret), K(from_coll), K(to_coll));
  }
  return ret;
}

int ObStopTokenCheckerGen::generate_stop_token_table(const ObCollationType coll)
{
  int ret = OB_SUCCESS;
  ObStopTokenTable *table = OB_NEWx(ObStopTokenTable, &allocator_);
  if (OB_ISNULL(table)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate stop token table", K(ret));
  } else if (OB_FAIL(table->create(DEFAULT_STOP_TOKEN_TABLE_CAPACITY,
                                   "FTStopBucket",
                                   "FTStopNode"))) {
    LOG_WARN("failed to create stop token table", K(ret), K(coll));
  } else {
    ObObjMeta meta;
    meta.set_varchar();
    meta.set_collation_type(coll);
    sql::ObExprBasicFuncs *basic_funcs = ObDatumFuncs::get_basic_func(meta.get_type(), coll);
    ObDatumCmpFuncType cmp_func = get_datum_cmp_func(meta, meta);
    if (OB_ISNULL(basic_funcs) || OB_ISNULL(basic_funcs->default_hash_) || OB_ISNULL(cmp_func)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get stop token datum functions", K(ret), K(meta));
    }
    const int64_t token_count = ARRAYSIZEOF(OB_STOP_TOKEN_TABLE_UTF8);
    for (int64_t i = 0; OB_SUCC(ret) && i < token_count; ++i) {
      ObString converted;
      ObFTToken token;
      uint64_t hash_val = 0;
      if (OB_FAIL(convert_charset(OB_STOP_TOKEN_TABLE_UTF8[i],
                                  CS_TYPE_UTF8MB4_GENERAL_CI, coll, converted))) {
        LOG_WARN("failed to convert stop token", K(ret), K(i), K(coll));
      } else if (OB_FAIL(token.init(converted.ptr(), converted.length(), meta,
                                    basic_funcs->default_hash_, cmp_func))) {
        LOG_WARN("failed to initialize stop token", K(ret), K(i), K(coll));
      } else if (OB_FAIL(token.hash(hash_val))) {
        LOG_WARN("failed to precompute stop token hash", K(ret), K(token));
      } else if (OB_FAIL(table->set_refactored(token))) {
        LOG_WARN("failed to insert stop token", K(ret), K(token));
      }
    }
    if (OB_SUCC(ret)
        && OB_FAIL(stop_token_tables_.set_refactored(static_cast<uint64_t>(coll), table))) {
      LOG_WARN("failed to register stop token table", K(ret), K(coll));
    }
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(table)) {
    table->destroy();
    OB_DELETE(ObStopTokenTable, &allocator_, table);
  }
  return ret;
}

int ObStopTokenCheckerGen::get_stop_token_checker_by_coll(
    const ObCollationType coll,
    ObStopTokenChecker &checker)
{
  int ret = OB_SUCCESS;
  ObStopTokenTable *table = nullptr;
  checker.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("stop token checker generator is not initialized", K(ret));
  } else if (OB_UNLIKELY(CS_TYPE_INVALID == coll || coll >= CS_TYPE_PINYIN_BEGIN_MARK)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid stop token collation", K(ret), K(coll));
  } else {
    {
      common::TCRLockGuard guard(lock_);
      ret = stop_token_tables_.get_refactored(static_cast<uint64_t>(coll), table);
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else if (OB_SUCC(ret) && OB_FAIL(checker.init(coll, table))) {
        LOG_WARN("failed to initialize stop token checker", K(ret), K(coll));
      }
    }
    if (OB_SUCC(ret) && OB_ISNULL(table)) {
      common::TCWLockGuard guard(lock_);
      ret = stop_token_tables_.get_refactored(static_cast<uint64_t>(coll), table);
      if (OB_HASH_NOT_EXIST == ret) {
        ret = generate_stop_token_table(coll);
        if (OB_SUCC(ret)) {
          ret = stop_token_tables_.get_refactored(static_cast<uint64_t>(coll), table);
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(checker.init(coll, table))) {
        LOG_WARN("failed to initialize generated stop token checker", K(ret), K(coll));
      }
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
