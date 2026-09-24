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
#include "storage/tx/ob_trans_service.h"
#include "storage/tx/ob_trans_define_v4.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace data_plane;
using namespace transaction;
using namespace transaction::tablelock;
using namespace blocksstable;
int worker_send(const Frame &, bool cleanup = false);
int worker_read(Frame &);
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

bool cleanup_write(Frame &request) {
  const int64_t position = request.pos;
  const uint64_t op = request.number();
  request.pos = position;
  return request.type() == 'T' && (op == 'B' || op == 'R' || op == 'U' || op == 'E');
}
int write_rpc(Frame &request, Frame &reply) {
  int ret = worker_send(request, cleanup_write(request));
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'w') { ret = OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  return ret ? ret : reply.ret;
}

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

int route_tablet_id(StorageSpaceHandle storage_space,
                    common::ObTabletID &tablet_id)
{
  return storage_space.is_global() ? OB_SUCCESS
      : storage_space.is_namespace()
          ? route_tablet_id(storage_space.namespace_id(), tablet_id)
          : OB_INVALID_ARGUMENT;
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
        if (OB_SUCC(ret) && storage::NamespaceForkKernelPrototype::namespace_of(logical.id()) == ns) {
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
      storage::ObCreateTabletSchema *schema = arg.create_tablet_schemas_.at(i);
      uint64_t storage_id = common::OB_INVALID_ID;
      if (OB_ISNULL(schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (schema->get_table_type() != share::schema::SYSTEM_TABLE
                 && OB_FAIL(storage::NamespaceForkKernelPrototype::storage_object_id(
                     ns, schema->get_table_id(), storage_id))) {
      } else if (schema->get_table_type() != share::schema::SYSTEM_TABLE) {
        schema->set_table_id(storage_id);
      }
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

constexpr uint64_t EXPLICIT_TABLE_LOCK_PLAN = 0x4e534c4f434b5031ULL; // NSLOCKP1

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
    Frame &request,
    StorageSpaceHandle &storage_space)
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
    ret = worker_storage_space_for_schema(*schema, guard, storage_space);
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
    request.number(EXPLICIT_TABLE_LOCK_PLAN);
    request.number(static_cast<uint64_t>(schema->get_schema_version()));
    request.number(static_cast<uint64_t>(tablet_ids.count()));
    for (int64_t i = 0; OB_SUCC(request.ret) && i < tablet_ids.count(); ++i) {
      request.number(tablet_ids.at(i).id());
    }
    ret = request.ret;
  }
  return ret;
}

int append_worker_table_lock_plan(
    obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
    const ObString &payload,
    Frame &request,
    StorageSpaceHandle &storage_space)
{
  using Operation = obcall::ObInnerSQLTransmitArg::InnerSQLOperationType;
  int ret = OB_SUCCESS;
#define DECODE_AND_APPEND(Type) do {                                              \
  Type arg;                                                                       \
  if (OB_FAIL(deserialize_lock_request(payload, arg))) {                           \
  } else {                                                                        \
    ret = append_worker_table_lock_plan(                                          \
        arg, operation, payload, request, storage_space);                         \
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
    if (!ret) {
      uint64_t table_id = arg.calc_items_.at(i).calc_table_id_;
      if (OB_FAIL(route_table_lock_id(storage_space, table_id))) {
      } else {
        arg.calc_items_.at(i).calc_table_id_ = table_id;
      }
    }
  }
  uint64_t target_table_id = arg.target_table_id_;
  uint64_t source_table_id = arg.source_table_id_;
  if (!ret && OB_FAIL(route_table_lock_id(storage_space, target_table_id))) {
  } else if (!ret && OB_FAIL(route_table_lock_id(storage_space, source_table_id))) {
  } else if (!ret) {
    arg.target_table_id_ = target_table_id;
    arg.source_table_id_ = source_table_id;
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
// catalog writes.  Decode the SQL worker's lock intent only at the shared
// gateway; the native table-lock service remains the sole lock implementation.
int process_table_lock(StorageSpaceHandle channel_space,
                       Frame &request,
                       ObTxDesc &tx)
{
  using namespace transaction::tablelock;
  const auto operation = static_cast<obcall::ObInnerSQLTransmitArg::InnerSQLOperationType>(request.number());
  StorageSpaceHandle storage_space;
  int ret = read_storage_space(request, channel_space, storage_space);
  const uint64_t ns = storage_space.namespace_id();
  ObTxParam tx_param;
  request.read(tx_param);
  const ObString payload = request.string();
  if (OB_SUCC(ret)) { ret = request.ret; }
  bool has_explicit_tablets = false;
  int64_t schema_version = OB_INVALID_VERSION;
  ObTabletIDArray tablet_ids;
  if (OB_SUCC(ret) && is_schema_table_lock_operation(operation)
      && !request.consumed()) {
    const uint64_t marker = request.number();
    schema_version = static_cast<int64_t>(request.number());
    const uint64_t count = request.number();
    if (request.ret || marker != EXPLICIT_TABLE_LOCK_PLAN
        || schema_version < 0 || count > 65536) {
      ret = OB_INVALID_ARGUMENT;
    }
    ObTabletIDArray logical_tablet_ids;
    for (uint64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      const uint64_t logical_tablet_id = request.number();
      ObTabletID tablet_id(logical_tablet_id);
      if (request.ret || !tablet_id.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        ret = logical_tablet_ids.push_back(tablet_id);
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
          ns, logical_tablet_ids, tablet_ids);
    } else if (OB_SUCC(ret) && storage_space.is_global()) {
      // Global-space tablets (the namespace-control catalog) are already
      // physical ids; namespace routing applies to namespace spaces only.
      ret = tablet_ids.assign(logical_tablet_ids);
    } else if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < logical_tablet_ids.count(); ++i) {
        ObTabletID tablet_id = logical_tablet_ids.at(i);
        if (OB_FAIL(route_tablet_id(ns, tablet_id))) {
        } else {
          ret = tablet_ids.push_back(tablet_id);
        }
      }
    }
    has_explicit_tablets = OB_SUCC(ret);
  }
  const bool consumed = request.consumed();
  const bool valid_param = tx_param.is_valid();
  ObTableLockService *service = share::server_service<ObTableLockService>();
  if (!ret && (!consumed || !valid_param || payload.empty())) {
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

struct EngineWrite {
  ObArenaAllocator allocator{ObMemAttr("NsRemoteWrite")};
  ObSchemaGetterGuard guard;
  ObTableSchema logical_schema{&allocator};
  ObTableSchema routed_schema{&allocator};
  std::vector<std::unique_ptr<ObTableSchema>> logical_materialization_schemas;
  std::vector<std::unique_ptr<ObTableSchema>> routed_materialization_schemas;
  ObArray<const ObTableSchema *> materialization_schemas;
  ObDmlTablePlan plan{allocator};
  ObTimeZoneInfo timezone;
  ObDmlWriteSpec spec;
  ObTxReadSnapshot snapshot;
  concurrent_control::ObWriteFlag write_flag;
  ObWriteContext context;
  ObDmlExecution execution; // Released before context, plan and allocator.
  ObSEArray<uint64_t, 2> columns;
  std::vector<uint64_t> logical_tablets;
  StorageSpaceHandle storage_space;
  const ObTableSchema *schema = nullptr;

  int prepare(const WritePrepareRequest &request, ObTxDesc &tx) {
    const uint64_t table = request.table_id;
    if (request.spec.tz_info_ == nullptr) { return OB_INVALID_ARGUMENT; }
    int64_t request_bytes = 137; // Former opcode and seventeen number fields.
    auto add_bytes = [&](int64_t size) {
      if (size < 0 || size > static_cast<int64_t>(MAX_SQL_MESSAGE) - request_bytes) {
        return OB_SIZE_OVERFLOW;
      }
      request_bytes += size;
      return OB_SUCCESS;
    };
    int ret = add_bytes(request.logical_schema.get_serialize_size());
    for (int64_t i = 0; !ret && i < request.materialization_schemas.count(); ++i) {
      const ObTableSchema *schema = request.materialization_schemas.at(i);
      ret = schema ? add_bytes(schema->get_serialize_size()) : OB_INVALID_ARGUMENT;
    }
    if (ret || OB_FAIL(add_bytes(request.spec.tz_info_->get_serialize_size()))
        || OB_FAIL(add_bytes(request.snapshot.get_serialize_size()))
        || OB_FAIL(add_bytes(request.write_flag.get_serialize_size()))
        || OB_FAIL(add_bytes(request.columns.size() * 8))) { return ret; }
    storage_space = request.storage_space;
    spec = request.spec;
    ret = logical_schema.assign(request.logical_schema);
    if (OB_FAIL(ret)) { return ret; }
    const bool namespace_local = storage_space.is_namespace();
    const uint64_t ns = storage_space.namespace_id();
    const int64_t materialization_schema_count = request.materialization_schemas.count();
    if (materialization_schema_count > 3
        || (!namespace_local && materialization_schema_count != 0)) {
      return OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; i < materialization_schema_count; ++i) {
      auto logical = std::make_unique<ObTableSchema>(&allocator);
      if (OB_FAIL(logical->assign(*request.materialization_schemas.at(i)))) { return ret; }
      auto routed = std::make_unique<ObTableSchema>(&allocator);
      int schema_ret = !namespace_local || ns == 1
          ? routed->assign(*logical)
          : NamespaceForkKernelPrototype::make_storage_schema(ns, *logical, *routed);
      if (schema_ret != OB_SUCCESS) { return schema_ret; }
      if (OB_FAIL(materialization_schemas.push_back(routed.get()))) { return ret; }
      logical_materialization_schemas.push_back(std::move(logical));
      routed_materialization_schemas.push_back(std::move(routed));
    }
    spec.timeout_ = std::min<int64_t>(spec.timeout_, THIS_WORKER.get_timeout_ts());
    if (OB_FAIL(timezone.assign(*request.spec.tz_info_))) { return ret; }
    spec.tz_info_ = &timezone;
    if (OB_FAIL(snapshot.assign(request.snapshot))) { return ret; }
    write_flag = request.write_flag;
    const int64_t count = request.columns.size();
    if (count == 0 || count > OB_MAX_COLUMN_NUMBER) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu columns=%llu stage=validate ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table, (unsigned long long)count,
          ret);
      return OB_NOT_SUPPORTED;
    }
    if ((!is_inner_table(table) && spec.schema_version_ <= 0)
            || logical_schema.get_table_id() != table
            || logical_schema.get_schema_version() < 0
            || (spec.schema_version_ > 0
                && logical_schema.get_schema_version() != spec.schema_version_)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      if (!namespace_local || ns == 1) {
        schema = &logical_schema;
      } else {
        ret = NamespaceForkKernelPrototype::make_storage_schema(
            ns, logical_schema, routed_schema);
        if (!ret) { schema = &routed_schema; }
      }
    }
    if (!ret) {
      // The request carries the exact schema pinned by the worker's SchemaGuard.
      // Asking storage to validate it against the shared process SchemaService
      // would reintroduce a second, stale namespace schema authority.
      spec.check_schema_version_ = false;
    }
    if (ret) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu stage=schema ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table, ret);
      return ret;
    }
    if (!schema || schema->get_schema_version() != spec.schema_version_) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu stage=version requested=%lld actual=%lld ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table, (long long)spec.schema_version_,
          (long long)(schema ? schema->get_schema_version() : OB_INVALID_VERSION), OB_SCHEMA_EAGAIN);
      return OB_SCHEMA_EAGAIN;
    }
    ObArray<ObTabletID> schema_tablets;
    if (OB_FAIL(schema->get_tablet_ids(schema_tablets)) || schema_tablets.empty()) {
      return ret ? ret : OB_INVALID_ARGUMENT;
    }
    logical_tablets.reserve(schema_tablets.count());
    for (int64_t i = 0; OB_SUCC(ret) && i < schema_tablets.count(); ++i) {
      uint64_t logical_tablet_id = schema_tablets.at(i).id();
      if (ns != 1 && namespace_local
          && NamespaceForkKernelPrototype::is_encoded_id(logical_tablet_id)) {
        ret = NamespaceForkKernelPrototype::local_object_id(
            ns, logical_tablet_id, logical_tablet_id);
      }
      if (OB_SUCC(ret)) { logical_tablets.push_back(logical_tablet_id); }
    }
    if (OB_FAIL(ret)) { return ret; }
    for (int64_t i = 0; !ret && i < count; ++i) {
      const uint64_t id = request.columns[i];
      const auto *column = schema->get_column_schema(id);
      if (!column || has_exist_in_array(columns, id)) { ret = OB_INVALID_ARGUMENT; }
      else { ret = columns.push_back(id); }
    }
    if (ret) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu stage=columns ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table, ret);
      return ret;
    }
    if (!ret) { ret = plan.build(schema, spec.schema_version_, columns); }
    if (!ret) { ret = acquire(tx); }
    if (ret) {
      fprintf(stderr, "PROTOTYPE_V17_WRITE_PREPARE ns=%llu table=%llu tablet=%llu stage=native ret=%d\n",
          (unsigned long long)ns, (unsigned long long)table,
          (unsigned long long)(logical_tablets.empty() ? 0 : logical_tablets.front()), ret);
    }
    return ret;
  }

  int acquire(ObTxDesc &tx) {
    int ret = OB_SUCCESS;
    if (context.is_valid()) { return ret; }
    if (!ret) { ret = share::server_service<ObIWriteContextService>()->acquire_write_context(
        spec.timeout_, tx, snapshot, spec.branch_id_, write_flag, context); }
    if (!ret) { ret = share::server_service<ObIDmlService>()->prepare_execution(
        spec, plan, snapshot, allocator, context, write_flag, execution); }
    return ret;
  }

  // Native store ctxs merge write state into the tx descriptor only when
  // released. Savepoint rollback decisions depend on that state, so the 'B'
  // handler releases all open contexts first; the next batch re-acquires.
  void release_context() {
    execution.reset();
    context.reset();
  }

  int batch(const WriteBatch &request, ObTxDesc &tx,
            int64_t &affected, WriteResult &returned) {
    const char operation = request.operation;
    const bool namespace_local = storage_space.is_namespace();
    const uint64_t ns = storage_space.namespace_id();
    const uint64_t tablet_id = request.tablet_id, count = request.rows;
    const bool update = operation == 'U';
    if (std::find(logical_tablets.begin(), logical_tablets.end(), tablet_id)
            == logical_tablets.end()
        || count == 0
        || count > (update ? 64 : 32) || (update && count % 2)
        || request.cells.size() != count * columns.count()
        || request.lob_headers.size() != request.cells.size()) { return OB_INVALID_ARGUMENT; }
    ObTabletID tablet(tablet_id);
    int ret = OB_SUCCESS;
    if (namespace_local && ns > 1) {
      ret = route_tablet_id(ns, tablet);
    }
    if (OB_SUCC(ret) && !materialization_schemas.empty()) {
      ret = NamespaceForkKernelPrototype::ensure_tablet(
          tablet, *schema, materialization_schemas);
    }
    if (OB_FAIL(ret)) { return ret; }
    if (OB_SUCC(ret)) { ret = acquire(tx); }
    if (OB_FAIL(ret)) { return ret; }
    const int64_t lock_timeout = request.lock_timeout;
    const ObRowLockMode lock_mode = request.lock_mode;
    if (operation == 'L' && lock_mode != ObRowLockMode::NONE
        && lock_mode != ObRowLockMode::WRITE) { return OB_INVALID_ARGUMENT; }
    ObSEArray<uint64_t, 2> updated_columns;
    const uint64_t updated_count = request.updated_columns.size();
    if (updated_count > uint64_t(columns.count())
        || (update || operation == 'f' ? updated_count == 0 : updated_count != 0)) { return OB_INVALID_ARGUMENT; }
    for (uint64_t i = 0; i < updated_count; ++i) {
      const uint64_t column = request.updated_columns[i];
      if (!has_exist_in_array(columns, column)
          || has_exist_in_array(updated_columns, column)) { return OB_INVALID_ARGUMENT; }
      int ret = updated_columns.push_back(column);
      if (ret) { return ret; }
    }
    const auto duplicate_mode = request.duplicate_mode;
    if (duplicate_mode != ObDuplicateReturnMode::ALL && duplicate_mode != ObDuplicateReturnMode::ONE) { return OB_INVALID_ARGUMENT; }
    class Rows final : public ObDatumRowIterator {
    public:
      const std::vector<ObObj> &cells;
      const std::vector<bool> &lob_headers;
      int64_t width;
      size_t position = 0;
      // UPDATE's old row must remain alive while storage obtains its new row.
      ObDatumRow rows[2];
      Rows(const std::vector<ObObj> &values, const std::vector<bool> &headers, int64_t columns)
          : cells(values), lob_headers(headers), width(columns) {}
      int get_next_row(ObDatumRow *&out) override {
        if (position == cells.size()) { return OB_ITER_END; }
        ObDatumRow &row = rows[(position / width) % 2];
        int ret = OB_SUCCESS;
        for (int64_t i = 0; !ret && i < width; ++i) {
          const size_t index = position++;
          ret = row.storage_datums_[i].from_obj_enhance(cells[index]);
          if (!ret && lob_headers[index]) { row.storage_datums_[i].set_has_lob_header(); }
        }
        row.row_flag_.set_flag(DF_INSERT); out = &row; return ret;
      }
    } rows(request.cells, request.lob_headers, columns.count());
    ret = rows.rows[0].init(columns.count());
    if (!ret) { ret = rows.rows[1].init(columns.count()); }
    auto *service = share::server_service<ObIDmlService>();
    if (!ret && operation == 'f') {
      ObDatumRowIterator *duplicates = nullptr;
      ret = service->insert_rows_fetch_duplicates(tablet, tx, execution, columns, updated_columns,
                                                 &rows, duplicate_mode, affected, duplicates);
      struct ReleaseDuplicates { ObIDmlService *service; ObDatumRowIterator *rows;
        ~ReleaseDuplicates() { if (rows) { service->free_duplicate_rows_iterator(rows); } }
      } release{service, duplicates};
      if (!ret || ret == OB_ERR_PRIMARY_KEY_DUPLICATE) {
        const int storage_ret = ret;
        int64_t duplicate_count = 0; ret = OB_SUCCESS;
        ObDatumRow *row = nullptr;
        while (duplicates && !ret && !(ret = duplicates->get_next_row(row))) {
          if (!row || row->get_column_count() != updated_columns.count()
              || duplicate_count >= 32) { ret = OB_SIZE_OVERFLOW; break; }
          for (int64_t i = 0; !ret && i < updated_columns.count(); ++i) {
            ObObj cell;
            const auto &descriptors = plan.get_col_descs();
            const ObColDesc *column = nullptr;
            for (int64_t j = 0; !column && j < descriptors.count(); ++j) {
              if (descriptors.at(j).col_id_ == updated_columns.at(i)) { column = &descriptors.at(j); }
            }
            ret = column ? row->storage_datums_[i].to_obj_enhance(cell, column->col_type_) : OB_INVALID_ARGUMENT;
            if (!ret) {
              ret = returned.append(cell, row->storage_datums_[i].has_lob_header());
            }
          }
          ++duplicate_count;
        }
        if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
        if (!ret) {
          returned.rows = duplicate_count;
          ret = storage_ret;
        }
      }
    } else if (!ret && update) { ret = service->update_rows(tablet, tx, execution, columns, updated_columns, &rows, affected); }
    else if (!ret && operation == 'D') { ret = service->delete_rows(tablet, tx, execution, columns, &rows, affected); }
    else if (!ret && operation == 'L') { ret = service->lock_rows(tablet, tx, execution, lock_timeout, lock_mode, &rows, affected); }
    else if (!ret && operation == 'p') { ret = service->put_rows(tablet, tx, execution, columns, &rows, affected); }
    else if (!ret) { ret = service->insert_rows(tablet, tx, execution, columns, &rows, affected); }
    fprintf(stderr, "PROTOTYPE_V15_WRITE_BATCH op=%c tx=%lld rows=%llu affected=%lld ret=%d\n",
        operation, (long long)tx.get_tx_id().get_id(), (unsigned long long)(update ? count / 2 : count), (long long)affected,
        ret);
    return ret;
  }
};

