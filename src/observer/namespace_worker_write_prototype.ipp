// Throwaway V15: DAS stays in the SQL worker; transactions and writes stay here.
#include "data_plane/ob_i_dml_service.h"
#include "data_plane/ob_i_write_context_service.h"
#include "data_plane/access/ob_dml_table_plan.h"
#include "data_plane/blocksstable/ob_datum_row_iterator.h"
#include "data_plane/transaction/ob_i_transaction_service.h"
#include "data_plane/transaction/ob_i_tx_callback.h"
#include "data_plane/lob/ob_lob_read.h"
#include "data_plane/ob_inner_sql_transmit_arg.h"
#include "data_plane/ddl/ob_ddl_coordinator.h"
#include "observer/ob_inner_sql_connection.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "rootserver/ob_rootserver_local_runtime.h"
#include "share/lob/ob_lob_text_iter_context.h"
#include "share/ob_lob_access_utils.h"
#include "share/autoincrement/ob_i_tablet_autoincrement_service.h"
#include "lib/charset/ob_charset.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "storage/tablelock/ob_lock_utils.h"
#include "storage/tablelock/ob_table_lock_service.h"
#include "storage/tablet/ob_batch_create_tablet_arg.h"
#include "storage/tablet/ob_tablet_binding_helper.h"
#include "storage/tablet/ob_tablet_fork_mds_helper.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/truncate_info/ob_truncate_tablet_arg.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx/ob_trans_define_v4.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace data_plane;
using namespace transaction;
using namespace transaction::tablelock;
using namespace blocksstable;
struct WritePrepareRequest {
  StorageSpaceHandle storage_space;
  uint64_t table_id;
  const ObDmlWriteSpec &spec;
  const ObTableSchema &logical_schema;
  const ObIArray<const ObTableSchema *> &materialization_schemas;
  const ObTxReadSnapshot &snapshot;
  const concurrent_control::ObWriteFlag &write_flag;
  const std::vector<uint64_t> &columns;
};
int prepare_in_process_write(const WritePrepareRequest &request,
                             const ObTxDesc &view, uint64_t &handle);
struct WriteCells {
  ObArenaAllocator allocator{ObMemAttr("NsWriteCells")};
  std::vector<ObObj> cells;
  std::vector<bool> lob_headers;
  size_t bytes;
  explicit WriteCells(size_t initial_bytes) : bytes(initial_bytes) {}
  int append(const ObObj &value, bool has_lob_header) {
    const int64_t size = value.get_serialize_size();
    if (size < 0 || bytes > MAX_SQL_MESSAGE - 8
        || static_cast<size_t>(size) > MAX_SQL_MESSAGE - bytes - 8) {
      return OB_SIZE_OVERFLOW;
    }
    ObObj copied;
    int ret = ob_write_obj(allocator, value, copied);
    if (!ret) {
      cells.push_back(copied);
      lob_headers.push_back(has_lob_header);
      bytes += static_cast<size_t>(size) + 8;
    }
    return ret;
  }
};
struct WriteBatch : WriteCells {
  char operation;
  uint64_t handle;
  uint64_t tablet_id;
  uint64_t rows = 0;
  int64_t lock_timeout = 0;
  ObRowLockMode lock_mode = ObRowLockMode::NONE;
  ObDuplicateReturnMode duplicate_mode = ObDuplicateReturnMode::ALL;
  std::vector<uint64_t> updated_columns;
  WriteBatch(char op, uint64_t h, uint64_t tablet)
      : WriteCells(49), operation(op), handle(h), tablet_id(tablet) {}
};
struct WriteResult : WriteCells {
  uint64_t rows = 0;
  WriteResult() : WriteCells(25) {}
};
int write_in_process_batch(const ObTxDesc &view, const WriteBatch &batch,
                           int64_t &affected, WriteResult &duplicates);

int route_tablet_id(uint64_t ns, common::ObTabletID &tablet_id)
{
  uint64_t storage_id = common::OB_INVALID_ID;
  int ret = storage::NamespaceForkKernelPrototype::storage_object_id(
      ns, tablet_id.id(), storage_id);
  if (OB_SUCC(ret)) {
    tablet_id = common::ObTabletID(storage_id);
  }
  return ret;
}

int route_object_id(StorageSpaceHandle storage_space,
                    uint64_t logical_id, uint64_t &storage_id)
{
  if (!storage_space.is_valid()) { return OB_INVALID_ARGUMENT; }
  if (storage_space.is_global() || storage_space.namespace_id() == 1) {
    storage_id = logical_id;
    return OB_SUCCESS;
  }
  return storage::NamespaceForkKernelPrototype::storage_object_id(
      storage_space.namespace_id(), logical_id, storage_id);
}

int route_tablet_id(StorageSpaceHandle storage_space,
                    common::ObTabletID &tablet_id)
{
  uint64_t storage_id = common::OB_INVALID_ID;
  int ret = route_object_id(storage_space, tablet_id.id(), storage_id);
  if (OB_SUCC(ret)) { tablet_id = common::ObTabletID(storage_id); }
  return ret;
}

