/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef SRC_OBSERVER_DBMS_SCHED_JOB_UTILS_H_
#define SRC_OBSERVER_DBMS_SCHED_JOB_UTILS_H_

#include "query/scheduler/ob_scheduler_job.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObUserInfo;
}
}
namespace dbms_scheduler
{

class ObDBMSSchedJobUtils
{
public:
  static int check_is_valid_name(const common::ObString &name);
  static int check_is_valid_job_style(const common::ObString &str);
  static int check_is_valid_job_type(const common::ObString &str);
  static int check_is_valid_argument_num(int64_t num);
  static int check_is_valid_state(const common::ObString &str);
  static int check_is_valid_end_date(int64_t start_date, int64_t end_date);
  static int check_is_valid_repeat_interval(
      const common::ObString &str, bool is_limit_interval_num = false);
  static int check_is_valid_max_run_duration(int64_t max_run_duration);
  static int generate_job_id(int64_t &max_job_id);
  static int create_dbms_sched_job(
      common::ObISQLClient &sql_client,
      int64_t job_id,
      const ObDBMSSchedJobInfo &job_info);
  static int remove_dbms_sched_job(
      common::ObISQLClient &sql_client,
      const common::ObString &job_name,
      bool if_exists = false);
  static int stop_dbms_sched_job(
      common::ObISQLClient &sql_client,
      const ObDBMSSchedJobInfo &job_info,
      bool is_delete_after_stop);
  static int update_dbms_sched_job_info(
      common::ObISQLClient &sql_client,
      const ObDBMSSchedJobInfo &job_info,
      const common::ObString &job_attribute_name,
      const common::ObObj &job_attribute_value,
      bool from_pl_set_attr = false);
  static int get_dbms_sched_job_info(
      common::ObISQLClient &sql_client,
      const common::ObString &job_name,
      common::ObIAllocator &allocator,
      ObDBMSSchedJobInfo &job_info);
  static int check_dbms_sched_job_priv(
      const share::schema::ObUserInfo *user_info,
      const ObDBMSSchedJobInfo &job_info);
  static int calc_dbms_sched_repeat_expr(
      const ObDBMSSchedJobInfo &job_info, int64_t &next_run_time);
  static int job_class_check_impl(const common::ObString &job_class_name);
  static int get_max_failures_value(const common::ObString &src_str, int64_t &value);
  static int reserve_user_with_minimun_id(
      common::ObIArray<const share::schema::ObUserInfo *> &user_infos);
  static void upgrade_legacy_func_type(
      common::ObISQLClient &sql_client, ObDBMSSchedJobInfo &job_info);
};

} // namespace dbms_scheduler
} // namespace oceanbase

#endif // SRC_OBSERVER_DBMS_SCHED_JOB_UTILS_H_
