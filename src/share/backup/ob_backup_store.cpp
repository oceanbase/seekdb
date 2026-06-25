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
#include "ob_backup_store.h"
#include "share/backup/ob_backup_io_adapter.h"
#include "share/backup/ob_backup_path.h"

using namespace oceanbase;
using namespace common;
using namespace share;

static const char *type_strs[] = {
    "backup_data",
    "archive_log",
    "backup_key",
    "restore_data",
    "restore_log",
};

const char *ObBackupDestType::get_str(const TYPE &type)
{
  const char *str = nullptr;

  if (type < 0 || type >= TYPE::DEST_TYPE_MAX) {
    str = "UNKNOWN";
  } else {
    str = type_strs[type];
  }
  return str;
}

ObBackupDestType::TYPE ObBackupDestType::get_type(const char *type_str)
{
  ObBackupDestType::TYPE type = ObBackupDestType::TYPE::DEST_TYPE_MAX;

  const int64_t count = ARRAYSIZEOF(type_strs);
  STATIC_ASSERT(static_cast<int64_t>(ObBackupDestType::TYPE::DEST_TYPE_MAX) == count, "type count mismatch");
  for (int64_t i = 0; i < count; ++i) {
    if (0 == strcmp(type_str, type_strs[i])) {
      type = static_cast<ObBackupDestType::TYPE>(i);
      break;
    }
  }
  return type;
}
/**
 * ------------------------------ObBackupFormatDesc---------------------
 */
OB_SERIALIZE_MEMBER(ObBackupFormatDesc, cluster_name_, tenant_name_, path_,
    cluster_id_, tenant_id_, incarnation_, dest_id_, dest_type_, ts_);

ObBackupFormatDesc::ObBackupFormatDesc()
{
  cluster_id_ = 0;
  tenant_id_ = OB_INVALID_TENANT_ID;
  incarnation_ = OB_START_INCARNATION;
  dest_id_ = 0;
  dest_type_ = ObBackupDestType::TYPE::DEST_TYPE_MAX;
  ts_ = ObTimeUtility::current_time();
}

bool ObBackupFormatDesc::is_valid() const
{
  return !cluster_name_.is_empty()
         && !tenant_name_.is_empty()
         && !path_.is_empty()
         && OB_INVALID_TENANT_ID != tenant_id_
         && OB_START_INCARNATION <= incarnation_
         && 0 < dest_id_
         && 0 <= ts_;
}

uint16_t ObBackupFormatDesc::get_data_type() const
{
  return ObBackupFileType::BACKUP_FORMAT_FILE;
}

uint16_t ObBackupFormatDesc::get_data_version() const
{
  return FILE_VERSION;
}

bool ObBackupFormatDesc::is_format_equal(const ObBackupFormatDesc &desc) const
{
  return path_ == desc.path_
      && cluster_id_ == desc.cluster_id_
      && tenant_id_ == desc.tenant_id_
      && incarnation_ == desc.incarnation_
      && dest_id_ == desc.dest_id_
      && dest_type_ == desc.dest_type_;
}

// ------------------------------ObBackupCheckDesc---------------------
OB_SERIALIZE_MEMBER(ObBackupCheckDesc, cluster_name_, tenant_name_, path_,
    cluster_id_, tenant_id_, incarnation_, ts_);

ObBackupCheckDesc::ObBackupCheckDesc()
{
  cluster_id_ = 0;
  tenant_id_ = OB_INVALID_TENANT_ID;
  incarnation_ = OB_START_INCARNATION;
  ts_ = ObTimeUtility::current_time();
}

bool ObBackupCheckDesc::is_valid() const
{
  return !cluster_name_.is_empty()
         && !tenant_name_.is_empty()
         && !path_.is_empty()
         && OB_INVALID_TENANT_ID != tenant_id_
         && OB_START_INCARNATION <= incarnation_
         && 0 <= ts_;
}

uint16_t ObBackupCheckDesc::get_data_type() const
{
  return ObBackupFileType::BACKUP_CHECK_FILE;
}

uint16_t ObBackupCheckDesc::get_data_version() const
{
  return FILE_VERSION;
}

/**
 * ------------------------------ObBackupStore---------------------
 */
ObBackupStore::ObBackupStore() 
  : is_inited_(false) 
{}


