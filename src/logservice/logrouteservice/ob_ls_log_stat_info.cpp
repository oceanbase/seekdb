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
#define USING_LOG_PREFIX  OBLOG
#include "ob_ls_log_stat_info.h"

namespace oceanbase
{
namespace logservice
{
/////////////////////////////////// LogStatRecord ///////////////////////////////
int64_t LogStatRecord::to_string(char *buffer, int64_t length) const
{
  int64_t pos = 0;
  (void)databuff_printf(buffer, length, pos, "{svr=");
  (void)databuff_printf(buffer, length, pos, server_);
  (void)databuff_printf(buffer, length, pos, ", role=%d, LSN:[%ld, %ld]}",
      role_, begin_lsn_.val_, end_lsn_.val_);

  return pos;
}

void ObLSLogInfo::reset()
{
  tenant_id_ = OB_INVALID_TENANT_ID;
  ls_id_.reset();
  log_stat_records_.reset();
}

int ObLSLogInfo::init(const uint64_t tenant_id, const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(! ls_id.is_valid_with_tenant(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(tenant_id), K(ls_id));
  } else {
    tenant_id_ = tenant_id;
    ls_id_ = ls_id;
    log_stat_records_.reset();
  }

  return ret;
}

int ObLSLogInfo::add(const LogStatRecord &log_stat_record)
{
  int ret = OB_SUCCESS;

  if (!is_valid() || !log_stat_record.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), "ObLSLogInfo", *this, K(log_stat_record));
  } else {
    if (OB_FAIL(log_stat_records_.push_back(log_stat_record))) {
      LOG_WARN("insert log_stat_record failed", KR(ret), K(log_stat_record));
    }
  }

  return ret;
}

} // namespace logservice
} // namespace oceanbase
