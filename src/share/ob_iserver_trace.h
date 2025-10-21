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

#ifndef OCEANBASE_PARTITION_TABLE_OB_ISERVER_TRACE_H_
#define OCEANBASE_PARTITION_TABLE_OB_ISERVER_TRACE_H_

#include "common/ob_zone.h"
#include "common/ob_region.h"
namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace share
{


// trace server alive status
class ObIServerTrace
{
public:
  ObIServerTrace() {}
  virtual ~ObIServerTrace() {};

  virtual int check_server_alive(const common::ObAddr &server, bool &is_alive) const = 0;
  virtual int check_in_service(const common::ObAddr &addr, bool &service_started) const = 0;
  virtual int check_migrate_in_blocked(const common::ObAddr &addr, bool &is_block) const = 0;
  virtual int check_server_permanent_offline(const common::ObAddr &server, bool &is_alive) const = 0;
  virtual int is_server_exist(const common::ObAddr &server, bool &exist) const = 0;
  virtual int is_server_stopped(const common::ObAddr &server, bool &is_stopped) const = 0;
};

class ObIZoneTrace
{
public:
  ObIZoneTrace() {}
  virtual ~ObIZoneTrace() {}
  virtual int get_region(const common::ObZone &zone, common::ObRegion &region) const = 0;
  virtual int get_zone_count(const common::ObRegion &region, int64_t &zone_count) const = 0;
};

} // end namespace share
} // end namespace oceanbase

#endif // OCEANBASE_PARTITION_TABLE_OB_ISERVER_TRACE_H_
