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

#ifndef OCEANBASE_SHARE_UPGRADE_UTILS_H_
#define OCEANBASE_SHARE_UPGRADE_UTILS_H_

#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/ob_common_rpc_proxy.h"
#include "share/ob_check_stop_provider.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "rootserver/ob_rs_job_table_operator.h"

namespace oceanbase
{
namespace rootserver
{
struct ObSysStat;
}
namespace share
{

static const int64_t UPGRADE_JOB_TYPE_COUNT = 1;
static const rootserver::ObRsJobType upgrade_job_type_array[UPGRADE_JOB_TYPE_COUNT] = {
  rootserver::JOB_TYPE_INVALID,
};

class ObUpgradeUtils
{
public:
  ObUpgradeUtils() {}
  virtual ~ObUpgradeUtils() {}
  static int check_upgrade_job_passed(rootserver::ObRsJobType job_type);
  static int check_schema_sync(const uint64_t tenant_id, bool &is_sync);
  // upgrade_sys_variable()/upgrade_sys_stat() can be called when enable_ddl = false.
  static int upgrade_sys_variable(
             obrpc::ObCommonRpcProxy &rpc_proxy,
             common::ObISQLClient &sql_client,
             const uint64_t tenant_id);
  /* ----------------------- */
private:
  static int check_rs_job_exist(rootserver::ObRsJobType job_type, bool &exist);
  static int check_rs_job_success(rootserver::ObRsJobType job_type, bool &success);

