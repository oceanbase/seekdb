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

#ifndef OCEANBASE_ROOTSERVER_OB_BOOTSTRAP_H_
#define OCEANBASE_ROOTSERVER_OB_BOOTSTRAP_H_

#include <typeinfo>
#include "share/ob_define.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "rootserver/ob_ddl_service.h"

namespace oceanbase
{
namespace common
{
class ObDataBuffer;
class ObMySQLProxy;
class ObAddr;
class ObServerConfig;
class ObISQLClient;
}

namespace obrpc
{
class ObSrvRpcProxy;
}

namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
class ObTableSchema;
class ObTenantSchema;
class ObSimpleTableSchemaV2;
}
}

namespace rootserver
{
class ObRsGtsManager;
struct ObSysStat;
class ObTableCreator;

class ObBaseBootstrap
{
public:
  explicit ObBaseBootstrap(obrpc::ObSrvRpcProxy &rpc_proxy,
                           common::ObServerConfig &config);
  virtual ~ObBaseBootstrap() {}


  inline obrpc::ObSrvRpcProxy &get_rpc_proxy() const { return rpc_proxy_; }
protected:
  virtual int check_inner_stat() const;
public:
  int64_t step_id_;
protected:
  obrpc::ObSrvRpcProxy &rpc_proxy_;
  common::ObServerConfig &config_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBaseBootstrap);
};

class ObPreBootstrap : public ObBaseBootstrap
{
public:
  explicit ObPreBootstrap(obrpc::ObSrvRpcProxy &rpc_proxy,
                          common::ObServerConfig &config,
                          obrpc::ObCommonRpcProxy &rs_rpc_proxy);
  virtual ~ObPreBootstrap() {}
  virtual int prepare_bootstrap(common::ObAddr &master_rs);

private:
  // wait leader elect time + root service start time
  static const int64_t WAIT_ELECT_SYS_LEADER_TIMEOUT_US = 30 * 1000 * 1000;
  static const int64_t NOTIFY_RESOURCE_RPC_TIMEOUT = 9 * 1000 * 1000; // 9 second

  virtual int check_server_is_empty();
  virtual int notify_sys_tenant_server_unit_resource();
  virtual int create_ls();

  int notify_sys_tenant_config_();
private:
  volatile bool stop_;
  int64_t begin_ts_;
  obrpc::ObCommonRpcProxy &common_proxy_;
  DISALLOW_COPY_AND_ASSIGN(ObPreBootstrap);
};

class ObBootstrap : public ObBaseBootstrap
{
public:
  class TableIdCompare
  {
  public:
    TableIdCompare() : ret_(common::OB_SUCCESS) {}
    ~TableIdCompare() {}
    bool operator() (const share::schema::ObSimpleTableSchemaV2* left,
                     const share::schema::ObSimpleTableSchemaV2* right);
    int get_ret() const { return ret_; }
private:
    int ret_;
  };
  explicit ObBootstrap(obrpc::ObSrvRpcProxy &rpc_proxy,
                       ObDDLService &ddl_service,
                       ObTenantDDLService &tenant_ddl_service,
                       common::ObServerConfig &config,
                       obrpc::ObCommonRpcProxy &rs_rpc_proxy);