int ObBackupStore::init(const char *backup_dest)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObBackupStore init twice.", K(ret));
  } else if (OB_ISNULL(backup_dest)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid archive dest.", K(ret), K(backup_dest));
  } else if (OB_FAIL(backup_dest_.set(backup_dest))) {
    LOG_WARN("failed to set archive dest", K(ret), K(backup_dest));
  } else {
    is_inited_ = true;
  }

  return ret;
}

int ObBackupStore::init(const share::ObBackupDest &backup_dest)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObBackupStore init twice.", K(ret));
  } else if (!backup_dest.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid archive dest.", K(ret), K(backup_dest));
  } else if (OB_FAIL(backup_dest_.deep_copy(backup_dest))) {
    LOG_WARN("failed to set archive dest", K(ret), K(backup_dest));
  } else {
    is_inited_ = true;
  }

  return ret;
}

bool ObBackupStore::is_init() const
{
  return IS_INIT;
}

void ObBackupStore::reset()
{
  is_inited_ = false;
  backup_dest_.reset();
}

const ObBackupDest &ObBackupStore::get_backup_dest() const
{
  return backup_dest_;
}

const ObBackupStorageInfo *ObBackupStore::get_storage_info() const
{
  return backup_dest_.get_storage_info();
}

// oss://archive/format
int ObBackupStore::get_format_file_path(ObBackupPathString &path) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  ObBackupPath format_path;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObBackupStore not init", K(ret));
  } else if (OB_FAIL(format_path.init(backup_dest_.get_root_path()))) {
    LOG_WARN("failed to get format path", K(ret));
  } else if (OB_FAIL(format_path.join(OB_STR_FORMAT_FILE_NAME, ObBackupFileSuffix::BACKUP))) {
    LOG_WARN("failed to assign format path", K(ret), K(format_path));
  } else if (OB_FAIL(databuff_printf(path.ptr(), path.capacity(), pos, "%s", format_path.get_ptr()))) {
    LOG_WARN("failed to assign format file name", K(ret), K(path));
  }
  return ret;
}

int ObBackupStore::is_format_file_exist(bool &is_exist) const
{
  int ret = OB_SUCCESS;
  ObBackupIoAdapter util;
  ObBackupPathString full_path;
  const ObBackupStorageInfo *storage_info = get_storage_info();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObBackupStore not init", K(ret));
  } else if (OB_FAIL(get_format_file_path(full_path))) {
    LOG_WARN("failed to get format file path", K(ret));
  } else if (OB_FAIL(util.is_exist(full_path.ptr(), storage_info, is_exist))) {
    LOG_WARN("failed to check format file exist.", K(ret), K(full_path));
  } 

  return ret;
}

int ObBackupStore::dest_is_empty_directory(bool &is_empty) const
{
  int ret = OB_SUCCESS;
  ObBackupIoAdapter util;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObBackupStore not init", K(ret));
  } else if (OB_FAIL(util.is_empty_directory(backup_dest_.get_root_path(), get_storage_info(), is_empty))) {
    LOG_WARN("fail to init store", K(ret), K_(backup_dest));
  }
  return ret;
}

int ObBackupStore::read_format_file(ObBackupFormatDesc &desc) const
{
  int ret = OB_SUCCESS;
  ObBackupPathString full_path;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObBackupStore not init", K(ret));
  } else if (OB_FAIL(get_format_file_path(full_path))) {
    LOG_WARN("failed to get format file path", K(ret));
  } else if (OB_FAIL(read_single_file(full_path, desc))) {
    LOG_WARN("failed to read single file", K(ret), K(full_path));
  }
  return ret;
}

int ObBackupStore::write_format_file(const ObBackupFormatDesc &desc) const
{
  int ret = OB_SUCCESS;
  ObBackupPathString full_path;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObBackupStore not init", K(ret));
  } else if (OB_FAIL(get_format_file_path(full_path))) {
    LOG_WARN("failed to get format file path", K(ret));
  } else if (OB_FAIL(write_single_file(full_path, desc))) {
    LOG_WARN("failed to write single file", K(ret), K(full_path));
  }
  return ret;
}

