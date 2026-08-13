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

#ifndef OCEANBASE_OBSERVER_OB_INTERNAL_TABLE_REFRESH_ADAPTER_H_
#define OCEANBASE_OBSERVER_OB_INTERNAL_TABLE_REFRESH_ADAPTER_H_

#include "share/log/ob_log_base_type.h"

namespace oceanbase
{
namespace omt
{
class ObTimezoneMgr;
class ObSrsService;
}
namespace observer
{

class ObInternalTableRefreshAdapter final
    : public logservice::ObILocalLogHandler
{
public:
  ObInternalTableRefreshAdapter()
      : timezone_mgr_(nullptr), srs_service_(nullptr)
  {}

  int init(omt::ObTimezoneMgr &timezone_mgr,
           omt::ObSrsService &srs_service);
  void reset();

  void deactivate() override {}
  int activate() override;

private:
  omt::ObTimezoneMgr *timezone_mgr_;
  omt::ObSrsService *srs_service_;
};

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_INTERNAL_TABLE_REFRESH_ADAPTER_H_
