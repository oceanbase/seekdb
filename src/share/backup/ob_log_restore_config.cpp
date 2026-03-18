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
#include "ob_log_restore_config.h"
#include "share/ob_log_restore_proxy.h"  // ObLogRestoreProxyUtil

using namespace oceanbase;
using namespace share;
using namespace common;

int ObLogRestoreSourceLocationConfigParser::update_inner_config_table(common::ObISQLClient &trans)
{
  // Config parameter log_restore_source is updated automatically by ALTER SYSTEM SET
  // This function is kept for interface compatibility but does not need to persist data
  UNUSED(trans);
  int ret = OB_SUCCESS;
  bool is_primary_cluster = true;

  if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid type", KR(ret), KPC(this));
  } else if (is_empty_) {
    // Empty value will clear the config, handled by framework
    LOG_INFO("log_restore_source will be cleared");
  } else if (!archive_dest_.is_dest_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid restore source", KR(ret), KPC(this));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_restore_source");
  } else if (OB_FAIL(archive_dest_.gen_path_config_items(config_items_))) {
    LOG_WARN("fail to gen archive config items", KR(ret), KPC(this));
  } else if (OB_FAIL(ObShareUtil::is_primary_cluster(is_primary_cluster))) {
    LOG_WARN("fail to check whether is primary cluster", KR(ret), K(is_primary_cluster));
  } else if (!is_primary_cluster) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("tenant is standby, not supported now", K(is_primary_cluster));
  } else {
    LOG_INFO("log_restore_source location config validated successfully", K(archive_dest_));
  }
  
  return ret;
}

int ObLogRestoreSourceLocationConfigParser::check_before_update_inner_config(
    obrpc::ObSrvRpcProxy &rpc_proxy, 
    common::ObISQLClient &trans)
{
  int ret = OB_SUCCESS;

  if (is_empty_) {
  } else if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", KR(ret), KPC(this));
  } else {
    share::ObBackupStore backup_store;
    share::ObBackupFormatDesc desc;
    if (OB_FAIL(backup_store.init(archive_dest_.dest_))) {
      LOG_WARN("backup store init failed", K_(archive_dest_.dest));
    } else if (OB_FAIL(backup_store.read_format_file(desc))) {
      LOG_WARN("backup store read format file failed", K_(archive_dest_.dest));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "access the log restore source location");
    } else if (OB_UNLIKELY(! desc.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("backup store desc is invalid");
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "access the log restore source location");
    } else if (GCONF.cluster_id == desc.cluster_id_ && tenant_id_ == desc.tenant_id_) {
      ret = OB_INVALID_ARGUMENT; 
      LOG_WARN("set standby itself as log restore source is not allowed");
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "set standby itself as log restore source");
    }
  }
  //TODO (mingqiao) need support access permission check
  // 
  return ret;
}

