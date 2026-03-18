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

#define USING_LOG_PREFIX SHARE
#include "ob_log_restore_source_mgr.h"
#include "share/ob_kv_storage.h"
#include "share/ob_server_struct.h"  // GCTX
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::share;

// Helper function to convert ObString to const char* for KV storage
static int get_cstring_from_obstring(const ObString &ob_str, char *buf, int64_t buf_size, const char *&cstr)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ob_str.length() >= buf_size)) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_WARN("buffer not enough", KR(ret), K(ob_str.length()), K(buf_size));
  } else {
    MEMCPY(buf, ob_str.ptr(), ob_str.length());
    buf[ob_str.length()] = '\0';
    cstr = buf;
  }
  return ret;
}

int ObLogRestoreSourceMgr::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObLogRestoreSourceMgr already init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_id", K(ret), K(tenant_id));
  } else if (OB_ISNULL(GCTX.kv_storage_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("kv_storage_ is not initialized", KR(ret));
  } else if (!GCTX.kv_storage_->is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("kv_storage_ is not initialized", KR(ret));
  } else {
    tenant_id_ = tenant_id;
    is_inited_ = true;
  }
  return ret;
}

int ObLogRestoreSourceMgr::get_kv_key_(const uint64_t tenant_id, ObString &key)
{
  UNUSED(tenant_id);
  key = ObString::make_string("restore_source");
  return OB_SUCCESS;
}

int ObLogRestoreSourceMgr::update_recovery_until_scn(const SCN &recovery_until_scn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogRestoreSourceMgr not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!recovery_until_scn.is_valid_and_not_min())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid recovery_until_scn", KR(ret), K(recovery_until_scn));
  } else {
    ObLogRestoreSourceItem item;
    if (OB_FAIL(get_source(item))) {
      LOG_WARN("failed to get source for update", KR(ret), K(tenant_id_));
    } else {
      item.until_scn_ = recovery_until_scn;
      // Save to KV storage
      ObString key;
      ObString value;
      if (OB_FAIL(get_kv_key_(tenant_id_, key))) {
        LOG_WARN("failed to get kv key", KR(ret), K(tenant_id_));
      } else if (OB_FAIL(serialize_to_kv_(item, value, item.allocator_))) {
        LOG_WARN("failed to serialize restore source", KR(ret), K(item));
      } else if (OB_FAIL(GCTX.kv_storage_->set(key, value))) {
        LOG_WARN("failed to set to kv storage", KR(ret), K(key), K(value));
      } else {
        LOG_INFO("update log restore source recovery until scn succ", K(recovery_until_scn));
      }
    }
  }
  return ret;
}

int ObLogRestoreSourceMgr::delete_source()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogRestoreSourceMgr not init", K(ret), K(is_inited_));
  } else {
    ObString key;
    if (OB_FAIL(get_kv_key_(tenant_id_, key))) {
      LOG_WARN("failed to get kv key", KR(ret), K(tenant_id_));
    } else if (OB_FAIL(GCTX.kv_storage_->del(key))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        LOG_INFO("restore source not found in KV storage, already deleted", K(tenant_id_));
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to delete from kv storage", KR(ret), K(key));
      }
    } else {
      LOG_INFO("delete log restore source succ", K(tenant_id_));
    }
  }
  return ret;
}

int ObLogRestoreSourceMgr::add_service_source(const SCN &recovery_until_scn,
    const ObString &service_source)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogRestoreSourceMgr not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(service_source.empty() || !recovery_until_scn.is_valid()) ) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(service_source), K(recovery_until_scn));
  } else {
    ObLogRestoreSourceItem item(tenant_id_,
                                OB_DEFAULT_LOG_RESTORE_SOURCE_ID,
                                ObLogRestoreSourceType::SERVICE,
                                service_source,
                                recovery_until_scn);
    // Save to KV storage
    ObString key;
    ObString value;
    if (OB_FAIL(get_kv_key_(tenant_id_, key))) {
      LOG_WARN("failed to get kv key", KR(ret), K(tenant_id_));
    } else if (OB_FAIL(serialize_to_kv_(item, value, item.allocator_))) {
      LOG_WARN("failed to serialize restore source", KR(ret), K(item));
    } else if (OB_FAIL(GCTX.kv_storage_->set(key, value))) {
      LOG_WARN("failed to set to kv storage", KR(ret), K(key), K(value));
    } else {
      LOG_INFO("add service source succ", K(recovery_until_scn), K(service_source));
    }
  }
  return ret;
}

