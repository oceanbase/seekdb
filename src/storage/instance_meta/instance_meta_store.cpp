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

#define USING_LOG_PREFIX STORAGE
#include "storage/instance_meta/instance_meta_store.h"
#include <chrono>
#include <condition_variable>
#include <new>
#include <mutex>
#include <unordered_map>
#include "share/schema/ob_table_schema.h"
#include "storage/access/ob_dml_table_plan_access.h"
#include "storage/access/ob_dml_param.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx_storage/ob_access_service.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace transaction;
namespace storage
{
struct InstanceMetaStore::ReadSnapshots
{
  std::mutex mutex;
  Transaction *transactions = nullptr;
  std::unordered_map<const Snapshot *, share::SCN> retained;
};

InstanceMetaStore::Snapshot::Snapshot(std::shared_ptr<ReadSnapshots> readers,
    const share::SCN &version) : readers_(std::move(readers)), version_(version)
{
  std::lock_guard<std::mutex> guard(readers_->mutex);
  readers_->retained.emplace(this, version_);
}

InstanceMetaStore::Snapshot::~Snapshot()
{
  std::lock_guard<std::mutex> guard(readers_->mutex);
  readers_->retained.erase(this);
}

struct InstanceMetaStore::State
{
  ObArenaAllocator allocator{ObMemAttr("InstanceMeta")};
  ObTableSchema schema{&allocator};
  ObTableParam read_plan{allocator};
  data_plane::ObDmlTablePlan write_plan{allocator};
  ObSEArray<uint64_t, 3> columns;
  ObTabletID tablet;
  std::shared_ptr<ReadSnapshots> readers = std::make_shared<ReadSnapshots>();
  std::mutex directory_guard_mutex;
  std::condition_variable directory_guard_changed;
  int64_t ordinary_transactions = 0;
  bool directory_gc_active = false;
};

InstanceMetaStore::Transaction::Transaction()
  : owner_(nullptr), descriptor_(nullptr), snapshot_(), deadline_(0),
    read_only_(false), scanning_(false), directory_guard_(false),
    directory_gc_(false), borrowed_(false), previous_(nullptr), next_(nullptr)
{}

InstanceMetaStore::Transaction::~Transaction()
{
  if (descriptor_ != nullptr) {
    const int ret = borrowed_ ? owner_->detach(*this) : owner_->rollback(*this);
    if (OB_SUCCESS != ret) { LOG_WARN("instance metadata transaction cleanup failed", K(ret)); }
  }
}

InstanceMetaStore::InstanceMetaStore(ObAccessService &access, ObTransService &transactions)
  : access_(access), transactions_(transactions), state_(nullptr)
{}

InstanceMetaStore::~InstanceMetaStore() { delete state_; }

int InstanceMetaStore::build_schema(const ObTabletID &tablet_id, ObTableSchema &schema)
{
  int ret = OB_SUCCESS;
  schema.reset();
  schema.set_table_id(tablet_id.id());
  schema.set_tablet_id(tablet_id);
  schema.set_database_id(OB_SYS_DATABASE_ID);
  schema.set_schema_version(SCHEMA_VERSION);
  schema.set_table_type(SYSTEM_TABLE);
  schema.set_rowkey_column_num(2);
  schema.set_max_used_column_id(OB_APP_MIN_COLUMN_ID + 2);
  schema.set_micro_index_clustered(false);
  schema.set_row_store_type(FLAT_ROW_STORE);
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(schema.set_table_name("instance_metadata"))) {
  } else {
    const char *names[] = {"collection_id", "key", "value"};
    for (int64_t i = 0; OB_SUCC(ret) && i < 3; ++i) {
      ObColumnSchemaV2 column;
      column.set_table_id(tablet_id.id());
      column.set_column_id(OB_APP_MIN_COLUMN_ID + i);
      column.set_schema_version(SCHEMA_VERSION);
      column.set_nullable(false);
      column.set_rowkey_position(i < 2 ? i + 1 : 0);
      column.set_order_in_rowkey(ASC);
      ObObjMeta type;
      if (i == 0) { type.set_uint64(); }
      else { type.set_varbinary(); }
      column.set_meta_type(type);
      column.set_data_length(i == 0 ? int64_t(sizeof(uint64_t))
                                   : (i == 1 ? MAX_KEY_LENGTH : MAX_VALUE_LENGTH));
      if (OB_FAIL(column.set_column_name(names[i]))) {
      } else if (OB_FAIL(schema.add_column(column))) {
      }
    }
  }
  return ret;
}

