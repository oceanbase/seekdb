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

#define USING_LOG_PREFIX RS_COMPACTION
#include "rootserver/freeze/ob_checksum_validator.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "storage/compaction/ob_table_ckm_items.h"
#include "storage/compaction/physical_merge_candidate.h"
#include "storage/ls/ob_ls.h"
#include "storage/ls/ob_ls_tablet_service.h"
#include "storage/tablet/ob_tablet.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_schema_service.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include <map>
#include <memory>

namespace oceanbase {
namespace rootserver {
using namespace common;
using namespace share;
using namespace share::schema;
using namespace storage;
using namespace compaction;
namespace {

struct MergeTable
{
  explicit MergeTable(const TableStorageLayouts::Definition &input)
    : definition(input), allocator(ObMemAttr("MergeDefinition")) {}
  ~MergeTable() { if (owned_schema != nullptr) { owned_schema->~ObTableSchema(); } }
  const TableStorageLayouts::Definition &definition;
  ObArenaAllocator allocator;
  ObTableSchema *owned_schema = nullptr;
  const ObTableSchema *schema = nullptr;
  ObArray<ObTabletID> tablets;
  bool retired = false;
  bool excluded = false;
};
using MergeTables = std::map<uint64_t, std::unique_ptr<MergeTable>>;

int physical_tablets(uint64_t owner, const ObTableSchema &schema, ObIArray<ObTabletID> &ids)
{
  ids.reuse();
  int ret = schema.get_tablet_ids(ids);
  for (int64_t i = 0; ret == OB_SUCCESS && i < ids.count(); ++i) {
    uint64_t physical = 0;
    ret = NamespaceForkKernelPrototype::storage_object_id(owner, ids.at(i).id(), physical);
    if (ret == OB_SUCCESS) { ids.at(i) = ObTabletID(physical); }
  }
  return ret;
}

int load_definition(MergeTable &table, ObSchemaGetterGuard &current,
    ObSchemaService &backend, ObMySQLProxy &sql)
{
  const auto &ref = table.definition;
  const ObTableSchema *cached = nullptr;
  int ret = current.get_table_schema(ref.table_id, cached);
  if (ret == OB_TABLE_NOT_EXIST) { ret = OB_SUCCESS; }
  if (ret == OB_SUCCESS && cached != nullptr && cached->get_schema_version() == ref.schema_version) {
    table.schema = cached;
  } else if (ret == OB_SUCCESS) {
    ObRefreshSchemaStatus status;
    ret = backend.get_table_schema(status, ref.table_id, ref.schema_version,
        sql, table.allocator, table.owned_schema);
    table.schema = table.owned_schema;
  }
  if (ret == OB_SUCCESS && (table.schema == nullptr
      || table.schema->get_table_id() != ref.table_id
      || table.schema->get_schema_version() != ref.schema_version)) {
    ret = OB_SCHEMA_EAGAIN;
  }
  if (ret == OB_SUCCESS) {
    table.excluded = !table.schema->has_tablet()
        || (table.schema->is_index_table() ? !table.schema->can_read_index()
                                          : !table.schema->should_check_major_merge_progress());
    if (!table.excluded) { ret = physical_tablets(ref.namespace_id, *table.schema, table.tablets); }
  }
  if (ret != OB_SUCCESS) { LOG_WARN("cannot read merge object's exact definition", K(ret), K(ref)); }
  return ret;
}

// Called only when native inputs have disappeared or changed identity. A
// missing report is never a reason to retire a table. Read committed catalog
// evidence, then compare actual bindings, including same-count replacements.
int check_retirement(MergeTable &table, ObSchemaService &backend, ObMySQLProxy &sql)
{
  int ret = OB_SUCCESS;
  ObSqlString query;
  ObISQLClient::ReadResult result;
  int64_t version = -1, deleted = 0;
  const auto &ref = table.definition;
  if (OB_FAIL(query.append_fmt("SELECT schema_version,is_deleted FROM oceanbase.__all_table_history "
      "WHERE table_id=%lu ORDER BY schema_version DESC LIMIT 1", ref.table_id))) {
  } else if (OB_FAIL(sql.read(result, query.ptr()))) {
  } else if (OB_FAIL(result.get_result()->next())) {
    // Missing history is not a committed DROP. The core catalog may not have
    // this ordinary history row; native inputs must remain available for it.
    if (ret == OB_ITER_END) { ret = OB_SCHEMA_EAGAIN; }
  } else if (OB_FAIL(result.get_result()->get_int("schema_version", version))) {
  } else if (OB_FAIL(result.get_result()->get_int("is_deleted", deleted))) {
  }
  const int closed = result.close();
  result.reset();
  if (ret == OB_SUCCESS) { ret = closed; }
  if (ret == OB_SUCCESS && deleted != 0 && version > ref.schema_version) {
    table.retired = true;
  } else if (ret == OB_SUCCESS && version > ref.schema_version) {
    ObArenaAllocator allocator(ObMemAttr("MergeRetirement"));
    ObTableSchema *latest = nullptr;
    ObRefreshSchemaStatus status;
    ObArray<ObTabletID> ids;
    if (OB_FAIL(backend.get_table_schema(status, ref.table_id, version, sql, allocator, latest))) {
    } else if (latest == nullptr) {
      ret = OB_SCHEMA_EAGAIN;
    } else if (OB_FAIL(physical_tablets(ref.namespace_id, *latest, ids))) {
    } else {
      table.retired = ids.count() != table.tablets.count();
      for (int64_t i = 0; !table.retired && i < ids.count(); ++i) {
        table.retired = ids.at(i) != table.tablets.at(i);
      }
    }
    if (latest != nullptr) { latest->~ObTableSchema(); }
  }
  if (ret == OB_SUCCESS && table.retired) {
    LOG_INFO("merge object retired by committed catalog change", K(ref), K(version), K(deleted));
  }
  return ret;
}

int inspect_table(MergeTable &table, const ObFreezeInfo &freeze, ObLS &ls,
    ObSchemaService &backend, ObMySQLProxy &sql, ObUncompactInfo &pending)
{
  int ret = OB_SUCCESS;
  const auto &ref = table.definition;
  for (int64_t i = 0; ret == OB_SUCCESS && !table.retired && i < table.tablets.count(); ++i) {
    const ObTabletID id = table.tablets.at(i);
    ObTabletHandle handle;
    PhysicalMergeCandidate candidate;
    ret = ls.get_tablet_svr()->get_tablet(id, handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK);
    bool changed = ret == OB_TABLET_NOT_EXIST;
    if (changed) { ret = OB_SUCCESS; }
    if (ret == OB_SUCCESS && !changed) {
      ret = candidate.load(*handle.get_obj());
      if (ret == OB_SUCCESS) {
        changed = candidate.state == PhysicalMergeCandidate::State::RETIRED
            || (candidate.is_live() && (!candidate.participates(freeze.frozen_scn_.get_val_for_tx())
                                      || candidate.layout_id != ref.layout_id));
      }
    }
    if (ret == OB_SUCCESS && changed) { ret = check_retirement(table, backend, sql); }
    if (ret == OB_SUCCESS && !table.retired
        && (changed || !candidate.participates(freeze.frozen_scn_.get_val_for_tx())
            || !handle.get_obj()->is_data_complete())) {
      ret = OB_EAGAIN;
    }
    if (ret != OB_SUCCESS) {
      pending.add_table(ref.table_id);
      pending.add_tablet(id);
      LOG_INFO("merge object's native input is pending", K(ret), K(ref), K(id), K(freeze.frozen_scn_));
    }
  }
  return ret;
}

int build_checksum(MergeTable &table, const ObFreezeInfo &freeze, ObLS &ls,
    ObTableCkmItems &items, ObUncompactInfo &pending)
{
  ObLocalTabletChecksumArray checksums;
  int ret = ObTabletLocalChecksumOperator::batch_get(table.tablets, freeze.frozen_scn_, checksums, false);
  if (ret == OB_SUCCESS && checksums.get_tablet_cnt() != table.tablets.count()) { ret = OB_EAGAIN; }
  for (int64_t i = 0; ret == OB_SUCCESS && i < table.tablets.count(); ++i) {
    const ObTabletID id = table.tablets.at(i);
    const ObTabletLocalChecksumItem *item = nullptr;
    ObTabletHandle handle;
    PhysicalMergeCandidate candidate;
    if (OB_FAIL(checksums.get(id, item))) {
      if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_EAGAIN; }
    } else if (OB_FAIL(ls.get_tablet_svr()->get_tablet(id, handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
      if (ret == OB_TABLET_NOT_EXIST) { ret = OB_EAGAIN; }
    } else if (OB_FAIL(candidate.load(*handle.get_obj()))) {
    } else if (!candidate.participates(freeze.frozen_scn_.get_val_for_tx())
        || !candidate.matches(*item) || item->storage_layout_id_ != table.definition.layout_id
        || item->schema_version_ != table.definition.schema_version
        || item->compaction_scn_ != freeze.frozen_scn_) {
      ret = OB_EAGAIN;
      LOG_INFO("merge checksum identity or definition pending", K(table.definition), KPC(item), K(freeze.frozen_scn_));
    }
    if (ret != OB_SUCCESS) { pending.add_tablet(id); }
  }
  if (ret == OB_SUCCESS) { ret = items.build(*table.schema, table.tablets, checksums); }
  if (ret != OB_SUCCESS) {
    pending.add_table(table.definition.table_id);
    LOG_INFO("merge checksum inputs pending", K(ret), K(table.definition), K(freeze.frozen_scn_),
        "expected_count", table.tablets.count(), "actual_count", checksums.count());
  }
  return ret;
}

int compare_pair(MergeTable &data, MergeTable &index, bool fts,
    const ObFreezeInfo &freeze, ObLS &ls, ObMySQLProxy &sql, ObUncompactInfo &pending)
{
  int ret = OB_SUCCESS;
  if (!data.retired && !index.retired && !data.excluded && !index.excluded) {
    ObTableCkmItems data_items, index_items;
    if (OB_FAIL(build_checksum(data, freeze, ls, data_items, pending))) {
    } else if (OB_FAIL(build_checksum(index, freeze, ls, index_items, pending))) {
    } else {
      data_items.set_is_fts_index(fts);
      index_items.set_is_fts_index(fts);
      ObColumnChecksumErrorInfo error;
      error.namespace_id_ = data.definition.namespace_id;
      ret = ObTableCkmItems::validate_ckm_func[fts ? 0 : index.schema->is_global_index_table()](
          freeze, data_items, index_items, error);
      if (ret == OB_CHECKSUM_ERROR) {
        const int report = ObColumnChecksumErrorOperator::insert_column_checksum_err_info(sql, error);
        LOG_ERROR("historical index checksum mismatch", K(error), K(report),
            "data", data.definition, "index", index.definition);
      }
      if (ret == OB_SUCCESS) {
        LOG_INFO("historical index checksum verified", "data", data.definition, "index", index.definition,
            K(fts), K(freeze.frozen_scn_));
      }
    }
  }
  return ret;
}

int verify_indexes(MergeTables &tables, const ObFreezeInfo &freeze, ObLS &ls,
    ObMySQLProxy &sql, volatile bool &stop, ObUncompactInfo &pending)
{
  int ret = OB_SUCCESS;
  for (const auto &entry : tables) {
    if (ret != OB_SUCCESS || stop) { break; }
    MergeTable &index = *entry.second;
    if (index.retired || index.excluded || !index.schema->is_index_table()) { continue; }
    const auto found = tables.find(index.schema->get_data_table_id());
    if (found == tables.end()) {
      // A readable index at F must have a definition for its data table too.
      ret = OB_SCHEMA_EAGAIN;
    } else if (!index.schema->should_not_validate_data_index_ckm()) {
      ret = compare_pair(*found->second, index, false, freeze, ls, sql, pending);
    } else if (is_rowkey_doc_aux(index.schema->get_index_type())) {
      ret = compare_pair(*found->second, index, true, freeze, ls, sql, pending);
    } else if (is_doc_rowkey_aux(index.schema->get_index_type())
        || is_fts_index_aux(index.schema->get_index_type())) {
      MergeTable *paired = nullptr;
      ObSqlString doc_word_name;
      const bool doc_rowkey = is_doc_rowkey_aux(index.schema->get_index_type());
      if (!doc_rowkey) {
        const ObString &name = index.schema->get_table_name_str();
        ret = doc_word_name.append_fmt("%.*s_fts_doc_word", name.length(), name.ptr());
      }
      for (const auto &other : tables) {
        MergeTable &candidate = *other.second;
        if (ret != OB_SUCCESS || candidate.excluded
            || candidate.schema->get_data_table_id() != index.schema->get_data_table_id()) { continue; }
        if ((doc_rowkey && is_rowkey_doc_aux(candidate.schema->get_index_type()))
            || (!doc_rowkey && is_fts_doc_word_aux(candidate.schema->get_index_type())
                && candidate.schema->get_table_name_str().case_compare(doc_word_name.ptr()) == 0)) {
          if (paired != nullptr) { ret = OB_ERR_UNEXPECTED; }
          else { paired = &candidate; }
        }
      }
      if (ret == OB_SUCCESS && paired == nullptr) { ret = OB_SCHEMA_EAGAIN; }
      if (ret == OB_SUCCESS) {
        ret = doc_rowkey ? compare_pair(*paired, index, true, freeze, ls, sql, pending)
                        : compare_pair(index, *paired, true, freeze, ls, sql, pending);
      }
    }
  }
  return stop ? OB_CANCELED : ret;
}
} // namespace

int ObChecksumValidator::check_namespace(uint64_t namespace_id,
    const ObIArray<TableStorageLayouts::Definition> &definitions,
    const ObFreezeInfo &freeze, ObLS &ls, ObMySQLProxy &sql,
    ObMultiVersionSchemaService &schemas, volatile bool &stop,
    ObMergeProgress &progress, ObUncompactInfo &pending)
{
  ObSchemaGetterGuard current;
  MergeTables tables;
  auto *backend = schemas.get_schema_service();
  int ret = backend == nullptr ? OB_NOT_INIT : schemas.get_runtime_schema_guard(current);
  for (int64_t i = 0; ret == OB_SUCCESS && !stop && i < definitions.count(); ++i) {
    const auto &definition = definitions.at(i);
    if (definition.namespace_id != namespace_id) { continue; }
    std::unique_ptr<MergeTable> table(new (std::nothrow) MergeTable(definition));
    if (!table) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else if (OB_FAIL(load_definition(*table, current, *backend, sql))) {
    } else if (!tables.emplace(definition.table_id, std::move(table)).second) { ret = OB_ERR_UNEXPECTED; }
  }
  // First fix the complete expected graph, independently of report arrival.
  // Inspect data objects before indexes; a committed binding replacement
  // retires the old whole-table comparison even if its index results vanished.
  for (int pass = 0; ret == OB_SUCCESS && !stop && pass < 2; ++pass) {
    for (auto &entry : tables) {
      MergeTable &table = *entry.second;
      if (ret != OB_SUCCESS || stop) { break; }
      if (table.excluded || table.schema->is_index_table() != (pass == 1)) { continue; }
      if (pass == 1) {
        const auto data = tables.find(table.schema->get_data_table_id());
        if (data != tables.end() && data->second->retired) { table.retired = true; }
      }
      if (!table.retired) { ret = inspect_table(table, freeze, ls, *backend, sql, pending); }
    }
  }
  for (auto &entry : tables) {
    MergeTable &table = *entry.second;
    if (ret != OB_SUCCESS || stop) { break; }
    if (!table.retired && !table.excluded) {
      ObTableCkmItems items;
      ret = build_checksum(table, freeze, ls, items, pending);
    }
  }
  if (ret == OB_SUCCESS && !stop) { ret = verify_indexes(tables, freeze, ls, sql, stop, pending); }
  if (stop) { ret = OB_CANCELED; }
  if (ret == OB_SUCCESS) {
    for (const auto &entry : tables) {
      const MergeTable &table = *entry.second;
      ++progress.total_table_cnt_;
      progress.update_table_cnt(table.retired || table.excluded
          ? ObTableCompactionInfo::CAN_SKIP_VERIFYING : ObTableCompactionInfo::VERIFIED);
      if (!table.retired && !table.excluded) { progress.merged_tablet_cnt_ += table.tablets.count(); }
    }
    LOG_INFO("Namespace historical checksum complete", K(namespace_id), K(freeze.frozen_scn_),
        "table_count", tables.size());
  }
  return ret;
}
} // namespace rootserver
} // namespace oceanbase