int ObLogRestoreSourceLocationConfigParser::do_parse_sub_config_(const common::ObString &config_str)
{
  int ret = OB_SUCCESS;
  const char *target= nullptr;
  if (config_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty log restore source is not allowed", KR(ret), K(config_str));
  } else {
    char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
    char *token = nullptr;
    char *saveptr = nullptr;
    char *p_end = nullptr;
    if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", config_str.length(), config_str.ptr()))) {
      LOG_WARN("fail to set config value", KR(ret), K(config_str));
    } else if (OB_ISNULL(token = ::STRTOK_R(tmp_str, "=", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to split config str", K(ret), KP(token));
    } else if (OB_FALSE_IT(str_tolower(token, strlen(token)))) {
    } else if (0 == STRCASECMP(token, OB_STR_LOCATION)) {
      if (OB_FAIL(do_parse_log_archive_dest_(token, saveptr))) {
        LOG_WARN("fail to do parse log archive dest", KR(ret), K(token), K(saveptr));
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("log restore source does not has this config", KR(ret), K(token));
    }
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::parse_from(const common::ObSqlString &value)
{
  int ret = OB_SUCCESS;
  char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  char *token = nullptr;
  char *saveptr = nullptr;
  is_empty_ = false;

  if (value.empty()) {
    is_empty_ = true;
  } else if (value.length() > OB_MAX_BACKUP_DEST_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("config value is too long");
  } else if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", static_cast<int>(value.length()), value.ptr()))) {
    LOG_WARN("fail to set config value", K(value));
  } else {
    token = tmp_str;
    for (char *str = token; OB_SUCC(ret); str = nullptr) {
      token = ::STRTOK_R(str, " ", &saveptr);
      if (nullptr == token) {
        break;
      } else if (OB_FAIL(do_parse_sub_config_(token))) {
        LOG_WARN("fail to do parse log restore source server sub config");
      }
    }
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::update_inner_config_table(common::ObISQLClient &trans)
{
  // Config parameter log_restore_source is updated automatically by ALTER SYSTEM SET
  // This function is kept for interface compatibility but does not need to persist data
  UNUSED(trans);
  int ret = OB_SUCCESS;
  bool is_primary_cluster = true;

  if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid type", KPC(this));
  } else if (is_empty_) {
    // Empty value will clear the config, handled by framework
    LOG_INFO("log_restore_source will be cleared");
  } else if (!service_attr_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid restore source", KPC(this)); 
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_restore_source");
  } else if (OB_FAIL(service_attr_.gen_config_items(config_items_))) {
    LOG_WARN("fail to gen restore source service config items", KPC(this));
  } else if (OB_FAIL(ObShareUtil::is_primary_cluster(is_primary_cluster))) {
    LOG_WARN("fail to check whether is primary cluster", KR(ret), K(is_primary_cluster));
  } else if (!is_primary_cluster) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("tenant is standby", K(is_primary_cluster));
  } else {
    LOG_INFO("log_restore_source config validated successfully", K(service_attr_));
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::check_before_update_inner_config(obrpc::ObSrvRpcProxy &rpc_proxy, common::ObISQLClient &trans) 
{
  int ret = OB_SUCCESS;
  ObCompatibilityMode compat_mode = ObCompatibilityMode::OCEANBASE_MODE;
  if (is_empty_) {
  } else if (!type_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parser", KR(ret), KPC(this));
  } else if (OB_FAIL(check_before_update_inner_config(false /* for_verify */, compat_mode))) {
    LOG_WARN("fail to check before update inner config");
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::check_before_update_inner_config(
    const bool for_verify, 
    ObCompatibilityMode &compat_mode)
{
  int ret = OB_SUCCESS;
  compat_mode = ObCompatibilityMode::OCEANBASE_MODE;
  // Note: Only ip_list is supported now, skip user/cluster_id/tenant_id checks
  LOG_INFO("check_before_update_inner_config success (ip_list only)", K(tenant_id_), K(service_attr_));
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::construct_restore_sql_proxy_(ObLogRestoreProxyUtil &log_restore_proxy)
{
  int ret = OB_NOT_SUPPORTED;
  // Note: Only ip_list is supported now, user/password/tenant_id/cluster_id are not available
  LOG_WARN("construct_restore_sql_proxy_ not supported (ip_list only)", KR(ret));
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::get_compatibility_mode(common::ObCompatibilityMode &compatibility_mode)
{
  int ret = OB_SUCCESS;
  // Note: Only ip_list is supported now, return default compatibility mode
  compatibility_mode = OCEANBASE_MODE;
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::
    get_primary_server_addr(const common::ObSqlString &value,
                            uint64_t &primary_tenant_id,
                            uint64_t &primary_cluster_id,
                            ObIArray<common::ObAddr> &addr_list) {
  int ret = OB_SUCCESS;
  if (value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(value));
  } else if (OB_FAIL(parse_from(value))) {
    LOG_WARN("failed to parse from value", KR(ret), K(value));
  } else {
    // Note: Only ip_list is supported now, directly use addr_ from service_attr_
    addr_list.reset();
    if (OB_FAIL(addr_list.assign(service_attr_.addr_))) {
      LOG_WARN("failed to assign addr list", KR(ret), K(service_attr_.addr_));
    } else {
      primary_tenant_id = OB_INVALID_TENANT_ID;
      primary_cluster_id = OB_INVALID_CLUSTER_ID;
      LOG_INFO("get primary server info (ip_list only)", K(primary_tenant_id), K(primary_cluster_id), K(addr_list));
    }
  }
  return ret;
}

int ObLogRestoreSourceServiceConfigParser::do_parse_sub_config_(const common::ObString &config_str)
{
  int ret = OB_SUCCESS;
  if (config_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("empty log restore source sub config is not allowed", K(config_str));
  } else {
    char tmp_str[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
    char *token = nullptr;
    char *saveptr = nullptr;
    if (OB_FAIL(databuff_printf(tmp_str, sizeof(tmp_str), "%.*s", config_str.length(), config_str.ptr()))) {
      LOG_WARN("fail to set config value", K(config_str));
    } else if (OB_ISNULL(token = ::STRTOK_R(tmp_str, "=", &saveptr))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to split config str", KP(token));
    } else if (OB_FALSE_IT(str_tolower(token, strlen(token)))) {
    } else if (0 == STRCASECMP(token, OB_STR_SERVICE)) {
      if (OB_FAIL(do_parse_restore_service_host_(token, saveptr))) {
        LOG_WARN("fail to do parse restore service host", K(token), K(saveptr));
      }
    } else {
      // Note: Only SERVICE is supported now, ignore USER and PASSWORD
      LOG_DEBUG("ignore unsupported config field", K(token));
    }
  }

  return ret;
}

int ObLogRestoreSourceServiceConfigParser::do_parse_restore_service_host_(const common::ObString &name, const
common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (name.empty() || value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log restore source service host config", K(name), K(value));
  } else if (OB_FAIL(service_attr_.parse_ip_port_from_str(value.ptr(), ";" /*delimiter*/))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parse restore source service host failed", K(name), K(value));
  }
  if (OB_FAIL(ret)) {
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "set ip list config, please check the length and format of ip list");
  }
  return ret;
}

// Note: do_parse_restore_service_user_ and do_parse_restore_service_passwd_ removed
// Only ip:port list is supported now