struct EngineWrites {
  StorageSpaceHandle storage_space;
  sql::ObSQLSessionInfo &session;
  uint32_t sid;
  ObTxDesc *&tx;
  uint64_t sequence = 0;
  std::map<uint64_t, std::unique_ptr<EngineWrite>> writes;
  explicit EngineWrites(StorageSpaceHandle space, sql::ObSQLSessionInfo &s)
      : storage_space(space), session(s), sid(s.get_server_sid()), tx(s.get_tx_desc()) {}
  void reset() {
    session.reset_reserved_snapshot_version();
    writes.clear();
    if (tx) {
      auto *service = query_transaction_service();
      const bool rollback = !tx->is_shadow() && tx->is_in_tx() && !tx->is_tx_end();
      if (rollback) { service->rollback_tx(*tx); }
      service->release_tx(*tx);
      tx = nullptr;
      fprintf(stderr, "PROTOTYPE_V14_TX_RELEASED session=%u rollback=%d\n", sid, rollback);
    }
  }
  ~EngineWrites() { reset(); }
  int release(uint64_t txid) {
    if (!tx || static_cast<uint64_t>(tx->get_tx_id().get_id()) != txid
        || !writes.empty()) { return OB_INVALID_ARGUMENT; }
    reset();
    return OB_SUCCESS;
  }
  int close(uint64_t txid, uint64_t handle, ObTxDesc &view) {
    if (!tx || static_cast<uint64_t>(tx->get_tx_id().get_id()) != txid
        || static_cast<uint64_t>(view.get_tx_id().get_id()) != txid) {
      return OB_INVALID_ARGUMENT;
    }
    auto it = writes.find(handle);
    if (it == writes.end()) { return OB_INVALID_ARGUMENT; }
    writes.erase(it);
    return query_transaction_service()->merge_tx_state(view, *tx);
  }
  int prepare(const WritePrepareRequest &request, const ObTxDesc &view,
              uint64_t &handle) {
    handle = 0;
    if (!tx || tx->get_tx_id() != view.get_tx_id()) { return OB_INVALID_ARGUMENT; }
    if (writes.size() >= 32) { return OB_SIZE_OVERFLOW; }
    auto prepared = std::make_unique<EngineWrite>();
    int ret = prepared->prepare(request, *tx);
    if (!ret) {
      handle = ++sequence;
      writes.emplace(handle, std::move(prepared));
    }
    return ret;
  }
  int batch(const ObTxDesc &view, const WriteBatch &request,
            int64_t &affected, WriteResult &duplicates) {
    if (!tx || tx->get_tx_id() != view.get_tx_id()) { return OB_INVALID_ARGUMENT; }
    if (request.operation != 'I' && request.operation != 'U'
        && request.operation != 'D' && request.operation != 'L'
        && request.operation != 'p' && request.operation != 'f') {
      return OB_INVALID_ARGUMENT;
    }
    auto it = writes.find(request.handle);
    return it == writes.end() ? OB_INVALID_ARGUMENT
        : it->second->batch(request, *tx, affected, duplicates);
  }
  int process(Frame &request, Frame &reply) {
    const uint64_t ns = storage_space.namespace_id();
    auto *service = query_transaction_service();
    const uint64_t operation = request.number();
    const uint64_t txid = request.number();
    int ret = request.ret ? request.ret
        : !storage_space.is_namespace() ? OB_INVALID_ARGUMENT : OB_SUCCESS;
    Frame values;
    if (!ret && request.type() == 'T' && operation == 't') {
      if (tx) { ret = OB_INIT_TWICE; }
      else { ret = service->acquire_tx(request.data.data(), request.data.size(), request.pos, tx); }
    }
    if (!ret && (!tx || static_cast<uint64_t>(tx->get_tx_id().get_id()) != txid)) { ret = OB_INVALID_ARGUMENT; }
    if (!ret && request.type() == 'T') {
      if (operation == 't') {
        if (!request.consumed()) { ret = OB_INVALID_ARGUMENT; }
      } else if (operation == 'M') {
        const int64_t raw_type = static_cast<int64_t>(request.number());
        StorageSpaceHandle request_space;
        if (OB_SUCC(ret)) {
          ret = read_storage_space(request, storage_space, request_space);
        }
        const ObString buffer = request.string();
        ObRegisterMdsFlag flag;
        ObTxSEQ sequence;
        request.read(flag);
        request.read(sequence);
        if (!request.consumed()
            || raw_type <= static_cast<int64_t>(ObTxDataSourceType::UNKNOWN)
            || raw_type >= static_cast<int64_t>(ObTxDataSourceType::MAX_TYPE)
            || buffer.empty()) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const auto type = static_cast<ObTxDataSourceType>(raw_type);
          std::vector<char> storage_buffer;
          bool skip_mds = false;
          if (OB_FAIL(route_tablet_mds(
                  request_space, type, buffer, storage_buffer, skip_mds))) {
            fprintf(stderr,
                "PROTOTYPE_NAMESPACE_MDS_ROUTE ns=%llu global=%d type=%lld input=%d ret=%d\n",
                (unsigned long long)request_space.namespace_id(),
                request_space.is_global(), (long long)raw_type,
                buffer.length(), ret);
          } else if (skip_mds) {
            ret = OB_SUCCESS;
          } else {
            const char *mds_buffer = storage_buffer.empty()
                ? buffer.ptr() : storage_buffer.data();
            const int64_t mds_size = storage_buffer.empty()
                ? buffer.length() : storage_buffer.size();
            ret = service->register_mds_into_tx(
                *tx,
                type,
                mds_buffer,
                mds_size,
                flag,
                sequence);
            if (OB_FAIL(ret)) {
              fprintf(stderr,
                  "PROTOTYPE_NAMESPACE_MDS_REGISTER ns=%llu type=%lld input=%d routed=%zu ret=%d\n",
                  (unsigned long long)ns, (long long)raw_type,
                  buffer.length(), storage_buffer.size(), ret);
            }
            if (OB_SUCC(ret)
                && type == ObTxDataSourceType::CREATE_TABLET_NEW_MDS) {
              obcall::ObBatchCreateTabletArg create_arg;
              int64_t create_pos = 0;
              if (OB_FAIL(create_arg.deserialize(
                      mds_buffer, mds_size, create_pos))) {
              } else if (create_pos != mds_size || !create_arg.is_valid()) {
                ret = OB_INVALID_ARGUMENT;
              } else if (create_arg.set_binding_info_outside_create()
                         && OB_FAIL(storage::ObTabletBindingMdsHelper::
                             modify_tablet_binding_for_create(
                                 create_arg,
                                 THIS_WORKER.get_timeout_ts(),
                                 *tx,
                                 *service))) {
              }
            }
          }
        }
      } else if (operation == 'O') {
        ret = process_table_lock(storage_space, request, *tx);
      } else { ret = OB_NOT_SUPPORTED; }
    }
    reply = Frame('w'); reply.number(ret);
    if (!ret) {
      if (request.type() == 'T') { reply.append(*tx); }
      reply.data.insert(reply.data.end(), values.data.begin() + Frame::HEADER_SIZE, values.data.end());
      if (values.ret) { reply.ret = values.ret; }
    }
    fprintf(stderr, "PROTOTYPE_V14_RPC type=%c op=%c tx=%llu ret=%d wire=%d bytes=%zu\n",
        request.type(), static_cast<char>(operation), (unsigned long long)txid, ret, reply.ret, reply.data.size());
    return reply.ret;
  }
};

