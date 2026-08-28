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

#ifndef OB_ALL_VIRTUAL_DML_STATS_H
#define OB_ALL_VIRTUAL_DML_STATS_H

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/ob_scanner.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"

namespace oceanbase
{

namespace observer
{

typedef std::pair<uint64_t, uint64_t> StatKey;
typedef common::hash::ObHashMap<StatKey, ObOptDmlStat> DmlStatMap;

class ObOptDmlStatMapGetter : public common::ObIOptDmlStatConsumer
{
public:
  explicit ObOptDmlStatMapGetter(common::ObScanner &scanner,
                                 common::ObIArray<uint64_t> &output_column_ids,
                                 char *svr_ip,
                                 int32_t port,
                                 common::ObNewRow &cur_row)
    : scanner_(scanner),
      output_column_ids_(output_column_ids),
      svr_ip_(svr_ip),
      port_(port),
      cur_row_(cur_row)
  {}
  virtual ~ObOptDmlStatMapGetter() {};
  int consume(ObOptDmlStat &dml_stat) override;
  DISALLOW_COPY_AND_ASSIGN(ObOptDmlStatMapGetter);
private:
  common::ObScanner &scanner_;
  common::ObIArray<uint64_t> &output_column_ids_;
  char *svr_ip_;
  int32_t port_;
  common::ObNewRow &cur_row_;
  
};

class ObAllVirtualDMmlStats : public ObVirtualTableScannerIterator
{
  friend class ObOptDmlStatMapGetter;
public:
  ObAllVirtualDMmlStats();
  virtual ~ObAllVirtualDMmlStats();
  virtual void reset() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;
private:
  enum COLUMNS
  {
        TABLE_ID = common::OB_APP_MIN_COLUMN_ID,
    TABLET_ID,
    INSERT_ROW_COUNT,
    UPDATE_ROW_COUNT,
    DELETE_ROW_COUNT,
  };
  int32_t port_;
  char svr_ip_[common::OB_IP_STR_BUFF];
  int fill_scanner();
  bool done_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualDMmlStats);
};

}// namespace observer
}// namespace oceanbase

#endif /* !OB_ALL_VIRTUAL_DML_STATS_H */
