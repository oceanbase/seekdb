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

#define USING_LOG_PREFIX RS

#include "ob_upgrade_utils.h"
#include "rootserver/ob_root_service.h"
#include "src/pl/ob_pl.h"
#include "share/stat/ob_dbms_stats_maintenance_window.h"
#include "share/ncomp_dll/ob_flush_ncomp_dll_task.h"
#include "rootserver/ob_tenant_ddl_service.h"
#include "share/ob_scheduled_manage_dynamic_partition.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share;
using namespace share::schema;
using namespace rootserver;
using namespace sql;

namespace share
{
const uint64_t ObUpgradeChecker::UPGRADE_PATH[] = {
  CALC_VERSION(1UL, 2UL, 0UL, 0UL),  // 1.2.0.0
};

int ObUpgradeChecker::get_data_version_by_cluster_version(
    const uint64_t cluster_version,
    uint64_t &data_version)
{
  int ret = OB_SUCCESS;
  switch (cluster_version) {
#define CONVERT_CLUSTER_VERSION_TO_DATA_VERSION(CLUSTER_VERSION, DATA_VERSION) \
    case CLUSTER_VERSION : { \
      data_version = DATA_VERSION; \
      break; \
    }
    CONVERT_CLUSTER_VERSION_TO_DATA_VERSION(CLUSTER_VERSION_1_0_0_0, DATA_VERSION_1_0_0_0)
    CONVERT_CLUSTER_VERSION_TO_DATA_VERSION(CLUSTER_VERSION_1_0_1_0, DATA_VERSION_1_0_1_0)
    CONVERT_CLUSTER_VERSION_TO_DATA_VERSION(CLUSTER_VERSION_1_1_0_0, DATA_VERSION_1_1_0_0)
    CONVERT_CLUSTER_VERSION_TO_DATA_VERSION(CLUSTER_VERSION_1_2_0_0, DATA_VERSION_1_2_0_0)

#undef CONVERT_CLUSTER_VERSION_TO_DATA_VERSION
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid cluster_version", KR(ret), KCV(cluster_version));
    }
  }
  return ret;
}

bool ObUpgradeChecker::check_data_version_exist(
     const uint64_t version)
{
  bool bret = false;
  STATIC_ASSERT(DATA_VERSION_NUM == ARRAYSIZEOF(UPGRADE_PATH), "data version count not match!!!");
  for (int64_t i = 0; !bret && i < ARRAYSIZEOF(UPGRADE_PATH); i++) {
    bret = (version == UPGRADE_PATH[i]);
  }
  return bret;
}

// TODO: should correspond to upgrade YML file.
//       For now, just consider the valid upgrade path for 4.x .
bool ObUpgradeChecker::check_data_version_valid_for_backup(const uint64_t data_version)
{
  return true;
}

//FIXME:(yanmu.ztl) cluster version should be discrete.
bool ObUpgradeChecker::check_cluster_version_exist(
     const uint64_t version)
{
  return version <= CLUSTER_CURRENT_VERSION;
}

#define FORMAT_STR(str) ObHexEscapeSqlStr(str.empty() ? ObString("") : str)

/* =========== upgrade processor ============= */
ObUpgradeProcesserSet::ObUpgradeProcesserSet()
  : inited_(false), allocator_(ObMemAttr(MTL_CTX() ? MTL_ID() : OB_SERVER_TENANT_ID,
                                         "UpgProcSet")),
    processor_list_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator_)),
    all_version_upgrade_processor_(NULL)
{
}

ObUpgradeProcesserSet::~ObUpgradeProcesserSet()
{
  for (int64_t i = 0; i < processor_list_.count(); i++) {
    if (OB_NOT_NULL(processor_list_.at(i))) {
      processor_list_.at(i)->~ObBaseUpgradeProcessor();
    }
  }
  if (OB_NOT_NULL(all_version_upgrade_processor_)) {
    all_version_upgrade_processor_->~ObBaseUpgradeProcessor();
  }
}

