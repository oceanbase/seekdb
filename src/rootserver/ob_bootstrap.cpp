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

#define USING_LOG_PREFIX BOOTSTRAP

#include "rootserver/ob_bootstrap.h"

#include "share/ob_global_stat_proxy.h"
#include "rootserver/ob_table_creator.h"
#include "rootserver/ob_root_service.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "share/object_storage/ob_device_connectivity.h"
#include "storage/shared_storage/ob_ss_format_util.h"
#endif
#include "share/inner_table/ob_load_inner_table_schema.h"
#include "rootserver/ob_load_inner_table_schema_executor.h"
#include "src/logservice/ob_server_log_block_mgr.h"
#include "share/inner_table/ob_dump_inner_table_schema.h"
#include "storage/tx_storage/ob_ls_service.h" // for ObLSService
#include "lib/hash/ob_hashset.h"
#include "rootserver/ob_partition_creator.h"
#include "share/ob_version.h" // for get_package_and_svn
#include "observer/ob_service.h" // for ObService
#include "share/ob_all_tenant_info.h" // ObAllTenantInfoProxy
#include "share/ob_server_struct.h" // GCTX

namespace oceanbase
{

using namespace common;
using namespace obrpc;
using namespace share;
using namespace share::schema;
using namespace storage;
namespace rootserver
{

ObBaseBootstrap::ObBaseBootstrap(ObSrvRpcProxy &rpc_proxy,
                                 common::ObServerConfig &config)
    : step_id_(0),
      rpc_proxy_(rpc_proxy),
      config_(config)
{
}


int ObBaseBootstrap::check_inner_stat() const
{
  int ret = OB_SUCCESS;
  return ret;
}

ObPreBootstrap::ObPreBootstrap(ObSrvRpcProxy &rpc_proxy,
                               common::ObServerConfig &config,
                               obrpc::ObCommonRpcProxy &rs_rpc_proxy)
  : ObBaseBootstrap(rpc_proxy, config),
    stop_(false),
    begin_ts_(0),
    common_proxy_(rs_rpc_proxy)
{
}

int ObPreBootstrap::prepare_bootstrap(ObAddr &master_rs)
{
  int ret = OB_SUCCESS;
  LOG_DBA_INFO_V2(OB_BOOTSTRAP_PREPARE_BEGIN, "bootstrap prepare begin.");
  begin_ts_ = ObTimeUtility::current_time();
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", KR(ret));
  } else if (OB_FAIL(check_server_is_empty())) {
    LOG_WARN("failed to check bootstrap stat", KR(ret));
  } else if (OB_FAIL(notify_sys_tenant_server_unit_resource())) {
    LOG_WARN("fail to notify sys tenant server unit resource", KR(ret));
  } else if (OB_FAIL(notify_sys_tenant_config_())) {
    LOG_WARN("fail to notify sys tenant config", KR(ret));
  } else if (OB_FAIL(create_ls())) {
    LOG_WARN("failed to create core table partition", KR(ret));
  } else {
    master_rs = GCTX.self_addr();
  }
  BOOTSTRAP_CHECK_SUCCESS();
  if (OB_FAIL(ret)) {
    LOG_DBA_ERROR_V2(OB_BOOTSTRAP_PREPARE_FAIL, ret, "bootstrap prepare fail. "
                     "you may find solutions in previous error logs or seek help from official technicians.");
  } else {
    LOG_DBA_INFO_V2(OB_BOOTSTRAP_PREPARE_SUCCESS, "bootstrap prepare success.", "server_id", GCTX.get_server_id());
  }
  return ret;
}

int ObPreBootstrap::notify_sys_tenant_server_unit_resource()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_TMP_FAIL(omt::ObTenantNodeBalancer::get_instance().notify_create_tenant())) {
    LOG_WARN("fail to handle notify unit resource", KR(tmp_ret));
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObPreBootstrap::notify_sys_tenant_config_()
{
  int ret = OB_SUCCESS;
  common::ObConfigPairs config;
  common::ObSEArray<common::ObConfigPairs, 1> init_configs;
  if (OB_FAIL(ObTenantDDLService::gen_tenant_init_config(
      OB_SYS_TENANT_ID, DATA_CURRENT_VERSION, config))) {
  } else if (OB_FAIL(init_configs.push_back(config))) {
    LOG_WARN("fail to push back config", KR(ret), K(config));
  } else if (OB_FAIL(ObTenantDDLService::notify_init_tenant_config(rpc_proxy_, init_configs))) {
    LOG_WARN("fail to notify init tenant config", KR(ret), K(init_configs));
  }

  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObPreBootstrap::create_ls()
{
  int ret = OB_SUCCESS;
  MTL_SWITCH(OB_SYS_TENANT_ID) {
    ObLSService *ls_svr = MTL(ObLSService*);
    if (OB_FAIL(check_inner_stat())) {
      LOG_WARN("fail to check inner stat", KR(ret));
    } else if (OB_ISNULL(ls_svr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("mtl ObLSService should not be null", K(ret));
    } else if (OB_FAIL(ls_svr->create_ls())) {
      LOG_WARN("failed create log stream", KR(ret));
    } else {
      LOG_INFO("succeed to create ls");
    }
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObPreBootstrap::check_server_is_empty()
{
  int ret = OB_SUCCESS;
  Bool is_server_empty;
  ObCheckServerEmptyArg arg;
  uint64_t server_id = OB_INIT_SERVER_ID;
  const ObCheckServerEmptyArg::Mode mode = ObCheckServerEmptyArg::BOOTSTRAP;
  const uint64_t data_version = DATA_CURRENT_VERSION;

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else if (OB_ISNULL(GCTX.ob_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.ob_service_));
  } else if (OB_FAIL(arg.init(mode, data_version, server_id))) {
    LOG_WARN("failed to init ObCheckServerEmptyArg", KR(ret), K(mode), K(data_version), K(server_id));
  } else if (OB_FAIL(GCTX.ob_service_->check_server_empty(arg, is_server_empty))) {
    LOG_WARN("failed to check if server is empty", KR(ret), K(arg));
  } else if (!is_server_empty) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot do bootstrap on not empty server", KR(ret));
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

bool ObBootstrap::TableIdCompare::operator() (const ObSimpleTableSchemaV2* left, const ObSimpleTableSchemaV2* right)
{
  bool bret = false;

  if (OB_ISNULL(left) || OB_ISNULL(right)) {
    ret_ = OB_INVALID_ARGUMENT;
    LOG_WARN_RET(ret_, "invalid argument", K_(ret), KP(left), KP(right));
  } else {
    bool left_is_sys_index = left->is_index_table() && is_sys_table(left->get_table_id());
    bool right_is_sys_index = right->is_index_table() && is_sys_table(right->get_table_id());
    uint64_t left_table_id = left->get_table_id();
    uint64_t left_data_table_id = left->get_data_table_id();
    uint64_t right_table_id = right->get_table_id();
    uint64_t right_data_table_id = right->get_data_table_id();
    if (!left_is_sys_index && !right_is_sys_index) {
      bret = left_table_id < right_table_id;
    } else if (left_is_sys_index && right_is_sys_index) {
      bret = left_data_table_id < right_data_table_id;
    } else if (left_is_sys_index) {
      if (left_data_table_id == right_table_id) {
        bret = true;
      } else {
        bret = left_data_table_id < right_table_id;
      }
    } else {
      if (left_table_id == right_data_table_id) {
        bret = false;
      } else {
        bret = left_table_id < right_data_table_id;
      }
    }
  }
  return bret;
}


ObBootstrap::ObBootstrap(
    ObSrvRpcProxy &rpc_proxy,
    ObDDLService &ddl_service,
    ObTenantDDLService &tenant_ddl_service,
    ObServerConfig &config,
    obrpc::ObCommonRpcProxy &rs_rpc_proxy)
  : ObBaseBootstrap(rpc_proxy, config),
    ddl_service_(ddl_service),
    tenant_ddl_service_(tenant_ddl_service),
    common_proxy_(rs_rpc_proxy),
    begin_ts_(0)
{
}

int ObBootstrap::execute_bootstrap()
{
  int ret = OB_SUCCESS;
  bool already_bootstrap = true;
  ObArenaAllocator arena_allocator("InnerTableSchem", OB_MALLOC_MIDDLE_BLOCK_SIZE);
  ObSArray<ObTableSchema> table_schemas;
  begin_ts_ = ObTimeUtility::current_time();
  ObPartitionCreator partition_creator;

  BOOTSTRAP_LOG(INFO, "start do execute_bootstrap");

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else if (OB_FAIL(check_is_already_bootstrap(already_bootstrap))) {
    LOG_WARN("failed to check_is_already_bootstrap", K(ret));
  } else if (already_bootstrap) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ob system is already bootstrap, cannot bootstrap again", K(ret));
  } else if (OB_FAIL(create_core_related_partitions())) {
    LOG_WARN("fail to create core related table partitions", KR(ret));
  } else if (OB_FAIL(set_in_bootstrap())) {
    LOG_WARN("failed to set in bootstrap", K(ret));
  } else if (OB_FAIL(init_global_stat())) {
    LOG_WARN("failed to init_global_stat", K(ret));
  } else if (OB_FAIL(construct_all_schema(table_schemas, arena_allocator))) {
    LOG_WARN("failed to construct all schema", K(ret));
  } else if (OB_FAIL(broadcast_sys_schema(table_schemas))) {
    LOG_WARN("broadcast_sys_schema failed", K(ret));
  } else if (OB_FAIL(partition_creator.init(this, &table_schemas))) {
    LOG_WARN("failed to init async partition creator", K(ret));
  } else if (OB_FAIL(partition_creator.submit_create_partitions_task())) {
    LOG_WARN("failed to submit partition creator task", K(ret));
  } else {
    LOG_INFO("succeed to submit partition creator task", K(ret));
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(load_all_schema(ddl_service_, table_schemas))) {
      LOG_WARN("load_all_schema failed", K(table_schemas), K(ret));
    } else {
      BOOTSTRAP_CHECK_SUCCESS_V2("load_all_schema");
    }
  }

  // wait partition creator to create sys table partitions
  if (OB_SUCC(ret)) {
    int task_ret = OB_SUCCESS;
    if (OB_FAIL(partition_creator.wait_task_completion(task_ret))) {
      LOG_WARN("failed to wait partition creator task completion", KR(ret));
    } else {
      LOG_INFO("succeed to wait partition creator task completion", KR(task_ret));
      ret = task_ret;
    }
  }

  if (FAILEDx(init_system_data())) {
    LOG_WARN("failed to init system data", KR(ret));
  } else {
    LOG_DBA_INFO_V2(OB_BOOTSTRAP_REFRESH_ALL_SCHEMA_BEGIN,
                    DBA_STEP_INC_INFO(bootstrap),
                    "bootstrap refresh all schema begin.");
  }
  if (FAILEDx(ddl_service_.refresh_schema(OB_SYS_TENANT_ID, true, nullptr, &table_schemas))) {
    LOG_WARN("failed to refresh_schema", K(ret));
    LOG_DBA_ERROR_V2(OB_BOOTSTRAP_REFRESH_ALL_SCHEMA_FAIL, ret,
                     DBA_STEP_INC_INFO(bootstrap),
                     "bootstrap refresh all schema fail. [suggestion] you can: "
                     "1. Search previous error logs that may indicate the cause of this failure. "
                     "2. Check if other nodes are accessible via ssh. "
                     "2. Check whether other nodes can establish connections through sql client. "
                     "3. Check the alert.log on others node to see if there are other error logs.");
  } else {
    LOG_DBA_INFO_V2(OB_BOOTSTRAP_REFRESH_ALL_SCHEMA_SUCCESS,
                    DBA_STEP_INC_INFO(bootstrap),
                    "bootstrap refresh all schema success.");
  }
  BOOTSTRAP_CHECK_SUCCESS_V2("refresh_schema");

#ifdef OB_BUILD_SHARED_STORAGE
  if (FAILEDx(write_shared_storage_args())) {
    LOG_WARN("failed to init write shared storage args", KR(ret));
  } else {}
#endif

  ROOTSERVICE_EVENT_ADD("bootstrap", "bootstrap_succeed");
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::load_all_schema(
    ObDDLService &ddl_service,
    common::ObIArray<share::schema::ObTableSchema> &table_schemas)
{
  int ret = OB_SUCCESS;
  const int64_t begin_time = ObTimeUtility::current_time();
  LOG_INFO("start load all schemas", "table count", table_schemas.count());
  LOG_DBA_INFO_V2(OB_BOOTSTRAP_CREATE_ALL_SCHEMA_BEGIN,
                  DBA_STEP_INC_INFO(bootstrap),
                  "bootstrap create all schema begin.");
  ObLoadInnerTableSchemaExecutor executor;
  if (OB_FAIL(executor.init(table_schemas, OB_SYS_TENANT_ID, get_cpu_count(), &rpc_proxy_))) {
    LOG_WARN("failed to init executor", KR(ret));
  } else if (OB_FAIL(executor.execute())) {
    LOG_WARN("failed to execute load all schema", KR(ret));
  } else if (OB_FAIL(ObLoadInnerTableSchemaExecutor::load_schema_version(
          OB_SYS_TENANT_ID, ddl_service.get_sql_proxy(),
          ObSchemaUtils::get_inner_table_core_schema_version(table_schemas),
          ObSchemaUtils::get_inner_table_sys_schema_version(table_schemas)))) {
      LOG_WARN("failed to load core schema version", KR(ret));
  }
  LOG_INFO("finish load all schemas", KR(ret), "cost", ObTimeUtility::current_time() - begin_time);
  if (OB_FAIL(ret)) {
    LOG_DBA_ERROR_V2(OB_BOOTSTRAP_CREATE_ALL_SCHEMA_FAIL, ret,
                     DBA_STEP_INC_INFO(bootstrap),
                     "bootstrap create all schema fail. "
                     "you may find solutions in previous error logs or seek help from official technicians.");
  } else {
    LOG_DBA_INFO_V2(OB_BOOTSTRAP_CREATE_ALL_SCHEMA_SUCCESS,
                    DBA_STEP_INC_INFO(bootstrap),
                    "bootstrap create all schema success.");
  }
  return ret;
}

int ObBootstrap::generate_table_schema_array_for_create_partition(
    const share::schema::ObTableSchema &tschema,
    common::ObIArray<share::schema::ObTableSchema> &table_schema_array)
{
  int ret = OB_SUCCESS;
  table_schema_array.reset();
  const uint64_t table_id = tschema.get_table_id();
  int64_t tschema_idx = table_schema_array.count();

  if (OB_FAIL(table_schema_array.push_back(tschema))) {
    LOG_WARN("fail to push back", KR(ret));
  } else if (OB_FAIL(ObSysTableChecker::append_sys_table_index_schemas(
             OB_SYS_TENANT_ID, table_id, table_schema_array))) {
    LOG_WARN("fail to append sys table index schemas", KR(ret), K(table_id));
  } else if (OB_FAIL(add_sys_table_lob_aux_table(table_id, table_schema_array))) {
    LOG_WARN("fail to add lob table to sys table", KR(ret), K(table_id));
  }
  return ret;
}

int ObBootstrap::prepare_create_partitions(
    ObTableCreator &creator,
    const share::schema::ObTableSchema &tschema,
    const common::hash::ObHashMap<uint64_t, const share::schema::ObTableSchema*> &table_id_to_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", KR(ret));
  } else if (tschema.has_partition()) {
    common::ObArray<const share::schema::ObTableSchema*> table_schema_ptrs;
    common::ObArray<share::ObLSID> ls_id_array;
    common::ObArray<bool> need_create_empty_majors;
    ObArray<uint64_t> index_tids;
    uint64_t lob_meta_table_id = 0;
    uint64_t lob_piece_table_id = 0;
    const uint64_t data_table_id = tschema.get_table_id();

    if (ObSysTableChecker::is_sys_table_has_index(data_table_id)
        && OB_FAIL(ObSysTableChecker::get_sys_table_index_tids(data_table_id, index_tids))) {
      LOG_WARN("fail to get sys table index tids", KR(ret), K(data_table_id));
    } else if (!get_sys_table_lob_aux_table_id(data_table_id, lob_meta_table_id, lob_piece_table_id)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get sys table lob aux table id", KR(ret), K(data_table_id));
    } else if (OB_FAIL(table_schema_ptrs.push_back(&tschema))) {
      LOG_WARN("fail to push back tschema", KR(ret), K(data_table_id));
    } else {
      for (int64_t j = 0; OB_SUCC(ret) && j < index_tids.count(); ++j) {
        const share::schema::ObTableSchema* schema_ptr = nullptr;
        if (OB_FAIL(table_id_to_schema.get_refactored(index_tids.at(j), schema_ptr))) {
          LOG_WARN("fail to get index table schema", KR(ret), K(index_tids.at(j)));
        } else if (OB_FAIL(table_schema_ptrs.push_back(schema_ptr))) {
          LOG_WARN("fail to push back index table schema", KR(ret), K(index_tids.at(j)));
        }
      }
    }

    if (OB_SUCC(ret)) {
      const share::schema::ObTableSchema* lob_meta_schema_ptr = nullptr;
      const share::schema::ObTableSchema* lob_piece_schema_ptr = nullptr;
      if (OB_FAIL(table_id_to_schema.get_refactored(lob_meta_table_id, lob_meta_schema_ptr))) {
        LOG_WARN("fail to get lob meta table schema", KR(ret), K(lob_meta_table_id));
      } else if (OB_FAIL(table_schema_ptrs.push_back(lob_meta_schema_ptr))) {
        LOG_WARN("fail to push back lob meta table schema", KR(ret), K(lob_meta_table_id));
      } else if (OB_FAIL(table_id_to_schema.get_refactored(lob_piece_table_id, lob_piece_schema_ptr))) {
        LOG_WARN("fail to get lob piece table schema", KR(ret), K(lob_piece_table_id));
      } else if (OB_FAIL(table_schema_ptrs.push_back(lob_piece_schema_ptr))) {
        LOG_WARN("fail to push back lob piece table schema", KR(ret), K(lob_piece_table_id));
      } else {
        for (int64_t j = 0; OB_SUCC(ret) && j < table_schema_ptrs.count(); ++j) {
          if (OB_FAIL(need_create_empty_majors.push_back(true))) {
            LOG_WARN("fail to push back need create empty major", KR(ret));
          }
        }

        for (int64_t j = 0; OB_SUCC(ret) && j < tschema.get_all_part_num(); ++j) {
          if (OB_FAIL(ls_id_array.push_back(share::ObLSID(SYS_LS)))) {
            LOG_WARN("fail to push back ls id", KR(ret));
          }
        }
      }
    }

    if (OB_SUCC(ret) && OB_FAIL(creator.add_create_tablets_of_tables_arg(
        table_schema_ptrs,
        ls_id_array,
        DATA_CURRENT_VERSION,
        need_create_empty_majors/*need_create_empty_major_sstable*/))) {
      LOG_WARN("fail to add create tablet arg", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("succeed prepare create table partition with table schemas",
             "table_id", tschema.get_table_id(),
             "table_name", tschema.get_table_name());
  }

  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::prepare_create_partition(
    ObTableCreator &creator,
    const share::schema_create_func func)
{
  int ret = OB_SUCCESS;
  ObArray<ObUnit> units;
  ObTableSchema tschema;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", KR(ret));
  } else if (NULL == func) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("func is null", KR(ret));
  } else if (OB_FAIL(func(tschema))) {
    LOG_WARN("failed to create table schema", KR(ret));
  } else if (tschema.has_partition()) {
    common::ObArray<share::schema::ObTableSchema> table_schema_array;
    common::ObArray<const share::schema::ObTableSchema*> table_schema_ptrs;
    common::ObArray<share::ObLSID> ls_id_array;
    common::ObArray<bool> need_create_empty_majors;
    if (OB_FAIL(generate_table_schema_array_for_create_partition(tschema, table_schema_array))) {
      LOG_WARN("fail to generate table schema array", KR(ret));
    } else if (OB_UNLIKELY(table_schema_array.count() < 1)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("generate table schema count is unexpected", KR(ret));
    } else if (OB_FAIL(table_schema_ptrs.reserve(table_schema_array.count()))
      || OB_FAIL(need_create_empty_majors.reserve(table_schema_array.count()))) {
      LOG_WARN("Fail to reserve rowkey column array", KR(ret));
    } else {
      for (int i = 0; i < table_schema_array.count() && OB_SUCC(ret); ++i) {
        if (OB_FAIL(table_schema_ptrs.push_back(&table_schema_array.at(i)))
          || OB_FAIL(need_create_empty_majors.push_back(true))) {
          LOG_WARN("fail to push back", KR(ret), K(table_schema_array));
        }
      }

      for (int i = 0; i < tschema.get_all_part_num() && OB_SUCC(ret); ++i) {
        if (OB_FAIL(ls_id_array.push_back(share::ObLSID(SYS_LS)))) {
          LOG_WARN("fail to push back", KR(ret));
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(creator.add_create_tablets_of_tables_arg(
            table_schema_ptrs,
            ls_id_array,
            DATA_CURRENT_VERSION,
            need_create_empty_majors/*need_create_empty_major_sstable*/))) {
      LOG_WARN("fail to add create tablet arg", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("succeed prepare create table partition",
             "table_id", tschema.get_table_id(),
             "table_name", tschema.get_table_name());
  }

  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::create_core_related_partitions()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else {
    ObMySQLTransaction trans;
    ObMySQLProxy &sql_proxy = ddl_service_.get_sql_proxy();
    ObTableCreator table_creator(OB_SYS_TENANT_ID,
                                 SCN::base_scn(),
                                 trans);
    if (OB_FAIL(trans.start(&sql_proxy, OB_SYS_TENANT_ID))) {
      LOG_WARN("fail to start trans", KR(ret));
    } else if (OB_FAIL(table_creator.init(false/*need_tablet_cnt_check*/))) {
      LOG_WARN("fail to init tablet creator", KR(ret));
    } else {
      // create core related table partitions
      for (int64_t i = 0; OB_SUCC(ret) && NULL != core_related_table_schema_creators[i]; ++i) {
        if (OB_FAIL(prepare_create_partition(
            table_creator, core_related_table_schema_creators[i]))) {
          LOG_WARN("prepare create partition fail", K(ret));
        }
      }
      // execute creating tablet
      if (OB_SUCC(ret)) {
        if (OB_FAIL(table_creator.execute())) {
          LOG_WARN("execute create partition failed", K(ret));
        }
      }
    }

    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      bool commit = OB_SUCC(ret);
      if (OB_SUCCESS != (temp_ret = trans.end(commit))) {
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
        LOG_WARN("trans end failed", K(commit), K(temp_ret));
      }
    }
  }

  LOG_INFO("finish creating core related table partitions", K(ret));
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::get_core_related_table_ids(common::hash::ObHashSet<uint64_t> &table_id_set)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else {
    int64_t table_count = ARRAYSIZEOF(core_related_table_schema_creators) - 1;
    if (table_count > 0) {
      if (OB_FAIL(table_id_set.create(hash::cal_next_prime(table_count)))) {
        LOG_WARN("failed to create core related table id set", K(ret));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < table_count; ++i) {
          ObTableSchema table_schema;
          if (OB_FAIL(core_related_table_schema_creators[i](table_schema))) {
            LOG_WARN("failed to create table schema", K(ret), K(i));
          } else if (OB_FAIL(table_id_set.set_refactored(table_schema.get_table_id()))) {
            LOG_WARN("failed to set core related table id", K(ret), K(table_schema.get_table_id()));
          }
        }
      }
    }
  }
  return ret;
}

int ObBootstrap::create_sys_table_partitions(const common::ObIArray<share::schema::ObTableSchema> &table_schemas)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else {
    ObMySQLTransaction trans;
    ObMySQLProxy &sql_proxy = ddl_service_.get_sql_proxy();
    ObTableCreator table_creator(OB_SYS_TENANT_ID,
                                 SCN::base_scn(),
                                 trans);
    if (OB_FAIL(trans.start(&sql_proxy, OB_SYS_TENANT_ID))) {
      LOG_WARN("fail to start trans", KR(ret));
    } else if (OB_FAIL(table_creator.init(false/*need_tablet_cnt_check*/))) {
      LOG_WARN("fail to init tablet creator", KR(ret));
    } else {
      // construct hashset table_id -> table_schema
      common::hash::ObHashMap<uint64_t, const share::schema::ObTableSchema*> table_id_to_schema;
      if (OB_SUCC(ret)) {
        if (OB_FAIL(table_id_to_schema.create(hash::cal_next_prime(table_schemas.count()), "TidToSchemaMap"))) {
          LOG_WARN("fail to create table id to schema map", KR(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); ++i) {
            if (OB_FAIL(table_id_to_schema.set_refactored(table_schemas.at(i).get_table_id(), &table_schemas.at(i)))) {
              LOG_WARN("fail to set refactored", KR(ret));
            }
          }
        }
      }

      if (OB_SUCC(ret)) {
        common::hash::ObHashSet<uint64_t> core_related_table_id_set;
        if (OB_FAIL(get_core_related_table_ids(core_related_table_id_set))) {
          LOG_WARN("failed to get core related table ids", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); ++i) {
            const share::schema::ObTableSchema &table_schema = table_schemas.at(i);
            const uint64_t table_id = table_schema.get_table_id();
            if (is_system_table(table_id)) {
              int exist_ret = core_related_table_id_set.exist_refactored(table_id);
              if (OB_HASH_EXIST == exist_ret) {
                LOG_INFO("skip creating partition for core related table",
                  "table_name", table_schema.get_table_name(),
                  "table_id", table_id);
              } else if (OB_HASH_NOT_EXIST == exist_ret) {
                if (OB_FAIL(prepare_create_partitions(table_creator, table_schema, table_id_to_schema))) {
                  LOG_WARN("prepare create partition with table schemas fail", KR(ret));
                }
              } else {
                ret = exist_ret;
                LOG_WARN("failed to check if table exists in core related table set",
                  K(ret), K(table_id), K(table_schema.get_table_name()));
              }
            } else {
              LOG_INFO("skip creating partition for non-system table",
                "table_name", table_schema.get_table_name(),
                "table_id", table_id);
            }
          }
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(table_creator.execute())) {
          LOG_WARN("execute create partition failed", K(ret));
        }
      }
    }
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      bool commit = OB_SUCC(ret);
      if (OB_SUCCESS != (temp_ret = trans.end(commit))) {
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
        LOG_WARN("trans end failed", K(commit), K(temp_ret));
      }
    }
  }

  LOG_INFO("finish creating sys table partitions", K(ret));
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::add_sys_table_lob_aux_table(
    uint64_t data_table_id,
    ObIArray<ObTableSchema> &table_schemas)
{
  int ret = OB_SUCCESS;
  if (is_system_table(data_table_id)) {
    HEAP_VARS_2((ObTableSchema, lob_meta_schema), (ObTableSchema, lob_piece_schema)) {
      if (OB_ALL_CORE_TABLE_TID == data_table_id) {
        // do nothing
      } else if (OB_FAIL(get_sys_table_lob_aux_schema(data_table_id, lob_meta_schema, lob_piece_schema))) {
        LOG_WARN("fail to get sys table lob aux schema", KR(ret), K(data_table_id));
      } else if (OB_FAIL(table_schemas.push_back(lob_meta_schema))) {
        LOG_WARN("fail to push lob meta into schemas", KR(ret), K(data_table_id));
      } else if (OB_FAIL(table_schemas.push_back(lob_piece_schema))) {
        LOG_WARN("fail to push lob piece into schemas", KR(ret), K(data_table_id));
      }
    }
  }
  return ret;
}

int ObBootstrap::construct_all_schema(ObSArray<ObTableSchema> &table_schemas, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", KR(ret));
  } else if (OB_FAIL(ObSchemaUtils::construct_inner_table_schemas(OB_SYS_TENANT_ID,
      table_schemas, allocator, true))) {
    LOG_WARN("failed to construct inner table schemas", KR(ret));
  } else if (OB_FAIL(ObSchemaUtils::generate_hard_code_schema_version(table_schemas))) {
    LOG_WARN("failed to generate hard code schema version", KR(ret));
  }

  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::broadcast_sys_schema(const ObSArray<ObTableSchema> &table_schemas)
{
  int ret = OB_SUCCESS;
  ObMultiVersionSchemaService &schema_service = ddl_service_.get_schema_service();
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("failed to check_inner_stat", KR(ret));
  } else if (OB_FAIL(schema_service.broadcast_tenant_schema(OB_SYS_TENANT_ID, table_schemas))) {
    LOG_WARN("failed to broadcast tenant schema", KR(ret));
  } else {
    LOG_INFO("successfully broadcast sys schema", K(OB_SYS_TENANT_ID));
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::batch_create_schema(ObDDLService &ddl_service,
                                     ObIArray<ObTableSchema> &table_schemas,
                                     const int64_t begin, const int64_t end)
{
  int ret = OB_SUCCESS;
  const int64_t begin_time = ObTimeUtility::current_time();
  ObDDLSQLTransaction trans(&(ddl_service.get_schema_service()), true, true, false, false);
  if (begin < 0 || begin >= end || end > table_schemas.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(begin), K(end),
        "table count", table_schemas.count());
  } else {
    ObDDLOperator ddl_operator(ddl_service.get_schema_service(),
        ddl_service.get_sql_proxy());
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(trans.start(&ddl_service.get_sql_proxy(),
                            OB_SYS_TENANT_ID,
                            refreshed_schema_version))) {
      LOG_WARN("start transaction failed", KR(ret));
    } else {
      bool is_truncate_table = false;
      for (int64_t i = begin; OB_SUCC(ret) && i < end; ++i) {
        ObTableSchema &table = table_schemas.at(i);
        const ObString *ddl_stmt = NULL;
        bool need_sync_schema_version = !(ObSysTableChecker::is_sys_table_index_tid(table.get_table_id()) ||
                                          is_sys_lob_table(table.get_table_id()));
        int64_t start_time = ObTimeUtility::current_time();
        if (FAILEDx(ddl_operator.create_table(table, trans, ddl_stmt,
                                              need_sync_schema_version,
                                              is_truncate_table))) {
          LOG_WARN("add table schema failed", K(ret),
              "table_id", table.get_table_id(),
              "table_name", table.get_table_name());
        } else {
          int64_t end_time = ObTimeUtility::current_time();
          LOG_INFO("add table schema succeed", K(i),
              "table_id", table.get_table_id(),
              "table_name", table.get_table_name(), "core_table", is_core_table(table.get_table_id()), "cost", end_time-start_time);
        }
      }
    }
  }

  const int64_t begin_commit_time = ObTimeUtility::current_time();
  if (trans.is_started()) {
    const bool is_commit = (OB_SUCCESS == ret);
    int tmp_ret = trans.end(is_commit);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("end trans failed", K(tmp_ret), K(is_commit));
      ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
    }
  }
  const int64_t now = ObTimeUtility::current_time();
  LOG_INFO("batch create schema finish", K(ret), "table count", end - begin,
      "total_time_used", now - begin_time,
      "end_transaction_time_used", now - begin_commit_time);
  //BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::construct_schema(
    const share::schema_create_func func, ObTableSchema &tschema)
{
  int ret = OB_SUCCESS;
  BOOTSTRAP_CHECK_SUCCESS_V2("before construct schema");
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else if (NULL == func) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("func is null", K(ret));
  } else if (OB_FAIL(func(tschema))) {
    LOG_WARN("failed to create table schema", K(ret));
  } else {} // no more to do
  return ret;
}

int ObBootstrap::check_is_already_bootstrap(bool &is_bootstrap)
{
  int ret = OB_SUCCESS;
  int64_t schema_version = OB_INVALID_VERSION;
  is_bootstrap = true;
  ObMultiVersionSchemaService &schema_service = ddl_service_.get_schema_service();
  ObSchemaGetterGuard guard;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else if (OB_FAIL(schema_service.get_tenant_schema_guard(OB_SYS_TENANT_ID, guard))) {
    LOG_WARN("get_schema_manager failed", K(ret));
  } else if (OB_FAIL(guard.get_schema_version(OB_SYS_TENANT_ID, schema_version))) {
    LOG_WARN("fail to get tenant schema version", K(ret));
  } else if (OB_CORE_SCHEMA_VERSION == schema_version) {
    is_bootstrap = false;
  } else {
    // don't need to set ret
    //LOG_WARN("observer is bootstrap already", "schema_table_count", guard->get_table_count());
    LOG_WARN("observer is already bootstrap");
    //const bool is_verbose = false;
    //guard->print_info(is_verbose);
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::init_global_stat()
{
  int ret = OB_SUCCESS;
  ObMySQLProxy &sql_proxy = ddl_service_.get_sql_proxy();
  ObMySQLTransaction trans;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", KR(ret));
  } else {
    const int64_t baseline_schema_version = OB_INVALID_VERSION; // OB_INVALID_VERSION == -1
    const int64_t rootservice_epoch = 0;
    const SCN snapshot_gc_scn = SCN::min_scn();
    const int64_t snapshot_gc_timestamp = 0;
    const int64_t ddl_epoch = 0;
    ObGlobalStatProxy global_stat_proxy(trans, OB_SYS_TENANT_ID);
    ObSchemaStatusProxy *schema_status_proxy = GCTX.schema_status_proxy_;
    if (OB_FAIL(trans.start(&sql_proxy, OB_SYS_TENANT_ID))) {
      LOG_WARN("trans start failed", KR(ret));
    } else if (OB_ISNULL(schema_status_proxy)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema_status_proxy is null", KR(ret));
    } else if (OB_FAIL(global_stat_proxy.set_init_value(
               OB_CORE_SCHEMA_VERSION, OB_CORE_SCHEMA_VERSION, baseline_schema_version,
               rootservice_epoch, snapshot_gc_scn, snapshot_gc_timestamp, ddl_epoch,
               DATA_CURRENT_VERSION, DATA_CURRENT_VERSION, DATA_CURRENT_VERSION))) {
      LOG_WARN("set_init_value failed", KR(ret), "schema_version", OB_CORE_SCHEMA_VERSION,
               K(baseline_schema_version), K(rootservice_epoch), K(ddl_epoch), "data_version", DATA_CURRENT_VERSION);
    }

    int temp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCCESS == ret))) {
      LOG_WARN("trans end failed", "commit", OB_SUCCESS == ret, KR(temp_ret));
      ret = (OB_SUCCESS == ret) ? temp_ret : ret;
    }

    // Initializes a new state of refresh schema
    if (OB_SUCC(ret)) {
      ObRefreshSchemaStatus tenant_status(OB_SYS_TENANT_ID, OB_INVALID_TIMESTAMP,
          OB_INVALID_VERSION);
      if (OB_FAIL(schema_status_proxy->set_tenant_schema_status(tenant_status))) {
        LOG_WARN("fail to init create partition status", KR(ret), K(tenant_status));
      } else {}
    }
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::create_sys_tenant()
{
  // insert zero system stat value for create system tenant.
  int ret= OB_SUCCESS;
  ObTenantSchema tenant;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else {
    obrpc::ObCreateTenantArg arg;
    arg.name_case_mode_ = OB_LOWERCASE_AND_INSENSITIVE;
    tenant.set_tenant_id(OB_SYS_TENANT_ID);
    tenant.set_schema_version(OB_CORE_SCHEMA_VERSION);

    share::schema::ObSchemaGetterGuard dummy_schema_guard;
    if (OB_FAIL(tenant.set_tenant_name(OB_SYS_TENANT_NAME))) {
      LOG_WARN("set_tenant_name failed", "tenant_name", OB_SYS_TENANT_NAME, K(ret));
    } else if (OB_FAIL(tenant.set_comment("system tenant"))) {
      LOG_WARN("set_comment failed", "comment", "system tenant", K(ret));
    } else if (OB_FAIL(set_replica_options(tenant))) {
      LOG_WARN("failed to set replica options", KR(ret));
    } else if (OB_FAIL(tenant_ddl_service_.create_sys_tenant(arg, tenant))) {
      LOG_WARN("create tenant failed", K(ret), K(tenant));
    } else {} // no more to do
  }

  LOG_INFO("create tenant", K(ret), K(tenant));
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::init_system_data()
{
  int ret = OB_SUCCESS;
  LOG_DBA_INFO_V2(OB_BOOTSTRAP_CREATE_SYS_TENANT_BEGIN,
                  DBA_STEP_INC_INFO(bootstrap),
                  "bootstrap create sys tenant begin.");
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", KR(ret));
  } else if (OB_FAIL(create_sys_tenant())) {
    LOG_WARN("create system tenant failed", KR(ret));
  }
  if (OB_FAIL(ret)) {
    LOG_DBA_ERROR_V2(OB_BOOTSTRAP_CREATE_SYS_TENANT_FAIL, ret,
                     DBA_STEP_INC_INFO(bootstrap),
                     "bootstrap create sys tenant fail. maybe some resources are not enough. "
                     "you may find solutions in previous error logs or seek help from official technicians.");
  } else {
    LOG_DBA_INFO_V2(OB_BOOTSTRAP_CREATE_SYS_TENANT_SUCCESS,
                    DBA_STEP_INC_INFO(bootstrap),
                    "bootstrap create sys tenant success.");
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::init_sys_unit_config(share::ObUnitConfig &unit_config)
{
  int ret = OB_SUCCESS;
  const bool is_hidden_sys = false;

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else if (OB_FAIL(unit_config.gen_sys_tenant_unit_config(is_hidden_sys, GCTX.log_block_mgr_->get_log_disk_size()))) {
    LOG_WARN("gen sys tenant unit config fail", KR(ret), K(is_hidden_sys));
  } else {
    LOG_INFO("init sys tenant unit config succ", K(unit_config));
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

template<typename SCHEMA>
int ObBootstrap::set_replica_options(SCHEMA &schema)
{
  int ret = OB_SUCCESS;
  BOOTSTRAP_CHECK_SUCCESS_V2("before set replica options");
  ObArray<ObString> zone_str_list;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check_inner_stat failed", K(ret));
  } else if (!schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(schema), K(ret));
  } else if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else if (OB_FAIL(zone_str_list.push_back(ObString::make_string(GCTX.config_->zone.str())))) {
    LOG_WARN("push_back failed", K(ret));
  } else if (zone_str_list.count() <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("zone_str_list is empty", K(zone_str_list), K(ret));
  } else if (OB_FAIL(schema.set_zone_list(zone_str_list))) {
    LOG_WARN("set_zone_list failed", K(zone_str_list), K(ret));
  }
  BOOTSTRAP_CHECK_SUCCESS();
  return ret;
}

int ObBootstrap::set_in_bootstrap()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("failed to check inner stat error", K(ret));
  } else {
    ObMultiVersionSchemaService &multi_schema_service = ddl_service_.get_schema_service();
    ObSchemaService *schema_service = multi_schema_service.get_schema_service();
    if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema_service is null", K(ret));
    } else {
      schema_service->set_cluster_schema_status(
          ObClusterSchemaStatus::BOOTSTRAP_STATUS);
    }
  }
  return ret;
}


} // end namespace rootserver
} // end namespace oceanbase
