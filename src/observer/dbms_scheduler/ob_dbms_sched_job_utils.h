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

#ifndef SRC_OBSERVER_DBMS_SCHED_JOB_UTILS_H_
#define SRC_OBSERVER_DBMS_SCHED_JOB_UTILS_H_

#include "lib/ob_define.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "lib/container/ob_iarray.h"
#include "lib/number/ob_number_v2.h"

namespace oceanbase
{
namespace common
{

class ObMySQLProxy;
class ObString;
class ObIAllocator;
class ObString;

namespace sqlclient
{
class ObMySQLResult;
}

}
namespace sql
{
class ObExecEnv;
class ObSQLSessionInfo;
class ObFreeSessionCtx;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObUserInfo;
}
}

namespace dbms_scheduler
{
enum class ObDBMSSchedFuncType : uint64_t
{
  USER_JOB = 0,
#define FUNCTION_TYPE(NAME, ...) NAME,
#include "ob_dbms_sched_func_type.h"
#undef FUNCTION_TYPE
  FUNCTION_TYPE_MAXNUM,
};

class ObDBMSSchedFuncSet
{
public:
  static const bool SHADOW = true;
  ObDBMSSchedFuncSet()
  {
#define FUNCTION_TYPE(name, args...) func_types_[static_cast<int64_t>(ObDBMSSchedFuncType::name)].set_args(args);
#include "ob_dbms_sched_func_type.h"
#undef FUNCTION_TYPE
  }
class FuncType
{
public:
  FuncType() : is_shadow_(false) {}
  void set_args(bool is_shadow = false) { is_shadow_ = is_shadow; }
  bool is_shadow_;
};
  bool is_shadow(ObDBMSSchedFuncType func_type)
  {
    bool is_shadow = false;
    if (func_type < ObDBMSSchedFuncType::FUNCTION_TYPE_MAXNUM) {
      is_shadow = func_types_[static_cast<uint64_t>(func_type)].is_shadow_;
    }
    return is_shadow;
  }
  static ObDBMSSchedFuncSet instance_;
  FuncType func_types_[static_cast<uint64_t>(ObDBMSSchedFuncType::FUNCTION_TYPE_MAXNUM)];
};

static const int64_t DEFAULT_LOG_HISTORY = 30; // days

class ObDBMSSchedJobInfo
{
public:
  ObDBMSSchedJobInfo() :
    tenant_id_(common::OB_INVALID_ID),
    user_id_(common::OB_INVALID_ID),
    database_id_(common::OB_INVALID_ID),
    job_(common::OB_INVALID_ID),
    lowner_(),
    powner_(),
    cowner_(),
    last_modify_(0),
    last_date_(0),
    this_date_(0),
    next_date_(0),
    total_(0),
    interval_(),
    failures_(0),
    flag_(0),
    what_(),
    nlsenv_(),
    charenv_(),
    field1_(),
    scheduler_flags_(0),
    exec_env_(),
    job_name_(),
    job_style_(),
    program_name_(),
    job_type_(),
    job_action_(),
    number_of_argument_(0),
    start_date_(0),
    repeat_interval_(),
    end_date_(0),
    job_class_(),
    enabled_(false),
    auto_drop_(false),
    state_(),
    run_count_(0),
    retry_count_(0),
    last_run_duration_(0),
    max_run_duration_(0),
    comments_(),
    credential_name_(),
    destination_name_(),
    interval_ts_(),
    is_oracle_tenant_(true),
    max_failures_(0),
    func_type_(ObDBMSSchedFuncType::FUNCTION_TYPE_MAXNUM),
    this_exec_date_(0),
    this_exec_addr_(),
    this_exec_trace_id_() {}