int ObUpgradeProcesserSet::init(
    ObBaseUpgradeProcessor::UpgradeMode mode,
    common::ObMySQLProxy &sql_proxy,
    common::ObOracleSqlProxy &oracle_sql_proxy,
    obrpc::ObSrvRpcProxy &rpc_proxy,
    obrpc::ObCommonRpcProxy &common_proxy,
    share::schema::ObMultiVersionSchemaService &schema_service,
    share::ObCheckStopProvider &check_server_provider)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
#define INIT_PROCESSOR_BY_NAME_AND_VERSION(PROCESSOR_NAME, VERSION, processor) \
    if (OB_SUCC(ret)) { \
      void *buf = NULL; \
      int64_t version = VERSION; \
      if (OB_ISNULL(buf = allocator_.alloc(sizeof(PROCESSOR_NAME)))) { \
        ret = OB_ALLOCATE_MEMORY_FAILED; \
        LOG_WARN("fail to alloc upgrade processor", KR(ret)); \
      } else if (OB_ISNULL(processor = new(buf)PROCESSOR_NAME)) { \
        ret = OB_NOT_INIT; \
        LOG_WARN("fail to new upgrade processor", KR(ret)); \
      } else if (OB_FAIL(processor->init(version, mode, sql_proxy, oracle_sql_proxy, rpc_proxy, common_proxy, \
                                         schema_service, check_server_provider))) { \
        LOG_WARN("fail to init processor", KR(ret), KDV(version)); \
      } \
      if (OB_FAIL(ret)) { \
        if (OB_NOT_NULL(processor)) { \
          processor->~ObBaseUpgradeProcessor(); \
          allocator_.free(buf); \
          processor = NULL; \
          buf = NULL; \
        } else if (OB_NOT_NULL(buf)) { \
          allocator_.free(buf); \
          buf = NULL; \
        } \
      } \
    }

#define INIT_PROCESSOR_BY_VERSION(MAJOR, MINOR, MAJOR_PATCH, MINOR_PATCH) \
    if (OB_SUCC(ret)) { \
      ObBaseUpgradeProcessor *processor = NULL; \
      int64_t data_version = cal_version(MAJOR, MINOR, MAJOR_PATCH, MINOR_PATCH); \
      INIT_PROCESSOR_BY_NAME_AND_VERSION(ObUpgradeFor##MAJOR##MINOR##MAJOR_PATCH##MINOR_PATCH##Processor,  \
          data_version, processor); \
      if (FAILEDx(processor_list_.push_back(processor))) { \
        LOG_WARN("fail to push back processor", KR(ret), KDV(data_version)); \
      } \
    }

    INIT_PROCESSOR_BY_NAME_AND_VERSION(ObUpgradeForAllVersionProcessor, DATA_CURRENT_VERSION,
        all_version_upgrade_processor_);

    // order by data version asc

#undef INIT_PROCESSOR_BY_NAME_AND_VERSION
#undef INIT_PROCESSOR_BY_VERSION
    inited_ = true;
  }
  return ret;
}

int ObUpgradeProcesserSet::check_inner_stat() const
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init yet", KR(ret));
  } else if (processor_list_.count() <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("processer_list cnt is less than 0", KR(ret));
  }
  return ret;
}

int ObUpgradeProcesserSet::get_processor_by_idx(
    const int64_t idx,
    ObBaseUpgradeProcessor *&processor) const
{
  int ret = OB_SUCCESS;
  int64_t cnt = processor_list_.count();
  processor = NULL;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check inner stat failed", KR(ret));
  } else if (idx >= cnt || idx < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid idx", KR(ret), K(idx), K(cnt));
  } else {
    processor = processor_list_.at(idx);
    if (OB_ISNULL(processor)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("processor is null", KR(ret), K(idx));
    } else {
      processor->set_tenant_id(OB_INVALID_ID); // reset
    }
  }
  return ret;
}

