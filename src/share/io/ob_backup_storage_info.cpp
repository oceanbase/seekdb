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
#include "share/io/ob_backup_storage_info.h"
#include "lib/utility/ob_print_utils.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace share
{

//***********************ObBackupStorageInfo***************************
ObBackupStorageInfo::~ObBackupStorageInfo()
{
  reset();
}

int ObBackupStorageInfo::set(
    const common::ObStorageType device_type,
    const char *endpoint,
    const char *authorization,
    const char *extension)
{
  int ret = OB_SUCCESS;
  char storage_info[OB_MAX_BACKUP_STORAGE_INFO_LENGTH] = { 0 };
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("storage info init twice", K(ret));
  } else if (OB_ISNULL(endpoint)
      || OB_ISNULL(authorization) || OB_ISNULL(extension) || OB_STORAGE_MAX_TYPE == device_type) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("invalid args", K(ret), KP(endpoint), KP(authorization), KP(extension), K(device_type));
  } else if (0 != strlen(endpoint)
      && OB_FAIL(set_storage_info_field_(endpoint, storage_info, sizeof(storage_info)))) {
    LOG_WARN("failed to set storage info", K(ret));
  } else if (0 != strlen(authorization)
      && OB_FAIL(set_storage_info_field_(authorization, storage_info, sizeof(storage_info)))) {
    LOG_WARN("failed to set storage info", K(ret));
  } else if (0 != strlen(extension)
      && OB_FAIL(set_storage_info_field_(extension, storage_info, sizeof(storage_info)))) {
    LOG_WARN("failed to set storage info", K(ret));
  } else if (OB_FAIL(set(device_type, storage_info))) {
    LOG_WARN("failed to set storage info", K(ret), KPC(this));
  }
  return ret;
}

int ObBackupStorageInfo::get_authorization_info(char *authorization, const int64_t length) const
{
  int ret = OB_SUCCESS;
  const int64_t key_len = MAX(OB_MAX_BACKUP_SERIALIZEKEY_LENGTH, OB_MAX_BACKUP_ACCESSKEY_LENGTH);
  char access_key_buf[key_len] = { 0 };
  STATIC_ASSERT(OB_MAX_BACKUP_AUTHORIZATION_LENGTH > (OB_MAX_BACKUP_ACCESSID_LENGTH + key_len), "array length overflow");
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("storage info not init", K(ret));
  } else if (OB_ISNULL(authorization) || length <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(authorization), K(length));
  } else if (OB_STORAGE_FILE == device_type_) {
    // do nothing
  } else if (!is_assume_role_mode_) {
    // access by ak/sk mode
    if (OB_FAIL(get_access_key_(access_key_buf, sizeof(access_key_buf)))) {
      LOG_WARN("failed to get access key", K(ret));
    } else if (OB_FAIL(databuff_printf(authorization, length, "%s&%s", access_id_, access_key_buf))) {
      LOG_WARN("failed to set authorization", K(ret), K(length), K_(access_id), K(strlen(access_key_buf)));
    }
  } else {
    // access by assume role mode
    int64_t pos = 0;
    if (OB_FAIL(databuff_printf(authorization, length, pos, "%s", role_arn_))) {
      LOG_WARN("failed to set authorization", K(ret), K(length), KP_(role_arn));
    } else if (external_id_[0] != '\0') {
      if (OB_FAIL(databuff_printf(authorization, length, pos, "&%s", external_id_))) {
        LOG_WARN("failed to set authorization", K(ret), K(length), KP_(external_id));
      }
    }
  }

  return ret;
}

int ObBackupStorageInfo::get_unencrypted_authorization_info(
    char *authorization, const int64_t length) const
{
  int ret = OB_SUCCESS;
  const int64_t key_len = MAX(OB_MAX_BACKUP_SERIALIZEKEY_LENGTH, OB_MAX_BACKUP_ACCESSKEY_LENGTH);
  char access_key_buf[key_len] = { 0 };
  STATIC_ASSERT(OB_MAX_BACKUP_AUTHORIZATION_LENGTH > (OB_MAX_BACKUP_ACCESSID_LENGTH + key_len), "array length overflow");
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("storage info not init", K(ret));
  } else if (OB_ISNULL(authorization) || length <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(authorization), K(length));
  } else if (OB_STORAGE_FILE == device_type_) {
    // do nothing
  } else if (OB_FAIL(databuff_printf(authorization, length, "%s&%s",  access_id_, access_key_))) {
    LOG_WARN("failed to set authorization", K(ret), K(length), K_(access_id), K(strlen(access_key_)));
  }

  return ret;
}