  TO_STRING_KV(K(tenant_id_),
               K(user_id_),
               K(database_id_),
               K(job_),
               K(job_name_),
               K(job_style_),
               K(job_type_),
               K(lowner_),
               K(powner_),
               K(cowner_),
               K(last_modify_),
               K(start_date_),
               K(last_date_),
               K(this_date_),
               K(next_date_),
               K(end_date_),
               K(total_),
               K(interval_),
               K(repeat_interval_),
               K(failures_),
               K(flag_),
               K(what_),
               K(nlsenv_),
               K(charenv_),
               K(field1_),
               K(scheduler_flags_),
               K(enabled_),
               K(auto_drop_),
               K(max_run_duration_),
               K(interval_ts_),
               K(max_failures_),
               K(state_),
               K(func_type_),
               K(this_exec_date_),
               K(this_exec_addr_),
               K(this_exec_trace_id_));

  bool valid()
  {
    return tenant_id_ != common::OB_INVALID_ID
            && job_ != common::OB_INVALID_ID
            && !exec_env_.empty();
  }

  uint64_t get_tenant_id() { return tenant_id_; }
  uint64_t get_user_id() { return user_id_; }
  uint64_t get_database_id() { return database_id_; }
  uint64_t get_job_id() { return job_; }
  uint64_t get_job_id_with_tenant() { return common::combine_two_ids(tenant_id_, job_); }
  int64_t  get_this_date() { return this_date_; }
  int64_t  get_next_date() { return next_date_; }
  int64_t  get_last_date() { return last_date_; }
  int64_t  get_last_modify() { return last_modify_; }
  int64_t  get_interval_ts() { return interval_ts_; }
  int64_t  get_max_run_duration() { return (max_run_duration_ == 0) ? 30 : max_run_duration_ ; } // 30s by default
  int64_t  get_start_date() { return start_date_; }
  int64_t  get_end_date() { return end_date_; }
  int64_t  get_auto_drop() { return auto_drop_; }
  int64_t  get_this_exec_date() { return this_exec_date_; }
  ObDBMSSchedFuncType get_func_type() const;

  bool is_completed() { return 0 == state_.case_compare("COMPLETED"); }
  bool is_broken() { return 0 == state_.case_compare("BROKEN"); }
  bool is_running(){ return this_date_ != 0; }
  bool is_on_executing(){ return this_exec_date_ != 0; }
  bool is_disabled() { return 0x0 == (enabled_ & 0x1); }
  bool is_killed() { return 0 == state_.case_compare("KILLED"); }

  common::ObString &get_what() { return what_; }
  common::ObString &get_exec_env() { return exec_env_; }
  common::ObString &get_lowner() { return lowner_; }
  common::ObString &get_powner() { return powner_; }
  common::ObString &get_cowner() { return cowner_; }
  common::ObString &get_zone() { return field1_; }
  common::ObString &get_repeat_interval() { return repeat_interval_; }
  common::ObString &get_interval() { return interval_; }
  common::ObString &get_program_name() { return program_name_; }
  common::ObString &get_job_name() { return job_name_; }
  common::ObString &get_job_class() { return job_class_; }
  common::ObString &get_job_action() { return job_action_; }
  common::ObString &get_this_exec_addr() { return this_exec_addr_; }
  common::ObString &get_this_exec_trace_id() { return this_exec_trace_id_; }

  bool is_oracle_tenant() { return is_oracle_tenant_; }
  bool is_default_job_class() const { return (0 == job_class_.case_compare("DEFAULT_JOB_CLASS")); }
  bool is_mview_job() const { return ObDBMSSchedFuncType::MVIEW_JOB == get_func_type(); }
  bool is_mysql_event_job() const { return ObDBMSSchedFuncType::MYSQL_EVENT_JOB == get_func_type(); }
  bool is_olap_async_job() const { return ObDBMSSchedFuncType::OLAP_ASYNC_JOB == get_func_type(); }
  bool is_stats_maintenance_job() const { return ObDBMSSchedFuncType::STAT_MAINTENANCE_JOB == get_func_type(); }
  bool is_dynamic_partition_job() const {return ObDBMSSchedFuncType::DYNAMIC_PARTITION_MANAGE_JOB == get_func_type(); }
  bool is_user_job() const { return ObDBMSSchedFuncType::USER_JOB == get_func_type(); }
  bool is_shadow() const { return ObDBMSSchedFuncSet::instance_.is_shadow(get_func_type()); }