int InstanceMetaStore::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  State *state = nullptr;
  if (state_ != nullptr) {
    ret = OB_INIT_TWICE;
  } else if (nullptr == (state = new (std::nothrow) State())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(build_schema(tablet_id, state->schema))) {
  } else {
    state->tablet = tablet_id;
    for (int64_t i = 0; OB_SUCC(ret) && i < 3; ++i) {
      ret = state->columns.push_back(OB_APP_MIN_COLUMN_ID + i);
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(state->read_plan.convert(state->schema, state->columns,
                                               sql::ObStoragePushdownFlag()))) {
    } else if (OB_FAIL(state->write_plan.build(&state->schema, SCHEMA_VERSION, state->columns))) {
    } else {
      state_ = state;
      state = nullptr;
    }
  }
  delete state;
  return ret;
}

int InstanceMetaStore::begin(Transaction &tx, const int64_t deadline, const bool read_only)
{
  return begin_impl(tx, deadline, read_only, false);
}

int InstanceMetaStore::begin_directory_gc(Transaction &tx, const int64_t deadline)
{
  return begin_impl(tx, deadline, false, true);
}

int InstanceMetaStore::begin_read(Transaction &tx, int64_t deadline,
    const SnapshotAcquirer &acquire)
{
  return acquire ? begin_impl(tx, deadline, true, false, nullptr, &acquire)
                 : OB_INVALID_ARGUMENT;
}

int InstanceMetaStore::begin_weak_read(Transaction &tx, int64_t deadline)
{
  return begin_read(tx, deadline, [&](share::SCN &snapshot) {
    return transactions_.get_weak_read_snapshot_version(-1, snapshot);
  });
}

int InstanceMetaStore::attach(Transaction &tx, ObTxDesc &descriptor, const int64_t deadline)
{
  if (!descriptor.is_in_tx() || descriptor.is_shadow()
      || !descriptor.is_RC_isolevel()) {
    return OB_INVALID_ARGUMENT;
  }
  return begin_impl(tx, deadline, descriptor.is_rdonly(), false, &descriptor);
}

int InstanceMetaStore::begin_impl(Transaction &tx, const int64_t deadline,
    const bool read_only, const bool directory_gc, ObTxDesc *borrowed,
    const SnapshotAcquirer *acquire)
{
  int ret = OB_SUCCESS;
  if (state_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (tx.is_active() || tx.directory_guard_) {
    ret = OB_INIT_TWICE;
  } else if (deadline <= ObTimeUtility::current_time()) {
    ret = OB_TIMEOUT;
  } else {
    {
      std::unique_lock<std::mutex> guard(state_->directory_guard_mutex);
      while ((directory_gc ? state_->directory_gc_active
                            || state_->ordinary_transactions != 0
                           : state_->directory_gc_active)
             && ret == OB_SUCCESS) {
        const int64_t remain = deadline - ObTimeUtility::current_time();
        if (remain <= 0) {
          ret = OB_TIMEOUT;
        } else {
          state_->directory_guard_changed.wait_for(
              guard, std::chrono::microseconds(remain));
        }
      }
      if (ret == OB_SUCCESS && deadline <= ObTimeUtility::current_time()) {
        ret = OB_TIMEOUT;
      }
      if (ret == OB_SUCCESS) {
        if (directory_gc) { state_->directory_gc_active = true; }
        else { ++state_->ordinary_transactions; }
        tx.directory_guard_ = true;
        tx.directory_gc_ = directory_gc;
      }
    }
  }
  if (ret == OB_SUCCESS) {
    ObTxParam param;
    param.access_mode_ = read_only ? ObTxAccessMode::RD_ONLY : ObTxAccessMode::RW;
    param.isolation_ = ObTxIsolationLevel::RC;
    param.timeout_us_ = deadline - ObTimeUtility::current_time();
    tx.descriptor_ = borrowed;
    if (borrowed == nullptr && OB_FAIL(transactions_.acquire_tx(tx.descriptor_))) {
      release_directory_guard(tx);
    } else {
      tx.borrowed_ = borrowed != nullptr;
      tx.owner_ = this;
      tx.deadline_ = deadline;
      tx.read_only_ = read_only;
      // Register before acquiring a timestamp. A not-yet-published snapshot
      // conservatively prevents compaction from advancing past this reader.
      {
        std::lock_guard<std::mutex> guard(state_->readers->mutex);
        tx.next_ = state_->readers->transactions;
        if (tx.next_ != nullptr) { tx.next_->previous_ = &tx; }
        state_->readers->transactions = &tx;
      }
      ObTxReadSnapshot snapshot;
      if (!tx.borrowed_ && OB_FAIL(transactions_.start_tx(*tx.descriptor_, param))) {
      } else if (OB_FAIL(transactions_.get_read_snapshot(*tx.descriptor_, param.isolation_,
                                                        deadline, snapshot))) {
      } else if (acquire != nullptr) {
        share::SCN selected;
        if (OB_FAIL((*acquire)(selected))) {
        } else if (!selected.is_valid() || selected.is_min() || selected.is_max()) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          snapshot.core_.version_ = selected;
        }
      }
      if (OB_SUCC(ret)) {
        std::lock_guard<std::mutex> guard(state_->readers->mutex);
        ret = tx.snapshot_.assign(snapshot);
      }
      if (OB_FAIL(ret)) {
        const int cleanup_ret = tx.borrowed_ ? detach(tx) : rollback(tx);
        if (cleanup_ret != OB_SUCCESS) { LOG_WARN("failed to close metadata transaction", K(cleanup_ret)); }
      }
    }
  }
  return ret;
}

