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

#ifndef _OB_OPT_STAT_MANAGER_H_
#define _OB_OPT_STAT_MANAGER_H_

#include "lib/queue/ob_dedup_queue.h"
#include "share/stat/ob_opt_column_stat.h"
#include "share/stat/ob_opt_table_stat.h"
#include "share/stat/ob_opt_stat_service.h"
#include "share/stat/ob_opt_stat_sql_service.h"
#include "share/ob_rpc_struct.h"
#include "lib/queue/ob_dedup_queue.h"
#include "share/stat/ob_stat_define.h"
#include "share/stat/ob_opt_ds_stat.h"
#include "share/stat/ob_stat_item.h"
#include "share/stat/ob_opt_system_stat.h"

namespace oceanbase {
namespace common {
class ObOptColumnStatHandle;

class ObOptStatManager
{
public:
  ObOptStatManager();
  virtual ~ObOptStatManager() {}
  virtual int init(ObMySQLProxy *proxy,
                   ObServerConfig *config);
  virtual void stop();
  virtual void wait();
  virtual void destroy();
  static int64_t get_default_data_size();

  static int64_t get_default_avg_row_size();

  static int64_t get_default_table_row_count();

  int check_opt_stat_validity(sql::ObExecContext &ctx,
                              const uint64_t tenant_id,
                              const uint64_t table_ref_id,
                              const ObIArray<int64_t> &part_ids,
                              bool &is_opt_stat_valid);

  int check_system_stat_validity(sql::ObExecContext *ctx,
                                 const uint64_t tenant_id, 
                                 bool &is_valid);

  int check_opt_stat_validity(sql::ObExecContext &ctx,
                              const uint64_t tenant_id,
                              const uint64_t tab_ref_id,
                              const int64_t global_part_id,
                              bool &is_opt_stat_valid);

  int update_table_stat(const uint64_t tenant_id,
                        sqlclient::ObISQLConnection *conn,
                        const ObOptTableStat *table_stats,
                        const bool is_index_stat);

  int update_table_stat(const uint64_t tenant_id,
                        sqlclient::ObISQLConnection *conn,
                        const ObIArray<ObOptTableStat*> &table_stats,
                        const bool is_index_stat);
  int batch_get_column_stats(const uint64_t tenant_id,
                             const uint64_t table_id,
                             const ObIArray<int64_t> &part_ids,
                             const ObIArray<uint64_t> &column_ids,
                             const int64_t row_cnt,
                             const double scale_ratio,
                             ObIArray<ObGlobalColumnStat> &stat,
                             ObIAllocator *alloc);

  int get_column_stat(const uint64_t tenant_id,
                      const uint64_t table_id,
                      const ObIArray<int64_t> &part_ids,
                      const uint64_t column_id,
                      const int64_t row_cnt,
                      const double scale_ratio,
                      ObGlobalColumnStat &stat,
                      ObIAllocator *alloc);

  int get_column_stat(const uint64_t tenant_id,
                      const uint64_t table_id,
                      const ObIArray<int64_t> &part_ids,
                      const ObIArray<uint64_t> &column_ids,
                      ObIArray<ObOptColumnStatHandle> &handles);

  int get_column_stat(const uint64_t tenant_id,
                      const uint64_t ref_id,
                      const int64_t part_id,
                      const uint64_t col_id,
                      ObOptColumnStatHandle &handle);

  int get_table_stat(const uint64_t tenant_id,
                     const uint64_t table_ref_id,
                     const int64_t part_id,
                     const double scale_ratio,
                     ObGlobalTableStat &stat);

  int get_table_stat(const uint64_t tenant_id,
                     const uint64_t tab_ref_id,
                     const ObIArray<int64_t> &part_ids,
                     const double scale_ratio,
                     ObGlobalTableStat &stat);

  int get_table_stat(const uint64_t tenant_id,
                     const uint64_t table_id,
                     const ObIArray<int64_t> &part_ids,
                     ObIArray<ObOptTableStat> &tstats);

  int get_table_stat(const uint64_t tenant_id,
                     const uint64_t table_id,
                     const ObIArray<int64_t> &part_ids,
                     ObIArray<ObOptTableStatHandle> &handles);

  /**
   *  @brief  The interface for external acquisition of column statistics, which returns a handle containing a reference to a pointer of the statistics object. Through this pointer,
   *          statistics can be obtained. This approach is determined by the underlying implementation of ObKVCache. If the pointer of the returned handle is not null,
   *          the handle object guarantees that its statistics pointer remains valid until its own destruction.
   */
  virtual int get_column_stat(const uint64_t tenant_id,
                              const ObOptColumnStat::Key &key,
                              ObOptColumnStatHandle &handle);
  virtual int update_column_stat(share::schema::ObSchemaGetterGuard *schema_guard,
                                 const uint64_t tenant_id,
                                 sqlclient::ObISQLConnection *conn,
                                 const common::ObIArray<ObOptColumnStat *> &column_stats,
                                 bool only_update_col_stat = false,
                                 const ObObjPrintParams &print_params = ObObjPrintParams());