int ObBackupStorageInfo::set_endpoint(const common::ObStorageType device_type, const char *storage_info) 
{
  int ret = OB_SUCCESS;
  bool has_needed_extension = false;

  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("storage info init twice", K(ret));
  } else if (OB_ISNULL(storage_info) || strlen(storage_info) >= OB_MAX_BACKUP_STORAGE_INFO_LENGTH) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("storage info is invalid", K(ret), KP(storage_info));
  } else if (FALSE_IT(device_type_ = device_type)) {
  } else if (OB_STORAGE_FILE == device_type_){
    //don't need endpoint
  } else if (OB_UNLIKELY(0 == strlen(storage_info))) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("storage info is empty", K(ret), K_(device_type));
  } else if (OB_FAIL(parse_storage_info_(storage_info, has_needed_extension))) {
    LOG_WARN("parse storage info failed", K(ret), KP(storage_info), K_(device_type));
  } else if (OB_UNLIKELY(0 == strlen(endpoint_))) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("backup device is not nfs, endpoint do not allow to be empty", K(ret),K_(device_type), K_(endpoint));
  } 

  return ret;
}

ObBackupDest::ObBackupDest()
  : root_path_(NULL),
    storage_info_(NULL),
    allocator_("ObBackupDest")
{
}

ObBackupDest::~ObBackupDest()
{
  reset();
}

bool ObBackupDest::is_valid() const
{
  return NULL != root_path_ && NULL != storage_info_;
}

void ObBackupDest::reset()
{
  allocator_.reset();
  root_path_ = NULL;
  storage_info_ = NULL;
}

bool ObBackupDest::operator ==(const ObBackupDest &backup_dest) const
{
  bool is_equal = true;
  is_equal = is_root_path_equal(backup_dest);
  if (!is_equal) {
    // do nothing
  } else if ((OB_ISNULL(storage_info_) && !OB_ISNULL(backup_dest.storage_info_))
      || (!OB_ISNULL(storage_info_) && OB_ISNULL(backup_dest.storage_info_))) {
    is_equal = false;
  } else if (!OB_ISNULL(storage_info_) && !OB_ISNULL(backup_dest.storage_info_)) {
    if (*storage_info_ != *(backup_dest.storage_info_)) {
      is_equal = false;
    }
  }
  return is_equal;
}

bool ObBackupDest::operator !=(const ObBackupDest &backup_dest) const
{
  return !(*this == backup_dest);
}

