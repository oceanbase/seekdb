/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "rootserver/fork_table/table_storage_layouts.h"
#include "storage/instance_meta/storage_schema_history.h"
#include "storage/ob_storage_schema.h"
#include "storage/ob_common_id_utils.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/worker.h"

namespace oceanbase {
namespace rootserver {
using namespace common;
using namespace storage;

int TableStorageLayouts::read_at(InstanceMetaStore &store, const share::SCN &target,
    int64_t deadline, ObIArray<Definition> &definitions)
{
  definitions.reuse();
  InstanceMetaStore::Transaction tx;
  int ret = StorageSchemaHistory::begin_read_at(store, tx, target, deadline);
  if (ret == OB_SUCCESS) {
    ret = store.scan(tx, MetaCollection::TABLE_STORAGE_LAYOUTS, {},
        [&](const ObString &key, const ObString &value, bool &) {
      Definition item;
      int64_t ns = 0, table = 0, layout = 0, pos = 0;
      int rc = key.length() != 16 || value.length() != 8 ? OB_CHECKSUM_ERROR : OB_SUCCESS;
      if (rc == OB_SUCCESS) { rc = serialization::decode_i64(key.ptr(), key.length(), pos, &ns); }
      if (rc == OB_SUCCESS) { rc = serialization::decode_i64(key.ptr(), key.length(), pos, &table); }
      pos = 0;
      if (rc == OB_SUCCESS) { rc = serialization::decode_i64(value.ptr(), value.length(), pos, &layout); }
      if (rc == OB_SUCCESS && (ns <= 0 || table <= 0 || layout <= 0)) { rc = OB_CHECKSUM_ERROR; }
      item.namespace_id = ns;
      item.table_id = table;
      item.layout_id = layout;
      if (rc == OB_SUCCESS) { rc = definitions.push_back(item); }
      return rc;
    });
  }
  // A scan visitor cannot reenter its transaction. Read heads after closing
  // the scan, while the very same native snapshot is still retained.
  StorageSchemaHistory history(store, tx);
  for (int64_t i = 0; ret == OB_SUCCESS && i < definitions.count(); ++i) {
    ret = history.read_version(definitions.at(i).layout_id, definitions.at(i).schema_version);
  }
  if (tx.is_active()) {
    const int end = store.commit(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  if (ret != OB_SUCCESS) { definitions.reuse(); }
  return ret;
}

int TableStorageLayouts::attach(ObMySQLTransaction &owner, transaction::ObTxDesc &native,
    InstanceMetaStore &store, std::shared_ptr<InstanceMetaStore::Transaction> &tx)
{
  tx.reset(new (std::nothrow) InstanceMetaStore::Transaction());
  int ret = tx == nullptr ? OB_ALLOCATE_MEMORY_FAILED : owner.retain_until_end(tx);
  if (ret == OB_SUCCESS) { ret = store.attach(*tx, native, THIS_WORKER.get_timeout_ts()); }
  return ret;
}

int TableStorageLayouts::bind(ObCreateTabletSchema &schema)
{
  int ret = OB_SUCCESS;
  if (namespace_id_ == 0 || namespace_id_ == OB_INVALID_ID || !schema.is_valid()
      || schema.is_column_info_simplified()) { return OB_INVALID_ARGUMENT; }
  char key_bytes[16];
  int64_t pos = 0;
  if (OB_FAIL(serialization::encode_i64(key_bytes, sizeof(key_bytes), pos, namespace_id_))) {
  } else if (OB_FAIL(serialization::encode_i64(key_bytes, sizeof(key_bytes), pos, schema.get_table_id()))) {
  }
  const ObString key(sizeof(key_bytes), key_bytes);
  ObArenaAllocator allocator(ObMemAttr("TableLayout"));
  ObString value;
  int64_t layout_id = 0;
  if (ret == OB_SUCCESS) {
    ret = store_.get(tx_, MetaCollection::TABLE_STORAGE_LAYOUTS, key, allocator, value);
    if (ret == OB_ENTRY_NOT_EXIST) {
      share::ObCommonID allocated;
      ret = ObCommonIDUtils::gen_unique_id(allocated);
      if (ret == OB_SUCCESS) {
        layout_id = allocated.id();
        char encoded[8];
        pos = 0;
        ret = serialization::encode_i64(encoded, sizeof(encoded), pos, layout_id);
        // INSERT is deliberately non-waiting on native row conflicts. A DDL
        // can arrive here before taking the Namespace publication lock, while
        // first materialization already holds that lock. Waiting on this row
        // would invert their lock order. The losing owner must roll back.
        if (ret == OB_SUCCESS) {
          ret = store_.insert(tx_, MetaCollection::TABLE_STORAGE_LAYOUTS,
              key, ObString(sizeof(encoded), encoded));
        }
        if (ret == OB_ERR_PRIMARY_KEY_DUPLICATE) { ret = OB_TRY_LOCK_ROW_CONFLICT; }
        if (ret == OB_SUCCESS) { ret = StorageSchemaHistory(store_, tx_).create(layout_id, schema); }
      }
    } else if (ret == OB_SUCCESS) {
      pos = 0;
      ret = serialization::decode_i64(value.ptr(), value.length(), pos, &layout_id);
      if (ret == OB_SUCCESS && (pos != value.length() || layout_id <= 0)) { ret = OB_CHECKSUM_ERROR; }
    }
  }
  if (ret == OB_SUCCESS) { schema.set_storage_layout_id(layout_id); }
  return ret;
}

int TableStorageLayouts::prepare_create(ObCreateTabletSchema &schema)
{
  return bind(schema);
}

int TableStorageLayouts::publish(ObCreateTabletSchema &schema)
{
  int ret = bind(schema);
  StorageSchemaHistory history(store_, tx_);
  int64_t version = OB_INVALID_VERSION;
  if (ret == OB_SUCCESS) { ret = history.read_version(schema.get_storage_layout_id(), version); }
  // CREATE may have seeded this exact definition earlier in this transaction.
  // A table version identifies an immutable definition, not a content hash.
  if (ret == OB_SUCCESS && version > schema.get_schema_version()) { ret = OB_STATE_NOT_MATCH; }
  if (ret == OB_SUCCESS && version < schema.get_schema_version()) {
    ret = history.publish(schema.get_storage_layout_id(), schema);
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
