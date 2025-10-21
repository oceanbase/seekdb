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

#ifndef OCEANBASE_SQL_OB_EXEC_STAT_COLLECTOR_H_
#define OCEANBASE_SQL_OB_EXEC_STAT_COLLECTOR_H_

#include "lib/utility/ob_macro_utils.h"
#include "lib/string/ob_string.h"
#include "lib/trace/ob_trace_event.h"
#include "sql/monitor/ob_phy_operator_monitor_info.h"
#include "sql/monitor/ob_exec_stat.h"
#include "lib/net/ob_addr.h"
namespace oceanbase
{
namespace observer
{
class ObInnerSQLResult;
}
namespace sql
{
class ObExecContext;
class ObPhyPlanMonitorInfo;
class ObPhyOperatorMonitorInfo;
class ObSql;
class ObPhysicalPlan;
class ObSQLSessionInfo;
class ObExecStatCollector
{
public:
  ObExecStatCollector() : length_(0) {}
  ~ObExecStatCollector() {}

  int collect_plan_monitor_info(uint64_t job_id,
                           uint64_t task_id,
                           ObPhyPlanMonitorInfo *monitor_info);
  int add_raw_stat(const common::ObString &str);
  int get_extend_info(common::ObIAllocator &allocator, common::ObString &str);
private:
  template<class T>
      int add_stat(const T *value);

  /* functions */
  DISALLOW_COPY_AND_ASSIGN(ObExecStatCollector);
  static const int64_t MAX_STAT_BUF_COUNT = 10240;
  char extend_buf_[MAX_STAT_BUF_COUNT];
  int64_t length_;
};

class ObExecStatDispatch
{
public:
  ObExecStatDispatch() : stat_str_(), pos_(0){};
  ~ObExecStatDispatch() {};
  int set_extend_info(const common::ObString &str);
  // Responsible for parsing the string into a normal data structure;
  int dispatch(bool need_add_monitor,
               ObPhyPlanMonitorInfo *monitor_info,
               bool need_update_plan,
               ObPhysicalPlan *plan);
private:
  int get_next_type(StatType &type);
  template<class T>
  int get_value(T *value);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExecStatDispatch);
  common::ObString stat_str_;
  int64_t pos_;
};

class ObExecStatUtils
{
public:
  template <class T>
      OB_INLINE static void record_exec_timestamp(const T &process,
                                        bool is_first,  // whether it is recorded during the first execution, not retry
                                        ObExecTimestamp &exec_timestamp)
      {
        exec_timestamp.rpc_send_ts_ = process.get_send_timestamp();
        exec_timestamp.receive_ts_ = process.get_receive_timestamp();
        exec_timestamp.enter_queue_ts_ = process.get_enqueue_timestamp();
        exec_timestamp.run_ts_ = process.get_run_timestamp();
        exec_timestamp.before_process_ts_ = process.get_process_timestamp();
        exec_timestamp.single_process_ts_ = process.get_single_process_timestamp();
        exec_timestamp.process_executor_ts_ = process.get_exec_start_timestamp();
        exec_timestamp.executor_end_ts_ = process.get_exec_end_timestamp();

        if (is_first) {
          /* packet encountered event sequence:
           * send -> receive -> enter_queue -> run -> before_process -> single_process -> executor_start -> executor_end
           * multistmt special handling, net_t, net_wait_ of the second and subsequent sqls are all 0
           */
          if (OB_UNLIKELY(exec_timestamp.multistmt_start_ts_ > 0)) {
            exec_timestamp.net_t_ = 0;
            exec_timestamp.net_wait_t_ = 0;
          } else {
            exec_timestamp.net_t_ = exec_timestamp.receive_ts_ - exec_timestamp.rpc_send_ts_;
            exec_timestamp.net_wait_t_ = exec_timestamp.enter_queue_ts_ - exec_timestamp.receive_ts_;
          }
        }
      }

private:
  DISALLOW_COPY_AND_ASSIGN(ObExecStatUtils);
  ObExecStatUtils();
  ~ObExecStatUtils();
};

}
}
#endif /* OCEANBASE_COMMON_STAT_OB_EXEC_STAT_COLLECTOR_H_ */
//// end of header file