int ObLogRestoreSourceMgr::add_location_source(const SCN &recovery_until_scn,
    const ObString &archive_dest)
{
  int ret = OB_SUCCESS;
  ObBackupDest dest;
  char dest_buf[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogRestoreSourceMgr not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(archive_dest.empty() || !recovery_until_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(archive_dest), K(recovery_until_scn));
  } else if (OB_FAIL(dest.set(archive_dest.ptr()))) {
    // use backup dest to manage oss key
    LOG_WARN("set backup dest failed", K(ret), K(archive_dest));
  } else if (OB_FAIL(dest.get_backup_dest_str(dest_buf, sizeof(dest_buf)))) {
    // store primary cluster id and tenant id in log restore source
    LOG_WARN("get backup dest str with primary attr failed", K(ret), K(dest));
  } else {
    ObLogRestoreSourceItem item(tenant_id_,
                                OB_DEFAULT_LOG_RESTORE_SOURCE_ID,
                                ObLogRestoreSourceType::LOCATION,
                                ObString(dest_buf),
                                recovery_until_scn);
    // Save to KV storage
    ObString key;
    ObString value;
    if (OB_FAIL(get_kv_key_(tenant_id_, key))) {
      LOG_WARN("failed to get kv key", KR(ret), K(tenant_id_));
    } else if (OB_FAIL(serialize_to_kv_(item, value, item.allocator_))) {
      LOG_WARN("failed to serialize restore source", KR(ret), K(item));
    } else if (OB_FAIL(GCTX.kv_storage_->set(key, value))) {
      LOG_WARN("failed to set to kv storage", KR(ret), K(key), K(value));
    } else {
      LOG_INFO("add location source succ", K(recovery_until_scn), K(archive_dest));
    }
  }
  return ret;
}

int ObLogRestoreSourceMgr::add_rawpath_source(const SCN &recovery_until_scn, const DirArray &array)
{
  int ret = OB_SUCCESS;
  ObSqlString rawpath_value;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogRestoreSourceMgr not init", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(array.empty() || !recovery_until_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument to add rawpath source", K(ret), K(array), K(recovery_until_scn));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < array.count(); i++) {
      ObBackupDest dest;
      ObBackupPathString rawpath = array[i];
      char dest_buf[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
      if (OB_UNLIKELY(rawpath.is_empty())) {
        LOG_WARN("raw path is empty", K(array));
      } else if (OB_FAIL(dest.set(rawpath.ptr()))) {
        LOG_WARN("set rawpath backup dest failed", K(ret), K(rawpath));
      } else if (OB_FAIL(dest.get_backup_dest_str(dest_buf, sizeof(dest_buf)))) {
        LOG_WARN("get rawpath backup path failed", K(ret), K(dest));
      } else if (0 == i) {
        if (OB_FAIL(rawpath_value.assign(dest_buf))) {
          LOG_WARN("fail to assign rawpath", K(ret), K(dest_buf));
        }
      } else if (OB_FAIL(rawpath_value.append(","))) {
        LOG_WARN("fail to append rawpath", K(ret));
      } else if (OB_FAIL(rawpath_value.append(dest_buf))) {
        LOG_WARN("fail to append rawpath", K(ret), K(dest_buf));
      }
    }
    if (OB_SUCC(ret)) {
      ObLogRestoreSourceItem item(tenant_id_,
                                  OB_DEFAULT_LOG_RESTORE_SOURCE_ID,
                                  ObLogRestoreSourceType::RAWPATH,
                                  ObString(rawpath_value.ptr()),
                                  recovery_until_scn);
      // Save to KV storage
      ObString key;
      ObString value;
      if (OB_FAIL(get_kv_key_(tenant_id_, key))) {
        LOG_WARN("failed to get kv key", KR(ret), K(tenant_id_));
      } else if (OB_FAIL(serialize_to_kv_(item, value, item.allocator_))) {
        LOG_WARN("failed to serialize restore source", KR(ret), K(item));
      } else if (OB_FAIL(GCTX.kv_storage_->set(key, value))) {
        LOG_WARN("failed to set to kv storage", KR(ret), K(key), K(value));
      } else {
        LOG_INFO("add rawpath source succ", K(recovery_until_scn), K(array));
      }
    }
  }
  return ret;
}

int ObLogRestoreSourceMgr::get_source(ObLogRestoreSourceItem &item)
{
  int ret = OB_SUCCESS;
  // only support src_id 1
  item.tenant_id_ = tenant_id_;
  item.id_ = OB_DEFAULT_LOG_RESTORE_SOURCE_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogRestoreSourceMgr not init", K(ret), K(is_inited_));
  } else {
    ObString key;
    ObString value;
    if (OB_FAIL(get_kv_key_(tenant_id_, key))) {
      LOG_WARN("failed to get kv key", KR(ret), K(tenant_id_));
    } else if (OB_FAIL(GCTX.kv_storage_->get(key, value))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        LOG_DEBUG("restore source not found in KV storage", K(tenant_id_));
        item.tenant_id_ = tenant_id_;
        item.id_ = OB_DEFAULT_LOG_RESTORE_SOURCE_ID;
      } else {
        LOG_WARN("failed to get from kv storage", KR(ret), K(key), K(tenant_id_));
      }
    } else if (OB_FAIL(deserialize_from_kv_(value, item))) {
      LOG_WARN("failed to deserialize restore source", KR(ret), K(value), K(value.length()));
      item.tenant_id_ = tenant_id_;
      item.id_ = OB_DEFAULT_LOG_RESTORE_SOURCE_ID;
    } else {
      // Ensure tenant_id and id are set after deserialization
      item.tenant_id_ = tenant_id_;
      item.id_ = OB_DEFAULT_LOG_RESTORE_SOURCE_ID;
      LOG_TRACE("get_source succ", K(item));
    }
  }
  return ret;
}