  /* upgrade sys variable */
  static int calc_diff_sys_var_(
      common::ObISQLClient &sql_client,
      const uint64_t tenant_id,
      common::ObArray<int64_t> &update_list,
      common::ObArray<int64_t> &add_list);
  static int update_sys_var_(
             obrpc::ObCommonRpcProxy &rpc_proxy,
             const uint64_t tenant_id,
             const bool is_update,
             common::ObArray<int64_t> &update_list);
  /* upgrade sys variable end */
  static int filter_sys_stat(
      common::ObISQLClient &sql_client,
      const uint64_t tenant_id,
      rootserver::ObSysStat &sys_stat);
private:
  typedef common::ObFixedLengthString<OB_MAX_CONFIG_NAME_LEN> Name;
};

/* =========== upgrade processor ============= */

// Special upgrade actions for specific data version,
// which should be stateless and reentrant.
class ObBaseUpgradeProcessor
{
public:
  enum UpgradeMode {
    UPGRADE_MODE_INVALID,
    UPGRADE_MODE_OB,
    UPGRADE_MODE_PHYSICAL_RESTORE
  };
public:
  ObBaseUpgradeProcessor();
  virtual ~ObBaseUpgradeProcessor() {};
public:
  int init(int64_t data_version,
           UpgradeMode mode,
           common::ObMySQLProxy &sql_proxy,
           common::ObOracleSqlProxy &oracle_sql_proxy,
           obrpc::ObSrvRpcProxy &rpc_proxy,
           obrpc::ObCommonRpcProxy &common_proxy,
           share::schema::ObMultiVersionSchemaService &schema_service,
           share::ObCheckStopProvider &check_server_provider);
  int64_t get_version() const { return data_version_; }
  void set_tenant_id(const uint64_t tenant_id) { tenant_id_ = tenant_id; }
  int64_t get_tenant_id() const { return tenant_id_; }
  // pre_upgrade not used now
  virtual int pre_upgrade() = 0;
  // post_upgrade is executed when compatible is not increased and system tables/variables are ready
  // the code written here should satisfy reentrancy. For example, use `insert ignore` instead of `insert`
  virtual int post_upgrade() = 0;
  // finish_upgrade is executed when compatible is increased
  // the code wirtten here can use high version function. For example, add new privilege to root user
  virtual int finish_upgrade() = 0;
  TO_STRING_KV(K_(inited), K_(data_version), K_(tenant_id), K_(mode));
protected:
  virtual int check_inner_stat() const;
protected:
  bool inited_;
  int64_t data_version_;
  uint64_t tenant_id_;
  UpgradeMode mode_;
  common::ObMySQLProxy *sql_proxy_;
  common::ObOracleSqlProxy *oracle_sql_proxy_;
  obrpc::ObSrvRpcProxy *rpc_proxy_;
  obrpc::ObCommonRpcProxy *common_proxy_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  share::ObCheckStopProvider *check_stop_provider_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBaseUpgradeProcessor);
};

class ObUpgradeProcesserSet
{
public:
  ObUpgradeProcesserSet();
  virtual ~ObUpgradeProcesserSet();
  int init(ObBaseUpgradeProcessor::UpgradeMode mode,
           common::ObMySQLProxy &sql_proxy,
           common::ObOracleSqlProxy &oracle_sql_proxy,
           obrpc::ObSrvRpcProxy &rpc_proxy,
           obrpc::ObCommonRpcProxy &common_proxy,
           share::schema::ObMultiVersionSchemaService &schema_service,
           share::ObCheckStopProvider &check_server_provider);
  int get_processor_by_idx(const int64_t idx,
                           ObBaseUpgradeProcessor *&processor) const;
  int get_processor_by_version(const int64_t version,
                               ObBaseUpgradeProcessor *&processor) const;
  int get_processor_idx_by_range(const int64_t start_version,
                                 const int64_t end_version,
                                 int64_t &start_idx,
                                 int64_t &end_idx);
  int get_all_version_processor(ObBaseUpgradeProcessor *&processor) const;
private:
  virtual int check_inner_stat() const;
  int get_processor_idx_by_version(
      const int64_t version,
      int64_t &idx) const;
private:
  bool inited_;
  common::ObArenaAllocator allocator_;
  common::ObArray<ObBaseUpgradeProcessor *> processor_list_;
  ObBaseUpgradeProcessor *all_version_upgrade_processor_;
  DISALLOW_COPY_AND_ASSIGN(ObUpgradeProcesserSet);
};

#define DEF_SIMPLE_UPGRARD_PROCESSER(MAJOR, MINOR, MAJOR_PATCH, MINOR_PATCH) \
class ObUpgradeFor##MAJOR##MINOR##MAJOR_PATCH##MINOR_PATCH##Processor : public ObBaseUpgradeProcessor \
{ \
public: \
  ObUpgradeFor##MAJOR##MINOR##MAJOR_PATCH##MINOR_PATCH##Processor() : ObBaseUpgradeProcessor() {} \
  virtual ~ObUpgradeFor##MAJOR##MINOR##MAJOR_PATCH##MINOR_PATCH##Processor() {} \
  virtual int pre_upgrade() override { return common::OB_SUCCESS; } \
  virtual int post_upgrade() override { return common::OB_SUCCESS; } \
  virtual int finish_upgrade() override { return common::OB_SUCCESS; } \
};

/*
 * NOTE: The Following code should be modified when DATA_CURRENT_VERSION changed.
 * 1. ObUpgradeChecker: DATA_VERSION_NUM, UPGRADE_PATH
 * 2. Implement new ObUpgradeProcessor by data_version.
 * 3. Modify int ObUpgradeProcesserSet::init().
 */
class ObUpgradeChecker
{
public:
  static bool check_data_version_exist(const uint64_t version);
  static bool check_data_version_valid_for_backup(const uint64_t data_version);
  static bool check_cluster_version_exist(const uint64_t version);
  static int get_data_version_by_cluster_version(
             const uint64_t cluster_version,
             uint64_t &data_version);
public:
  static const int64_t DATA_VERSION_NUM = 1;
  static const uint64_t UPGRADE_PATH[];
};

/* =========== special upgrade processor start ============= */
class ObUpgradeForAllVersionProcessor : public ObBaseUpgradeProcessor
{
public:
  ObUpgradeForAllVersionProcessor() : ObBaseUpgradeProcessor() {}
  virtual ~ObUpgradeForAllVersionProcessor() {}
  virtual int pre_upgrade() override { return common::OB_SUCCESS; }
  virtual int post_upgrade() override;
  virtual int finish_upgrade() override { return common::OB_SUCCESS; }
private:
  int flush_ncomp_dll_job();
};

/* =========== special upgrade processor end   ============= */

/* =========== upgrade processor end ============= */

} // end namespace share
} // end namespace oceanbase
#endif /* _OB_UPGRADE_UTILS_H */
