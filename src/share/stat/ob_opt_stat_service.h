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

#ifndef _OB_USER_TAB_COL_STAT_SERVICE_H_
#define _OB_USER_TAB_COL_STAT_SERVICE_H_

#include "share/stat/ob_opt_table_stat.h"
#include "share/stat/ob_opt_table_stat_cache.h"
#include "share/stat/ob_opt_column_stat_cache.h"
#include "share/stat/ob_opt_stat_sql_service.h"
#include "share/stat/ob_opt_ds_stat.h"
#include "share/stat/ob_opt_ds_stat_cache.h"
#include "share/stat/ob_opt_system_stat_cache.h"

namespace oceanbase {
namespace common {
class ObOptStatService
{
public:
  ObOptStatService() : inited_(false) {}
  virtual int init(common::ObMySQLProxy *proxy, ObServerConfig *config);
  virtual int get_table_stat(const uint64_t tenant_id,
                             const ObOptTableStat::Key &key,
                             ObOptTableStat &tstat);
  virtual int get_column_stat(const uint64_t tenant_id,
                              const ObOptColumnStat::Key &key,
                              ObOptColumnStatHandle &handle);
  virtual int load_table_stat_and_put_cache(const uint64_t tenant_id,
                                            const ObOptTableStat::Key &key,
                                            ObOptTableStatHandle &handle);
  int get_column_stat(const uint64_t tenant_id,
                      ObIArray<const ObOptColumnStat::Key*> &keys,
                      ObIArray<ObOptColumnStatHandle> &handles);
  int get_ds_stat(const ObOptDSStat::Key &key,
                  ObOptDSStatHandle &handle);

  int erase_table_stat(const ObOptTableStat::Key &key);
  int erase_column_stat(const ObOptColumnStat::Key &key);
  int erase_ds_stat(const ObOptDSStat::Key &key);
  int add_ds_stat_cache(const ObOptDSStat::Key &key,
                        const ObOptDSStat &value,
                        ObOptDSStatHandle &ds_stat_handle);

  int batch_get_table_stats(const uint64_t tenant_id,
                            ObIArray<const ObOptTableStat::Key *> &keys,
                            ObIArray<ObOptTableStatHandle> &handles);

  ObOptStatSqlService &get_sql_service() { return sql_service_; }

  int get_table_rowcnt(const uint64_t tenant_id,
                       const uint64_t table_id,
                       const ObIArray<ObTabletID> &all_tablet_ids,
                       const ObIArray<share::ObLSID> &all_ls_ids,
                       int64_t &table_rowcnt);

  int get_system_stat(const uint64_t tenant_id,
                      const ObOptSystemStat::Key &key,
                      ObOptSystemStat &stat);

  int load_system_stat_and_put_cache(const uint64_t tenant_id,
                                     const ObOptSystemStat::Key &key,
                                     ObOptSystemStatHandle &handle);

  int erase_system_stat(const ObOptSystemStat::Key &key);

private:
  /**
    * Implementation of the interface load_and_put_cache(key, handle), this function should not be called directly by external code
    * new_entry is space allocated on the stack, used for temporarily storing statistics
    */
  int load_column_stat_and_put_cache(const uint64_t tenant_id,
                                     ObIArray<const ObOptColumnStat::Key*> &keys,
                                     ObIArray<ObOptColumnStatHandle> &handles);

  int init_key_column_stats(ObIAllocator &allocator,
                            ObIArray<const ObOptColumnStat::Key*> &keys,
                            ObIArray<ObOptKeyColumnStat> &key_column_stats);

  int load_table_rowcnt_and_put_cache(const uint64_t tenant_id,
                                      const uint64_t table_id,
                                      const ObIArray<ObTabletID> &all_tablet_ids,
                                      const ObIArray<share::ObLSID> &all_ls_ids,
                                      int64_t &table_rowcnt);

  int init_table_stats(ObIAllocator &allocator,
                       const ObIArray<const ObOptTableStat::Key *> &keys,
                       ObIArray<ObOptTableStat *> &table_stats);

  int batch_load_table_stats_and_put_cache(const uint64_t tenant_id,
                                           ObIArray<const ObOptTableStat::Key *> &keys,
                                           ObIArray<ObOptTableStatHandle> &handles,
                                           ObIArray<int64_t> &regather_handles_indices);

protected:
  bool inited_;
  static const int64_t DEFAULT_TAB_STAT_CACHE_PRIORITY = 1;
  static const int64_t DEFAULT_COL_STAT_CACHE_PRIORITY = 1;
  static const int64_t DEFAULT_DS_STAT_CACHE_PRIORITY = 1;
  static const int64_t DEFAULT_SYSTEM_STAT_CACHE_PRIORITY = 1;
  ObOptStatSqlService sql_service_;

  ObOptTableStatCache table_stat_cache_;
  ObOptColumnStatCache column_stat_cache_;
  ObOptDSStatCache ds_stat_cache_;
  ObOptSystemStatCache system_stat_cache_;
};

}
}

#endif /* _OB_OPT_STAT_SERVICE_H_ */
