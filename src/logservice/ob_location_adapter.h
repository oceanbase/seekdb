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

#ifndef OCEANBASE_LOGSERVICE_OB_LOCATION_ADAPTER_H_
#define OCEANBASE_LOGSERVICE_OB_LOCATION_ADAPTER_H_

#include <stdint.h>
#include "logservice/palf/palf_callback.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace share
{
class ObLocationService;
}
namespace logservice
{
class ObLocationAdapter : public palf::PalfLocationCacheCb
{
public:
  ObLocationAdapter();
  virtual ~ObLocationAdapter();
  int init(share::ObLocationService *location_service);
  void destroy();
public:
  // Gets leader address of a log stream synchronously.
  int get_leader(const int64_t id, common::ObAddr &leader) override final;
  int get_leader(const uint64_t tenant_id, 
                 const int64_t id, 
                 const bool force_renew, 
                 common::ObAddr &leader);
  // Nonblock way to get leader address of the log stream.
  int nonblock_get_leader(int64_t id, common::ObAddr &leader) override final;
  int nonblock_renew_leader(int64_t id) override final;
  int nonblock_get_leader(const uint64_t tenant_id, int64_t id, common::ObAddr &leader) override final;
  int nonblock_renew_leader(const uint64_t tenant_id, int64_t id) override final;
  bool is_location_service_renew_error(const int err) const;
private:
  bool is_inited_;
  share::ObLocationService *location_service_;
};

} // logservice
} // oceanbase

#endif
