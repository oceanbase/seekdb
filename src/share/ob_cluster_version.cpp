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

#include "ob_cluster_version.h"
#include "observer/omt/ob_tenant_config_mgr.h"
namespace oceanbase
{
namespace common
{
static int parse_version(const char *str, uint64_t *versions, const int64_t size)
{
  int ret = OB_SUCCESS;
  int64_t i = 0;
  char buf[64] = {0};
  char *ptr = buf;
  const char *delim = ".";
  char *saveptr = NULL;
  char *token = NULL;
  const int64_t VERSION_ITEM = 4;
  const int64_t LAST_VERSION_ITEM = VERSION_ITEM - 1;

  if (NULL == str || NULL == versions || VERSION_ITEM > size) {
    COMMON_LOG(WARN, "invalid argument", KP(str), KP(versions), K(size));
    ret = OB_INVALID_ARGUMENT;
  } else if (strlen(str) >= sizeof(buf)) {
    COMMON_LOG(WARN, "invalid version", "version", str);
    ret = OB_INVALID_ARGUMENT;
  } else {
    strncpy(buf, str, sizeof(buf) - 1);
    for (i = 0; i < size; i++) {
      if (NULL != (token = strtok_r(ptr, delim, &saveptr))) {
        versions[i] = atoi(token);
      } else {
        break;
      }
      ptr = NULL;
    }
    if (VERSION_ITEM < i || LAST_VERSION_ITEM > i) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "invalid package version", KR(ret), "version", str, K(i), K(VERSION_ITEM));
    } else if (i == LAST_VERSION_ITEM) {
      // padding cluster_versions with 0
      versions[VERSION_ITEM - 1] = 0;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(
        versions[ObClusterVersion::MAJOR_POS] > OB_VSN_MAJOR_MASK
        || versions[ObClusterVersion::MINOR_POS] > OB_VSN_MINOR_MASK
        || versions[ObClusterVersion::MAJOR_PATCH_POS] > OB_VSN_MAJOR_PATCH_MASK
        || versions[ObClusterVersion::MINOR_PATCH_POS] > OB_VSN_MINOR_PATCH_MASK)) {
      ret = OB_SIZE_OVERFLOW;
      COMMON_LOG(WARN, "invalid package version",
                 KR(ret), "version", str,
                 "major", versions[ObClusterVersion::MAJOR_POS],
                 "minor", versions[ObClusterVersion::MINOR_POS],
                 "major_patch", versions[ObClusterVersion::MAJOR_PATCH_POS],
                 "minor_patch", versions[ObClusterVersion::MINOR_PATCH_POS]);
    }
  }
  return ret;
}

ObClusterVersion::ObClusterVersion()
  : is_inited_(false), config_(NULL),
    cluster_version_(0), data_version_(0)
{
  cluster_version_ = cal_version(DEF_MAJOR_VERSION,
                                 DEF_MINOR_VERSION,
                                 DEF_MAJOR_PATCH_VERSION,
                                 DEF_MINOR_PATCH_VERSION);
}

int ObClusterVersion::init(const uint64_t cluster_version)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(ERROR, "cluster version init twice", KR(ret), K(cluster_version));
  } else {
    ATOMIC_SET(&cluster_version_, cluster_version);
    COMMON_LOG(INFO, "cluster version inited success", K(cluster_version));
    is_inited_ = true;
  }

  return ret;
}

int ObClusterVersion::init(const ObServerConfig *config)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(ERROR, "cluster version init twice", KR(ret), KP(config));
  } else if (OB_ISNULL(config)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid argument", KR(ret), KP(config));
  } else if (OB_FAIL(refresh_cluster_version(config->min_observer_version.str()))) {
    COMMON_LOG(WARN, "refresh cluster version error", KR(ret));
  } else {
    config_ = config;
    COMMON_LOG(INFO, "cluster version inited success", K_(cluster_version));
    is_inited_ = true;
  }

  return ret;
}

void ObClusterVersion::destroy()
{
  if (is_inited_) {
    is_inited_ = false;
    config_ = NULL;
    cluster_version_ = 0;
    data_version_ = 0;
  }
}

int64_t ObClusterVersion::to_string(char *buf, const int64_t buf_len) const
{
  const uint64_t version = ATOMIC_LOAD(&cluster_version_);
  return print_vsn(buf, buf_len, version);
}

int64_t ObClusterVersion::print_vsn(char *buf, const int64_t buf_len, uint64_t version)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  const uint32_t major = OB_VSN_MAJOR(version);
  const uint16_t minor = OB_VSN_MINOR(version);
  const uint8_t major_patch = OB_VSN_MAJOR_PATCH(version);
  const uint8_t minor_patch = OB_VSN_MINOR_PATCH(version);
  if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%lu(%u, %u, %u, %u)",
              version, major, minor, major_patch, minor_patch))) {
    COMMON_LOG(WARN, "fail to print vsn", KR(ret), K(version));
  }
  if (OB_FAIL(ret)) {
    pos = OB_INVALID_INDEX;
  }
  return pos;
}