// A child DDL may inspect inherited tablets and locally-created hidden
// tablets in the same task. Binding MDS must mutate only physical tablets that
// exist in this child; applying it to the parent's backing tablet would leak
// child schema state into the parent, while blindly encoding every logical id
// addresses a non-existent child tablet. Newly-created DDL tablets are found
// by physical existence even before the namespace directory publishes them.
int route_existing_namespace_tablets(
    uint64_t ns,
    const common::ObIArray<common::ObTabletID> &logical_tablets,
    common::ObIArray<common::ObTabletID> &storage_tablets)
{
  storage_tablets.reset();
  if (ns <= 1) { return storage_tablets.assign(logical_tablets); }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < logical_tablets.count(); ++i) {
    common::ObTabletID tablet = logical_tablets.at(i);
    if (OB_FAIL(route_tablet_id(ns, tablet))) {
    } else {
      storage::ObTabletHandle handle;
      const int find_ret = storage::ObTabletCreateDeleteHelper::check_and_get_tablet(
          storage::ObTabletMapKey(tablet), handle, 0,
          storage::ObMDSGetTabletMode::READ_READABLE_COMMITED,
          transaction::ObTransVersion::MAX_TRANS_VERSION);
      if (find_ret == OB_SUCCESS) {
        ret = storage_tablets.push_back(tablet);
      } else if (find_ret != OB_TABLET_NOT_EXIST
                 && find_ret != OB_ENTRY_NOT_EXIST) {
        ret = find_ret;
      }
    }
  }
  return ret;
}