// Compatibility view for query's existing descriptor accessors. Constructing
// and decoding this value does not start a transaction service, register a
// transaction, or allocate a storage context in the worker. Engine owns all
// authoritative transaction state; this view is refreshed by transaction RPC.
// Ticket 05c: resolve the transaction's owning session when the ambient
// worker names another session or none (async end-trans completion runs off
// the query thread). A session borrowed from the session manager is returned
// through `borrowed` and must be reverted by the caller.
sql::ObSQLSessionInfo *tx_owner_session(transaction::ObTxDesc &tx,
                                        sql::ObSQLSessionInfo *&borrowed)
{
  borrowed = nullptr;
  sql::ObSQLSessionInfo *session = THIS_WORKER.get_session();
  if (session != nullptr && session->get_tx_desc() == &tx) { return session; }
  auto *mgr = share::server_service<sql::ObSQLSessionMgr>();
  sql::ObSQLSessionInfo *resolved = nullptr;
  if (OB_NOT_NULL(mgr)
      && OB_SUCCESS == mgr->get_session(tx.get_session_id(), resolved)
      && OB_NOT_NULL(resolved) && resolved->get_tx_desc() == &tx
      && in_process_session_ns(resolved) > 1) {
    borrowed = resolved;
    return resolved;
  }
  if (OB_NOT_NULL(resolved)) { mgr->revert_session(resolved); }
  return session;
}
void revert_tx_owner_session(sql::ObSQLSessionInfo *borrowed)
{
  if (OB_NOT_NULL(borrowed)) {
    share::server_service<sql::ObSQLSessionMgr>()->revert_session(borrowed);
  }
}
int tx_rpc(char operation, ObTxDesc &tx, Frame &request, Frame &reply) {
  // A PX task owns a deserialized session. Explicit inner-SQL scopes can also
  // run while THIS_WORKER still names their caller, so require pointer identity.
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(tx, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &tx ? session : nullptr);
  if (scope.error()) { revert_tx_owner_session(borrowed); return scope.error(); }
  Frame message('T'); message.number(operation); message.number(tx.get_tx_id().get_id());
  message.data.insert(message.data.end(), request.data.begin() + Frame::HEADER_SIZE, request.data.end());
  message.ret = request.ret;
  int ret = write_rpc(message, reply);
  if (!ret) { reply.read(tx); ret = reply.ret; }
  revert_tx_owner_session(borrowed);
  return ret;
}
int call_in_process_tx_state(char operation, ObTxDesc &view,
                             const ObTxParam *param, int64_t deadline);