int64_t ObClusterVersion::print_version_str(char *buf, const int64_t buf_len, uint64_t version)
{
  return VersionUtil::print_version_str(buf, buf_len, version);
}

int ObClusterVersion::refresh_cluster_version(const char *verstr)
{
  int ret = OB_SUCCESS;
  uint64_t items[MAX_VERSION_ITEM] = {0};
  if (NULL == verstr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_version(verstr, items, MAX_VERSION_ITEM))) {
    // do nothing
  } else {
    const uint64_t version = cal_version(items[ObClusterVersion::MAJOR_POS],
                                         items[ObClusterVersion::MINOR_POS],
                                         items[ObClusterVersion::MAJOR_PATCH_POS],
                                         items[ObClusterVersion::MINOR_PATCH_POS]);
    ATOMIC_SET(&cluster_version_, version);
    COMMON_LOG(INFO, "refresh cluster version", "cluster_version", *this);
  }

  return ret;
}

int ObClusterVersion::reload_config()
{
  int ret = OB_SUCCESS;

  if (NULL == config_) {
    COMMON_LOG(WARN, "config is null", KP_(config));
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = refresh_cluster_version(config_->min_observer_version.str());
  }

  return ret;
}

void ObClusterVersion::update_cluster_version(const uint64_t cluster_version)
{
  ATOMIC_SET(&cluster_version_, cluster_version);
}

void ObClusterVersion::update_data_version(const uint64_t data_version)
{
  ATOMIC_SET(&data_version_, data_version);
  ODV_MGR.set_mock_data_version(data_version);
}

int ObClusterVersion::get_tenant_data_version(
    const uint64_t tenant_id,
    uint64_t &data_version)
{
  int ret = OB_SUCCESS;
  data_version = 0;
  if (OB_UNLIKELY(0 != data_version_)) {
    // only work for unittest
    data_version = ATOMIC_LOAD(&cluster_version_);
  } else {
    ret = ODV_MGR.get(tenant_id, data_version);
  }
  return ret;
}

int ObClusterVersion::tenant_need_upgrade(
    const uint64_t tenant_id,
    bool &need_upgrade)
{
  int ret = OB_SUCCESS;
  uint64_t data_version = 0;
  if (OB_FAIL(get_tenant_data_version(tenant_id, data_version))) {
    COMMON_LOG(WARN, "fail to get tenant data version", KR(ret), K(tenant_id));
  } else {
    need_upgrade = (data_version < DATA_CURRENT_VERSION);
  }
  return ret;
}


int ObClusterVersion::is_valid(const char *verstr)
{
  int ret = OB_SUCCESS;
  uint64_t items[MAX_VERSION_ITEM] = {0};
  if (NULL == verstr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_version(verstr, items, MAX_VERSION_ITEM))) {
    COMMON_LOG(WARN, "invalid version", "version_str", verstr);
  }
  return ret;
}

int ObClusterVersion::get_version(const common::ObString &verstr, uint64_t &version)
{
  int ret = OB_SUCCESS;
  char buf[OB_CLUSTER_VERSION_LENGTH];
  version = 0;

  if (OB_FAIL(databuff_printf(buf, OB_CLUSTER_VERSION_LENGTH, "%.*s", verstr.length(), verstr.ptr()))) {
    COMMON_LOG(WARN, "failed to print version", K(ret), K(verstr));
  } else if (OB_FAIL(get_version(buf, version))) {
    COMMON_LOG(WARN, "failed to get version", K(ret), K(buf));
  }

  return ret;
}

int ObClusterVersion::get_version(const char *verstr, uint64_t &version)
{
  int ret = OB_SUCCESS;
  uint64_t items[MAX_VERSION_ITEM] = {0};
  if (NULL == verstr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_version(verstr, items, MAX_VERSION_ITEM))) {
    COMMON_LOG(WARN, "invalid version", "version_str", verstr);
  } else {
    version = cal_version(items[ObClusterVersion::MAJOR_POS],
                          items[ObClusterVersion::MINOR_POS],
                          items[ObClusterVersion::MAJOR_PATCH_POS],
                          items[ObClusterVersion::MINOR_PATCH_POS]);
  }
  return ret;
}

ObClusterVersion &ObClusterVersion::get_instance()
{
  static ObClusterVersion cluster_version;
  return cluster_version;
}

} // end namespace common
} // end namespace oceanbase
