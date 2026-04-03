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

#include "ob_cdc_service_monitor.h"

namespace oceanbase
{
namespace cdc
{
int64_t ObCdcServiceMonitor::locate_count_ = 0;
int64_t ObCdcServiceMonitor::fetch_count_ = 0;

int64_t ObCdcServiceMonitor::locate_time_;
int64_t ObCdcServiceMonitor::fetch_time_;
int64_t ObCdcServiceMonitor::l2s_time_;
int64_t ObCdcServiceMonitor::svr_queue_time_;

int64_t ObCdcServiceMonitor::fetch_size_;
int64_t ObCdcServiceMonitor::fetch_log_count_;
int64_t ObCdcServiceMonitor::reach_upper_ts_pkey_count_;
int64_t ObCdcServiceMonitor::reach_max_log_pkey_count_;
int64_t ObCdcServiceMonitor::need_fetch_pkey_count_;
int64_t ObCdcServiceMonitor::scan_round_count_;
int64_t ObCdcServiceMonitor::round_rate_;

} // namespace cdc
} // namespace oceanbase