int call_in_process_tx_read_snapshot(ObTxDesc &view,
    ObTxIsolationLevel isolation, int64_t deadline, ObTxReadSnapshot &snapshot);
int call_in_process_tx_create_savepoint(ObTxDesc &view, char operation,
    const ObTxParam *param, bool release, int16_t branch, ObTxSEQ &savepoint);
int call_in_process_tx_rollback_savepoint(ObTxDesc &view, ObTxSEQ savepoint,
    int64_t deadline, bool touched_storage, ObTxCleanPolicy policy);
int call_in_process_tx_named_savepoint(ObTxDesc &view, char operation,
    const ObString &name, int64_t deadline);
int call_in_process_tx_exec_result(ObTxDesc &view, char operation,
    const ObTxExecResult *input, ObTxExecResult *output);
int call_in_process_tablet_binding(ObTxDesc &view, char operation,
    StorageSpaceHandle space, const ObIArray<ObTabletID> &tablets,
    const ObIArray<ObTabletID> *hidden, int64_t schema_version,
    int64_t deadline);
int tx_state(char operation, ObTxDesc &view,
             const ObTxParam *param = nullptr, int64_t deadline = 0)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_state(operation, view, param, deadline);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_read_snapshot(ObTxDesc &view, ObTxIsolationLevel isolation,
                     int64_t deadline, ObTxReadSnapshot &snapshot)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_read_snapshot(view, isolation, deadline, snapshot);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_create_savepoint(ObTxDesc &view, char operation,
    const ObTxParam *param, bool release, int16_t branch, ObTxSEQ &savepoint)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_create_savepoint(
          view, operation, param, release, branch, savepoint);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_rollback_savepoint(ObTxDesc &view, ObTxSEQ savepoint,
    int64_t deadline, bool touched_storage, ObTxCleanPolicy policy)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_rollback_savepoint(
          view, savepoint, deadline, touched_storage, policy);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_named_savepoint(ObTxDesc &view, char operation,
    const ObString &name, int64_t deadline = 0)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_named_savepoint(view, operation, name, deadline);
  revert_tx_owner_session(borrowed);
  return ret;
}
int tx_exec_result(ObTxDesc &view, char operation,
    const ObTxExecResult *input, ObTxExecResult *output)
{
  sql::ObSQLSessionInfo *borrowed = nullptr;
  auto *session = tx_owner_session(view, borrowed);
  StorageSessionScope scope(session && session->get_tx_desc() == &view ? session : nullptr);
  const int ret = scope.error() ? scope.error()
      : call_in_process_tx_exec_result(view, operation, input, output);
  revert_tx_owner_session(borrowed);
  return ret;
}

class RemoteInnerConnectionLockRuntime final : public ObIInnerConnectionLockRuntime
{
public:
  int process_lock_rpc(
      const obcall::ObInnerSQLTransmitArg &arg,
      common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke_raw(arg.get_operation_type(), arg.get_inner_sql(), conn);
  }

  int lock_table(uint64_t table_id,
                 ObTableLockMode lock_mode,
                 int64_t timeout_us,
                 common::sqlclient::ObISQLConnection *conn,
                 ObTableLockOwnerID owner_id,
                 ObTableLockPriority lock_priority) override
  {
    ObLockTableRequest arg;
    arg.table_id_ = table_id;
    arg.owner_id_ = owner_id;
    arg.lock_mode_ = lock_mode;
    arg.op_type_ = IN_TRANS_COMMON_LOCK;
    arg.timeout_us_ = timeout_us;
    arg.lock_priority_ = lock_priority;
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE, arg, conn);
  }

  int lock_table(const ObLockTableRequest &arg,
                 common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLE, arg, conn);
  }

  int unlock_table(const ObUnLockTableRequest &arg,
                   common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_TABLE, arg, conn);
  }

  int lock_tablet(uint64_t table_id,
                  ObTabletID tablet_id,
                  ObTableLockMode lock_mode,
                  int64_t timeout_us,
                  common::sqlclient::ObISQLConnection *conn) override
  {
    ObLockTabletsRequest arg;
    arg.table_id_ = table_id;
    arg.owner_id_.set_default();
    arg.lock_mode_ = lock_mode;
    arg.op_type_ = IN_TRANS_COMMON_LOCK;
    arg.timeout_us_ = timeout_us;
    int ret = arg.tablet_ids_.push_back(tablet_id);
    return ret ? ret : invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET, arg, conn);
  }

  int lock_tablet(uint64_t table_id,
                  const ObIArray<ObTabletID> &tablet_ids,
                  ObTableLockMode lock_mode,
                  int64_t timeout_us,
                  common::sqlclient::ObISQLConnection *conn) override
  {
    ObLockTabletsRequest arg;
    arg.table_id_ = table_id;
    arg.owner_id_.set_default();
    arg.lock_mode_ = lock_mode;
    arg.op_type_ = IN_TRANS_COMMON_LOCK;
    arg.timeout_us_ = timeout_us;
    int ret = arg.tablet_ids_.assign(tablet_ids);
    return ret ? ret : invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_TABLET, arg, conn);
  }

  int lock_tablet(const ObLockAloneTabletRequest &arg,
                  common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_ALONE_TABLET, arg, conn);
  }

  int unlock_tablet(const ObUnLockAloneTabletRequest &arg,
                    common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_ALONE_TABLET, arg, conn);
  }

  int lock_obj(const ObLockObjRequest &arg,
               common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJ, arg, conn);
  }

  int unlock_obj(const ObUnLockObjRequest &arg,
                 common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJ, arg, conn);
  }

  int lock_obj(const ObLockObjsRequest &arg,
               common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_LOCK_OBJS, arg, conn);
  }

  int unlock_obj(const ObUnLockObjsRequest &arg,
                 common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_UNLOCK_OBJS, arg, conn);
  }

  int replace_lock(const ObReplaceLockRequest &arg,
                   common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_REPLACE_LOCK, arg, conn);
  }

  int replace_lock(const ObReplaceAllLocksRequest &arg,
                   common::sqlclient::ObISQLConnection *conn) override
  {
    return invoke(obcall::ObInnerSQLTransmitArg::OPERATION_TYPE_REPLACE_LOCKS, arg, conn);
  }

  int execute_write_sql(common::sqlclient::ObISQLConnection *conn,
                        const ObSqlString &sql,
                        int64_t &affected_rows) override
  {
    auto *inner = static_cast<ObInnerSQLConnection *>(conn);
    return inner ? inner->execute_write(sql.ptr(), affected_rows) : OB_INVALID_ARGUMENT;
  }

  int execute_read_sql(common::sqlclient::ObISQLConnection *conn,
                       const ObSqlString &sql,
                       ObISQLClient::ReadResult &result) override
  {
    auto *inner = static_cast<ObInnerSQLConnection *>(conn);
    return inner ? inner->execute_read(sql.ptr(), result) : OB_INVALID_ARGUMENT;
  }

private:
  int invoke_raw(obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
                 const ObString &payload,
                 common::sqlclient::ObISQLConnection *conn)
  {
    auto *inner = static_cast<ObInnerSQLConnection *>(conn);
    ObTxDesc *tx = inner ? inner->get_session().get_tx_desc() : nullptr;
    if (!inner || !inner->is_in_trans() || !tx || payload.empty()) {
      return OB_INVALID_ARGUMENT;
    }
    // A shared-originated inner SQL runs inside an outer IPC request, while its
    // native transaction lives on the inner session's persistent storage
    // route.  ObSqlTransControl establishes that route when the transaction is
    // opened; restore it here so the lock and catalog writes use the same
    // EngineWrites/ObTxDesc instead of the outer request's transaction owner.
    if (!inner->get_session().namespace_storage_binding()) {
      return OB_NOT_INIT;
    }
    StorageSessionScope storage_scope(&inner->get_session(), false);
    if (storage_scope.error()) {
      return storage_scope.error();
    }
    ObTxParam param;
    param.access_mode_ = ObTxAccessMode::RW;
    param.isolation_ = inner->get_session().get_tx_isolation();
    inner->get_session().get_tx_timeout(param.timeout_us_);
    param.lock_timeout_us_ = inner->get_session().get_trx_lock_timeout();
    Frame request, reply;
    request.number(operation);
    StorageSpaceHandle storage_space = active_worker_storage_space();
    Frame lock_plan;
    int ret = OB_SUCCESS;
    if (is_schema_table_lock_operation(operation)) {
      ret = append_worker_table_lock_plan(
          operation, payload, lock_plan, storage_space);
    }
    write_storage_space(request, storage_space);
    request.append(param);
    request.string(payload);
    if (OB_SUCC(ret) && OB_FAIL(request.ret)) {
    } else if (OB_SUCC(ret) && is_schema_table_lock_operation(operation)) {
      request.data.insert(request.data.end(),
          lock_plan.data.begin() + Frame::HEADER_SIZE, lock_plan.data.end());
      ret = lock_plan.ret;
    }
    return ret ? ret : tx_rpc('O', *tx, request, reply);
  }

  template <typename T>
  int invoke(obcall::ObInnerSQLTransmitArg::InnerSQLOperationType operation,
             const T &arg,
             common::sqlclient::ObISQLConnection *conn)
  {
    const int64_t size = arg.get_serialize_size();
    if (size <= 0 || size > static_cast<int64_t>(MAX_SQL_MESSAGE)) {
      return OB_SIZE_OVERFLOW;
    }
    std::vector<char> buffer(size);
    int64_t pos = 0;
    int ret = arg.serialize(buffer.data(), buffer.size(), pos);
    if (!ret && pos != size) { ret = OB_ERR_UNEXPECTED; }
    return ret ? ret : invoke_raw(
        operation, ObString(static_cast<int32_t>(pos), buffer.data()), conn);
  }
};