int ObUpgradeProcesserSet::get_processor_by_version(
    const int64_t version,
    ObBaseUpgradeProcessor *&processor) const
{
  int ret = OB_SUCCESS;
  int64_t idx = OB_INVALID_INDEX;
  if (OB_FAIL(get_processor_idx_by_version(version, idx))) {
    LOG_WARN("fail to get processor idx by version", KR(ret), KDV(version));
  } else if (OB_FAIL(get_processor_by_idx(idx, processor))) {
    LOG_WARN("fail to get processor by idx", KR(ret), KDV(version));
  }
  return ret;
}

int ObUpgradeProcesserSet::get_all_version_processor(ObBaseUpgradeProcessor *&processor) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check inner stat failed", KR(ret));
  } else if (OB_ISNULL(all_version_upgrade_processor_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("processor is NULL", KR(ret), KP(all_version_upgrade_processor_));
  } else {
    processor = all_version_upgrade_processor_;
    processor->set_tenant_id(OB_INVALID_ID); // reset
  }
  return ret;
}

// run upgrade processor by (start_version, end_version]
int ObUpgradeProcesserSet::get_processor_idx_by_range(
    const int64_t start_version,
    const int64_t end_version,
    int64_t &start_idx,
    int64_t &end_idx)
{
  int ret = OB_SUCCESS;
  start_idx = OB_INVALID_INDEX;
  end_idx = OB_INVALID_INDEX;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check inner stat failed", KR(ret));
  } else if (start_version <= 0 || end_version <= 0 || start_version > end_version) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid version", KR(ret), KDV(start_version), KDV(end_version));
  } else if (OB_FAIL(get_processor_idx_by_version(start_version, start_idx))) {
    LOG_WARN("fail to get processor idx by version", KR(ret), KDV(start_version));
  } else if (OB_FAIL(get_processor_idx_by_version(end_version, end_idx))) {
    LOG_WARN("fail to get processor idx by version", KR(ret), KDV(end_version));
  }
  return ret;
}

int ObUpgradeProcesserSet::get_processor_idx_by_version(
    const int64_t version,
    int64_t &idx) const
{
  int ret = OB_SUCCESS;
  idx = OB_INVALID_INDEX;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check inner stat failed", KR(ret));
  } else if (version <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid version", KR(ret), KDV(version));
  } else if (processor_list_.count() <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("processor_list cnt shoud greator than 0", KR(ret));
  } else {
    int64_t start = 0;
    int64_t end = processor_list_.count() - 1;
    while (OB_SUCC(ret) && start <= end) {
      int64_t mid = (start + end) / 2;
      if (OB_ISNULL(processor_list_.at(mid))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("processor is null", KR(ret), K(mid));
      } else if (processor_list_.at(mid)->get_version() == version) {
        idx = mid;
        break;
      } else if (processor_list_.at(mid)->get_version() > version) {
        end = mid - 1;
      } else {
        start = mid + 1;
      }
    }
    if (OB_SUCC(ret) && OB_INVALID_INDEX == idx) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("fail to find processor by version", KR(ret), KDV(version));
    }
  }
  return ret;
}

ObBaseUpgradeProcessor::ObBaseUpgradeProcessor()
  : inited_(false), data_version_(OB_INVALID_VERSION),
    tenant_id_(common::OB_INVALID_ID), mode_(UPGRADE_MODE_INVALID),
    sql_proxy_(NULL), rpc_proxy_(NULL), common_proxy_(NULL), schema_service_(NULL),
    check_stop_provider_(NULL)
{
}