void InstanceMetaStore::release_directory_guard(Transaction &tx)
{
  if (tx.directory_guard_) {
    {
      std::lock_guard<std::mutex> guard(state_->directory_guard_mutex);
      if (tx.directory_gc_) { state_->directory_gc_active = false; }
      else { --state_->ordinary_transactions; }
      tx.directory_guard_ = false;
      tx.directory_gc_ = false;
    }
    state_->directory_guard_changed.notify_all();
  }
}

int InstanceMetaStore::end(Transaction &tx, const bool do_commit)
{
  int ret = OB_SUCCESS;
  if (tx.owner_ != this || !tx.is_active() || tx.scanning_ || tx.borrowed_) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = do_commit ? transactions_.commit_tx(*tx.descriptor_, tx.deadline_)
                    : transactions_.rollback_tx(*tx.descriptor_);
    const int release_ret = transactions_.release_tx(*tx.descriptor_);
    if (ret == OB_SUCCESS) { ret = release_ret; }
    reset_transaction(tx);
  }
  return ret;
}

int InstanceMetaStore::detach(Transaction &tx)
{
  if (tx.owner_ != this || !tx.is_active() || tx.scanning_ || !tx.borrowed_) {
    return OB_INVALID_ARGUMENT;
  }
  reset_transaction(tx);
  return OB_SUCCESS;
}

void InstanceMetaStore::reset_transaction(Transaction &tx)
{
  // The owner may already have released the native descriptor. Cleanup only
  // touches our reader registration and never dereferences that descriptor.
  {
    std::lock_guard<std::mutex> guard(state_->readers->mutex);
    if (tx.previous_ != nullptr) { tx.previous_->next_ = tx.next_; }
    else { state_->readers->transactions = tx.next_; }
    if (tx.next_ != nullptr) { tx.next_->previous_ = tx.previous_; }
    tx.previous_ = tx.next_ = nullptr;
    tx.descriptor_ = nullptr;
    tx.owner_ = nullptr;
    tx.borrowed_ = false;
    tx.snapshot_.reset();
  }
  release_directory_guard(tx);
}

int InstanceMetaStore::commit(Transaction &tx) { return end(tx, true); }
int InstanceMetaStore::rollback(Transaction &tx) { return end(tx, false); }

