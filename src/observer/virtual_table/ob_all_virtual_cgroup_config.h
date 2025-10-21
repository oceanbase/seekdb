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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_CGROUP_CONFIG_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_CGROUP_CONFIG_H_

#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualCgroupConfig : public common::ObVirtualTableScannerIterator
{
  enum COLUMN_ID_LIST
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    CFS_QUOTA_US,
    CFS_PERIOD_US,
    SHARES,
    CGROUP_PATH
  };

public:
  ObAllVirtualCgroupConfig();
  virtual ~ObAllVirtualCgroupConfig() override;
  virtual int inner_open() override;
  virtual void reset() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
private:
  static const int32_t PATH_BUFSIZE = 256;
  static const int32_t VALUE_BUFSIZE = 32;
  static constexpr const char *const root_cgroup_path = "/sys/fs/cgroup/cpu";
  static constexpr const char *const cgroup_link_path = "cgroup";
  bool is_inited_;
  char ip_buf_[common::OB_IP_STR_BUFF];
  char cgroup_path_buf_[PATH_BUFSIZE];
  char cgroup_origin_path_[PATH_BUFSIZE];
private:
  int check_cgroup_dir_exist_(const char *cgroup_path);
  int read_cgroup_path_dir_(const char *cgroup_path);
  int add_cgroup_config_info_(const char *cgroup_path);
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualCgroupConfig);
};

}
}
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_CGROUP_CONFIG_H_ */
