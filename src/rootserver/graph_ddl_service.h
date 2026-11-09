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

#pragma once
#include "share/schema/graph_schema.h"
namespace oceanbase
{
namespace rootserver
{
class ObDDLService;
class GraphDDLService final
{
public:
  explicit GraphDDLService(ObDDLService &service) : service_(service) {}
  int create_graph(const share::schema::GraphSchema &definition, const common::ObString &ddl);
  int drop_graph(uint64_t database_id, const common::ObString &name, bool if_exists,
                 const common::ObString &ddl);
private:
  ObDDLService &service_;
  DISALLOW_COPY_AND_ASSIGN(GraphDDLService);
};
} // namespace rootserver
} // namespace oceanbase
