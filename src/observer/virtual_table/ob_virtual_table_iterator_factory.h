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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLE_ITERATOR_FACTORY_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLE_ITERATOR_FACTORY_

#include "share/ob_define.h"
#include "lib/container/ob_se_array.h"
#include "share/ob_virtual_table_iterator.h"
#include "share/config/ob_server_config.h"
#include "sql/engine/table/ob_i_virtual_table_iterator_factory.h"

namespace oceanbase
{
namespace common
{
class ObVTableScanParam;
class ObVirtualTableIterator;
class ObServerConfig;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObTableSchema;
}
}
namespace rootserver
{
class ObRootService;
}
namespace observer
{
class ObVTIterCreator
{
public:
  ObVTIterCreator(rootserver::ObRootService &root_service, common::ObAddr &addr, common::ObServerConfig *config = NULL)
    : root_service_(root_service),
      addr_(addr),
      config_(config)
  {}
  virtual ~ObVTIterCreator() {}
  int get_latest_expected_schema(const uint64_t tenant_id,
                                 const uint64_t table_id,
                                 const int64_t table_version,
                                 share::schema::ObSchemaGetterGuard &schema_guard,
                                 const share::schema::ObTableSchema *&t_schema);
  virtual int create_vt_iter(common::ObVTableScanParam &params,
                             common::ObVirtualTableIterator *&vt_iter);
  virtual int check_can_create_iter(common::ObVTableScanParam &params);
  rootserver::ObRootService &get_root_service() { return root_service_; }

public:
  int check_is_index(const share::schema::ObTableSchema &table,
      const char *index_name, bool &is_index) const;

private:
  rootserver::ObRootService &root_service_;
  common::ObAddr &addr_;
  common::ObServerConfig *config_;
};

class ObVirtualTableIteratorFactory : public sql::ObIVirtualTableIteratorFactory
{
public:
  explicit ObVirtualTableIteratorFactory(ObVTIterCreator &vt_iter_creator);
  ObVirtualTableIteratorFactory(rootserver::ObRootService &root_service, common::ObAddr &addr,
                                common::ObServerConfig *config = NULL);
  virtual ~ObVirtualTableIteratorFactory();
  virtual int create_virtual_table_iterator(common::ObVTableScanParam &params,
                                            common::ObVirtualTableIterator *&vt_iter);
  virtual int revert_virtual_table_iterator(common::ObVirtualTableIterator *vt_iter);
  virtual int check_can_create_iter(common::ObVTableScanParam &params);
  ObVTIterCreator &get_vt_iter_creator() { return vt_iter_creator_; }
private:
  ObVTIterCreator vt_iter_creator_;
  DISALLOW_COPY_AND_ASSIGN(ObVirtualTableIteratorFactory);
};

}
}
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLE_ITERATOR_FACTORY_ */