  int deep_copy(common::ObIAllocator &allocator, const ObDBMSSchedJobInfo &other);

public:
  uint64_t tenant_id_;
  uint64_t user_id_;
  uint64_t database_id_;
  uint64_t job_;
  common::ObString lowner_;
  common::ObString powner_;
  common::ObString cowner_;
  int64_t last_modify_;
  int64_t last_date_;
  int64_t this_date_;
  int64_t next_date_;
  int64_t total_;
  common::ObString interval_;
  int64_t failures_;
  int64_t flag_;
  common::ObString what_;
  common::ObString nlsenv_;
  common::ObString charenv_;
  common::ObString field1_;
  int64_t scheduler_flags_;
  common::ObString exec_env_;
  common::ObString job_name_;
  common::ObString job_style_;
  common::ObString program_name_;
  common::ObString job_type_;
  common::ObString job_action_;
  uint64_t number_of_argument_;
  int64_t start_date_;
  common::ObString repeat_interval_;
  int64_t end_date_;
  common::ObString job_class_;
  bool enabled_;
  bool auto_drop_;
  common::ObString state_;
  int64_t  run_count_;
  int64_t retry_count_;
  int64_t last_run_duration_;
  int64_t max_run_duration_;
  common::ObString comments_;
  common::ObString credential_name_;
  common::ObString destination_name_;
  int64_t interval_ts_;
  bool is_oracle_tenant_;
  int64_t max_failures_;
  ObDBMSSchedFuncType func_type_;
  int64_t this_exec_date_;
  common::ObString this_exec_addr_;
  common::ObString this_exec_trace_id_;

public:
  static const int64_t JOB_SCHEDULER_FLAG_DATE_EXPRESSION_JOB_CLASS = 1;
  static const int64_t DEFAULT_MAX_END_DATE = 64060560000000000LL;
};

class ObDBMSSchedJobClassInfo
{
public:
  ObDBMSSchedJobClassInfo() :
    tenant_id_(common::OB_INVALID_ID),
    job_class_name_(),
    resource_consumer_group_(),
    logging_level_(),
    log_history_(),
    comments_(),
    is_oracle_tenant_(true) {}