int ObBackupDest::deep_copy(const ObBackupDest &backup_dest)
{
  reset();
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  char *backup_dest_str = NULL;
  if (OB_ISNULL(backup_dest_str = reinterpret_cast<char *>(allocator.alloc(share::OB_MAX_BACKUP_DEST_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", KR(ret));
  } else if (OB_FAIL(backup_dest.get_backup_dest_str(backup_dest_str, share::OB_MAX_BACKUP_DEST_LENGTH))) {
    LOG_WARN("failed to get backup dest str", K(ret));
  } else if (OB_FAIL(set(backup_dest_str))) {
    LOG_WARN("failed to set backup dest", K(ret));
  }
  return ret;
}

int64_t ObBackupDest::hash() const
{
  int64_t hash_val = 0;
  if (is_valid()) {
    hash_val = murmurhash(root_path_, static_cast<int32_t>(strlen(root_path_)), hash_val);
    hash_val += storage_info_->hash();
  }
  return hash_val;
}

int ObBackupDest::alloc_and_init()
{
  int ret = OB_SUCCESS;
  void *raw_ptr = NULL;
  if (is_valid()) {
    // do nothing
  } else if (OB_ISNULL(root_path_ = static_cast<char *>(allocator_.alloc(OB_MAX_BACKUP_PATH_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc root_path memory", K(ret));
  } else if (OB_ISNULL(raw_ptr = allocator_.alloc(sizeof(ObBackupStorageInfo)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc storage_info memory", K(ret));
  } else {
    storage_info_ = new (raw_ptr) ObBackupStorageInfo();
    MEMSET(root_path_, 0, OB_MAX_BACKUP_PATH_LENGTH);
  }
  return ret;
}


int ObBackupDest::parse_backup_dest_str_(const char *backup_dest, const bool only_parse_for_unique_path)
{
  int ret = OB_SUCCESS;
  ObString bakup_dest_str(backup_dest);
  common::ObStorageType type;
  int64_t pos = 0;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("backup_dest not init", K(ret), K(backup_dest));
  } else if (OB_FAIL(get_storage_type_from_path(bakup_dest_str, type))) {
    LOG_WARN("failed to get storage type", K(ret));
  } else {
    // file:///root_backup_dir"
    while (backup_dest[pos] != '\0') {
      if ('?' == backup_dest[pos]) {
        break;
      }
      ++pos;
    }

    if (pos >= OB_MAX_BACKUP_PATH_LENGTH) {
      ret = OB_INVALID_BACKUP_DEST;
      LOG_ERROR("backup dest is too long, cannot work", K(ret), K(pos), K(backup_dest));
    } else {
      MEMCPY(root_path_, backup_dest, pos);
      root_path_[pos] = '\0';
      if ('?' == backup_dest[pos]) {
        ++pos;
      }
      if (!only_parse_for_unique_path) {
        if (OB_FAIL(storage_info_->set(type, backup_dest + pos))) {
          LOG_WARN("failed to init storage_info", K(ret), K(type), K(pos), K(backup_dest));
        }
      } else {
        if (OB_FAIL(storage_info_->set_endpoint(type, backup_dest + pos))) {
          LOG_WARN("failed to set endpoint", K(ret), K(type), K(pos), K(backup_dest));
        }
      } 
    }
  }
  return ret;
}

int ObBackupDest::set(const char *backup_dest)
{
  int ret = OB_SUCCESS;
  reset();
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot init twice", K(ret), K(*this));
  } else if (OB_ISNULL(backup_dest) || strlen(backup_dest) >= OB_MAX_BACKUP_DEST_LENGTH) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("invalid args", K(ret), KP(backup_dest));
  } else if (OB_FAIL(alloc_and_init())) {
    LOG_WARN("failed to alloc and init backup dest", K(ret));
  } else if (OB_FAIL(parse_backup_dest_str_(backup_dest, false/*only_parse_for_unique_path*/))) {
    LOG_WARN("failed to parse backup dest str", K(ret), K(backup_dest));
  } else {
    root_path_trim_();
  }
  return ret;
}

int ObBackupDest::set(const common::ObString &backup_dest)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  char *backup_dest_str = NULL;
  if (OB_ISNULL(backup_dest_str = reinterpret_cast<char *>(allocator.alloc(backup_dest.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", KR(ret));
  } else {
    MEMCPY(backup_dest_str, backup_dest.ptr(), backup_dest.length());
    backup_dest_str[backup_dest.length()] = '\0';
    if (OB_FAIL(set(backup_dest_str))) {
      LOG_WARN("failed to set backup dest", KR(ret), K(backup_dest));
    }
  }
  return ret;
}

int ObBackupDest::set(const ObBackupPathString &backup_dest_str)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(set(backup_dest_str.ptr()))) {
    LOG_WARN("failed to set backup dest", KR(ret), K(backup_dest_str));
  }
  return ret;
}

int ObBackupDest::set(
    const char *path,
    const char *endpoint,
    const char *authorization,
    const char *extension)
{
  int ret = OB_SUCCESS;
  reset();
  common::ObStorageType type;
  ObString root_path_str(path);
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot init twice", K(ret), K(*this));
  } else if (OB_ISNULL(path) || OB_ISNULL(endpoint) || OB_ISNULL(authorization) || OB_ISNULL(extension)) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("invalid args", K(ret), KP(path), KP(endpoint));
  } else if (OB_FAIL(alloc_and_init())) {
    LOG_WARN("failed to alloc and init backup dest", K(ret));
  } else if (OB_FAIL(get_storage_type_from_path(root_path_str, type))) {
    LOG_WARN("failed to get storage type", K(ret));
  } else if (OB_FAIL(databuff_printf(root_path_, OB_MAX_BACKUP_PATH_LENGTH, "%s", path))) {
    LOG_WARN("failed to set root path", K(ret), K(path), K(strlen(path)));
  } else if (OB_FAIL(storage_info_->set(type, endpoint, authorization, extension))) {
    LOG_WARN("failed to set storage info", K(ret), K(endpoint), K(authorization), K(extension));
  } else {
    root_path_trim_();
  }
  return ret;
}

int ObBackupDest::set(const char *root_path, const char *storage_info)
{
  int ret = OB_SUCCESS;
  reset();
  common::ObStorageType type;
  char storage_info_str[OB_MAX_BACKUP_STORAGE_INFO_LENGTH] = { 0 };
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot init twice", K(ret), K(*this));
  } else if (OB_ISNULL(root_path) || OB_ISNULL(storage_info)) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("invalid args", K(ret));
  } else if (OB_FAIL(alloc_and_init())) {
    LOG_WARN("failed to alloc and init backup dest", K(ret));
  } else if (OB_FAIL(get_storage_type_from_path(root_path, type))) {
    LOG_WARN("failed to get storage type", K(ret));
  } else if (OB_FAIL(databuff_printf(root_path_, OB_MAX_BACKUP_PATH_LENGTH, "%s", root_path))) {
    LOG_WARN("failed to set root path", K(ret), K(root_path));
  } else if (OB_FAIL(storage_info_->set(type, storage_info))) {
    LOG_WARN("failed to set storage info", K(ret));
  } else {
    root_path_trim_();
  }
  return ret;
}

int ObBackupDest::set(const char *root_path, const ObBackupStorageInfo *storage_info)
{
  int ret = OB_SUCCESS;
  reset();
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot init twice", K(ret), K(*this));
  } else if (OB_ISNULL(root_path) || OB_ISNULL(storage_info) || !storage_info->is_valid()) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("invalid args", K(ret), K(root_path), K(storage_info));
  } else if (OB_FAIL(alloc_and_init())) {
    LOG_WARN("failed to alloc and init backup dest", K(ret));
  } else if (OB_FAIL(databuff_printf(root_path_, OB_MAX_BACKUP_PATH_LENGTH, "%s", root_path))) {
    LOG_WARN("failed to set root path", K(ret), K(root_path));
  } else if (OB_FAIL(storage_info_->assign(*storage_info))) {
    LOG_WARN("failed to set storage info", K(ret));
  } else {
    root_path_trim_();
  }
  return ret;
}

