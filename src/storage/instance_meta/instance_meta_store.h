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

#ifndef OCEANBASE_STORAGE_INSTANCE_META_STORE_H_
#define OCEANBASE_STORAGE_INSTANCE_META_STORE_H_

#include <functional>
#include "common/ob_tablet_id.h"
#include "data_plane/transaction/ob_tx_read_snapshot.h"
#include "lib/string/ob_string.h"
#include "share/instance_meta/instance_meta_collection.h"

namespace oceanbase
{
namespace common { class ObIAllocator; }
namespace share { namespace schema { class ObTableSchema; } }
namespace transaction { class ObTransService; class ObTxDesc; }
namespace storage
{
class ObAccessService;

using share::instance_meta::MetaCollection;

class InstanceMetaStore final
{
public:
  static constexpr int64_t MAX_KEY_LENGTH = 512;
  static constexpr int64_t MAX_VALUE_LENGTH = 64 * 1024;
  static constexpr int64_t SCHEMA_VERSION = 1;

  class Transaction final
  {
  public:
    Transaction();
    ~Transaction(); // Rolls back an unfinished transaction.
    bool is_active() const { return descriptor_ != nullptr; }
    bool is_directory_gc() const { return directory_gc_; }
  private:
    friend class InstanceMetaStore;
    InstanceMetaStore *owner_;
    transaction::ObTxDesc *descriptor_;
    transaction::ObTxReadSnapshot snapshot_;
    int64_t deadline_;
    bool read_only_;
    bool scanning_;
    bool directory_guard_;
    bool directory_gc_;
    Transaction *previous_;
    Transaction *next_;
    DISALLOW_COPY_AND_ASSIGN(Transaction);
  };

  // Bounds apply to the key within one collection. Empty byte strings are
  // valid keys; bound presence is therefore separate from bound contents.
  struct KeyRange
  {
    common::ObString lower;
    common::ObString upper;
    bool has_lower = false;
    bool has_upper = false;
    bool include_lower = true;
    bool include_upper = false;
  };

  // Row bytes are borrowed for the duration of the callback. Set stop to end
  // the scan successfully. The callback must not reenter this transaction.
  using RowVisitor = std::function<int(const common::ObString &key,
                                      const common::ObString &value, bool &stop)>;

  InstanceMetaStore(ObAccessService &access, transaction::ObTransService &transactions);
  ~InstanceMetaStore();
  int init(const common::ObTabletID &tablet_id);
  static int build_schema(const common::ObTabletID &tablet_id,
                          share::schema::ObTableSchema &schema);

  int begin(Transaction &tx, int64_t deadline, bool read_only = false);
  // Excludes ordinary KV transactions while a directory page collector marks
  // roots and removes unreachable pages in this transaction.
  int begin_directory_gc(Transaction &tx, int64_t deadline);
  int commit(Transaction &tx);
  int rollback(Transaction &tx);
  // Compaction must retain every snapshot held by a native KV transaction.
  int min_retained_snapshot(share::SCN &snapshot);
  int get(Transaction &tx, MetaCollection collection, const common::ObString &key,
          common::ObIAllocator &allocator, common::ObString &value);
  int get_for_update(Transaction &tx, MetaCollection collection, const common::ObString &key,
                     common::ObIAllocator &allocator, common::ObString &value);
  int insert(Transaction &tx, MetaCollection collection, const common::ObString &key,
             const common::ObString &value);
  int put(Transaction &tx, MetaCollection collection, const common::ObString &key,
          const common::ObString &value);
  int erase(Transaction &tx, MetaCollection collection, const common::ObString &key,
            bool &existed);
  int scan(Transaction &tx, MetaCollection collection, const KeyRange &range,
           const RowVisitor &visitor);

private:
  enum class Write { INSERT, PUT, ERASE, LOCK };
  struct State;
  int check(const Transaction &tx, MetaCollection collection, const common::ObString &key,
            bool write) const;
  int read(Transaction &tx, MetaCollection collection, const common::ObString &key,
           common::ObIAllocator &allocator, common::ObString &value, bool latest);
  int scan_rows(Transaction &tx, MetaCollection collection, const KeyRange &range,
                const RowVisitor &visitor, bool latest);
  int write(Transaction &tx, MetaCollection collection, const common::ObString &key,
            const common::ObString &value, Write operation);
  int end(Transaction &tx, bool commit);
  int begin_impl(Transaction &tx, int64_t deadline, bool read_only,
                 bool directory_gc);
  void release_directory_guard(Transaction &tx);
  ObAccessService &access_;
  transaction::ObTransService &transactions_;
  State *state_;
  DISALLOW_COPY_AND_ASSIGN(InstanceMetaStore);
};

} // namespace storage
} // namespace oceanbase
#endif