int ObLogRestoreSourceMgr::serialize_to_kv_(const ObLogRestoreSourceItem &item, ObString &value, common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  char value_buf[1024];
  int64_t pos = 0;

  // Simple serialization: type|value|until_scn
  // value is a string that may contain special characters, so we need to escape or use base64
  // For simplicity, we'll use a pipe-separated format and assume value doesn't contain '|'
  if (OB_FAIL(databuff_printf(value_buf, sizeof(value_buf), pos, "%d|", static_cast<int>(item.type_)))) {
    LOG_WARN("failed to serialize type", KR(ret), K(item));
  } else {
    // Append value string (need to handle potential '|' characters)
    int64_t value_len = item.value_.length();
    if (pos + value_len + 20 >= sizeof(value_buf)) {  // +20 for until_scn
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("buffer not enough for value", KR(ret), K(value_len), K(sizeof(value_buf)));
    } else {
      MEMCPY(value_buf + pos, item.value_.ptr(), value_len);
      pos += value_len;
      value_buf[pos++] = '|';

      // Append until_scn
      if (OB_FAIL(databuff_printf(value_buf, sizeof(value_buf), pos, "%lu",
                                   item.until_scn_.get_val_for_logservice()))) {
        LOG_WARN("failed to serialize until_scn", KR(ret), K(item));
      } else {
        // Allocate memory from allocator and copy the serialized data
        char *buf = static_cast<char *>(allocator.alloc(pos));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate memory", KR(ret), K(pos));
        } else {
          MEMCPY(buf, value_buf, pos);
          value.assign_ptr(buf, static_cast<int32_t>(pos));
        }
      }
    }
  }
  return ret;
}

int ObLogRestoreSourceMgr::deserialize_from_kv_(const ObString &value, ObLogRestoreSourceItem &item)
{
  int ret = OB_SUCCESS;
  if (value.empty()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("value is empty", KR(ret));
  } else {
    // Simple deserialization: type|value|until_scn
    // Find first '|' for type, last '|' for until_scn
    const char *value_ptr = value.ptr();
    int64_t value_len = value.length();
    int64_t first_pipe = -1;
    int64_t last_pipe = -1;

    for (int64_t i = 0; i < value_len; i++) {
      if (value_ptr[i] == '|') {
        if (first_pipe == -1) {
          first_pipe = i;
        }
        last_pipe = i;
      }
    }

    if (first_pipe == -1 || last_pipe == -1 || first_pipe == last_pipe) {
      ret = OB_INVALID_DATA;
      LOG_WARN("invalid format: missing pipe separators", KR(ret), K(value));
    } else {
      // Parse type
      char type_buf[16];
      int64_t type_len = MIN(first_pipe, sizeof(type_buf) - 1);
      MEMCPY(type_buf, value_ptr, type_len);
      type_buf[type_len] = '\0';
      int type_val = 0;
      if (1 != sscanf(type_buf, "%d", &type_val)) {
        ret = OB_INVALID_DATA;
        LOG_WARN("failed to parse type", KR(ret), K(type_buf));
      } else {
        item.type_ = static_cast<ObLogRestoreSourceType>(type_val);

        // Parse value (between first and last pipe)
        int64_t value_start = first_pipe + 1;
        int64_t value_str_len = last_pipe - value_start;
        if (value_str_len <= 0) {
          ret = OB_INVALID_DATA;
          LOG_WARN("invalid value string length", KR(ret), K(value_str_len));
        } else {
          // Allocate memory for value string
          char *value_buf = static_cast<char*>(item.allocator_.alloc(value_str_len + 1));
          if (OB_ISNULL(value_buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate memory for value", KR(ret), K(value_str_len));
          } else {
            MEMCPY(value_buf, value_ptr + value_start, value_str_len);
            value_buf[value_str_len] = '\0';
            item.value_.assign_ptr(value_buf, static_cast<int32_t>(value_str_len));

            // Parse until_scn
            char scn_buf[32];
            int64_t scn_len = MIN(value_len - last_pipe - 1, sizeof(scn_buf) - 1);
            MEMCPY(scn_buf, value_ptr + last_pipe + 1, scn_len);
            scn_buf[scn_len] = '\0';
            uint64_t scn_val = 0;
            if (1 != sscanf(scn_buf, "%lu", &scn_val)) {
              ret = OB_INVALID_DATA;
              LOG_WARN("failed to parse until_scn", KR(ret), K(scn_buf));
            } else if (OB_FAIL(item.until_scn_.convert_for_logservice(scn_val))) {
              LOG_WARN("failed to convert until_scn", KR(ret), K(scn_val));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObLogRestoreSourceMgr::get_backup_dest(const ObLogRestoreSourceItem &item, ObBackupDest &dest)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(! item.is_valid() || ! is_location_log_source_type(item.type_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(item));
  } else if OB_FAIL(dest.set(item.value_)) {
    LOG_WARN("backup dest set failed", K(ret), K(item));
  }
  return ret;
}