int ObBackupStore::write_check_file(const ObBackupPathString &full_path, const ObBackupCheckDesc &desc) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObBackupStore not init", K(ret));
  } else if (OB_FAIL(write_single_file(full_path, desc))) {
    LOG_WARN("failed to write single file", K(ret), K(full_path));
  }
  return ret; 
}

int ObBackupStore::read_check_file(const ObBackupPathString &full_path, ObBackupCheckDesc &desc) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObBackupStore not init", K(ret));
  } else if (OB_FAIL(read_single_file(full_path, desc))) {
    LOG_WARN("failed to write single file", K(ret), K(full_path));
  }
  return ret; 
}

int ObBackupStore::write_single_file(const ObBackupPathString &full_path, const ObIBackupSerializeProvider &serializer) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  char *buf = nullptr;
  int64_t buf_size = 0;
  ObArenaAllocator allocator;
  ObBackupIoAdapter util;
  // wrapper with common header.
  ObBackupSerializeHeaderWrapper serializer_wrapper(&(const_cast<ObIBackupSerializeProvider &>(serializer)));
  const ObBackupStorageInfo *storage_info = get_storage_info();
  buf_size = serializer_wrapper.get_serialize_size();
  if (OB_ISNULL(buf = reinterpret_cast<char*>(allocator.alloc(buf_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc buf", K(ret), K(buf_size), K(full_path), K(serializer));
  } else if (OB_FAIL(serializer_wrapper.serialize(buf, buf_size, pos))) {
    LOG_WARN("failed to serialize file.", K(ret), K(buf_size), K(full_path), K(serializer));
  } else if (pos != buf_size) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("serialized size not match.", K(ret), K(pos), K(buf_size), K(full_path), K(serializer));
  } else if (OB_FAIL(util.mk_parent_dir(full_path.str(), storage_info))) {
    LOG_WARN("failed to mk dir.", K(ret), K(full_path), K(serializer));
  // TODO(yangyi.yyy & zhaoyongheng.zyh) use valid dest_id for QoS, including ObBackupDataStore & ObArchiveStore
  } else if (OB_FAIL(util.write_single_file(full_path.str(), storage_info, buf, buf_size,
                                            ObStorageIdMod::get_default_backup_id_mod()))) {
    LOG_WARN("failed to write single file.", K(ret), K(full_path), K(serializer));
  } else {
    FLOG_INFO("succeed to write single file.", K(full_path), K(serializer));
  }

  return ret;
}

int ObBackupStore::read_single_file(const ObBackupPathString &full_path, ObIBackupSerializeProvider &serializer) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t file_length = 0;
  char *buf = nullptr;
  int64_t read_size = 0;
  ObArenaAllocator allocator;
  ObBackupIoAdapter util;
  // wrapper with common header.
  ObBackupSerializeHeaderWrapper serializer_wrapper(&serializer);
  const ObBackupStorageInfo *storage_info = get_storage_info();

  if (OB_FAIL(util.get_file_length(full_path.ptr(), storage_info, file_length))) {
    if (OB_OBJECT_NOT_EXIST != ret) {
      LOG_WARN("failed to get file length.", K(ret), K(full_path));
    } else {
      LOG_INFO("file not exist.", K(ret), K(full_path));
    }
  } else if (0 == file_length) {
    ret = OB_ERR_UNEXPECTED;
    LOG_INFO("file is empty.", K(ret), K(full_path));
  } else if (OB_ISNULL(buf = reinterpret_cast<char*>(allocator.alloc(file_length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc buf", K(ret), K(full_path), K(file_length));
  // TODO(yangyi.yyy & zhaoyongheng.zyh) use valid dest_id for QoS, including ObBackupDataStore & ObArchiveStore
  } else if (OB_FAIL(util.read_single_file(full_path.str(), storage_info, buf, file_length, read_size,
                                           ObStorageIdMod::get_default_backup_id_mod()))) {
    LOG_WARN("failed to read file.", K(ret), K(full_path), K(file_length));
  } else if (file_length != read_size) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("read file length not match.", K(ret), K(full_path), K(file_length), K(read_size));
  } else if (OB_FAIL(serializer_wrapper.deserialize(buf, file_length, pos))) {
    LOG_WARN("failed to deserialize.", K(ret), K(full_path), K(file_length));
  } else {
    FLOG_INFO("succeed to read single file.", K(full_path), K(serializer));
  }

  return ret;
}
