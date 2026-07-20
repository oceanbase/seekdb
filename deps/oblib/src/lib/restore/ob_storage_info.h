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

#ifndef OCEANBASE_LIB_RESTORE_OB_STORAGE_INFO_H_
#define OCEANBASE_LIB_RESTORE_OB_STORAGE_INFO_H_

#include "lib/restore/ob_device_common.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/string/ob_string.h"
#ifdef _WIN32
#ifdef CHECKSUM_TYPE_CRC32
#undef CHECKSUM_TYPE_CRC32
#endif
#endif

namespace oceanbase
{

namespace common
{

const int64_t OB_MAX_BACKUP_EXTENSION_LENGTH = 512;
const int64_t OB_MAX_BACKUP_ENDPOINT_LENGTH = 256;
const int64_t OB_MAX_BACKUP_ACCESSID_LENGTH = 256;
const int64_t OB_MAX_BACKUP_ACCESSKEY_LENGTH = 256;
const int64_t OB_MAX_BACKUP_STORAGE_INFO_LENGTH = 1600;
// OB_MAX_DEVICE_KEY_LENGTH = OB_MAX_BACKUP_STORAGE_INFO_LENGTH + strlen("&storage_type=x")
const int64_t OB_MAX_DEVICE_KEY_LENGTH = OB_MAX_BACKUP_STORAGE_INFO_LENGTH + 15;
const int64_t OB_MAX_BACKUP_ENCRYPTKEY_LENGTH = OB_MAX_BACKUP_ACCESSKEY_LENGTH + 32;
const int64_t OB_MAX_BACKUP_SERIALIZEKEY_LENGTH = OB_MAX_BACKUP_ENCRYPTKEY_LENGTH * 2;
const char *const ACCESS_ID = "access_id=";
const char *const ACCESS_KEY = "access_key=";
const char *const HOST = "host=";
const char *const APPID = "appid=";
const char *const DELETE_MODE = "delete_mode=";
const char *const MAX_IOPS = "max_iops=";
const char *const MAX_BANDWIDTH = "max_bandwidth=";
const char *const ENABLE_WORM = "enable_worm=";
const char* const SEPERATE_SYMBOL = "&";

const char *const CHECKSUM_TYPE = "checksum_type=";
const char *const CHECKSUM_TYPE_NO_CHECKSUM = "no_checksum";
const char *const CHECKSUM_TYPE_MD5 = "md5";
const char *const CHECKSUM_TYPE_CRC32 = "crc32";

enum ObStorageChecksumType : uint8_t
{
  OB_NO_CHECKSUM_ALGO = 0,
  OB_MD5_ALGO = 1,
  OB_CRC32_ALGO = 2,
  OB_STORAGE_CHECKSUM_MAX_TYPE = 3
};

const char *get_storage_checksum_type_str(const ObStorageChecksumType &type);
bool is_use_obdal();
// [Extensions]
//   load_data_* : sql/engine/cmd/ob_load_data_storage_info.h

class ObClusterVersionBaseMgr
{
public:
  ObClusterVersionBaseMgr() {}
  virtual ~ObClusterVersionBaseMgr() {}
  virtual int is_supported_enable_worm_version() const
  {
    return OB_SUCCESS;
  };
  virtual int is_supported_azblob_version() const
  {
    return OB_SUCCESS;
  }
  static ObClusterVersionBaseMgr &get_instance()
  {
    static ObClusterVersionBaseMgr mgr;
    return mgr;
  }
};

enum ObStorageDeleteMode: uint8_t
{
  NONE = 0,
  STORAGE_DELETE_MODE = 1,
  STORAGE_TAGGING_MODE = 2,
  MAX
};

class ObObjectStorageInfo;
class ObStorageAccount
{
public:
  ObStorageAccount();
  virtual ~ObStorageAccount() {};
  virtual void reset();
  virtual bool is_valid() const { return is_valid_; }
  virtual int assign(const ObObjectStorageInfo *storage_info) = 0;