int call_in_process_tx_clock(
    const std::function<int(ObITransactionService &)> &call);
int call_in_process_tx_interrupt(const transaction::ObTxDesc &tx, int cause);
int call_in_process_tx_snapshot(char operation,
                                transaction::ObTxReadSnapshot &snapshot);
int release_in_process_tx(const transaction::ObTxDesc &tx);
int close_in_process_write(transaction::ObTxDesc &view, uint64_t handle);

class RemoteTransactionService final : public ObITransactionService {
public:
  int gen_unique_id(int64_t &unique_id, int64_t timeout_us) override {
    if (timeout_us <= 0) { return OB_INVALID_ARGUMENT; }
    return call_in_process_tx_clock(
        [&](ObITransactionService &service) {
          return service.gen_unique_id(unique_id, timeout_us);
        });
  }
  int get_gts_sync(int64_t timeout_us, share::SCN &gts) override {
    if (timeout_us <= 0) { return OB_INVALID_ARGUMENT; }
    return call_in_process_tx_clock(
        [&](ObITransactionService &service) {
          return service.get_gts_sync(timeout_us, gts);
        });
  }
  int acquire_tx(transaction::ObTxDesc *&tx,
                         uint32_t session_id) override {
    if (tx) { return OB_INVALID_ARGUMENT; }
    auto owned = std::make_unique<ObTxDesc>();
    int ret = tx_state('A', *owned);
    if (!ret) { tx = owned.release(); }
    return ret; }
  int acquire_tx(const char *buf,
                         int64_t len,
                         int64_t &pos,
                         transaction::ObTxDesc *&tx) override {
    if (tx) { return OB_INVALID_ARGUMENT; }
    auto owned = std::make_unique<ObTxDesc>();
    int ret = owned->deserialize_shadow(buf, len, pos);
    if (!ret) {
      // Native PX deserialization creates a private execution-state copy. Its
      // release must never terminate the coordinator's storage transaction.
      tx = owned.release();
    }
    return ret; }
  int start_tx(transaction::ObTxDesc &tx,
                       const transaction::ObTxParam &tx_param) override {
    return tx_state('H', tx, &tx_param); }
  int abort_tx(transaction::ObTxDesc &tx, int cause) override { return rollback_tx(tx); }
  int rollback_tx(transaction::ObTxDesc &tx) override { return tx_state('R', tx); }
  int commit_tx(transaction::ObTxDesc &tx,
                        int64_t expire_ts) override { return tx_state('C', tx, nullptr, expire_ts); }
  int submit_commit_tx(transaction::ObTxDesc &tx,
                               int64_t expire_ts,
                               transaction::ObITxCallback &callback) override {
    // Native callbacks support completion before submit returns. Commit stays
    // authoritative in storage; only then release the native SQL response.
    const int ret = commit_tx(tx, expire_ts);
    if (!ret) { callback.callback(OB_SUCCESS); }
    return ret;
  }
  int release_tx(transaction::ObTxDesc &tx) override {
    int ret = OB_SUCCESS;
    if (!tx.is_shadow()) {
      // Release through the owning session's in-process storage context.
      sql::ObSQLSessionInfo *borrowed = nullptr;
      auto *session = tx_owner_session(tx, borrowed);
      if (session != nullptr && session->get_tx_desc() == &tx
          && in_process_session_ns(session) > 1) {
        StorageSessionScope scope(session, false);
        ret = scope.error() ? scope.error() : release_in_process_tx(tx);
      }
      revert_tx_owner_session(borrowed);
    }
    delete &tx; return ret; }
  int reuse_tx(transaction::ObTxDesc &tx) override { return tx_state('U', tx); }
  int prepare_tx_for_statement(transaction::ObTxDesc &tx) override { return tx_state('S', tx); }
  int prepare_tx_for_autocommit_retry(transaction::ObTxDesc &tx) override { return tx_state('N', tx); }
  int register_mds_into_tx(
      transaction::ObTxDesc &tx,
      const transaction::ObTxDataSourceType &type,
      const char *buffer,
      int64_t buffer_size,
      const transaction::ObRegisterMdsFlag &flag,
      transaction::ObTxSEQ sequence) override {
    if (buffer == nullptr || buffer_size <= 0 || buffer_size > INT32_MAX) {
      return OB_INVALID_ARGUMENT;
    }
    Frame request, reply;
    request.number(static_cast<int64_t>(type));
    StorageSpaceHandle storage_space;
    int ret = worker_mds_storage_space(type, buffer, buffer_size, storage_space);
    write_storage_space(request, storage_space);
    request.string(ObString(static_cast<int32_t>(buffer_size), buffer));
    request.append(flag);
    request.append(sequence);
    if (OB_SUCC(ret) && OB_FAIL(request.ret)) {
    }
    return ret ? ret : tx_rpc('M', tx, request, reply);
  }
  int interrupt(transaction::ObTxDesc &tx, int cause) override {
    return call_in_process_tx_interrupt(tx, cause);
  }
  int get_read_snapshot(transaction::ObTxDesc &tx,
                                transaction::ObTxIsolationLevel isolation_level,
                                int64_t expire_ts,
                                transaction::ObTxReadSnapshot &snapshot) override {
    return tx_read_snapshot(tx, isolation_level, expire_ts, snapshot); }
  int get_read_snapshot_version(int64_t expire_ts,
                                share::SCN &snapshot_version) override {
    if (expire_ts <= 0) { return OB_INVALID_ARGUMENT; }
    return call_in_process_tx_clock(
        [&](ObITransactionService &service) {
          return service.get_read_snapshot_version(
              std::min(expire_ts, THIS_WORKER.get_timeout_ts()), snapshot_version);
        });
  }
  int get_weak_read_snapshot_version(int64_t max_read_stale_time,
                                     share::SCN &snapshot_version) override {
    return call_in_process_tx_clock(
        [&](ObITransactionService &service) {
          return service.get_weak_read_snapshot_version(
              max_read_stale_time, snapshot_version);
        });
  }
  int register_tx_snapshot_verify(
      transaction::ObTxReadSnapshot &snapshot) override {
    if (!snapshot.tx_id().is_valid()) { return OB_SUCCESS; }
    sql::ObSQLSessionInfo *session = THIS_WORKER.get_session();
    transaction::ObTxDesc *tx = session ? session->get_tx_desc() : nullptr;
    if (!tx || tx->get_tx_id() != snapshot.tx_id()) {
      return OB_INVALID_ARGUMENT;
    }
    return call_in_process_tx_snapshot('v', snapshot);
  }
  int refresh_tx_snapshot_verify(
      transaction::ObTxReadSnapshot &snapshot) override {
    if (!snapshot.tx_id().is_valid()
        || !snapshot.is_valid()
        || snapshot.is_committed()) {
      return OB_SUCCESS;
    }
    return call_in_process_tx_snapshot('z', snapshot);
  }
  int unregister_tx_snapshot_verify(
      transaction::ObTxReadSnapshot &snapshot) override {
    if (!snapshot.tx_id().is_valid()) { return OB_SUCCESS; }
    return call_in_process_tx_snapshot('y', snapshot);
  }
  int create_implicit_savepoint(transaction::ObTxDesc &tx,
                                        const transaction::ObTxParam &tx_param,
                                        transaction::ObTxSEQ &savepoint,
                                        bool release) override {
    return tx_create_savepoint(tx, 'P', &tx_param, release, 0, savepoint); }
  int create_branch_savepoint(transaction::ObTxDesc &tx,
                                      int16_t branch,
                                      transaction::ObTxSEQ &savepoint) override {
    return tx_create_savepoint(tx, 'J', nullptr, false, branch, savepoint); }
  int create_in_txn_implicit_savepoint(transaction::ObTxDesc &tx,
                                               transaction::ObTxSEQ &savepoint) override {
    return tx_create_savepoint(tx, 'I', nullptr, false, 0, savepoint); }
  int create_explicit_savepoint(transaction::ObTxDesc &tx,
                                        const common::ObString &savepoint) override {
    return tx_named_savepoint(tx, 'F', savepoint); }
  int rollback_to_implicit_savepoint(
      transaction::ObTxDesc &tx,
      transaction::ObTxSEQ savepoint,
      int64_t expire_ts,
      bool touched_storage,
      transaction::ObTxCleanPolicy clean_policy) override {
    return tx_rollback_savepoint(tx, savepoint, expire_ts,
        touched_storage, clean_policy); }
  int rollback_to_explicit_savepoint(transaction::ObTxDesc &tx,
                                             const common::ObString &savepoint,
                                             int64_t expire_ts) override {
    return tx_named_savepoint(tx, 'L', savepoint, expire_ts); }
  int release_explicit_savepoint(transaction::ObTxDesc &tx,
                                         const common::ObString &savepoint) override {
    return tx_named_savepoint(tx, 'D', savepoint); }
  int create_stash_savepoint(transaction::ObTxDesc &tx,
                                     const common::ObString &name) override {
    return tx_named_savepoint(tx, 'K', name); }
  int merge_tx_state(transaction::ObTxDesc &to,
                             const transaction::ObTxDesc &from) override {
    if (to.get_tx_id() != from.get_tx_id()) { return OB_INVALID_ARGUMENT; }
    // These are the same descriptor operations used by ObTransService. Task
    // copies aggregate locally; add_tx_exec_result publishes to the owner.
    return to.merge_exec_info_with(from);
  }
  int get_tx_exec_result(transaction::ObTxDesc &tx,
                                 transaction::ObTxExecResult &exec_info) override {
    return tx.is_shadow() ? tx.get_inc_exec_info(exec_info) : collect_tx_exec_result(tx, exec_info);
  }
  int add_tx_exec_result(transaction::ObTxDesc &tx,
                                 const transaction::ObTxExecResult &exec_info) override {
    if (tx.is_shadow()) { return tx.add_exec_info(exec_info); }
    return tx_exec_result(tx, 'a', &exec_info, nullptr);
  }
  int collect_tx_exec_result(transaction::ObTxDesc &tx,
                                     transaction::ObTxExecResult &result) override {
    if (tx.is_shadow()) { return tx.get_inc_exec_info(result); }
    return tx_exec_result(tx, 'E', nullptr, &result); }
  bool can_elr() const override { return false; }
};

