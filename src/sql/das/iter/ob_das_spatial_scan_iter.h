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

#ifndef OBDEV_SRC_SQL_DAS_ITER_OB_DAS_SPATIAL_SCAN_ITER_H_
#define OBDEV_SRC_SQL_DAS_ITER_OB_DAS_SPATIAL_SCAN_ITER_H_

#include "sql/das/iter/ob_das_iter.h"
#include "sql/das/iter/ob_das_scan_iter.h"
namespace oceanbase
{
using namespace common;
namespace sql
{

struct ObDASSpatialScanIterParam : public ObDASScanIterParam 
{
public:
  ObDASSpatialScanIterParam()
    : ObDASScanIterParam(),
      scan_rtdef_(nullptr)
  {}
  
  ObDASScanRtDef *scan_rtdef_;

  virtual bool is_valid() const override
  {
    return nullptr != scan_rtdef_ && ObDASIterParam::is_valid();
  }  
};

class ObDASSpatialScanIter : public ObDASScanIter
{
public:
  ObDASSpatialScanIter(ObIAllocator &allocator)
    : ObDASScanIter(),
      scan_ctdef_(nullptr),
      scan_rtdef_(nullptr),
      mbr_filters_(nullptr),
      mbr_filter_cnt_(0),
      max_rowkey_cnt_(-1),
      allocator_(&allocator),
      obj_ptr_(nullptr) {}

  void set_scan_param(storage::ObTableScanParam &scan_param);

protected:
  virtual int inner_init(ObDASIterParam &param) override;
  virtual int inner_get_next_row() override;
  
private:
  int filter_by_mbr(bool &got_row);
  int filter_by_mbr(const ObObj &mbr_obj, bool &pass_through);

  const ObDASScanCtDef *scan_ctdef_;
  ObDASScanRtDef *scan_rtdef_;

  const ObMbrFilterArray *mbr_filters_;
  bool is_whole_range_;

  int64_t mbr_filter_cnt_;
  int64_t max_rowkey_cnt_;

  ObIAllocator* allocator_;
  ObObj *obj_ptr_;
};


}  // namespace sql
}  // namespace oceanbase



#endif /* OBDEV_SRC_SQL_DAS_ITER_OB_DAS_SPATIAL_SCAN_ITER_H_ */