  TO_STRING_KV(K(is_valid_), K(delete_mode_), K(endpoint_), K(access_id_), KP(access_key_));

public:
  bool is_valid_;
  char endpoint_[OB_MAX_BACKUP_ENDPOINT_LENGTH];
  char access_id_[OB_MAX_BACKUP_ACCESSID_LENGTH];
  char access_key_[OB_MAX_BACKUP_ACCESSKEY_LENGTH];
  ObStorageDeleteMode delete_mode_;
};
// ObObjectStorageInfo stores all the information needed to access object storage, including ak, sk, endpoint, etc.
// ObObjectStorageInfo can be initialized by a string of a specific format, or by another ObObjectStorageInfo object, no other means of modifying the data are provided
// Optional fields exist in the extension field; selected values are cached for convenient access.
class ObObjectStorageInfo
{
  OB_UNIS_VERSION(1);

public:
  ObObjectStorageInfo();
  virtual ~ObObjectStorageInfo();
  virtual void reset();
  virtual int set(const common::ObStorageType device_type, const char *storage_info);
  virtual int set(const char *uri, const char *storage_info);
  virtual int assign(const ObObjectStorageInfo &storage_info);
  int reset_access_id_and_access_key(const char *access_id, const char *access_key);
  static int register_cluster_version_mgr(ObClusterVersionBaseMgr *cluster_version_mgr);

public:
  int64_t hash() const;
  bool operator ==(const ObObjectStorageInfo &storage_info) const;
  bool operator !=(const ObObjectStorageInfo &storage_info) const;
  bool is_access_info_equal(const ObObjectStorageInfo &storage_info) const;

  ObStorageDeleteMode get_delete_mode() const { return delete_mode_; }
  ObStorageType get_type() const;
  const char *get_type_str() const;
  ObStorageChecksumType get_checksum_type() const;
  const char *get_checksum_type_str() const;

  bool is_enable_worm() const;
  virtual bool is_valid() const;
  virtual int validate_arguments() const;

  // This function allows the device_manager to determine the key values for different storage information.
  // Since delete_mode and addressing_mode are recorded in the extension field, they are not separately recorded.
  virtual int get_device_map_key_str(char *key_str, const int64_t len) const;
  virtual int64_t get_device_map_key_len() const;
  virtual int get_storage_info_str(char *storage_info, const int64_t info_len) const;
  virtual int to_account(ObStorageAccount &account) const;

  TO_STRING_KV(K_(endpoint), K_(access_id), K_(extension), "type", get_type_str(),
      K_(checksum_type), K_(max_iops), K_(max_bandwidth), K_(enable_worm));
protected:
  virtual int get_access_key_(char *buf, const int64_t buf_len) const;
  virtual int get_info_str_(char *storage_info, const int64_t info_len) const;
  virtual int append_extension_str_(char *storage_info, const int64_t info_len) const;
  virtual int parse_storage_info_(const char *storage_info, bool &has_appid);
  int set_storage_info_field_(const char *info, char *field, const int64_t length);
  int set_enable_worm_(const char *enable_worm);
  int set_delete_mode_(const char *delete_mode);
  int set_checksum_type_(const char *checksum_type_str);

public:
  // TODO: Rename device_type_ to storage_protocol_type_ for better clarity
  common::ObStorageType device_type_;
  // Optional parameter. If not provided, the default value OB_MD5_ALGO will be used.
  ObStorageChecksumType checksum_type_;                                 // Repeated in extension_
  ObStorageDeleteMode delete_mode_;                                     // Repeated in extension_
  char endpoint_[OB_MAX_BACKUP_ENDPOINT_LENGTH];
  char access_id_[OB_MAX_BACKUP_ACCESSID_LENGTH];
  char access_key_[OB_MAX_BACKUP_ENCRYPTKEY_LENGTH];
  char extension_[OB_MAX_BACKUP_EXTENSION_LENGTH];
  int64_t max_iops_;
  int64_t max_bandwidth_;
  bool enable_worm_;
  static ObClusterVersionBaseMgr *cluster_version_mgr_;
};

}
}

#endif
