/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef OCEANBASE_ROOTSERVER_NAMESPACE_SCHEMA_PUBLICATION_H_
#define OCEANBASE_ROOTSERVER_NAMESPACE_SCHEMA_PUBLICATION_H_

#include "storage/instance_meta/instance_meta_store.h"

namespace oceanbase {
namespace common { class ObMySQLTransaction; }
namespace share { namespace schema {
class ObMultiVersionSchemaService;
class ObSchemaGetterGuard;
} }
namespace rootserver {

// Stages a DDL publication in the SQL owner's native transaction. Keep this
// object alive until that owner has committed/rolled back; then detach().
class NamespaceSchemaPublication final
{
public:
  NamespaceSchemaPublication(storage::InstanceMetaStore &store, uint64_t namespace_id)
      : store_(store), namespace_id_(namespace_id) {}
  int stage(common::ObMySQLTransaction &sql,
             share::schema::ObMultiVersionSchemaService &schema_service,
             int64_t schema_version);
  // Explicit installation boundary, before the template or user forks exist.
  int initialize(share::schema::ObSchemaGetterGuard &guard);
  int detach();
private:
  storage::InstanceMetaStore &store_;
  const uint64_t namespace_id_;
  storage::InstanceMetaStore::Transaction transaction_;
  DISALLOW_COPY_AND_ASSIGN(NamespaceSchemaPublication);
};

} // namespace rootserver
} // namespace oceanbase
#endif
