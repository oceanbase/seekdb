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

#define USING_LOG_PREFIX SQL_RESV

#include "ob_ddl_resolver.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace share;
using namespace obrpc;
namespace sql
{
int ObDDLResolver::resolve_external_file_format(const ParseNode *format_node,
                                                ObResolverParams &params,
                                                ObExternalFileFormat& format,
                                                ObString &format_str)
{
  int ret = OB_SUCCESS;
  bool has_file_format = false;
  if (OB_FAIL(format.csv_format_.init_format(ObDataInFileStruct(),
                                            OB_MAX_COLUMN_NUMBER,
                                            CS_TYPE_UTF8MB4_BIN))) {
    LOG_WARN("failed to init csv format", K(ret));
  }
  // resolve file type and encoding type
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(format_node) || (T_EXTERNAL_FILE_FORMAT != format_node->type_ && T_EXTERNAL_PROPERTIES != format_node->type_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected format node", K(ret), K(format_node->type_));
    }
  }
  ObResolverUtils::FileFormatContext ff_ctx;
  for (int i = 0; OB_SUCC(ret) && i < format_node->num_child_; ++i) {
    if (OB_ISNULL(format_node->children_[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed. get unexpected NULL ptr", K(ret), K(format_node->num_child_));
    } else if (T_EXTERNAL_FILE_FORMAT_TYPE == format_node->children_[i]->type_
                || T_CHARSET == format_node->children_[i]->type_) {
      if (OB_FAIL(ObResolverUtils::resolve_file_format(format_node->children_[i], format, params, ff_ctx))) {
        LOG_WARN("fail to resolve file format", K(ret));
      }
      has_file_format |= (T_EXTERNAL_FILE_FORMAT_TYPE == format_node->children_[i]->type_);
    }
  }
  if (OB_SUCC(ret) && !has_file_format) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "format");
  }
  // resolve other format value
  for (int i = 0; OB_SUCC(ret) && i < format_node->num_child_; ++i) {
    if (OB_ISNULL(format_node->children_[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed. get unexpected NULL ptr", K(ret), K(format_node->num_child_));
    } else if (OB_FAIL(ObResolverUtils::resolve_file_format(format_node->children_[i], format, params, ff_ctx))) {
      LOG_WARN("fail to resolve file format", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    bool is_valid = true;
    if (ObExternalFileFormat::ODPS_FORMAT == format.format_type_ && OB_FAIL(format.odps_format_.encrypt())) {
      LOG_WARN("failed to encrypt odps format", K(ret));
    } else if (OB_FAIL(ObDDLResolver::check_format_valid(format, is_valid))) {
      LOG_WARN("check format valid failed", K(ret));
    } else if (!is_valid) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("file format is not valid", K(ret));
    } else if (OB_FAIL(format.to_string_with_alloc(format_str, *params.allocator_))) {
      LOG_WARN("failed to convert format to string", K(ret));
    }
  }
  return ret;
}

int ObDDLResolver::resolve_external_file_pattern(const ParseNode *option_node,
                                                bool is_external_table,
                                                common::ObIAllocator &allocator,
                                                const ObSQLSessionInfo *session_info,
                                                ObString &pattern)
{
  int ret = OB_SUCCESS;
  if (!is_external_table) {
    ret = OB_NOT_SUPPORTED;
    ObSqlString err_msg;
    err_msg.append_fmt("Using PATTERN as a CREATE TABLE option");
    LOG_USER_ERROR(OB_NOT_SUPPORTED, err_msg.ptr());
    LOG_WARN("using PATTERN as a table option is support in external table only", K(ret));
  } else if (option_node->num_child_ != 1 || OB_ISNULL(option_node->children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected child num", K(option_node->num_child_));
  } else if (0 == option_node->children_[0]->str_len_) {
    ObSqlString err_msg;
    err_msg.append_fmt("empty regular expression");
    ret = OB_ERR_REGEXP_ERROR;
    LOG_USER_ERROR(OB_ERR_REGEXP_ERROR, err_msg.ptr());
    LOG_WARN("empty regular expression", K(ret));
  } else {
    pattern = ObString(option_node->children_[0]->str_len_,
                      option_node->children_[0]->str_value_);
    if (OB_FAIL(ObSQLUtils::convert_sql_text_to_schema_for_storing(allocator,
                                                            session_info->get_dtc_params(),
                                                            pattern))) {
      LOG_WARN("failed to convert pattern to utf8", K(ret));
    }
  }
  return ret;
}

int ObDDLResolver::resolve_external_file_location(ObResolverParams &params,
                                                 ObTableSchema &table_schema,
                                                 common::ObString table_location)
{
  int ret = OB_SUCCESS;
  if (!table_schema.is_external_table()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "location option");
  } else {
    ObString url = table_location;
    const bool is_hdfs_type = url.prefix_match(OB_HDFS_PREFIX);

    ObHDFSStorageInfo hdfs_storage_info;
    ObBackupStorageInfo backup_storage_info;
    ObObjectStorageInfo *storage_info = is_hdfs_type
                                        ? static_cast<ObObjectStorageInfo *>(&hdfs_storage_info)
                                        : static_cast<ObObjectStorageInfo *>(&backup_storage_info);
    char storage_info_buf[OB_MAX_BACKUP_STORAGE_INFO_LENGTH] = { 0 };
    ObString path = url.split_on('?');
    if (OB_FAIL(ret)) {
      /* do nothing */
    } else if (path.empty()) {
      // url like: oss://ak:sk@host/bucket/...
      ObSqlString tmp_location;
      ObSqlString prefix;

      if (OB_FAIL(resolve_file_prefix(url, prefix, storage_info->device_type_, params))) {
        LOG_WARN("failed to resolve file prefix", K(ret));
      } else if (OB_FAIL(tmp_location.append(prefix.string()))) {
        LOG_WARN("failed to append prefix", K(ret));
      } else {
        url = url.trim_space_only();
      }

      if (OB_SUCC(ret)) {
        if (OB_STORAGE_FILE != storage_info->device_type_  &&
            OB_STORAGE_HDFS != storage_info->device_type_ /* hdfs with simple auth*/) {
          if (OB_FAIL(ObSQLUtils::split_remote_object_storage_url(url, storage_info))) {
            LOG_WARN("failed to split remote object storage url", K(ret));
          }
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(tmp_location.append(url))) {
          LOG_WARN("failed to append url", K(ret));
        } else if (OB_FAIL(storage_info->get_storage_info_str(storage_info_buf, sizeof(storage_info_buf)))) {
          LOG_WARN("failed to get storage info str", K(ret));
        } else if (OB_FAIL(table_schema.set_external_file_location(tmp_location.string()))) {
          LOG_WARN("failed to set external file location", K(ret));
        } else if (OB_FAIL(table_schema.set_external_file_location_access_info(storage_info_buf))) {
          LOG_WARN("failed to set external file location access info", K(ret));
        }
      }
    } else {
      // url like: oss://bucket/...?host=xxxx&access_id=xxx&access_key=xxx
      ObString uri_cstr;
      ObString storage_info_cstr;
      if (OB_FAIL(ob_write_string(*params.allocator_, path, uri_cstr, true))) {
        LOG_WARN("failed to write string", K(ret));
      } else if (OB_FAIL(ob_write_string(*params.allocator_, url, storage_info_cstr, true))) {
        LOG_WARN("failed to write string", K(ret));
      } else if (OB_FAIL(storage_info->set(uri_cstr.ptr(), storage_info_cstr.ptr()))) {
        LOG_WARN("failed to set storage info", K(ret));
      } else if (OB_FAIL(storage_info->get_storage_info_str(storage_info_buf, sizeof(storage_info_buf)))) {
        LOG_WARN("failed to get storage info str", K(ret));
      } else if (OB_FAIL(table_schema.set_external_file_location(path))) {
        LOG_WARN("failed to set external file location", K(ret));
      } else if (OB_FAIL(table_schema.set_external_file_location_access_info(storage_info_buf))) {
        LOG_WARN("failed to set external file location access info", K(ret));
      }
    }
  }
  return ret;
}

int ObDDLResolver::resolve_external_file_location_object(ObResolverParams &params,
                                                         ObTableSchema &table_schema,
                                                         common::ObString location_obj,
                                                         common::ObString sub_path)
{
  int ret = OB_SUCCESS;
  if (!table_schema.is_external_table()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "location option");
  } else {
    const uint64_t tenant_id = params.session_info_->get_effective_tenant_id();
    ObSchemaGetterGuard *schema_guard = NULL;
    const ObLocationSchema *schema_ptr = NULL;
    if (OB_ISNULL(params.schema_checker_)
        || NULL == (schema_guard = params.schema_checker_->get_schema_guard())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema checker or schema guard is null", K(ret));
    } else if (OB_FAIL(schema_guard->get_location_schema_by_name(tenant_id, location_obj, schema_ptr))) {
      LOG_WARN("failed to get schema by location name", K(ret), K(tenant_id), K(location_obj));
    } else if (OB_ISNULL(schema_ptr)) {
      ret = OB_LOCATION_OBJ_NOT_EXIST;
      LOG_WARN("location object does't exist", K(ret), K(tenant_id), K(location_obj));
    } else {
      table_schema.set_external_location_id(schema_ptr->get_location_id());
      OZ (table_schema.set_external_sub_path(sub_path));
    }
  }
  return ret;
}
}
}