// Tablet MDS is produced by the namespace-local DDL engine with logical ids.
// Translate it exactly once at the storage boundary so every native MDS helper,
// replay path and tablet service below this point remains namespace-oblivious.
int route_tablet_mds(StorageSpaceHandle storage_space,
                     transaction::ObTxDataSourceType type,
                     const common::ObString &input,
                     std::vector<char> &storage_buffer,
                     bool &skip_mds)
{
  skip_mds = false;
  if (!storage_space.is_valid()) {
    return OB_INVALID_ARGUMENT;
  }
  if (storage_space.is_global()) {
    return OB_SUCCESS;
  }
  const uint64_t ns = storage_space.namespace_id();
  if (ns <= 1) {
    return OB_SUCCESS;
  }
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (type == transaction::ObTxDataSourceType::CREATE_TABLET_NEW_MDS) {
    obcall::ObBatchCreateTabletArg arg;
    if (OB_FAIL(arg.deserialize(input.ptr(), input.length(), pos))) {
    } else if (pos != input.length() || !arg.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablets_.count(); ++i) {
      obcall::ObCreateTabletInfo &info = arg.tablets_.at(i);
      if (OB_FAIL(route_tablet_id(ns, info.data_tablet_id_))) {
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < info.tablet_ids_.count(); ++j) {
        ret = route_tablet_id(ns, info.tablet_ids_.at(j));
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < info.fork_tablet_infos_.count(); ++j) {
        share::ObForkTabletInfo &fork = info.fork_tablet_infos_.at(j);
        ObTabletID logical = fork.get_fork_src_tablet_id();
        if (!fork.is_valid()) {
          ret = OB_INVALID_ARGUMENT;
        } else if (!storage::NamespaceForkKernelPrototype::is_encoded_id(logical.id())) {
          uint64_t storage_id = OB_INVALID_ID;
          if (OB_FAIL(storage::NamespaceForkKernelPrototype::storage_object_id(
                  ns, logical.id(), storage_id))) {
          } else { logical = ObTabletID(storage_id); }
        }
        if (OB_SUCC(ret) && storage::NamespaceForkKernelPrototype::is_encoded_id(logical.id())
            && ::oceanbase::ns::NamespaceObjectKey::encoded_namespace(logical.id()) == ns) {
          ObTabletID physical;
          int64_t inherited_cap = 0;
          if (OB_FAIL(storage::NamespaceForkKernelPrototype::resolve_read_tablet(
                  logical, physical, inherited_cap))) {
          } else {
            fork.set_fork_src_tablet_id(physical);
            if (inherited_cap > 0 && inherited_cap < fork.get_fork_snapshot_version()) {
              fork.set_fork_snapshot_version(inherited_cap);
            }
          }
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.table_schemas_.count(); ++i) {
      share::schema::ObTableSchema storage_schema;
      if (OB_FAIL(storage::NamespaceForkKernelPrototype::make_storage_schema(
              ns, arg.table_schemas_.at(i), storage_schema))) {
      } else {
        ret = arg.table_schemas_.at(i).assign(storage_schema);
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.create_tablet_schemas_.count(); ++i) {
      if (OB_ISNULL(arg.create_tablet_schemas_.at(i))) { ret = OB_ERR_UNEXPECTED; }
    }
    if (OB_SUCC(ret)) {
      storage_buffer.resize(arg.get_serialize_size());
      pos = 0;
      if (OB_FAIL(arg.serialize(storage_buffer.data(), storage_buffer.size(), pos))) {
      } else if (pos != static_cast<int64_t>(storage_buffer.size())) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  } else if (type == transaction::ObTxDataSourceType::TABLET_FORK) {
    storage::ObTabletForkMdsArg arg;
    if (OB_FAIL(arg.deserialize(input.ptr(), input.length(), pos))) {
    } else if (pos != input.length() || !arg.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; OB_SUCC(ret)
         && i < arg.autoinc_seq_arg_.autoinc_params_.count(); ++i) {
      ret = route_tablet_id(ns,
          arg.autoinc_seq_arg_.autoinc_params_.at(i).dest_tablet_id_);
    }
    if (OB_SUCC(ret) && arg.truncate_arg_.is_valid()) {
      ret = route_tablet_id(ns, arg.truncate_arg_.index_tablet_id_);
    }
    if (OB_SUCC(ret)) {
      storage_buffer.resize(arg.get_serialize_size());
      pos = 0;
      if (OB_FAIL(arg.serialize(storage_buffer.data(), storage_buffer.size(), pos))) {
      } else if (pos != static_cast<int64_t>(storage_buffer.size())) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  } else if (type == transaction::ObTxDataSourceType::DELETE_TABLET_NEW_MDS) {
    obcall::ObBatchRemoveTabletArg arg;
    if (OB_FAIL(arg.deserialize(input.ptr(), input.length(), pos))) {
    } else if (pos != input.length() || !arg.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    }
    if (OB_SUCC(ret)) {
      // The namespace directory is the ownership authority for both inherited
      // and materialized tablets.  Let the post-commit schema replacement
      // remove stale bindings and reclaim only child-owned physical tablets.
      // Keeping one cleanup owner also makes DROP, TRUNCATE and repartitioning
      // follow the same path instead of teaching MDS about statement kinds.
      skip_mds = true;
    }
  } else if (type == transaction::ObTxDataSourceType::SYNC_TRUNCATE_INFO) {
    ObArenaAllocator allocator(ObMemAttr("NsTruncateMds"));
    storage::ObTruncateTabletArg arg;
    if (OB_FAIL(arg.deserialize(allocator, input.ptr(), input.length(), pos))) {
    } else if (pos != input.length() || !arg.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(route_tablet_id(ns, arg.index_tablet_id_))) {
    } else {
      storage_buffer.resize(arg.get_serialize_size());
      pos = 0;
      if (OB_FAIL(arg.serialize(storage_buffer.data(), storage_buffer.size(), pos))) {
      } else if (pos != static_cast<int64_t>(storage_buffer.size())) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

// MDS registration is a generic transaction API, so carry the storage space
// selected by the SQL worker instead of asking the shared transaction service
// to infer a namespace from tablet ids. CREATE_TABLET contains the target
// schemas and can therefore be classified at this boundary. Other MDS records
// belong to the worker's namespace unless an enclosing native operation has
// explicitly selected GLOBAL.
int worker_mds_storage_space(transaction::ObTxDataSourceType type,
                             const char *buffer,
                             int64_t buffer_size,
  StorageSpaceHandle &storage_space)
{
  storage_space = active_worker_storage_space();
  if (!storage_space.is_valid() || buffer == nullptr || buffer_size <= 0) {
    return OB_INVALID_ARGUMENT;
  }
  if (storage_space.is_global()
      || type != transaction::ObTxDataSourceType::CREATE_TABLET_NEW_MDS) {
    return OB_SUCCESS;
  }

  obcall::ObBatchCreateTabletArg arg;
  int64_t pos = 0;
  int ret = arg.deserialize(buffer, buffer_size, pos);
  if (OB_SUCC(ret) && (pos != buffer_size || !arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret) && !arg.table_schemas_.empty()) {
    ObSchemaGetterGuard guard;
    ObMultiVersionSchemaService *service =
        namespace_schema_service(storage_space.namespace_id());
    if (service == nullptr) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(service->get_runtime_schema_guard(guard))) {
    }
    StorageSpaceHandle classified;
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.table_schemas_.count(); ++i) {
      StorageSpaceHandle candidate;
      if (OB_FAIL(worker_storage_space_for_schema(
              arg.table_schemas_.at(i), guard, candidate))) {
      } else if (!classified.is_valid()) {
        classified = candidate;
      } else if (classified != candidate) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
    if (OB_SUCC(ret) && classified.is_valid()) {
      storage_space = classified;
    }
  }
  return ret;
}

template <typename T>
int deserialize_lock_request(const ObString &payload, T &request)
{
  int64_t pos = 0;
  int ret = request.deserialize(payload.ptr(), payload.length(), pos);
  return ret ? ret : pos == payload.length() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
}

struct TableLockPlan {
  StorageSpaceHandle storage_space;
  int64_t schema_version = OB_INVALID_VERSION;
  ObTabletIDArray tablet_ids;
  bool explicit_tablets = false;
};

bool is_schema_table_lock_operation(
    obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation)
{
  using Operation = obcall::ObInnerSQLTransmitArg::InnerSQLOperationType;
  return operation == Operation::OPERATION_TYPE_LOCK_TABLE
      || operation == Operation::OPERATION_TYPE_UNLOCK_TABLE
      || operation == Operation::OPERATION_TYPE_LOCK_TABLET
      || operation == Operation::OPERATION_TYPE_UNLOCK_TABLET
      || operation == Operation::OPERATION_TYPE_LOCK_PART
      || operation == Operation::OPERATION_TYPE_UNLOCK_PART
      || operation == Operation::OPERATION_TYPE_LOCK_SUBPART
      || operation == Operation::OPERATION_TYPE_UNLOCK_SUBPART;
}

int append_worker_table_lock_plan(
    const ObLockTableRequest &arg,
    obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
    const ObString &payload,
    TableLockPlan &plan)
{
  using Operation = obcall::ObInnerSQLTransmitArg::InnerSQLOperationType;
  ObSchemaGetterGuard guard;
  const ObTableSchema *schema = nullptr;
  ObTabletIDArray tablet_ids;
  ObMultiVersionSchemaService *service = namespace_schema_service(serving_namespace());
  int ret = service == nullptr ? OB_NOT_INIT : service->get_runtime_schema_guard(guard);
  if (OB_SUCC(ret)) {
    ret = guard.get_table_schema(arg.table_id_, schema);
  }
  if (OB_SUCC(ret) && OB_ISNULL(schema)) {
    ret = OB_TABLE_NOT_EXIST;
  } else if (OB_SUCC(ret)
             && !schema->is_user_table()
             && !schema->is_tmp_table()
             && !ObInnerTableLockUtil::in_inner_table_lock_white_list(arg.table_id_)) {
    ret = OB_OP_NOT_ALLOW;
  }
  if (OB_SUCC(ret)) {
    ret = worker_storage_space_for_schema(*schema, guard, plan.storage_space);
  }

  if (OB_SUCC(ret)
      && (operation == Operation::OPERATION_TYPE_LOCK_TABLE
          || operation == Operation::OPERATION_TYPE_UNLOCK_TABLE)) {
    if (is_need_lock_tablet_mode(arg.lock_mode_)) {
      ret = schema->get_tablet_ids(tablet_ids);
    }
  } else if (OB_SUCC(ret)
             && operation == Operation::OPERATION_TYPE_LOCK_TABLET) {
    ObLockTabletsRequest tablets;
    if (OB_FAIL(deserialize_lock_request(payload, tablets))) {
    } else {
      ret = tablet_ids.assign(tablets.tablet_ids_);
    }
  } else if (OB_SUCC(ret)
             && operation == Operation::OPERATION_TYPE_UNLOCK_TABLET) {
    ObUnLockTabletRequest tablet;
    if (OB_FAIL(deserialize_lock_request(payload, tablet))) {
    } else {
      ret = tablet_ids.push_back(tablet.tablet_id_);
    }
  } else if (OB_SUCC(ret)
             && (operation == Operation::OPERATION_TYPE_LOCK_PART
                 || operation == Operation::OPERATION_TYPE_UNLOCK_PART
                 || operation == Operation::OPERATION_TYPE_LOCK_SUBPART
                 || operation == Operation::OPERATION_TYPE_UNLOCK_SUBPART)) {
    const bool unlock = operation == Operation::OPERATION_TYPE_UNLOCK_PART
        || operation == Operation::OPERATION_TYPE_UNLOCK_SUBPART;
    const bool subpartition = operation == Operation::OPERATION_TYPE_LOCK_SUBPART
        || operation == Operation::OPERATION_TYPE_UNLOCK_SUBPART;
    uint64_t part_object_id = OB_INVALID_ID;
    if (unlock) {
      ObUnLockPartitionRequest partition;
      if (OB_FAIL(deserialize_lock_request(payload, partition))) {
      } else {
        part_object_id = partition.part_object_id_;
      }
    } else {
      ObLockPartitionRequest partition;
      if (OB_FAIL(deserialize_lock_request(payload, partition))) {
      } else {
        part_object_id = partition.part_object_id_;
      }
    }
    if (OB_SUCC(ret) && subpartition) {
      ObTabletID tablet_id;
      if (OB_FAIL(schema->get_tablet_id_by_object_id(part_object_id, tablet_id))) {
      } else {
        ret = tablet_ids.push_back(tablet_id);
      }
    } else if (OB_SUCC(ret)) {
      ret = schema->get_tablet_ids_by_part_object_id(part_object_id, tablet_ids);
    }
  } else if (OB_SUCC(ret)) {
    ret = OB_NOT_SUPPORTED;
  }

  if (OB_SUCC(ret)) {
    plan.schema_version = schema->get_schema_version();
    ret = plan.tablet_ids.assign(tablet_ids);
    if (OB_SUCC(ret)) { plan.explicit_tablets = true; }
  }
  return ret;
}

int append_worker_table_lock_plan(
    obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
    const ObString &payload,
    TableLockPlan &plan)
{
  using Operation = obcall::ObInnerSQLTransmitArg::InnerSQLOperationType;
  int ret = OB_SUCCESS;
#define DECODE_AND_APPEND(Type) do {                                              \
  Type arg;                                                                       \
  if (OB_FAIL(deserialize_lock_request(payload, arg))) {                           \
  } else {                                                                        \
    ret = append_worker_table_lock_plan(                                          \
        arg, operation, payload, plan);                                           \
  }                                                                               \
} while (false)
  if (operation == Operation::OPERATION_TYPE_UNLOCK_TABLE) {
    DECODE_AND_APPEND(ObUnLockTableRequest);
  } else if (operation == Operation::OPERATION_TYPE_UNLOCK_TABLET) {
    DECODE_AND_APPEND(ObUnLockTabletRequest);
  } else if (operation == Operation::OPERATION_TYPE_UNLOCK_PART
             || operation == Operation::OPERATION_TYPE_UNLOCK_SUBPART) {
    DECODE_AND_APPEND(ObUnLockPartitionRequest);
  } else if (operation == Operation::OPERATION_TYPE_LOCK_TABLET) {
    DECODE_AND_APPEND(ObLockTabletsRequest);
  } else if (operation == Operation::OPERATION_TYPE_LOCK_PART
             || operation == Operation::OPERATION_TYPE_LOCK_SUBPART) {
    DECODE_AND_APPEND(ObLockPartitionRequest);
  } else if (operation == Operation::OPERATION_TYPE_LOCK_TABLE) {
    DECODE_AND_APPEND(ObLockTableRequest);
  } else {
    ret = OB_NOT_SUPPORTED;
  }
#undef DECODE_AND_APPEND
  return ret;
}

int route_table_lock_id(uint64_t ns, uint64_t &table_id)
{
  int ret = OB_SUCCESS;
  if (ns > 1 && !is_sys_table(table_id)) {
    uint64_t storage_id = OB_INVALID_ID;
    if (OB_FAIL(storage::NamespaceForkKernelPrototype::storage_object_id(
            ns, table_id, storage_id))) {
    } else {
      table_id = storage_id;
    }
  }
  return ret;
}

int route_table_lock_id(StorageSpaceHandle storage_space, uint64_t &table_id)
{
  return storage_space.is_global() ? OB_SUCCESS
      : storage_space.is_namespace()
          ? route_table_lock_id(storage_space.namespace_id(), table_id)
          : OB_INVALID_ARGUMENT;
}

// A namespace DDL wait is expressed over logical tablets.  A child can have a
// mixture of local physical tablets (materialized source tablets and hidden
// DDL targets) and inherited tablets with no child physical object.  Run the
// native elapsed check for the former and report the latter as having no child
// transaction, while preserving the one-result-per-input contract.
template <typename Arg, typename Result, typename Check>
int check_namespace_tablet_elapsed(
    StorageSpaceHandle storage_space,
    Arg &arg,
    Result &result,
    Check &&check)
{
  if (!storage_space.is_valid()) {
    return OB_INVALID_ARGUMENT;
  }
  if (storage_space.is_global()) {
    return check(arg, result);
  }
  const uint64_t ns = storage_space.namespace_id();
  if (ns <= 1) {
    return check(arg, result);
  }

  int ret = OB_SUCCESS;
  const int64_t logical_count = arg.tablets_.count();
  common::ObSEArray<obcall::ObTabletPair, 10> storage_tablets;
  common::ObSEArray<int64_t, 10> storage_positions;
  for (int64_t i = 0; OB_SUCC(ret) && i < logical_count; ++i) {
    obcall::ObTabletPair pair = arg.tablets_.at(i);
    if (OB_FAIL(route_tablet_id(ns, pair.tablet_id_))) {
    } else {
      storage::ObTabletHandle handle;
      const int find_ret = storage::ObTabletCreateDeleteHelper::check_and_get_tablet(
          storage::ObTabletMapKey(pair.tablet_id_), handle, 0,
          storage::ObMDSGetTabletMode::READ_READABLE_COMMITED,
          transaction::ObTransVersion::MAX_TRANS_VERSION);
      if (find_ret == OB_SUCCESS) {
        if (OB_FAIL(storage_tablets.push_back(pair))) {
        } else if (OB_FAIL(storage_positions.push_back(i))) {
        }
      } else if (find_ret != OB_TABLET_NOT_EXIST
                 && find_ret != OB_ENTRY_NOT_EXIST) {
        ret = find_ret;
      }
    }
  }

  Result storage_result;
  if (OB_SUCC(ret) && !storage_tablets.empty()) {
    if (OB_FAIL(arg.tablets_.assign(storage_tablets))) {
    } else if (OB_FAIL(check(arg, storage_result))) {
    } else if (storage_result.results_.count() != storage_tablets.count()) {
      ret = OB_ERR_UNEXPECTED;
    }
  }

  share::SCN max_commit_scn;
  if (OB_SUCC(ret) && storage_tablets.count() != logical_count) {
    transaction::ObTransService *tx_service =
        share::server_service<transaction::ObTransService>();
    if (OB_ISNULL(tx_service)) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(tx_service->get_max_commit_version(max_commit_scn))) {
    } else if (!max_commit_scn.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
    }
  }

  result.results_.reset();
  for (int64_t logical_pos = 0, storage_pos = 0;
       OB_SUCC(ret) && logical_pos < logical_count;
       ++logical_pos) {
    obcall::ObCheckTransElapsedResult item;
    if (storage_pos < storage_positions.count()
        && storage_positions.at(storage_pos) == logical_pos) {
      item = storage_result.results_.at(storage_pos++);
    } else {
      item.ret_code_ = OB_SUCCESS;
      item.snapshot_ = max_commit_scn.get_val_for_tx();
    }
    if (OB_FAIL(result.results_.push_back(item))) {
    }
  }
  return ret;
}

// Rootserver owns DDL task orchestration; these operations touch physical
// tablets and DAGs. Route logical ids before calling the native runtime.
int route_rootserver_build_arg(
    StorageSpaceHandle storage_space,
    const obcall::ObDDLLocalBuildArg &arg,
    obcall::ObDDLLocalBuildArg &routed)
{
  int ret = routed.assign(arg);
  uint64_t source_table_id = routed.source_table_id_;
  uint64_t dest_table_id = routed.dest_schema_id_;
  if (!ret && OB_FAIL(route_tablet_id(storage_space, routed.source_tablet_id_))) {
  } else if (!ret && OB_FAIL(route_tablet_id(storage_space, routed.dest_tablet_id_))) {
  } else if (!ret && OB_FAIL(route_table_lock_id(storage_space, source_table_id))) {
  } else if (!ret && OB_FAIL(route_table_lock_id(storage_space, dest_table_id))) {
  } else if (!ret) {
    routed.source_table_id_ = source_table_id;
    routed.dest_schema_id_ = dest_table_id;
  }
  return ret;
}

int calc_namespace_column_checksum(
    StorageSpaceHandle storage_space,
    rootserver::ObIRootserverLocalRuntime &runtime,
    const obcall::ObCalcColumnChecksumRequestArg &input,
    obcall::ObCalcColumnChecksumRequestRes &result)
{
  const uint64_t ns = storage_space.namespace_id();
  obcall::ObCalcColumnChecksumRequestArg arg;
  int ret = arg.assign(input);
  ObTableSchema storage_source_schema;
  ObTableSchema storage_target_schema;
  if (!ret && storage_space.is_namespace() && ns > 1 && OB_FAIL(
          storage::NamespaceForkKernelPrototype::make_storage_schema(
              ns, arg.source_schema_, storage_source_schema))) {
  } else if (!ret && storage_space.is_namespace() && ns > 1 && OB_FAIL(
          storage::NamespaceForkKernelPrototype::make_storage_schema(
              ns, arg.target_schema_, storage_target_schema))) {
  } else if (!ret && storage_space.is_namespace() && ns > 1 && OB_FAIL(
          arg.source_schema_.assign(storage_source_schema))) {
  } else if (!ret && storage_space.is_namespace() && ns > 1 && OB_FAIL(
          arg.target_schema_.assign(storage_target_schema))) {
  }
  for (int64_t i = 0; !ret && i < arg.calc_items_.count(); ++i) {
    ret = route_tablet_id(storage_space, arg.calc_items_.at(i).tablet_id_);
  }
  obcall::ObCalcColumnChecksumRequestArg submit_arg;
  ObSEArray<int64_t, 10> submit_positions;
  if (!ret && OB_FAIL(submit_arg.assign(arg))) {
  } else if (!ret) {
    submit_arg.calc_items_.reset();
  }
  result.ret_codes_.reset();
  result.completions_.reset();
  for (int64_t i = 0; !ret && i < arg.calc_items_.count(); ++i) {
    obcall::ObCalcColumnChecksumResponseArg key;
    key.tablet_id_ = arg.calc_items_.at(i).tablet_id_;
    key.target_table_id_ = arg.target_table_id_;
    key.source_table_id_ = arg.source_table_id_;
    key.schema_version_ = arg.schema_version_;
    key.task_id_ = arg.task_id_;
    bool should_submit = false;
    bool is_finished = false;
    obcall::ObCalcColumnChecksumResponseArg completion;
    obcall::ObCalcColumnChecksumCompletion wire_completion;
    int item_ret = OB_EAGAIN;
    if (OB_FAIL(data_plane::prepare_column_checksum_poll(
            key, should_submit, is_finished, completion))) {
    } else if (is_finished) {
      wire_completion.finished_ = true;
      wire_completion.ret_code_ = completion.ret_code_;
      item_ret = completion.ret_code_;
      if (OB_FAIL(wire_completion.column_ids_.assign(completion.column_ids_))) {
      } else if (OB_FAIL(wire_completion.column_checksums_.assign(
                     completion.column_checksums_))) {
      }
    } else if (should_submit) {
      if (OB_FAIL(submit_arg.calc_items_.push_back(arg.calc_items_.at(i)))) {
      } else if (OB_FAIL(submit_positions.push_back(i))) {
      }
    }
    if (!ret && OB_FAIL(result.ret_codes_.push_back(item_ret))) {
    } else if (!ret && OB_FAIL(result.completions_.push_back(wire_completion))) {
    }
  }
  if (!ret && !submit_arg.calc_items_.empty()) {
    obcall::ObCalcColumnChecksumRequestRes submit_result;
    if (OB_FAIL(runtime.calc_column_checksum_request(submit_arg, submit_result))) {
    } else if (submit_result.ret_codes_.count() != submit_positions.count()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      for (int64_t i = 0; !ret && i < submit_positions.count(); ++i) {
        const int64_t pos = submit_positions.at(i);
        const int schedule_ret = submit_result.ret_codes_.at(i);
        if (schedule_ret == OB_SUCCESS || schedule_ret == OB_EAGAIN
            || schedule_ret == OB_HASH_EXIST) {
          result.ret_codes_.at(pos) = OB_EAGAIN;
        } else {
          obcall::ObCalcColumnChecksumResponseArg key;
          key.tablet_id_ = arg.calc_items_.at(pos).tablet_id_;
          key.target_table_id_ = arg.target_table_id_;
          key.source_table_id_ = arg.source_table_id_;
          key.schema_version_ = arg.schema_version_;
          key.task_id_ = arg.task_id_;
          result.ret_codes_.at(pos) = schedule_ret;
          if (OB_FAIL(data_plane::cancel_column_checksum_poll(key))) {
          }
        }
      }
    }
    if (ret) {
      for (int64_t i = 0; i < submit_positions.count(); ++i) {
        const int64_t pos = submit_positions.at(i);
        obcall::ObCalcColumnChecksumResponseArg key;
        key.tablet_id_ = arg.calc_items_.at(pos).tablet_id_;
        key.target_table_id_ = arg.target_table_id_;
        key.source_table_id_ = arg.source_table_id_;
        key.schema_version_ = arg.schema_version_;
        key.task_id_ = arg.task_id_;
        (void)data_plane::cancel_column_checksum_poll(key);
      }
    }
  }
  return ret;
}

// A lock is storage state attached to the same native transaction as the DDL
// catalog writes. The native table-lock service remains the sole lock
// implementation.
int process_table_lock(
    obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
    StorageSpaceHandle storage_space, const ObTxParam &tx_param,
    const ObString &payload, const TableLockPlan &plan, ObTxDesc &tx)
{
  using namespace transaction::tablelock;
  int ret = OB_SUCCESS;
  const uint64_t ns = storage_space.namespace_id();
  bool has_explicit_tablets = plan.explicit_tablets;
  const int64_t schema_version = plan.schema_version;
  ObTabletIDArray tablet_ids;
  if (is_schema_table_lock_operation(operation) && has_explicit_tablets) {
    if (schema_version < 0 || plan.tablet_ids.count() > 65536) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < plan.tablet_ids.count(); ++i) {
      if (!plan.tablet_ids.at(i).is_valid()) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
    if (OB_SUCC(ret) && ns > 1
        && (operation == obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE
            || operation == obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLE)) {
      // A whole-table lock is a namespace-logical object. Its routed table id
      // already separates the child from its parent, while the set of locally
      // materialized tablets can change during the DDL. Keeping that mutable
      // set out of whole-table lock identity makes lock and unlock symmetric.
      // Explicit tablet/partition lock requests below still route their
      // concrete, child-owned physical tablets.
    } else if (OB_SUCC(ret) && ns > 1) {
      ret = storage::NamespaceForkKernelPrototype::owned_storage_tablets(
          ns, plan.tablet_ids, tablet_ids);
    } else if (OB_SUCC(ret) && storage_space.is_global()) {
      // Global-space tablets (the namespace-control catalog) are already
      // physical ids; namespace routing applies to namespace spaces only.
      ret = tablet_ids.assign(plan.tablet_ids);
    } else if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < plan.tablet_ids.count(); ++i) {
        ObTabletID tablet_id = plan.tablet_ids.at(i);
        if (OB_FAIL(route_tablet_id(ns, tablet_id))) {
        } else {
          ret = tablet_ids.push_back(tablet_id);
        }
      }
    }
  }
  const bool valid_param = tx_param.is_valid();
  ObTableLockService *service = share::server_service<ObTableLockService>();
  if (!ret && (!storage_space.is_valid() || !valid_param || payload.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!ret && OB_ISNULL(service)) {
    ret = OB_NOT_INIT;
  }

#define DECODE_AND_LOCK(Type) do {                                                \
  Type arg;                                                                       \
  if (OB_FAIL(deserialize_lock_request(payload, arg))) {                           \
  } else if (OB_FAIL(service->lock(tx, tx_param, arg))) {                         \
  }                                                                               \
} while (false)
#define DECODE_AND_UNLOCK(Type) do {                                              \
  Type arg;                                                                       \
  if (OB_FAIL(deserialize_lock_request(payload, arg))) {                           \
  } else if (OB_FAIL(service->unlock(tx, tx_param, arg))) {                       \
  }                                                                               \
} while (false)
#define DECODE_AND_EXPLICIT_LOCK(Type, NativeCall) do {                           \
  Type arg;                                                                       \
  if (OB_FAIL(deserialize_lock_request(payload, arg))) {                           \
  } else if (has_explicit_tablets) {                                               \
    if (OB_FAIL(route_table_lock_id(ns, arg.table_id_))) {                         \
    } else if (OB_FAIL(service->lock_with_explicit_tablets(                       \
                   tx, tx_param, arg, schema_version, tablet_ids))) {              \
    }                                                                              \
  } else if (ns > 1) {                                                            \
    ret = OB_NOT_SUPPORTED;                                                        \
  } else if (OB_FAIL(service->NativeCall(tx, tx_param, arg))) {                    \
  }                                                                                \
} while (false)

  if (OB_SUCC(ret)) {
    switch (operation) {
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE:
        DECODE_AND_EXPLICIT_LOCK(ObLockTableRequest, lock); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLE:
        DECODE_AND_EXPLICIT_LOCK(ObUnLockTableRequest, unlock); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET:
        DECODE_AND_EXPLICIT_LOCK(ObLockTabletsRequest, lock); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLET:
        DECODE_AND_EXPLICIT_LOCK(ObUnLockTabletRequest, unlock); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_PART:
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_SUBPART:
        DECODE_AND_EXPLICIT_LOCK(ObLockPartitionRequest, lock); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_PART:
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_SUBPART:
        DECODE_AND_EXPLICIT_LOCK(ObUnLockPartitionRequest, unlock); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJ: {
        ObLockObjRequest arg;
        ObLockObjsRequest args;
        if (OB_FAIL(deserialize_lock_request(payload, arg))) {
        } else if (OB_FAIL(args.assign(arg))) {
        } else if (OB_FAIL(service->lock(tx, tx_param, args))) {
        }
        break;
      }
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJS:
        DECODE_AND_LOCK(ObLockObjsRequest); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJ: {
        ObUnLockObjRequest arg;
        ObUnLockObjsRequest args;
        if (OB_FAIL(deserialize_lock_request(payload, arg))) {
        } else if (OB_FAIL(args.assign(arg))) {
        } else if (OB_FAIL(service->unlock(tx, tx_param, args))) {
        }
        break;
      }
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJS:
        DECODE_AND_UNLOCK(ObUnLockObjsRequest); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_ALONE_TABLET:
        DECODE_AND_LOCK(ObLockAloneTabletRequest); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_ALONE_TABLET:
        DECODE_AND_UNLOCK(ObUnLockAloneTabletRequest); break;
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_REPLACE_LOCKS: {
        ObArenaAllocator allocator("NsRemoteLock");
        ObReplaceAllLocksRequest arg(allocator);
        if (OB_FAIL(deserialize_lock_request(payload, arg))) {
        } else if (OB_FAIL(service->replace_lock(tx, tx_param, arg))) {
        }
        break;
      }
      case obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_REPLACE_LOCK: {
        ObReplaceLockRequest arg;
        ObLockRequest header;
        int64_t pos = 0;
        int64_t request_pos = 0;
        if (OB_FAIL(arg.deserialize_and_check_header(payload.ptr(), payload.length(), pos))) {
        } else if (OB_FAIL(arg.deserialize_new_lock_mode_and_owner(payload.ptr(), payload.length(), pos))) {
        } else if (FALSE_IT(request_pos = pos)) {
        } else if (OB_FAIL(header.deserialize(payload.ptr(), payload.length(), request_pos))) {
        } else {
#define DECODE_AND_REPLACE(Type) do {                                             \
          Type unlock;                                                           \
          if (OB_FAIL(unlock.deserialize(payload.ptr(), payload.length(), pos))) {\
          } else if (pos != payload.length()) {                                  \
            ret = OB_INVALID_ARGUMENT;                                           \
          } else {                                                               \
            arg.unlock_req_ = &unlock;                                            \
            ret = service->replace_lock(tx, tx_param, arg);                      \
            arg.unlock_req_ = nullptr;                                            \
          }                                                                      \
        } while (false)
          switch (header.type_) {
            case ObLockRequest::ObLockMsgType::UNLOCK_OBJ_REQ:
              DECODE_AND_REPLACE(ObUnLockObjsRequest); break;
            case ObLockRequest::ObLockMsgType::UNLOCK_TABLE_REQ:
              DECODE_AND_REPLACE(ObUnLockTableRequest); break;
            case ObLockRequest::ObLockMsgType::UNLOCK_PARTITION_REQ:
              DECODE_AND_REPLACE(ObUnLockPartitionRequest); break;
            case ObLockRequest::ObLockMsgType::UNLOCK_TABLET_REQ:
              DECODE_AND_REPLACE(ObUnLockTabletsRequest); break;
            case ObLockRequest::ObLockMsgType::UNLOCK_ALONE_TABLET_REQ:
              DECODE_AND_REPLACE(ObUnLockAloneTabletRequest); break;
            default: ret = OB_INVALID_ARGUMENT; break;
          }
#undef DECODE_AND_REPLACE
        }
        break;
      }
      default: ret = OB_NOT_SUPPORTED; break;
    }
  }
#undef DECODE_AND_LOCK
#undef DECODE_AND_UNLOCK
#undef DECODE_AND_EXPLICIT_LOCK
  return ret;
}

#include "observer/namespace_inprocess_write_state.ipp"

#include "observer/namespace_inprocess_transaction_services.ipp"

#include "observer/namespace_inprocess_dml_services.ipp"
} } }