int call_in_process_rootserver_runtime(
    uint64_t namespace_id,
    const std::function<int(rootserver::ObIRootserverLocalRuntime &,
                            StorageSpaceHandle)> &call);

class RemoteRootserverLocalRuntime final
    : public rootserver::ObIRootserverLocalRuntime
{
public:
  explicit RemoteRootserverLocalRuntime(uint64_t namespace_id = 0)
      : namespace_id_(namespace_id) {}
  int set_ds_action(const obcall::ObDebugSyncActionArg &arg) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle) {
          return runtime.set_ds_action(arg);
        });
  }
  int calc_column_checksum_request(
      const obcall::ObCalcColumnChecksumRequestArg &arg,
      obcall::ObCalcColumnChecksumRequestRes &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          return calc_namespace_column_checksum(space, runtime, arg, result);
        });
  }
  int build_ddl_local(
      const obcall::ObDDLLocalBuildArg &arg,
      obcall::ObDDLLocalBuildResult &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObDDLLocalBuildArg routed;
          const int ret = route_rootserver_build_arg(space, arg, routed);
          return ret ? ret : runtime.build_ddl_local(routed, result);
        });
  }
  int check_and_cancel_ddl_complement_data_dag(
      const obcall::ObDDLLocalBuildArg &arg,
      bool &is_dag_exist) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObDDLLocalBuildArg routed;
          const int ret = route_rootserver_build_arg(space, arg, routed);
          return ret ? ret : runtime.check_and_cancel_ddl_complement_data_dag(
              routed, is_dag_exist);
        });
  }
  int check_and_cancel_delete_lob_meta_row_dag(
      const obcall::ObDDLLocalBuildArg &arg,
      bool &is_dag_exist) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObDDLLocalBuildArg routed;
          const int ret = route_rootserver_build_arg(space, arg, routed);
          return ret ? ret : runtime.check_and_cancel_delete_lob_meta_row_dag(
              routed, is_dag_exist);
        });
  }
  int minor_freeze(
      const obcall::ObMinorFreezeArg &arg,
      obcall::Int64 &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObMinorFreezeArg routed = arg;
          int ret = routed.tablet_id_.is_valid()
              ? route_tablet_id(space, routed.tablet_id_) : OB_SUCCESS;
          return ret ? ret : runtime.minor_freeze(routed, result);
        });
  }
  int check_schema_version_elapsed(
      const obcall::ObCheckSchemaVersionElapsedArg &arg,
      obcall::ObCheckSchemaVersionElapsedResult &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObCheckSchemaVersionElapsedArg routed = arg;
          routed.schema_version_refreshed_by_caller_ = true;
          return check_namespace_tablet_elapsed(
              space, routed, result,
              [&runtime](const obcall::ObCheckSchemaVersionElapsedArg &storage_arg,
                         obcall::ObCheckSchemaVersionElapsedResult &storage_result) {
                return runtime.check_schema_version_elapsed(storage_arg, storage_result);
              });
        });
  }
  int check_modify_time_elapsed(
      const obcall::ObCheckModifyTimeElapsedArg &arg,
      obcall::ObCheckModifyTimeElapsedResult &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObCheckModifyTimeElapsedArg routed = arg;
          return check_namespace_tablet_elapsed(
              space, routed, result,
              [&runtime](const obcall::ObCheckModifyTimeElapsedArg &storage_arg,
                         obcall::ObCheckModifyTimeElapsedResult &storage_result) {
                return runtime.check_modify_time_elapsed(storage_arg, storage_result);
              });
        });
  }
  int check_ddl_tablet_merge_status(
      const obcall::ObDDLCheckTabletMergeStatusArg &arg,
      obcall::ObDDLCheckTabletMergeStatusResult &result) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle space) {
          obcall::ObDDLCheckTabletMergeStatusArg routed = arg;
          int ret = OB_SUCCESS;
          for (int64_t i = 0; !ret && i < routed.tablet_ids_.count(); ++i) {
            ret = route_tablet_id(space, routed.tablet_ids_.at(i));
          }
          return ret ? ret : runtime.check_ddl_tablet_merge_status(routed, result);
        });
  }
  int check_server_empty(bool &is_empty) override {
    return call_in_process_rootserver_runtime(namespace_id_,
        [&](rootserver::ObIRootserverLocalRuntime &runtime, StorageSpaceHandle) {
          return runtime.check_server_empty(is_empty);
        });
  }
  int modify_tablet_binding_for_rw_defensive(
      common::ObMySQLTransaction &trans,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      int64_t schema_version,
      int64_t abs_timeout_us) override {
    return modify_tablet_binding_defensive_(
        'b', trans, tablet_ids, schema_version, abs_timeout_us);
  }
  int modify_tablet_binding_for_write_defensive(
      common::ObMySQLTransaction &trans,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      int64_t schema_version,
      int64_t abs_timeout_us) override {
    return modify_tablet_binding_defensive_(
        'd', trans, tablet_ids, schema_version, abs_timeout_us);
  }
private:
  int modify_tablet_binding_defensive_(
      char operation,
      common::ObMySQLTransaction &trans,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      int64_t schema_version,
      int64_t abs_timeout_us) {
    sqlclient::ObISQLConnection *connection = trans.get_connection();
    sql::ObSQLSessionInfo *session =
        query::ObInnerSQLConnectionAccess::get_session(connection);
    transaction::ObTxDesc *tx = session ? session->get_tx_desc() : nullptr;
    int ret = !trans.is_started() || !connection || !session || !tx
        ? OB_INVALID_ARGUMENT : OB_SUCCESS;
    StorageSessionScope scope(session, false);
    if (!ret && scope.error()) { ret = scope.error(); }
    if (!ret) { ret = call_in_process_tablet_binding(*tx, operation,
        storage_space_(), tablet_ids, nullptr, schema_version, abs_timeout_us); }
    return ret;
  }
public:
  int modify_tablet_binding_for_unbind(
      common::ObMySQLTransaction &trans,
      const common::ObIArray<common::ObTabletID> &orig_tablet_ids,
      const common::ObIArray<common::ObTabletID> &hidden_tablet_ids,
      int64_t schema_version,
      int64_t abs_timeout_us) override {
    sqlclient::ObISQLConnection *connection = trans.get_connection();
    sql::ObSQLSessionInfo *session =
        query::ObInnerSQLConnectionAccess::get_session(connection);
    transaction::ObTxDesc *tx = session ? session->get_tx_desc() : nullptr;
    int ret = !trans.is_started() || !connection || !session || !tx
        ? OB_INVALID_ARGUMENT : OB_SUCCESS;
    StorageSessionScope scope(session, false);
    if (!ret && scope.error()) { ret = scope.error(); }
    if (!ret) { ret = call_in_process_tablet_binding(*tx, 'u',
        storage_space_(), orig_tablet_ids, &hidden_tablet_ids,
        schema_version, abs_timeout_us); }
    return ret;
  }
  int wait_until_change_stream_refreshed(
      common::ObMySQLProxy &mysql_proxy,
      int64_t timeout_us) override {
    UNUSEDx(mysql_proxy, timeout_us);
    // Change-stream freshness is SQL/catalog work and must stay in the worker.
    // The namespace-fork path does not require it; expose no shared-process SQL
    // fallback while that worker-local service is being separated.
    return OB_NOT_SUPPORTED;
  }

private:
  StorageSpaceHandle storage_space_() const {
    return namespace_id_ == 0 ? active_worker_storage_space()
        : StorageSpaceHandle::namespace_space(namespace_id_);
  }
  uint64_t namespace_id_;
};

class RemoteWriteContext final : public ObIWriteContextService {
public:
  int acquire_write_context(int64_t, ObTxDesc &tx, const ObTxReadSnapshot &, int16_t,
                            concurrent_control::ObWriteFlag &, ObWriteContext &context) override {
    // Deferred acquisition is combined with prepare_execution in one RPC.
    context.bind(&tx, nullptr); return OB_SUCCESS;
  }
};

struct RemoteExecution final : public ObIDmlExecutionState {
  uint64_t handle = 0, txid = 0;
  ObTxDesc *tx = nullptr; // Borrowed query view; never sent across IPC.
  sql::ObSQLSessionInfo *session = nullptr;
  int64_t deadline = 0;
  std::vector<ObObjMeta> types;
  std::vector<uint64_t> columns;
  void destroy() override {
    if (handle) {
      StorageSessionScope scope(session);
      if (!scope.error() && tx) { close_in_process_write(*tx, handle); }
    }
    delete this;
  }
  int64_t timeout() const override { return deadline; }
  void set_skip_flush_redo(bool) override {} // No index writes in this slice.
};