  int delete_table_stat(uint64_t tenant_id,
                        const uint64_t ref_id,
                        const ObIArray<int64_t> &part_ids,
                        const bool cascade_column,
                        const int64_t degree,
                        int64_t &affected_rows);

  int delete_column_stat(const uint64_t tenant_id,
                         const uint64_t ref_id,
                         const ObIArray<uint64_t> &column_ids,
                         const ObIArray<int64_t> &part_ids,
                         const bool only_histogram = false,
                         const int64_t degree = 1);

  int erase_column_stat(const ObOptColumnStat::Key &key);
  int erase_table_stat(const ObOptTableStat::Key &key);

  int erase_table_stat(const uint64_t tenant_id,
                       const uint64_t table_id,
                       const ObIArray<int64_t> &part_ids);
  int erase_column_stat(const uint64_t tenant_id,
                        const uint64_t table_id,
                        const ObIArray<int64_t> &part_ids,
                        const ObIArray<uint64_t> &column_ids);

  int batch_write(share::schema::ObSchemaGetterGuard *schema_guard,
                  const uint64_t tenant_id,
                  sqlclient::ObISQLConnection *conn,
                  ObIArray<ObOptTableStat *> &table_stats,
                  ObIArray<ObOptColumnStat *> &column_stats,
                  const int64_t current_time,
                  const bool is_index_stat,
                  const ObObjPrintParams &print_params);

  /**  @brief  External interface for obtaining row statistics information */
  virtual int get_table_stat(const uint64_t tenant_id,
                             const ObOptTableStat::Key &key,
                             ObOptTableStat &tstat);
  virtual int add_refresh_stat_task(const obrpc::ObUpdateStatCacheArg &analyze_arg);

  int invalidate_plan(const uint64_t tenant_id, const uint64_t table_id);

  int handle_refresh_stat_task(const obrpc::ObUpdateStatCacheArg &arg);

  int handle_refresh_system_stat_task(const obrpc::ObUpdateStatCacheArg &arg);

  int get_table_rowcnt(const uint64_t tenant_id,
                       const uint64_t table_id,
                       const ObIArray<ObTabletID> &all_tablet_ids,
                       const ObIArray<share::ObLSID> &all_ls_ids,
                       int64_t &table_rowcnt);

  static ObOptStatManager &get_instance()
  {
    static ObOptStatManager instance_;
    return instance_;
  }
  bool is_inited() const { return inited_; }
  ObOptStatSqlService &get_stat_sql_service()
  {
    return stat_service_.get_sql_service();
  }
  int check_stat_tables_ready(share::schema::ObSchemaGetterGuard &schema_guard,
                              const uint64_t tenant_id,
                              bool &are_stat_tables_ready);

  int get_ds_stat(const ObOptDSStat::Key &key, ObOptDSStatHandle &ds_stat_handle);
  int add_ds_stat_cache(const ObOptDSStat::Key &key,
                        const ObOptDSStat &value,
                        ObOptDSStatHandle &ds_stat_handle);
  int update_opt_stat_gather_stat(const ObOptStatGatherStat &gather_stat);

  int update_table_stat_failed_count(const uint64_t tenant_id,
                                     const uint64_t table_id,
                                     const ObIArray<int64_t> &part_ids,
                                     int64_t &affected_rows);
  int update_opt_stat_task_stat(const ObOptStatTaskInfo &task_info);
  ObOptStatService &get_stat_service() { return stat_service_; }

  int get_system_stat(const uint64_t tenant_id,
                      ObOptSystemStat &stat);
  int update_system_stats(const uint64_t tenant_id,
                        const ObOptSystemStat *system_stats);
  int delete_system_stats(const uint64_t tenant_id);

private:
  int trans_col_handle_to_evals(const ObArray<ObOptColumnStatHandle> &new_handles,
                                hash::ObHashMap<uint64_t, ObGlobalAllColEvals *> &column_id_col_evals);

  int update_all_eval_to_stats(const int64_t row_cnt,
                               const double scale_ratio,
                               ObIAllocator *alloc,
                               const ObIArray<uint64_t> &column_ids,
                               const hash::ObHashMap<uint64_t, ObGlobalAllColEvals *> &column_id_col_evals,
                               ObIArray<ObGlobalColumnStat> &column_stats);

  int flush_evals(ObIAllocator *alloc,
                  const int64_t start_pos,
                  const int64_t end_pos,
                  const ObIArray<uint64_t> &column_ids,
                  const hash::ObHashMap<uint64_t, ObGlobalAllColEvals *> &column_id_col_evals);

protected:
  static const int64_t REFRESH_STAT_TASK_NUM = 5;
  bool inited_;
  common::ObDedupQueue refresh_stat_task_queue_;
  ObOptStatService stat_service_;
  int64_t last_schema_version_;
};

template <typename T>
inline void assign_value(const T &val, T *ptr)
{
  if (NULL != ptr) {
    *ptr = val;
  }
}

}
}

#endif /* _OB_OPT_STAT_MANAGER_H_ */
