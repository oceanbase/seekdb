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

#ifndef OCEANBASE_LOGSERVICE_OB_LOCALITY_ADAPTER_H_
#define OCEANBASE_LOGSERVICE_OB_LOCALITY_ADAPTER_H_

#include <stdint.h>
#include "logservice/palf/palf_callback.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace storage
{
class ObLocalityManager;
}
namespace logservice
{
class ObLocalityAdapter : public palf::PalfLocalityInfoCb
{
public:
  ObLocalityAdapter();
  virtual ~ObLocalityAdapter();
  int init(storage::ObLocalityManager *locality_manager);
  void destroy();
public:
  int get_server_region(const common::ObAddr &server, common::ObRegion &region) const override final;
private:
  bool is_inited_;
  storage::ObLocalityManager *locality_manager_;
};

} // logservice
} // oceanbase

#endif