// check if backup_dest contains "encrypt_key=" then set
int ObBackupDest::set_without_decryption(const common::ObString &backup_dest) {
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  char *backup_dest_str = nullptr;
  char *result = nullptr;
  if (OB_ISNULL(backup_dest_str = reinterpret_cast<char *>(allocator.alloc(backup_dest.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed" ,KR(ret));
  } else {
    MEMCPY(backup_dest_str, backup_dest.ptr(), backup_dest.length());
    backup_dest_str[backup_dest.length()] = '\0';
    if (OB_NOT_NULL(result = strstr(backup_dest_str, ENCRYPT_KEY))) {
      ret = OB_INVALID_BACKUP_DEST;
      LOG_WARN("backup destination should not contain encrypt_key", K(ret), K(backup_dest_str));
      LOG_USER_ERROR(OB_INVALID_BACKUP_DEST, "backup destination contains encrypt_key, which");
    } else if (OB_FAIL(set(backup_dest_str))) {
      LOG_WARN("fail to set backup dest", K(ret));
    }
  }
  return ret;
}

// file:///root_backup_dir" -> root_path=file:///root_backup_dir
int ObBackupDest::set_storage_path(const common::ObString &storage_path_str) 
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  char *backup_dest_str = NULL;
  reset();
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot init twice", K(ret), K(*this));
  } else if (storage_path_str.empty() || storage_path_str.length() >= OB_MAX_BACKUP_PATH_LENGTH) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("storage path is empty", K(ret), K(storage_path_str));
  } else if (OB_FAIL(alloc_and_init())) {
    LOG_WARN("failed to alloc and init backup dest", K(ret));
  } else if (OB_ISNULL(backup_dest_str = reinterpret_cast<char *>(allocator.alloc(storage_path_str.length()+1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", KR(ret));
  } else {
    MEMCPY(backup_dest_str, storage_path_str.ptr(), storage_path_str.length());
    backup_dest_str[storage_path_str.length()] = '\0';
    if (OB_FAIL(parse_backup_dest_str_(backup_dest_str, true/*only_parse_for_unique_path*/))) {
      LOG_WARN("failed to parse backup dest str", K(ret), K(backup_dest_str));
    } else {
      root_path_trim_();
    }
  }

  return ret;
}

int ObBackupDest::reset_access_id_and_access_key(
    const char *access_id, const char *access_key)
{
  int ret = OB_SUCCESS;
  char current_authorization[OB_MAX_BACKUP_AUTHORIZATION_LENGTH] = { 0 };
  char new_authorization[OB_MAX_BACKUP_AUTHORIZATION_LENGTH] = { 0 };
  int64_t pos = 0;
  if (OB_ISNULL(storage_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("storage info is null", K(ret));
  } else if (OB_FAIL(databuff_printf(new_authorization, OB_MAX_BACKUP_AUTHORIZATION_LENGTH, pos, "%s%s&%s%s", 
                ACCESS_ID, access_id, ACCESS_KEY, access_key))) {
    LOG_WARN("failed to print authorization", K(ret), KCSTRING(access_id));
  } else if (OB_FAIL(storage_info_->get_authorization_info(current_authorization, sizeof(current_authorization)))) {
    LOG_WARN("fail to set authorization", K(ret));
  } else if (OB_FAIL(storage_info_->reset_access_id_and_access_key(access_id, access_key))) {
    LOG_WARN("failed to reset access id and access key", K(ret), KCSTRING(access_id));
  } else {
    LOG_INFO("reset access id and access key", KCSTRING(access_id));
  }
  return ret;
}

void ObBackupDest::root_path_trim_()
{
  int len = static_cast<int32_t>(strlen(root_path_));
  for (int i = len - 1; i >=0 ; i--) {
    if (root_path_[i] == '/') {
      root_path_[i] = '\0';
    } else {
      break;
    }
  }
}

bool ObBackupDest::is_root_path_equal(const ObBackupDest &backup_dest) const
{
  bool is_equal = true;
  if ((OB_ISNULL(root_path_) && !OB_ISNULL(backup_dest.root_path_))
      || (!OB_ISNULL(root_path_) && OB_ISNULL(backup_dest.root_path_))) {
    is_equal = false;
  } else if (!OB_ISNULL(root_path_) && !OB_ISNULL(backup_dest.root_path_)) {
    if (strlen(root_path_) != strlen(backup_dest.root_path_)) {
      is_equal = false;
    } else if (0 != STRCMP(root_path_, backup_dest.root_path_)) {
      is_equal = false;
    }
  }
  return is_equal;
}

int ObBackupDest::is_backup_path_equal(const ObBackupDest &backup_dest, bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  if (!is_valid() || !backup_dest.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("backup dest is valid", K(ret), K(*this), K(backup_dest));
  } else if(0 != STRCMP(root_path_, backup_dest.root_path_)) {
    is_equal = false;
  } else if (0 != STRCMP(storage_info_->endpoint_, backup_dest.storage_info_->endpoint_)) {
    is_equal = false;
  }
  return ret;
}

// backup_path = root_path + host
int ObBackupDest::get_backup_path_str(char *buf, const int64_t buf_size) const
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("backup dest is not init", K(ret), K(*this));
  } else if (OB_ISNULL(buf) || buf_size < share::OB_MAX_BACKUP_DEST_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get backup dest str get invalid argument", K(ret), KP(buf), K(buf_size));
  } else if (OB_FAIL(databuff_printf(buf, buf_size, "%s", root_path_))) {
    LOG_WARN("failed to set backup path", K(ret), K(root_path_), K(sizeof(root_path_)));
  } else if (ObStorageType::OB_STORAGE_FILE != storage_info_->device_type_) {
    const int64_t str_len = strlen(buf);
    if (OB_FAIL(databuff_printf(buf + str_len, buf_size - str_len, "?%s", storage_info_->endpoint_))) {
      LOG_WARN("failed to set backup path", K(ret), K(storage_info_->endpoint_));
    }
  }
  return ret;
}

// backup_dest access_key encrypt
int ObBackupDest::get_backup_dest_str(char *buf, const int64_t buf_size) const
{
  int ret = OB_SUCCESS;
  char storage_info_str[OB_MAX_BACKUP_STORAGE_INFO_LENGTH] = { 0 };
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("backup dest is not init", K(ret), K(*this));
  } else if (OB_ISNULL(buf) || buf_size < share::OB_MAX_BACKUP_DEST_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(buf), K(buf_size));
  } else if (OB_FAIL(databuff_printf(buf, buf_size, "%s", root_path_))) {
    LOG_WARN("failed to get backup dest str", K(ret), K(root_path_), K(storage_info_));
  } else if (OB_FAIL(storage_info_->get_storage_info_str(storage_info_str, sizeof(storage_info_str)))) {
    OB_LOG(WARN, "fail to get storage info str!", K(ret), K(storage_info_));
  } else if (0 != strlen(storage_info_str) && OB_FAIL(databuff_printf(buf + strlen(buf), buf_size - strlen(buf), "?%s",storage_info_str))) {
    LOG_WARN("failed to get backup dest str", K(ret), K(root_path_), K(storage_info_));
  }

  return ret;
}


int64_t ObBackupDest::to_string(char *buf, int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0 || !is_valid()) {
    // do nothing
  } else {
    J_OBJ_START();
    ObString root_path(root_path_);
    J_KV(K(root_path), K_(storage_info));
    J_OBJ_END();
  }
  return pos;
}

}  // namespace share
}  // namespace oceanbase