int InstanceMetaStore::retain_snapshot(const Transaction &tx, SnapshotHandle &snapshot)
{
  if (state_ == nullptr) { return OB_NOT_INIT; }
  if (snapshot || tx.owner_ != this || !tx.is_active() || !tx.snapshot_.is_valid()) {
    return OB_INVALID_ARGUMENT;
  }
  // tx remains registered throughout construction: no gap between the short
  // transaction's retention and the independent handle's retention.
  auto *retained = new (std::nothrow) Snapshot(state_->readers, tx.snapshot_.core_.version_);
  if (retained == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  snapshot.reset(retained);
  return OB_SUCCESS;
}

int InstanceMetaStore::min_retained_snapshot(share::SCN &snapshot)
{
  int ret = OB_SUCCESS;
  share::SCN weak_snapshot;
  if (state_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(transactions_.get_read_snapshot_version(
      ObTimeUtility::current_time() + 1000000, snapshot))) {
  } else if (OB_FAIL(transactions_.get_weak_read_snapshot_version(-1, weak_snapshot))) {
  } else {
    // Future weak readers can select an older timestamp than a strong reader.
    // Sample both native horizons before inspecting registered readers. Reads
    // at an older fixed snapshot must already have a transaction or lease.
    if (weak_snapshot < snapshot) { snapshot = weak_snapshot; }
    std::lock_guard<std::mutex> guard(state_->readers->mutex);
    for (Transaction *tx = state_->readers->transactions; tx != nullptr; tx = tx->next_) {
      if (!tx->snapshot_.is_valid()) { snapshot.set_min(); }
      else if (tx->snapshot_.core_.version_ < snapshot) { snapshot = tx->snapshot_.core_.version_; }
    }
    for (const auto &held : state_->readers->retained) {
      if (held.second < snapshot) { snapshot = held.second; }
    }
  }
  return ret;
}

int InstanceMetaStore::check(const Transaction &tx, MetaCollection collection,
                            const ObString &key, const bool write) const
{
  int ret = OB_SUCCESS;
  if (state_ == nullptr) { ret = OB_NOT_INIT; }
  else if (tx.owner_ != this || !tx.is_active() || tx.scanning_
           || static_cast<uint64_t>(collection) == 0 || key.length() < 0
           || (key.length() > 0 && key.ptr() == nullptr) || (write && tx.read_only_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (key.length() > MAX_KEY_LENGTH) { ret = OB_SIZE_OVERFLOW; }
  else if (tx.deadline_ <= ObTimeUtility::current_time()) { ret = OB_TIMEOUT; }
  return ret;
}

int InstanceMetaStore::scan_rows(Transaction &tx, MetaCollection collection, const KeyRange &range,
                                 const RowVisitor &visitor, const bool latest)
{
  int ret = check(tx, collection, range.has_lower ? range.lower : ObString(), false);
  if (OB_SUCC(ret) && range.has_upper) { ret = check(tx, collection, range.upper, false); }
  if (OB_SUCC(ret) && !visitor) { ret = OB_INVALID_ARGUMENT; }
  if (OB_SUCC(ret)) {
    ObArenaAllocator allocator(ObMemAttr("InstanceMetaRd"));
    ObTableScanParam param;
    ObNewRange keys;
    ObObj lower[2], upper[2];
    lower[0].set_uint64(static_cast<uint64_t>(collection));
    upper[0] = lower[0];
    if (range.has_lower) { lower[1].set_varbinary(range.lower); } else { lower[1].set_min_value(); }
    if (range.has_upper) { upper[1].set_varbinary(range.upper); } else { upper[1].set_max_value(); }
    keys.table_id_ = state_->schema.get_table_id();
    keys.start_key_.assign(lower, 2);
    keys.end_key_.assign(upper, 2);
    if (range.include_lower) { keys.border_flag_.set_inclusive_start(); }
    if (range.include_upper) { keys.border_flag_.set_inclusive_end(); }
    param.index_id_ = state_->schema.get_table_id();
    param.tablet_id_ = state_->tablet;
    param.schema_tablet_id_ = state_->tablet;
    param.table_param_ = &state_->read_plan;
    param.schema_version_ = SCHEMA_VERSION;
    param.runtime_schema_version_ = SCHEMA_VERSION;
    param.timeout_ = tx.deadline_;
    param.tx_lock_timeout_ = tx.deadline_;
    param.trans_desc_ = tx.descriptor_;
    param.tx_id_ = tx.descriptor_->get_tx_id();
    param.scan_flag_.scan_order_ = ObQueryFlag::Forward;
    param.allocator_ = &allocator;
    param.scan_allocator_ = &allocator;
    param.reserved_cell_count_ = 3;
    param.limit_param_.limit_ = -1;
    param.limit_param_.offset_ = 0;
    // The current transaction sequence includes preceding KV writes. Ordinary
    // reads retain the pinned commit snapshot; locked reads use the snapshot
    // acquired after obtaining the row lock.
    if (OB_FAIL(transactions_.get_read_snapshot(*tx.descriptor_, ObTxIsolationLevel::RC,
                                               tx.deadline_, param.snapshot_))) {
    }
    if (OB_SUCC(ret) && !latest) { param.snapshot_.core_.version_ = tx.snapshot_.core_.version_; }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(param.column_ids_.assign(state_->columns))) {
    } else if (OB_FAIL(param.key_ranges_.push_back(keys))) {
    } else {
      ObNewRowIterator *iter = nullptr;
      tx.scanning_ = true;
      ret = access_.table_scan(param, iter);
      bool stop = false;
      blocksstable::ObDatumRow *row = nullptr;
      while (OB_SUCC(ret) && !stop
          && OB_SUCC(ret = static_cast<ObTableScanIterator *>(iter)->get_next_row(row))) {
        if (row == nullptr || row->get_column_count() != 3
            || row->storage_datums_[0].is_null()
            || row->storage_datums_[1].is_null() || row->storage_datums_[2].is_null()
            || row->storage_datums_[0].get_uint64() != static_cast<uint64_t>(collection)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ret = visitor(row->storage_datums_[1].get_string(), row->storage_datums_[2].get_string(), stop);
        }
      }
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
      if (iter != nullptr) {
        const int cleanup_ret = access_.revert_scan_iter(iter);
        if (OB_SUCC(ret)) { ret = cleanup_ret; }
      }
      tx.scanning_ = false;
    }
  }
  return ret;
}

int InstanceMetaStore::scan(Transaction &tx, MetaCollection collection, const KeyRange &range,
                            const RowVisitor &visitor)
{ return scan_rows(tx, collection, range, visitor, false); }

int InstanceMetaStore::read(Transaction &tx, MetaCollection collection, const ObString &key,
                            ObIAllocator &allocator, ObString &value, bool latest)
{
  value.reset();
  KeyRange range;
  range.has_lower = range.has_upper = range.include_lower = range.include_upper = true;
  range.lower = range.upper = key;
  bool found = false;
  int ret = scan_rows(tx, collection, range,
      [&](const ObString &, const ObString &bytes, bool &stop) {
        found = true;
        stop = true;
        return ob_write_string(allocator, bytes, value);
      }, latest);
  if (OB_SUCC(ret) && !found) { ret = OB_ENTRY_NOT_EXIST; }
  return ret;
}

int InstanceMetaStore::get(Transaction &tx, MetaCollection collection, const ObString &key,
                           ObIAllocator &allocator, ObString &value)
{ return read(tx, collection, key, allocator, value, false); }

int InstanceMetaStore::write(Transaction &tx, MetaCollection collection, const ObString &key,
                             const ObString &value, const Write operation)
{
  int ret = check(tx, collection, key, true);
  if (OB_SUCC(ret) && (value.length() < 0 || (value.length() > 0 && value.ptr() == nullptr))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCC(ret) && value.length() > MAX_VALUE_LENGTH) { ret = OB_SIZE_OVERFLOW; }
  if (OB_SUCC(ret)) {
    ObArenaAllocator allocator(ObMemAttr("InstanceMetaWr"));
    ObStoreCtxGuard context;
    ObDMLBaseParam param;
    ObTimeZoneInfo timezone;
    ObTxReadSnapshot snapshot;
    // Each write uses a current snapshot, while ordinary reads retain the
    // transaction's pinned snapshot. The engine arbitrates concurrent writers.
    if (OB_FAIL(transactions_.get_read_snapshot(*tx.descriptor_, ObTxIsolationLevel::RC,
                                               tx.deadline_, snapshot))) {
    } else if (OB_FAIL(access_.get_write_store_ctx_guard(tx.deadline_, *tx.descriptor_,
                       snapshot, 0, param.write_flag_, context))) {
    } else {
      param.timeout_ = tx.deadline_;
      param.schema_version_ = SCHEMA_VERSION;
      param.runtime_schema_version_ = SCHEMA_VERSION;
      param.tz_info_ = &timezone;
      param.table_param_ = ObDmlTablePlanAccess::get(state_->write_plan);
      param.check_schema_version_ = false;
      param.dml_allocator_ = &allocator;
      param.store_ctx_guard_ = &context;
      ret = param.snapshot_.assign(snapshot);
      class Row final : public blocksstable::ObDatumRowIterator
      {
      public:
        blocksstable::ObDatumRow row;
        bool consumed = false;
        int get_next_row(blocksstable::ObDatumRow *&out) override {
          if (consumed) { return OB_ITER_END; }
          consumed = true;
          out = &row;
          return OB_SUCCESS;
        }
      } rows;
      const bool key_only = operation == Write::LOCK;
      ObObj cells[3];
      cells[0].set_uint64(static_cast<uint64_t>(collection));
      cells[1].set_varbinary(key);
      cells[2].set_varbinary(value);
      if (OB_SUCC(ret)) { ret = rows.row.init(key_only ? 2 : 3); }
      for (int64_t i = 0; OB_SUCC(ret) && i < (key_only ? 2 : 3); ++i) {
        ret = rows.row.storage_datums_[i].from_obj_enhance(cells[i]);
      }
      rows.row.row_flag_.set_flag(blocksstable::ObDmlFlag::DF_INSERT);
      int64_t affected = 0;
      if (OB_FAIL(ret)) {
      } else if (operation == Write::INSERT) {
        ret = access_.insert_rows(state_->tablet, *tx.descriptor_, param, state_->columns, &rows, affected);
      } else if (operation == Write::PUT) {
        ret = access_.put_rows(state_->tablet, *tx.descriptor_, param, state_->columns, &rows, affected);
      } else if (operation == Write::ERASE) {
        ret = access_.delete_rows(state_->tablet, *tx.descriptor_, param, state_->columns, &rows, affected);
      } else {
        ret = access_.lock_rows(state_->tablet, *tx.descriptor_, param, tx.deadline_, LF_NONE, &rows, affected);
      }
    }
    // context is destroyed before the caller can commit or start another
    // operation, merging its write sequence/result into the transaction.
  }
  return ret;
}

int InstanceMetaStore::get_for_update(Transaction &tx, MetaCollection collection, const ObString &key,
                                      ObIAllocator &allocator, ObString &value)
{
  value.reset();
  int ret = OB_SUCCESS;
  // AccessService returns a transient conflict instead of waiting for the
  // holder. This direct KV caller owns the wait, just as the SQL executor does
  // for SELECT FOR UPDATE. Retry only the lock operation, preserving all prior
  // work in a borrowed DDL transaction and its original deadline.
  while (OB_TRY_LOCK_ROW_CONFLICT ==
      (ret = write(tx, collection, key, ObString(), Write::LOCK))) {
    if (ObTimeUtility::current_time() >= tx.deadline_) {
      ret = OB_TIMEOUT;
      break;
    } else if (OB_FAIL(THIS_WORKER.check_status())) {
      break;
    }
    ob_usleep(1000);
  }
  if (OB_SUCC(ret)) { ret = read(tx, collection, key, allocator, value, true); }
  return ret;
}
int InstanceMetaStore::insert(Transaction &tx, MetaCollection collection, const ObString &key,
                              const ObString &value)
{ return write(tx, collection, key, value, Write::INSERT); }
int InstanceMetaStore::put(Transaction &tx, MetaCollection collection, const ObString &key,
                           const ObString &value)
{ return write(tx, collection, key, value, Write::PUT); }
int InstanceMetaStore::erase(Transaction &tx, MetaCollection collection, const ObString &key, bool &existed)
{
  ObArenaAllocator allocator(ObMemAttr("InstanceMetaDel"));
  ObString old_value;
  int ret = get_for_update(tx, collection, key, allocator, old_value);
  existed = ret == OB_SUCCESS;
  if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_SUCCESS; }
  else if (OB_SUCC(ret)) { ret = write(tx, collection, key, old_value, Write::ERASE); }
  return ret;
}

} // namespace storage
} // namespace oceanbase
