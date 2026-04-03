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

#ifndef OCEANBASE_SHARE_BACKUP_OB_LOG_RESTORE_STRUCT_H_
#define OCEANBASE_SHARE_BACKUP_OB_LOG_RESTORE_STRUCT_H_

#include "lib/container/ob_array.h"
#include "share/scn.h"
#include "ob_backup_struct.h"

namespace oceanbase
{
namespace share
{

// Forward declaration
struct BackupConfigItemPair;

struct ObRestoreSourceServiceAttr final
{
  ObRestoreSourceServiceAttr();
  ~ObRestoreSourceServiceAttr() {}
  void reset();
  int parse_service_attr_from_str(ObSqlString &str);
  int parse_ip_port_from_str(const char *buf, const char *delimiter);
  bool is_valid() const;
  int gen_service_attr_str(char *buf, const int64_t buf_size) const;
  int gen_service_attr_str(ObSqlString &str) const;
  int gen_config_items(common::ObIArray<BackupConfigItemPair> &items) const;
  int get_ip_list_str_(char *buf, const int64_t buf_size) const;
  int get_ip_list_str_(ObSqlString &str) const;
  bool operator ==(const ObRestoreSourceServiceAttr &other) const;
  int assign(const ObRestoreSourceServiceAttr &attr);
  TO_STRING_KV(K_(addr));
  common::ObArray<common::ObAddr> addr_;
};

}//share
}//oceanbase

#endif /* OCEANBASE_SHARE_BACKUP_OB_LOG_RESTORE_STRUCT_H_ */