// Standby cluster runs sys tenant's upgrade process only.
int ObBaseUpgradeProcessor::check_inner_stat() const
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init yet", KR(ret));
  } else if (data_version_ <= 0
             || tenant_id_ == OB_INVALID_ID
             || UPGRADE_MODE_INVALID == mode_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid processor status",
             KR(ret), K_(data_version), K_(tenant_id), K_(mode));
  } else if (OB_ISNULL(check_stop_provider_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("check_stop_provider is null", KR(ret));
  } else if (OB_FAIL(check_stop_provider_->check_stop())) {
    LOG_WARN("check stop", KR(ret));
  }
  return ret;
}

int ObBaseUpgradeProcessor::init(
    int64_t data_version,
    UpgradeMode mode,
    common::ObMySQLProxy &sql_proxy,
    common::ObOracleSqlProxy &oracle_sql_proxy,
    obrpc::ObSrvRpcProxy &rpc_proxy,
    obrpc::ObCommonRpcProxy &common_proxy,
    share::schema::ObMultiVersionSchemaService &schema_service,
    share::ObCheckStopProvider &check_server_provider)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    mode_ = mode;
    data_version_ = data_version;
    sql_proxy_ = &sql_proxy;
    oracle_sql_proxy_ = &oracle_sql_proxy;
    rpc_proxy_ = &rpc_proxy;
    common_proxy_ = &common_proxy;
    schema_service_ = &schema_service;
    check_stop_provider_ = &check_server_provider;
    inited_ = true;
  }
  return ret;
}

#undef FORMAT_STR

/* =========== special upgrade processor start ============= */
int ObUpgradeForAllVersionProcessor::post_upgrade() {
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(flush_ncomp_dll_job())) {
    LOG_WARN("fail to flush ncomp dll job", KR(ret));
  }
  return ret;
}

int ObUpgradeForAllVersionProcessor::flush_ncomp_dll_job()
{
  int ret = OB_SUCCESS;

  ObSchemaGetterGuard schema_guard;
  const ObSysVariableSchema *sys_variable_schema = NULL;
  bool is_primary_cluster = true;
  if (OB_ISNULL(sql_proxy_) || OB_ISNULL(schema_service_) || !is_valid_tenant_id(tenant_id_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", KR(ret), KP(sql_proxy_), KP(schema_service_), K(tenant_id_));
  } else if (!is_user_tenant(tenant_id_)) {
    LOG_INFO("not user tenant, ignore", K(tenant_id_));
  } else if (OB_FAIL(ObShareUtil::is_primary_cluster(is_primary_cluster))) {
    LOG_WARN("fail to check whether is primary cluster", KR(ret), K(is_primary_cluster));
  } else if (!is_primary_cluster) {
    LOG_INFO("not primary tenant, ignore", K(tenant_id_));
  } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(tenant_id_, schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", KR(ret), K(tenant_id_));
  } else if (OB_FAIL(schema_guard.get_sys_variable_schema(tenant_id_, sys_variable_schema))) {
    LOG_WARN("get sys variable schema failed", KR(ret), K(tenant_id_));
  } else if (OB_ISNULL(sys_variable_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sys variable schema is null", KR(ret));
  } else {
    ObMySQLTransaction trans;
    if (FAILEDx(trans.start(sql_proxy_, tenant_id_))) {
      LOG_WARN("failed to start trans", KR(ret), K(tenant_id_));
    } else if (OB_FAIL(ObFlushNcompDll::create_flush_ncomp_dll_job(
        *sys_variable_schema,
        tenant_id_,
        true/*is_enabled*/,
        trans))) { // insert ignore
      LOG_WARN("create flush ncomp dll job failed", KR(ret), K(tenant_id_));
    }
    if (trans.is_started()) {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(trans.end(OB_SUCC(ret)))) {
        ret = OB_SUCC(ret) ? tmp_ret : ret;
        SHARE_LOG(WARN, "failed to end trans", KR(ret), K(tmp_ret));
      }
    }
    LOG_INFO("post upgrade for create flush ncomp dll finished", KR(ret), K(tenant_id_));
  }

  return ret;
}


} // end share
} // end oceanbase