class DuplicateRows final : public ObDatumRowIterator {
public:
  std::vector<std::unique_ptr<WriteResult>> batches;
  size_t current = 0;
  uint64_t remaining = 0;
  int64_t width;
  ObDatumRow row;
  explicit DuplicateRows(int64_t n) : width(n) {}
  int get_next_row(ObDatumRow *&out) override {
    while (!remaining) {
      if (current == batches.size()) { return OB_ITER_END; }
      remaining = batches[current]->rows;
      if (!remaining) { ++current; }
    }
    WriteResult &batch = *batches[current];
    if (batch.cells.size() != batch.rows * width
        || batch.lob_headers.size() != batch.cells.size()) { return OB_INVALID_ARGUMENT; }
    int ret = row.is_valid() ? OB_SUCCESS : row.init(width);
    for (int64_t i = 0; !ret && i < width; ++i) {
      const size_t index = (batch.rows - remaining) * width + i;
      ret = row.storage_datums_[i].from_obj_enhance(batch.cells[index]);
      if (!ret && batch.lob_headers[index]) { row.storage_datums_[i].set_has_lob_header(); }
    }
    if (!--remaining) {
      ++current;
    }
    out = &row; return ret;
  }
};

class CompletedLobReadCursor final : public common::ObILobReadCursor {
public:
  int get_next_row(ObString &) override { return OB_ITER_END; }
  void reset() override {}
};

int read_in_process_lob(common::ObLobLocatorV2 &locator, int64_t timeout,
                        common::ObIAllocator &allocator, common::ObString &output);

class InProcessLobReadService final : public common::ObILobReadService {
public:
  void set_local(common::ObILobReadService *service) { local_ = service; }

  int get_outrow_lob_full_data(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      bool has_lob_header,
      bool is_outrow,
      common::ObIAllocator *tmp_alloc) override {
    return use_remote(ctx.locator_)
        ? (!has_lob_header || !is_outrow ? OB_INVALID_ARGUMENT : materialize(ctx, ctx.locator_))
        : local_ ? local_->get_outrow_lob_full_data(
              ctx, cs_type, has_lob_header, is_outrow, tmp_alloc) : OB_NOT_INIT;
  }

  int get_delta_lob_full_data(
      common::ObLobTextIterCtx &ctx,
      common::ObObjType type,
      common::ObCollationType cs_type,
      common::ObLobLocatorV2 &locator,
      common::ObIAllocator *allocator,
      common::ObString &data) override {
    return local_ ? local_->get_delta_lob_full_data(
        ctx, type, cs_type, locator, allocator, data) : OB_NOT_INIT;
  }

  int get_outrow_prefix_data(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      bool has_lob_header,
      bool is_outrow,
      common::ObIAllocator *tmp_alloc,
      uint32_t prefix_char_len) override {
    if (!use_remote(ctx.locator_)) {
      return local_ ? local_->get_outrow_prefix_data(
          ctx, cs_type, has_lob_header, is_outrow, tmp_alloc, prefix_char_len) : OB_NOT_INIT;
    }
    int ret = !has_lob_header || !is_outrow ? OB_INVALID_ARGUMENT : materialize(ctx, ctx.locator_);
    if (!ret && ctx.content_byte_len_ > 0) {
      const int64_t chars = common::ObCharset::strlen_char(cs_type, ctx.buff_, ctx.content_byte_len_);
      const int64_t wanted = std::min<int64_t>(chars, prefix_char_len);
      ctx.content_byte_len_ = static_cast<uint32_t>(
          common::ObCharset::charpos(cs_type, ctx.buff_, ctx.content_byte_len_, wanted));
    }
    return ret;
  }

  int get_first_block(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      bool has_lob_header,
      bool is_outrow,
      common::ObIAllocator *tmp_alloc,
      common::ObString &str,
      common::ObTextStringIterState &state) override {
    if (!use_remote(ctx.locator_)) {
      return local_ ? local_->get_first_block(
          ctx, cs_type, has_lob_header, is_outrow, tmp_alloc, str, state) : OB_NOT_INIT;
    }
    int ret = !has_lob_header || !is_outrow ? OB_INVALID_ARGUMENT : materialize(ctx, ctx.locator_);
    if (!ret) {
      free_lob_query_iter(ctx);
      // The reader materializes the full LOB; callers size their result for
      // the configured window, so expose only that window as the first block.
      const int64_t byte_len = ctx.content_byte_len_;
      const int64_t char_len = common::ObCharset::strlen_char(cs_type, ctx.buff_, byte_len);
      const int64_t start_char = static_cast<int64_t>(
          std::min<uint64_t>(ctx.start_offset_, static_cast<uint64_t>(char_len)));
      const int64_t read_chars = ctx.total_access_len_ == 0
          ? char_len - start_char
          : std::min<int64_t>(ctx.total_access_len_, char_len - start_char);
      if (read_chars < 0) { return OB_INVALID_ARGUMENT; }
      const int64_t start_byte = common::ObCharset::charpos(
          cs_type, ctx.buff_, byte_len, start_char);
      const int64_t end_byte = common::ObCharset::charpos(
          cs_type, ctx.buff_, byte_len, start_char + read_chars);
      if (start_byte > end_byte || end_byte > byte_len) {
        return OB_INVALID_ARGUMENT;
      }
      if (ctx.buff_ != nullptr) { ctx.buff_ += start_byte; }
      ctx.buff_byte_len_ -= static_cast<uint32_t>(start_byte);
      ctx.content_byte_len_ = static_cast<uint32_t>(end_byte - start_byte);
      ctx.content_len_ = static_cast<uint32_t>(read_chars);
      str.assign_ptr(ctx.buff_, ctx.content_byte_len_);
      ctx.accessed_byte_len_ = ctx.content_byte_len_;
      ctx.accessed_len_ = ctx.content_len_;
      ++ctx.iter_count_;
      if (ctx.content_byte_len_ == 0) {
        state = common::TEXTSTRING_ITER_END;
      } else if (OB_ISNULL(ctx.read_cursor_ = new (std::nothrow) CompletedLobReadCursor())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        state = common::TEXTSTRING_ITER_NEXT;
      }
    }
    return ret;
  }

  int get_next_block_inner(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      bool has_lob_header,
      bool is_outrow,
      common::ObString &str,
      common::ObTextStringIterState &state) override {
    if (!use_remote(ctx.locator_)) {
      return local_ ? local_->get_next_block_inner(
          ctx, cs_type, has_lob_header, is_outrow, str, state) : OB_NOT_INIT;
    }
    if (!has_lob_header || !is_outrow || !ctx.read_cursor_) { return OB_INVALID_ARGUMENT; }
    free_lob_query_iter(ctx);
    str.reset();
    state = common::TEXTSTRING_ITER_END;
    return OB_SUCCESS;
  }

  int get_outrow_char_len(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      common::ObIAllocator *tmp_alloc,
      int64_t &char_length) override {
    if (!use_remote(ctx.locator_)) {
      return local_ ? local_->get_outrow_char_len(ctx, cs_type, tmp_alloc, char_length) : OB_NOT_INIT;
    }
    int ret = materialize(ctx, ctx.locator_);
    if (!ret) {
      char_length = common::ObCharset::strlen_char(cs_type, ctx.buff_, ctx.content_byte_len_);
    }
    return ret;
  }

  void free_lob_query_iter(common::ObLobTextIterCtx &ctx) override {
    if (dynamic_cast<CompletedLobReadCursor *>(ctx.read_cursor_)) {
      delete static_cast<CompletedLobReadCursor *>(ctx.read_cursor_);
      ctx.read_cursor_ = nullptr;
    } else if (local_) { local_->free_lob_query_iter(ctx); }
  }

private:
  bool use_remote(common::ObLobLocatorV2 &locator) const {
    // All persistent LOB storage belongs to the shared process. This includes
    // namespace-local __all_* tables; routing those locators to the worker's
    // local LobManager loses the only process that owns their tablets.
    return locator.has_lob_header() && locator.is_persist_lob();
  }

  int materialize(common::ObLobTextIterCtx &ctx, common::ObLobLocatorV2 &locator) {
    if (!ctx.alloc_ || !locator.has_lob_header()) { return OB_INVALID_ARGUMENT; }
    auto *session = THIS_WORKER.get_session();
    StorageSessionScope scope(session);
    if (scope.error()) { return scope.error(); }
    const int64_t timeout = ctx.timeout_ts_ > 0 ? ctx.timeout_ts_ : THIS_WORKER.get_timeout_ts();
    ObString data;
    int ret = read_in_process_lob(locator, timeout, *ctx.alloc_, data);
    if (!ret && data.length() > UINT32_MAX) { ret = OB_INVALID_ARGUMENT; }
    if (!ret) {
      ctx.buff_ = data.empty() ? nullptr : data.ptr();
      ctx.buff_byte_len_ = static_cast<uint32_t>(data.length());
      ctx.content_byte_len_ = static_cast<uint32_t>(data.length());
      ctx.total_byte_len_ = data.length();
    }
    return ret;
  }

  common::ObILobReadService *local_ = nullptr;
};

int compare_in_process_lobs(ObLobLocatorV2 &left, ObLobLocatorV2 &right,
                            int64_t timeout, ObTxDesc &tx, bool &equal);

class RemoteDmlService final : public ObIDmlService {
public:
  int lob_binary_equal(
      ObLobLocatorV2 &left,
      ObLobLocatorV2 &right,
      int64_t timeout,
      ObTxDesc &tx,
      bool &equal) override {
    auto *session = THIS_WORKER.get_session();
    StorageSessionScope scope(session && session->get_tx_desc() == &tx ? session : nullptr);
    if (scope.error()) { return scope.error(); }
    return compare_in_process_lobs(left, right, timeout, tx, equal);
  }

