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

#ifndef OCEANBASE_OB_SCHEMA_RUNTIME_SERVICE_H
#define OCEANBASE_OB_SCHEMA_RUNTIME_SERVICE_H

#include <atomic>
#include <cstdint>

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
class ObSchemaRuntimeService
{
public:
  using TabletSchemaResolver = int (*)(uint64_t, ObMultiVersionSchemaService *&, uint64_t &);
  static int server_module_init(
      ObSchemaRuntimeService *&schema_runtime_service,
      ObMultiVersionSchemaService &schema_service);

  ObSchemaRuntimeService()
    : schema_service_(nullptr), tablet_schema_resolver_(nullptr)
  {
  }
  ~ObSchemaRuntimeService() {}

  void destroy();
  ObMultiVersionSchemaService *get_schema_service() { return schema_service_; }
  void set_tablet_schema_resolver(TabletSchemaResolver resolver)
  { tablet_schema_resolver_.store(resolver, std::memory_order_release); }
  int resolve_tablet_schema(uint64_t tablet_id,
                            ObMultiVersionSchemaService *&schema_service,
                            uint64_t &schema_tablet_id);
  int resolve_tablet_schema(uint64_t tablet_id,
                            ObMultiVersionSchemaService *&schema_service)
  {
    uint64_t schema_tablet_id = tablet_id;
    return resolve_tablet_schema(tablet_id, schema_service, schema_tablet_id);
  }

private:
  ObMultiVersionSchemaService *schema_service_;
  std::atomic<TabletSchemaResolver> tablet_schema_resolver_;
};

}
}
}

#endif // OCEANBASE_OB_SCHEMA_RUNTIME_SERVICE_H
