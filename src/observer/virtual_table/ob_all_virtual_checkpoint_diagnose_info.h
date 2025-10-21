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

#ifndef OB_ALL_VIRTUAL_CHEKPOINT_DIAGNOSE_INFO_H
#define OB_ALL_VIRTUAL_CHEKPOINT_DIAGNOSE_INFO_H

#include "share/ob_virtual_table_scanner_iterator.h"
#include "observer/omt/ob_multi_tenant_operator.h"
#include "observer/omt/ob_multi_tenant.h"
#include "src/storage/checkpoint/ob_checkpoint_diagnose.h"
namespace oceanbase
{
namespace observer
{

class ObAllVirtualCheckpointDiagnoseInfo : public common::ObVirtualTableScannerIterator,
                                           public omt::ObMultiTenantOperator
{
friend class GenerateTraceRow;
public:
  virtual ~ObAllVirtualCheckpointDiagnoseInfo() { omt::ObMultiTenantOperator::reset(); }
  virtual int inner_get_next_row(common::ObNewRow *&row) { return execute(row); }
  inline void set_addr(common::ObAddr &addr) { addr_ = addr; }

private:
  virtual bool is_need_process(uint64_t tenant_id) override;
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  virtual void release_last_tenant() override;

  common::ObAddr addr_;
};

struct GenerateTraceRow 
{
public:
  GenerateTraceRow() = delete;
  GenerateTraceRow(const GenerateTraceRow&) = delete;
  GenerateTraceRow& operator=(const GenerateTraceRow&) = delete;
  GenerateTraceRow(ObAllVirtualCheckpointDiagnoseInfo &virtual_table)
    : virtual_table_(virtual_table)
  {}
  int operator()(const storage::checkpoint::ObTraceInfo &trace_info) const;

private:
  ObAllVirtualCheckpointDiagnoseInfo &virtual_table_;
};


} // observer
} // oceanbase
#endif