  int prepare_execution(
      const ObDmlWriteSpec &write_spec,
      const ObDmlTablePlan &table_plan,
      const transaction::ObTxReadSnapshot &snapshot,
      common::ObIAllocator &allocator,
      const ObWriteContext &write_context,
      const concurrent_control::ObWriteFlag &write_flag,
      ObDmlExecution &execution) override {
    const auto view = table_plan.get_data_table();
    const auto &columns = table_plan.get_col_descs();
    const uint64_t table_id = view.is_valid() ? view.get_table_id() : write_spec.table_id_;
    ObSEArray<uint64_t, 16> effective_columns;
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *logical_schema = nullptr;
    // Inner tables carry their schema even for namespace 1, matching the scan
    // path: the shared side must not re-resolve them through its own
    // SchemaService, whose lazy load needs inner SQL to this Worker.
    bool send_logical_schema = serves_namespace_schema()
        && !NamespaceForkKernelPrototype::is_encoded_id(table_id);
    StorageSpaceHandle storage_space =
        StorageSpaceHandle::namespace_space(serving_namespace());
    int ret = OB_SUCCESS;
    if (send_logical_schema) {
      ret = worker_local_table_schema(
          table_id, write_spec.schema_version_, schema_guard, logical_schema,
          storage_space);
      if (ret) {
        fprintf(stderr,
            "PROTOTYPE_V17_WORKER_WRITE_SCHEMA ns=%llu table=%llu version=%lld "
            "ret=%d found=%d actual_table=%llu actual_version=%lld database=%llu local=%d\n",
            (unsigned long long)serving_namespace(), (unsigned long long)table_id,
            (long long)write_spec.schema_version_, ret, logical_schema != nullptr,
            (unsigned long long)(logical_schema ? logical_schema->get_table_id() : OB_INVALID_ID),
            (long long)(logical_schema ? logical_schema->get_schema_version() : OB_INVALID_VERSION),
            (unsigned long long)(logical_schema ? logical_schema->get_database_id() : OB_INVALID_ID),
            storage_space.is_namespace());
      }
      send_logical_schema = !ret;
    } else if (columns.empty() && table_id != 0) {
      ObMultiVersionSchemaService *service = namespace_schema_service(serving_namespace());
      ret = service == nullptr ? OB_NOT_INIT
          : service->get_runtime_schema_guard(schema_guard, write_spec.schema_version_);
      if (!ret) { ret = schema_guard.get_table_schema(table_id, logical_schema); }
    }
    ObArray<const ObTableSchema *> materialization_schemas;
    if (!ret && send_logical_schema && storage_space.is_namespace()
        && serving_namespace() > 1) {
      ret = worker_materialization_schemas(
          *logical_schema, schema_guard, materialization_schemas);
    }
    if (ret) { return ret; }
    if (columns.empty() && logical_schema != nullptr) {
        for (int64_t i = 0; i < logical_schema->get_column_count(); ++i) {
          const auto *column = logical_schema->get_column_schema_by_idx(i);
          if (column != nullptr && !column->is_hidden()) { effective_columns.push_back(column->get_column_id()); }
        }
    }
    const int64_t column_count = columns.empty() ? effective_columns.count() : columns.count();
    if (!write_context.is_valid() || column_count == 0
        || column_count > OB_MAX_COLUMN_NUMBER || !write_spec.tz_info_) {
      fprintf(stderr,
          "PROTOTYPE_V22_WRITE_UNSUPPORTED table=%llu context=%d columns=%lld tz=%d\n",
          (unsigned long long)table_id, write_context.is_valid(),
          (long long)column_count, write_spec.tz_info_ != nullptr);
      return OB_NOT_SUPPORTED;
    }
    auto prepared = std::make_unique<RemoteExecution>();
    prepared->session = THIS_WORKER.get_session();
    StorageSessionScope scope(prepared->session);
    if (scope.error()) { return scope.error(); }
    auto &tx = *static_cast<ObTxDesc *>(write_context.native_handle());
    prepared->tx = &tx;
    prepared->txid = tx.get_tx_id().get_id(); prepared->deadline = write_spec.timeout_;
    for (int64_t i = 0; i < column_count; ++i) {
      const uint64_t column_id = columns.empty() ? effective_columns.at(i) : columns.at(i).col_id_;
      prepared->columns.push_back(column_id);
      if (!columns.empty()) { prepared->types.push_back(columns.at(i).col_type_); }
      else {
        if (logical_schema == nullptr || logical_schema->get_column_schema(column_id) == nullptr) { return OB_INVALID_ARGUMENT; }
        prepared->types.push_back(logical_schema->get_column_schema(column_id)->get_meta_type());
      }
    }
    execution.reset();
    if (!send_logical_schema || logical_schema == nullptr) { return OB_NOT_SUPPORTED; }
    const WritePrepareRequest request{storage_space, table_id, write_spec, *logical_schema,
        materialization_schemas, snapshot, write_flag, prepared->columns};
    ret = prepare_in_process_write(request, tx, prepared->handle);
    if (!ret) {
      if (!prepared->handle) { ret = OB_INVALID_ARGUMENT; }
      else { bind_execution(execution, prepared.release()); }
    }
    return ret; }
  int delete_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('D', tablet_id, tx_desc, execution, &column_ids, nullptr, row_iter, affected_rows); }
  int put_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('p', tablet_id, tx_desc, execution, &column_ids, nullptr, row_iter, affected_rows); }
  int insert_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('I', tablet_id, tx_desc, execution, &column_ids, nullptr, row_iter, affected_rows); }
  int write_rows(char operation, const ObTabletID &tablet_id, ObTxDesc &tx_desc,
      const ObDmlExecution &execution, const ObIArray<uint64_t> *column_ids,
      const ObIArray<uint64_t> *updated_column_ids, ObDatumRowIterator *row_iter, int64_t &affected_rows,
      int64_t lock_timeout = 0, ObRowLockMode lock_mode = ObRowLockMode::NONE,
      DuplicateRows *duplicates = nullptr, ObDuplicateReturnMode duplicate_mode = ObDuplicateReturnMode::ALL) {
    auto *state = static_cast<RemoteExecution *>(execution_state(execution));
    if (!state || !row_iter || (column_ids && column_ids->count() != static_cast<int64_t>(state->columns.size()))
        || state->txid != static_cast<uint64_t>(tx_desc.get_tx_id().get_id())) { return OB_INVALID_ARGUMENT; }
    StorageSessionScope scope(state->session);
    if (scope.error()) { return scope.error(); }
    const int64_t width = state->columns.size();
    for (int64_t i = 0; column_ids && i < width; ++i) {
      if (column_ids->at(i) != state->columns[i]) { return OB_INVALID_ARGUMENT; }
    }
    affected_rows = 0;
    int ret = OB_SUCCESS;
    bool end = false, duplicated = false;
    while (!ret && !end) {
      WriteBatch batch(operation, state->handle, tablet_id.id());
      batch.lock_timeout = lock_timeout;
      batch.lock_mode = lock_mode;
      batch.duplicate_mode = duplicate_mode;
      if (operation == 'L') { batch.bytes += 16; }
      if (duplicates) { batch.bytes += 8; }
      if (updated_column_ids) {
        if (updated_column_ids->count() > static_cast<int64_t>((MAX_SQL_MESSAGE - batch.bytes) / 8)) {
          return OB_SIZE_OVERFLOW;
        }
        batch.bytes += updated_column_ids->count() * 8;
        for (int64_t i = 0; i < updated_column_ids->count(); ++i) {
          batch.updated_columns.push_back(updated_column_ids->at(i));
        }
      }
      int64_t rows = 0;
      // Keep old/new pairs in the same batch: at most 32 logical writes.
      while (!ret && rows < (operation == 'U' ? 64 : 32)) {
        ObDatumRow *row = nullptr;
        ret = THIS_WORKER.check_status();
        if (!ret) { ret = row_iter->get_next_row(row); }
        if (ret == OB_ITER_END) { ret = OB_SUCCESS; end = true; break; }
        if (!ret && (!row || row->get_column_count() != width)) { ret = OB_INVALID_ARGUMENT; }
        for (int64_t i = 0; !ret && i < width; ++i) {
          ObObj value;
          ret = row->storage_datums_[i].to_obj_enhance(value, state->types[i]);
          if (!ret) {
            ret = batch.append(value, row->storage_datums_[i].has_lob_header());
          }
        }
        if (!ret) { ++rows; }
      }
      if (!ret && operation == 'U' && rows % 2) { ret = OB_INVALID_ARGUMENT; }
      if (!ret && rows) {
        batch.rows = rows;
        auto returned = std::make_unique<WriteResult>();
        int64_t affected = 0;
        ret = write_in_process_batch(tx_desc, batch, affected, *returned);
        if (duplicates && ret == OB_ERR_PRIMARY_KEY_DUPLICATE) { duplicated = true; ret = OB_SUCCESS; }
        if (!ret) {
          affected_rows += affected;
          if (duplicates) { duplicates->batches.push_back(std::move(returned)); }
        }
      }
    }
    return ret ? ret : duplicated ? OB_ERR_PRIMARY_KEY_DUPLICATE : OB_SUCCESS; }
  int insert_rows_fetch_duplicates(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      const common::ObIArray<uint64_t> &duplicated_column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      const ObDuplicateReturnMode return_mode,
      int64_t &affected_rows,
      blocksstable::ObDatumRowIterator *&duplicated_rows) override {
    if (duplicated_rows) { return OB_INVALID_ARGUMENT; }
    auto result = std::make_unique<DuplicateRows>(duplicated_column_ids.count());
    const int ret = write_rows('f', tablet_id, tx_desc, execution, &column_ids, &duplicated_column_ids,
                              row_iter, affected_rows, 0, ObRowLockMode::NONE, result.get(), return_mode);
    if (ret == OB_ERR_PRIMARY_KEY_DUPLICATE) { duplicated_rows = result.release(); }
    return ret;
  }
  void free_duplicate_rows_iterator(
      blocksstable::ObDatumRowIterator *iterator) override { delete iterator; }
  int update_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      const common::ObIArray<uint64_t> &updated_column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('U', tablet_id, tx_desc, execution, &column_ids, &updated_column_ids, row_iter, affected_rows); }
  int lock_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const int64_t abs_lock_timeout,
      const ObRowLockMode lock_mode,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('L', tablet_id, tx_desc, execution, nullptr, nullptr, row_iter, affected_rows, abs_lock_timeout, lock_mode); }
};
} } }
