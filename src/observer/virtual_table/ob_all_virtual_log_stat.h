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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PALF_STAT_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_PALF_STAT_
#include "observer/omt/ob_multi_tenant.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"
#include "logservice/palf/palf_handle.h"
#include "logservice/palf/log_meta_info.h"//CONFIG_VERSION_LEN

namespace oceanbase
{
namespace logservice
{
class ObLogStat;
}

namespace observer
{
class ObAllVirtualPalfStat: public common::ObVirtualTableScannerIterator
{
public:
  explicit ObAllVirtualPalfStat(omt::ObMultiTenant *omt);
  virtual ~ObAllVirtualPalfStat();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  void destroy();
private:
  int insert_log_stat_(const logservice::ObLogStat &log_stat, common::ObNewRow *row);
  int member_list_to_string_(const common::ObMemberList &member_list);
  int learner_list_to_string_(const common::GlobalLearnerList &learner_list, char *output_buf);
private:
  static const int64_t VARCHAR_32 = 32;
  static const int64_t VARCHAR_64 = 64;
  char role_str_[VARCHAR_32] = {'\0'};
  char access_mode_str_[VARCHAR_32] = {'\0'};
  char ip_[common::OB_IP_PORT_STR_BUFF] = {'\0'};
  ObSqlString member_list_buf_;
  char arbitration_member_buf_[MAX_SINGLE_MEMBER_LENGTH] = {'\0'};
  char degraded_list_buf_[MAX_LEARNER_LIST_LENGTH] = {'\0'};
  char config_version_buf_[palf::LogConfigVersion::CONFIG_VERSION_LEN] = {'\0'};
  char replica_type_str_[VARCHAR_32] = {'\0'};
  char learner_list_buf_[MAX_LEARNER_LIST_LENGTH] = {'\0'};
  omt::ObMultiTenant *omt_;
};
}//namespace observer
}//namespace oceanbase
#endif