  TO_STRING_KV(K(tenant_id_),
              K(job_class_name_),
              K(service_),
              K(resource_consumer_group_),
              K(logging_level_),
              K(log_history_),
              K(comments_));
  bool valid()
  {
    return tenant_id_ != common::OB_INVALID_ID
            && !job_class_name_.empty();
  }
  uint64_t get_tenant_id() { return tenant_id_; }
  common::number::ObNumber &get_log_history() { return log_history_; }
  common::ObString &get_job_class_name() { return job_class_name_; }
  common::ObString &get_service() { return service_; }
  common::ObString &get_resource_consumer_group() { return resource_consumer_group_; }
  common::ObString &get_logging_level() { return logging_level_; }
  common::ObString &get_comments() { return comments_; }
  bool is_oracle_tenant() { return is_oracle_tenant_; }
  int deep_copy(common::ObIAllocator &allocator, const ObDBMSSchedJobClassInfo &other);
public:
  uint64_t tenant_id_;
  common::ObString job_class_name_;
  common::ObString service_;
  common::ObString resource_consumer_group_;
  common::ObString logging_level_;
  common::number::ObNumber log_history_;
  common::ObString comments_;
  bool is_oracle_tenant_;
};

class ObDBMSSchedJobUtils
{
public:
  /**
   * @brief  Check if the sql name is valid
   * @param [in] name  - sql name
   * @retval OB_SUCCESS execute success
   * @retval OB_ERR_ILLEGAL_NAME illegal name
  */
  static int check_is_valid_name(const ObString &name);
  /**
   * @brief  Check if the job style is valid
   * @param [in] str  - job style
   * @retval OB_SUCCESS execute success
   * @retval OB_NOT_SUPPORTED not supported
  */
  static int check_is_valid_job_style(const ObString &str);
  /**
   * @brief  Check if the job type is valid
   * @param [in] str  - job type
   * @retval OB_SUCCESS execute success
   * @retval OB_NOT_SUPPORTED not supported
  */
  static int check_is_valid_job_type(const ObString &str);
  /**
   * @brief  Check if the job argument num is valid
   * @param [in] num
   * @retval OB_SUCCESS execute success
   * @retval OB_NOT_SUPPORTED not supported
  */
  static int check_is_valid_argument_num(const int64_t num);
  /**
   * @brief  Check if the sched_type is valid
   * @param [in] str  - sched_type
   * @retval OB_SUCCESS execute success
   * @retval OB_NOT_SUPPORTED not supported
  */
  /**
   * @brief  Check if the state is valid
   * @param [in] str  - state
   * @retval OB_SUCCESS execute success
   * @retval OB_NOT_SUPPORTED not supported
  */
  static int check_is_valid_state(const ObString &str);
  /**
   * @brief  Check if the end_date is valid
   * @param [in] start_date
   * @param [in] end_date
   * @retval OB_SUCCESS execute success
   * @retval OB_NOT_SUPPORTED unsupported time
  */
  static int check_is_valid_end_date(const int64_t start_date, const int64_t end_date);
  /**
   * @brief  Check if the repeat interval is valid
   * @param [in] str repeat_interval
   * @retval OB_SUCCESS execute success
   * @retval OB_NOT_SUPPORTED not supported
  */
  static int check_is_valid_repeat_interval(const ObString &str, bool is_limit_interval_num = false);
  /**
   * @brief  Check if max_run_duration is valid
   * @param [in] max_run_duration
   * @retval OB_SUCCESS execute success
   * @retval OB_INVALID_ARGUMENT_NUM interval_num out of range
   * @retval OB_NOT_SUPPORTED syntax not supported
  */
  static int check_is_valid_max_run_duration(const int64_t max_run_duration);
  //TO DO DELETE continuous rain
  static int generate_job_id(int64_t tenant_id, int64_t &max_job_id);
  /**
   * @brief  Create a job
   * @param [in] sql_client
   * @param [in] tenant_id  - tenant id
   * @param [in] job_id  - job id
   * @retval OB_SUCCESS execute success
   * @retval OB_ERR_UNEXPECTED unknown error
   * @retval OB_INVALID_ARGUMENT current JOB does not exist
   * @retval OB_ERR_NO_PRIVILEGE the user passed in for the current JOB does not have permission to modify
   */
  static int create_dbms_sched_job(common::ObISQLClient &sql_client,
                                   const uint64_t tenant_id,
                                   const int64_t job_id,
                                   const ObDBMSSchedJobInfo &job_info);
  /**
   * @brief  Directly delete a job
   * @param [in] sql_client
   * @param [in] tenant_id  - tenant id
   * @param [in] job_name  - job name
   * @param [in] if_exists  - if true (deleting a non-existent JOB will result in an error)
   * @retval OB_SUCCESS execute success
   * @retval OB_ERR_UNEXPECTED unknown error
   * @retval OB_INVALID_ARGUMENT current JOB does not exist
   */
  static int remove_dbms_sched_job(common::ObISQLClient &sql_client,
                                   const uint64_t tenant_id,
                                   const common::ObString &job_name,
                                   const bool if_exists = false);
  /**
   * @brief  Stop a job that has not started/running
   * @param [in] sql_client
   * @param [in] is_delete_after_stop  - Whether to delete after stopping
   * @param [in] job_info  - Information of the job to stop
   * @retval OB_SUCCESS execute success
   * @retval OB_NOT_SUPPORTED Not supported in the current version
   * @retval OB_ERR_UNEXPECTED Unknown error
   * @retval OB_ENTRY_NOT_EXIST The current JOB is not running
   * @retval OB_ERR_NO_PRIVILEGE The user provided for the current JOB does not have permission to modify
   */
  static int stop_dbms_sched_job(common::ObISQLClient &sql_client,
                                 const ObDBMSSchedJobInfo &job_info,
                                 const bool is_delete_after_stop);
  /**
   * @brief  Update JOB information
   * @param [in] job_info
   * @param [in] job_attribute_name - The job column to be updated
   * @param [in] job_attribute_value - The value of the column to be updated
   * @retval OB_SUCCESS execute success
   * @retval OB_ERR_UNEXPECTED unknown error
   * @retval OB_INVALID_ARGUMENT invalid argument
   * @retval OB_ENTRY_NOT_EXIST JOB does not exist/no changes made
   */
  static int update_dbms_sched_job_info(common::ObISQLClient &sql_client,
                                        const ObDBMSSchedJobInfo &job_info,
                                        const ObString &job_attribute_name,
                                        const ObObj &job_attribute_value,
                                        const bool from_pl_set_attr = false);
  /**
   * @brief  Get JOB information
   * @param [in] tenant_id  - tenant id
   * @param [in] is_oracle_tenant  - whether it is an oracle tenant, internal table does not have this value, determined by the caller
   * @param [in] job_name  - job name
   * @param [in] allocator  - reasonable allocator, to prevent job_info from becoming invalid
   * @param [in] job_info  - job information
   * @retval OB_SUCCESS execute success
   * @retval OB_ERR_UNEXPECTED unknown error
   * @retval OB_ENTRY_NOT_EXIST JOB does not exist
   */
  static int get_dbms_sched_job_info(common::ObISQLClient &sql_client,
                                     const uint64_t tenant_id,
                                     const bool is_oracle_tenant,
                                     const ObString &job_name,
                                     common::ObIAllocator &allocator,
                                     ObDBMSSchedJobInfo &job_info);
  /**
   * @brief  Check if the user has permission to modify the JOB (policy: root user can modify all jobs, ordinary users can only modify their own jobs)
   * @param [in] user_info  - User information
   * @param [in] job_info  - Job information
   * @retval OB_SUCCESS execute success
   * @retval OB_ERR_UNEXPECTED unknown error
   * @retval OB_INVALID_ARGUMENT invalid argument/ user_info is NULL
   * @retval OB_ERR_NO_PRIVILEGE the input user does not have permission
   */
  static int check_dbms_sched_job_priv(const share::schema::ObUserInfo *user_info,
                                       const ObDBMSSchedJobInfo &job_info);
  /**
   * @brief  Calculate the next execution time based on the job's time expression
   * @param [in] job_info  - job information
   * @param [out] next_run_time 
   * @retval OB_SUCCESS execute success
   * @retval OB_ERR_UNEXPECTED unknown error
   * @retval OB_INVALID_ARGUMENT invalid argument
   */
  static int calc_dbms_sched_repeat_expr(const ObDBMSSchedJobInfo &job_info, int64_t &next_run_time);
  static int zone_check_impl(int64_t tenant_id, const ObString &zone);
  static int job_class_check_impl(int64_t tenant_id, const ObString &job_class_name);
  static int get_max_failures_value(int64_t tenant_id, const ObString &src_str, int64_t &value);
  static int reserve_user_with_minimun_id(ObIArray<const share::schema::ObUserInfo *> &user_infos); // TO DO Lianyu delete
};
}
}

#endif /* SRC_OBSERVER_DBMS_SCHED_JOB_UTILS_H_ */
