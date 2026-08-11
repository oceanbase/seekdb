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

#ifndef OCEANBASE_STANDBY_STANDBY_MODULE_H_
#define OCEANBASE_STANDBY_STANDBY_MODULE_H_

#include <stdint.h>
#include <functional>
#include "standby/standby_host.h"

namespace oceanbase
{
namespace standby
{

class StandbyModule final
{
public:
  StandbyModule();
  ~StandbyModule();
  int init(const StandbyConfig &config, IStandbyHost &host);
  int stop();
  int wait();
  void destroy();
  int prepare_storage_replay();
  int prepare_service_start(const bool need_bootstrap);
  int start();
  int wait_replay_ready(const std::function<bool()> &is_stopping);
  int wait_metadata_ready();
  int reload_config(const bool rpc_service_enabled);
  int start_listener();

private:
  class Impl;
  Impl *impl_;
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_STANDBY_MODULE_H_ */
