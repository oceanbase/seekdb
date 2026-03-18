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
#include "ob_log_restore_struct.h"
#include "share/backup/ob_backup_config.h"


using namespace oceanbase;
using namespace common;
using namespace share;

ObRestoreSourceServiceAttr::ObRestoreSourceServiceAttr()
  : addr_()
{
}

void ObRestoreSourceServiceAttr::reset()
{
  addr_.reset();
}

/*
   parse service attr from string
   eg: "127.0.0.1:1001;127.0.0.1:1002" (ip:port list separated by semicolon)
   or: "127.0.0.1:1001" (single ip:port format)
*/
int ObRestoreSourceServiceAttr::parse_service_attr_from_str(ObSqlString &value)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(value.empty() || value.length() > OB_MAX_BACKUP_DEST_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("log restore source attr value is invalid");
  } else if (OB_FAIL(parse_ip_port_from_str(value.ptr(), ";"))) {
    LOG_WARN("fail to parse ip:port list", K(value));
  }
  return ret;
}

// 127.0.0.1:1000;127.0.0.1:1001;127.0.0.1:1002 ==> 127.0.0.1:1000 127.0.0.1:1001 127.0.0.1:1002
int ObRestoreSourceServiceAttr::parse_ip_port_from_str(const char *ip_list, const char *delimiter)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ip_list) || OB_ISNULL(delimiter) || OB_UNLIKELY(STRLEN(ip_list) > OB_MAX_RESTORE_SOURCE_IP_LIST_LEN)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("log restore source service ip list is invalid");
  }

  char tmp_str[OB_MAX_RESTORE_SOURCE_IP_LIST_LEN + 1] = { 0 };
  char *token = nullptr;
  char *saveptr = nullptr;
  if (FAILEDx(databuff_printf(tmp_str, sizeof(tmp_str), "%s", ip_list))) {
    LOG_WARN("fail to get ip list", K(ip_list));
  } else {
    token = tmp_str;
    for (char *str = token; OB_SUCC(ret); str = nullptr) {
      token = ::STRTOK_R(str, delimiter, &saveptr);
      if (nullptr == token) {
        break;
      } else {
        ObAddr addr;
        if (OB_FAIL(addr.parse_from_string(ObString(token)))) {
          LOG_WARN("fail to parse addr", K(addr), K(token));
        } else if (!addr.is_valid()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("service addr is invalid", K(addr), K(ip_list));
        } else if (OB_FAIL(addr_.push_back(addr))){
          LOG_WARN("fail to push addr", K(addr));
        }
      }
    }
  }
  return ret;
}

bool ObRestoreSourceServiceAttr::is_valid() const
{
  return !addr_.empty();
}

int ObRestoreSourceServiceAttr::gen_service_attr_str(char *buf, const int64_t buf_size) const
{
  int ret = OB_SUCCESS;
  ObSqlString ip_str;

  if (OB_UNLIKELY(!is_valid() || OB_ISNULL(buf) || buf_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid service attr argument", KP(buf), K(buf_size));
  } else if (OB_FAIL(get_ip_list_str_(ip_str))) {
    LOG_WARN("get ip list str failed");
  } else if (OB_FAIL(databuff_printf(buf, buf_size, "%.*s", static_cast<int>(ip_str.length()), ip_str.ptr()))) {
    LOG_WARN("fail to print str", K(ip_str));
  }
  return ret;
}

int ObRestoreSourceServiceAttr::gen_service_attr_str(ObSqlString &str) const
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid service attr argument");
  } else if (OB_FAIL(get_ip_list_str_(str))) {
    LOG_WARN("get ip list str failed");
  }
  return ret;
}

int ObRestoreSourceServiceAttr::get_ip_list_str_(char *buf, const int64_t buf_size) const
{
  int ret = OB_SUCCESS;
  ObSqlString str;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ip list argument", KP(buf), K(buf_size));
  } else if (OB_FAIL(get_ip_list_str_(str))) {
    LOG_WARN("get ip list str failed");
  } else if (OB_FAIL(databuff_printf(buf, buf_size, "%.*s", static_cast<int>(str.length()), str.ptr()))) {
    LOG_WARN("fail to print str", K(str));
  }
  return ret;
}

int ObRestoreSourceServiceAttr::get_ip_list_str_(ObSqlString &str) const
{
  int ret = OB_SUCCESS;

  ARRAY_FOREACH_N(addr_, idx, cnt) {
    char ip_str[MAX_IP_PORT_LENGTH] = { 0 };
    const ObAddr ip = addr_.at(idx);
    if (OB_FAIL(ip.ip_port_to_string(ip_str, sizeof(ip_str)))) {
      LOG_WARN("fail to convert ip port to string", K(ip), K(ip_str));
    } else {
      if (0 == idx && (OB_FAIL(str.assign_fmt("%s", ip_str)))) {
        LOG_WARN("fail to assign ip str", K(str) ,K(ip), K(ip_str));
      } else if ( 0 != idx && OB_FAIL(str.append_fmt(";%s", ip_str))) {
        LOG_WARN("fail to append ip str", K(str), K(ip), K(ip_str));
      }
    }
  }
  LOG_DEBUG("get ip list str", K(str));
  return ret;
}

bool ObRestoreSourceServiceAttr::operator==(const ObRestoreSourceServiceAttr &other) const
{
  if (addr_.size() != other.addr_.size()) {
    return false;
  }
  for (int64_t i = 0; i < addr_.size(); ++i) {
    bool found = false;
    for (int64_t j = 0; j < other.addr_.size(); ++j) {
      if (addr_.at(i) == other.addr_.at(j)) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

int ObRestoreSourceServiceAttr::gen_config_items(common::ObIArray<BackupConfigItemPair> &items) const
{
  int ret = OB_SUCCESS;
  BackupConfigItemPair config;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KPC(this));
  } else {
    ObSqlString tmp;
    // gen ip list config
    if (OB_FAIL(config.key_.assign(OB_STR_IP_LIST))) {
      LOG_WARN("failed to assign ip list key");
    } else if (OB_FAIL(get_ip_list_str_(tmp))) {
      LOG_WARN("failed to get ip list str");
    } else if (OB_FAIL(config.value_.assign(tmp.ptr()))) {
      LOG_WARN("failed to assign ip list value");
    } else if (OB_FAIL(items.push_back(config))) {
      LOG_WARN("failed to push service source attr config", K(config));
    }
  }
  return ret;
}

int ObRestoreSourceServiceAttr::assign(const ObRestoreSourceServiceAttr &attr)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!attr.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(attr));
  } else if (FALSE_IT(addr_.reset())) {
  } else if (OB_FAIL(addr_.assign(attr.addr_))) {
    LOG_WARN("addr_ assign failed", K(attr));
  }
  return ret;
}
