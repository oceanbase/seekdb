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

#include "storage/fts/dict/ob_ft_dict_hub.h"

#include "lib/allocator/page_arena.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_sql_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "storage/fts/dict/ob_ft_cache_container.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_range_dict.h"
namespace oceanbase
{
namespace storage
{
namespace
{

bool is_builtin_ik_dict(const ObFTDictDesc &desc)
{
  bool is_builtin = false;

  switch (desc.type_) {
    case ObFTDictType::DICT_IK_MAIN:
      is_builtin =
          0 == desc.name_.case_compare("main_dict");
      break;

    case ObFTDictType::DICT_IK_QUAN:
      is_builtin =
          0 == desc.name_.case_compare("quan_dict");
      break;

    case ObFTDictType::DICT_IK_STOP:
      is_builtin =
          0 == desc.name_.case_compare("stopword");
      break;

    default:
      is_builtin = false;
      break;
  }

  return is_builtin;
}

int split_custom_dict_name(
    const ObString &full_name,
    ObString &database_name,
    ObString &table_name)
{
  int ret = OB_SUCCESS;
  int64_t dot_pos = -1;

  database_name.reset();
  table_name.reset();

  if (full_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dictionary name is empty", K(ret));
  } else {
    for (int64_t i = 0;
         OB_SUCC(ret) && i < full_name.length();
         ++i) {
      if ('.' == full_name.ptr()[i]) {
        if (-1 != dot_pos) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("dictionary name contains multiple dots",
                   K(ret),
                   K(full_name));
        } else {
          dot_pos = i;
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (dot_pos <= 0
          || dot_pos >= full_name.length() - 1) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("custom dictionary name must use database.table format",
                 K(ret),
                 K(full_name));
      } else {
        database_name.assign_ptr(
            full_name.ptr(),
            static_cast<int32_t>(dot_pos));

        table_name.assign_ptr(
            full_name.ptr() + dot_pos + 1,
            static_cast<int32_t>(
                full_name.length() - dot_pos - 1));
      }
    }
  }

  return ret;
}

int build_dict_cache(
    const ObFTDictDesc &desc,
    ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;

  if (is_builtin_ik_dict(desc)) {
    if (OB_FAIL(
            ObFTRangeDict::build_cache_from_ik_dict(
                desc,
                container))) {
      LOG_WARN("failed to build builtin ik dictionary cache",
               K(ret),
               K(desc.name_),
               K(desc.type_));
    }
  } else {
    ObString database_name;
    ObString table_name;

    if (OB_FAIL(
            split_custom_dict_name(
                desc.name_,
                database_name,
                table_name))) {
      LOG_WARN("failed to split custom dictionary name",
               K(ret),
               K(desc.name_));
    } else if (OB_FAIL(
                   ObFTRangeDict::build_cache_from_table(
                       desc,
                       database_name,
                       table_name,
                       container))) {
      LOG_WARN("failed to build custom dictionary cache",
               K(ret),
               K(database_name),
               K(table_name),
               K(desc.type_));
    }
  }

  return ret;
}

} // namespace

int ObFTDictHub::init()
{
  static constexpr int K_MAX_DICT_BUCKET = 128; // for now, only built-in dicts.
  int ret = OB_SUCCESS;
  if (OB_FAIL(dict_map_.create(K_MAX_DICT_BUCKET, "dict_map"))) {
    LOG_WARN("init dict map failed", K(ret));
  } else if (OB_FAIL(rw_dict_lock_.init(K_MAX_DICT_BUCKET))) {
    LOG_WARN("init dict lock failed", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
};

int ObFTDictHub::destroy()
{
  int ret = OB_SUCCESS;
  is_inited_ = false;
  return ret;
}
int ObFTDictHub::build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  ObFTDictInfoKey key(
      desc.name_,
      static_cast<uint64_t>(desc.type_));
  ObFTDictInfo info;
  container.reset();

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else {
    ObBucketHashWLockGuard guard(rw_dict_lock_, key.hash());

    // try if valid with no recursive lock
    if (OB_FAIL(get_dict_info(key, info))) {
      if (OB_HASH_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        LOG_WARN("Failed to get dict info", K(ret));
      }
    } else if (OB_FAIL(ObFTRangeDict::try_load_cache(desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
      } else {
        LOG_WARN("Failed to load cache", K(ret));
      }
    }

    if (OB_FAIL(ret)) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        if (OB_FAIL(build_dict_cache(desc, container))) {
          LOG_WARN("failed to build dictionary cache",
                   K(ret),
                   K(desc.name_),
                   K(desc.type_));
        } else if (OB_UNLIKELY(
                       desc.name_.length()
                       >= static_cast<int64_t>(sizeof(info.name_)))) {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("dictionary name is too long",
                   K(ret),
                   K(desc.name_.length()),
                   K(sizeof(info.name_)));
        } else {
          MEMSET(info.name_, 0, sizeof(info.name_));

          if (!desc.name_.empty()) {
            MEMCPY(
                info.name_,
                desc.name_.ptr(),
                desc.name_.length());
          }

          info.type_ = desc.type_;
          info.charset_ = desc.charset_;
          info.range_count_ = container.get_handles().size();

          if (OB_FAIL(put_dict_info(key, info))) {
            LOG_WARN("Failed to put dict info",
                     K(ret),
                     K(desc.name_),
                     K(desc.type_));
          }
        }
      }
    }
  }
  return ret;
}

int ObFTDictHub::load_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container)
{
  int ret = OB_SUCCESS;
  ObFTDictInfo info;
  container.reset();
  ObFTDictInfoKey key(
      desc.name_,
      static_cast<uint64_t>(desc.type_));
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dict hub not init", K(ret));
  } else {
    {
      ObBucketHashRLockGuard guard(rw_dict_lock_, key.hash());
      if (OB_FAIL(get_dict_info(key, info))) {
        if (OB_HASH_NOT_EXIST == ret) {
          // dict not exist, make new one, by caller
          ret = OB_ENTRY_NOT_EXIST;
        } else {
          LOG_WARN("Failed to get dict info", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObFTRangeDict::try_load_cache(desc, info.range_count_, container))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        // dict not exist, make new one, by caller
      } else {
        LOG_WARN("Failed to load cache", K(ret));
      }
    }
  }

  return ret;
}

int ObFTDictHub::refresh_dict(
    const common::ObString &database_name,
    const common::ObString &table_name)
{
  int ret = OB_SUCCESS;
  common::ObSqlString full_name;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("dictionary hub is not initialized", K(ret));
  } else if (database_name.empty() || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database name or dictionary table name is empty",
             K(ret),
             K(database_name),
             K(table_name));
  } else if (OB_FAIL(
                 full_name.append_fmt(
                     "%.*s.%.*s",
                     database_name.length(),
                     database_name.ptr(),
                     table_name.length(),
                     table_name.ptr()))) {
    LOG_WARN("failed to build full dictionary table name",
             K(ret),
             K(database_name),
             K(table_name));
  } else {
    const ObFTDictType dict_types[] = {
        ObFTDictType::DICT_IK_MAIN,
        ObFTDictType::DICT_IK_QUAN,
        ObFTDictType::DICT_IK_STOP
    };

    for (int64_t i = 0;
         OB_SUCC(ret) && i < ARRAYSIZEOF(dict_types);
         ++i) {
      const ObFTDictType dict_type = dict_types[i];

      ObFTDictDesc desc(
          full_name.string(),
          dict_type,
          ObCharsetType::CHARSET_UTF8MB4,
          ObCollationType::CS_TYPE_UTF8MB4_BIN);

      ObFTDictInfoKey key(
          desc.name_,
          static_cast<uint64_t>(desc.type_));

      ObBucketHashWLockGuard guard(
          rw_dict_lock_,
          key.hash());

      ObFTDictInfo old_info;
      ObFTDictInfo new_info;
      int64_t new_version = 1;

      const int get_ret = get_dict_info(key, old_info);

      if (OB_SUCCESS == get_ret) {
        new_version = old_info.version_ + 1;
      } else if (OB_HASH_NOT_EXIST == get_ret) {
        // The first refresh creates version 1.
        new_version = 1;
      } else {
        ret = get_ret;
        LOG_WARN("failed to get old dictionary info",
                 K(ret),
                 K(desc.name_),
                 K(desc.type_));
      }

      if (OB_SUCC(ret)) {
        common::ObArenaAllocator allocator(
            lib::ObMemAttr("FTDictRefresh"));
        ObFTCacheRangeContainer container(allocator);

        if (OB_FAIL(build_dict_cache(desc, container))) {
          LOG_WARN("failed to rebuild custom dictionary cache",
                   K(ret),
                   K(desc.name_),
                   K(desc.type_));
        } else if (OB_UNLIKELY(
                       desc.name_.length()
                       >= static_cast<int64_t>(sizeof(new_info.name_)))) {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("dictionary table name is too long",
                   K(ret),
                   K(desc.name_.length()),
                   K(sizeof(new_info.name_)));
        } else {
          MEMSET(
              new_info.name_,
              0,
              sizeof(new_info.name_));

          if (!desc.name_.empty()) {
            MEMCPY(
                new_info.name_,
                desc.name_.ptr(),
                desc.name_.length());
          }

          new_info.type_ = desc.type_;
          new_info.charset_ = desc.charset_;
          new_info.version_ = new_version;
          new_info.range_count_ =
              static_cast<int32_t>(
                  container.get_handles().size());

          if (OB_FAIL(put_dict_info(key, new_info))) {
            LOG_WARN("failed to update dictionary info",
                     K(ret),
                     K(desc.name_),
                     K(desc.type_),
                     K(new_version),
                     K(new_info.range_count_));
          } else {
            LOG_INFO("successfully refreshed fulltext dictionary",
                     K(desc.name_),
                     K(desc.type_),
                     K(new_version),
                     K(new_info.range_count_));
          }
        }
      }
    }
  }

  return ret;
}


int ObFTDictHub::get_dict_info(const ObFTDictInfoKey &key, ObFTDictInfo &info)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(dict_map_.get_refactored(key, info))) {
    if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("get dict info failed", K(ret));
    }
  }

  return ret;
}

int ObFTDictHub::put_dict_info(const ObFTDictInfoKey &key, const ObFTDictInfo &info)
{
  int ret = OB_SUCCESS;
  const int cover_exist_flag = 1;
  if (OB_FAIL(dict_map_.set_refactored(key, info, cover_exist_flag))) {
    LOG_WARN("put dict info failed", K(ret));
  }

  return ret;
}
} //  namespace storage
} //  namespace oceanbase