  virtual ~ObBootstrap() {}
  virtual int execute_bootstrap();
  int load_all_schema(
      ObDDLService &ddl_service,
      common::ObIArray<share::schema::ObTableSchema> &table_schemas);
  int construct_all_schema(
      common::ObSArray<share::schema::ObTableSchema> &table_schemas,
      ObIAllocator &allocator);
  virtual int create_sys_table_partitions(const common::ObIArray<share::schema::ObTableSchema> &table_schemas);
private:
  static const int64_t HEAT_BEAT_INTERVAL_US = 2 * 1000 * 1000; //2s
  static const int64_t BATCH_INSERT_SCHEMA_CNT = 128;
  virtual int generate_table_schema_array_for_create_partition(
      const share::schema::ObTableSchema &tschema,
      common::ObIArray<share::schema::ObTableSchema> &table_schema_array);
  virtual int prepare_create_partition(
      ObTableCreator &creator,
      const share::schema_create_func func);
  virtual int prepare_create_partitions(
      ObTableCreator &creator,
      const share::schema::ObTableSchema &tschema,
      const common::hash::ObHashMap<uint64_t, const share::schema::ObTableSchema*> &table_id_to_schema);
  virtual int create_core_related_partitions();
  virtual int get_core_related_table_ids(common::hash::ObHashSet<uint64_t> &table_id_set);
  virtual int construct_schema(
      const share::schema_create_func func,
      share::schema::ObTableSchema &tschema);
  virtual int broadcast_sys_schema(const ObSArray<ObTableSchema> &table_schemas);
  static int batch_create_schema(
      ObDDLService &ddl_service,
      common::ObIArray<share::schema::ObTableSchema> &table_schemas,
      const int64_t begin, const int64_t end);
  virtual int check_is_already_bootstrap(bool &is_bootstrap);
  virtual int init_global_stat();
  virtual int init_system_data();
  template<typename SCHEMA>
    int set_replica_options(SCHEMA &schema);

  int init_sys_unit_config(share::ObUnitConfig &unit_config);
  int create_sys_tenant();
  int set_in_bootstrap();
  int add_sys_table_lob_aux_table(
      uint64_t data_table_id,
      ObIArray<ObTableSchema> &table_schemas);
private:
  ObDDLService &ddl_service_;
  ObTenantDDLService &tenant_ddl_service_;
  obrpc::ObCommonRpcProxy &common_proxy_;
  int64_t begin_ts_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBootstrap);
};

#define BOOTSTRAP_CHECK_SUCCESS_V2(function_name) \
    do { \
      step_id_ ++;   \
      int64_t major_step = 1; \
      if (NULL == strstr(typeid(*this).name(), "ObPreBootstrap")) { \
        major_step = 3; \
      } else { \
        major_step = 2; \
      } \
      int64_t end_ts = ObTimeUtility::current_time(); \
      int64_t cost = end_ts - begin_ts_; \
      begin_ts_ = end_ts ; \
      if (OB_SUCC(ret)) { \
        ObTaskController::get().allow_next_syslog(); \
        _BOOTSTRAP_LOG(INFO, "STEP_%ld.%ld:%s execute success, cost=%ld", \
                       major_step, step_id_, function_name, cost); \
      } else { \
        ObTaskController::get().allow_next_syslog(); \
        _BOOTSTRAP_LOG(WARN, "STEP_%ld.%ld:%s execute fail, ret=%d, cost=%ld", \
                       major_step, step_id_, function_name, ret, cost); \
      }\
    } while (0)

#define BOOTSTRAP_CHECK_SUCCESS() \
    do { \
      step_id_ ++;   \
      int64_t major_step = 1; \
      if (NULL == strstr(typeid(*this).name(), "ObPreBootstrap")) { \
        major_step = 3; \
      } else { \
        major_step = 2; \
      } \
      int64_t end_ts = ObTimeUtility::current_time(); \
      int64_t cost = end_ts - begin_ts_; \
      begin_ts_ = end_ts ; \
      if (OB_SUCC(ret)) { \
        ObTaskController::get().allow_next_syslog(); \
        _BOOTSTRAP_LOG(INFO, "STEP_%ld.%ld:%s execute success, cost=%ld", \
                       major_step, step_id_, __FUNCTION__, cost); \
      } else { \
        ObTaskController::get().allow_next_syslog(); \
        _BOOTSTRAP_LOG(WARN, "STEP_%ld.%ld:%s execute fail, ret=%d, cost=%ld", \
                       major_step, step_id_, __FUNCTION__, ret, cost); \
      }\
    } while (0)

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_BOOTSTRAP_H_
