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

#ifndef OCEANBASE_SHARE_TABLET_OB_TABLET_TABLE_ITERATOR_H
#define OCEANBASE_SHARE_TABLET_OB_TABLET_TABLE_ITERATOR_H

#include "share/tablet/ob_tablet_info.h"

namespace oceanbase
{
namespace share
{
class ObTabletMetaIterator
{
public:
  ObTabletMetaIterator();
  virtual ~ObTabletMetaIterator() { reset(); }
  virtual void reset();
  virtual int next(ObTabletRuntimeInfo &tablet_info);
protected:
  int inner_init();
  virtual int prefetch() = 0;
protected:
  bool is_inited_;
  int64_t prefetch_tablet_idx_;
  
  common::ObArray<ObTabletRuntimeInfo> prefetched_tablets_;
};

class ObCompactionTabletMetaIterator : public ObTabletMetaIterator
{
public:
  ObCompactionTabletMetaIterator(
    const bool first_check,
    const int64_t compaction_scn);
  ~ObCompactionTabletMetaIterator() override { reset(); }
  int init(
    const int64_t batch_size);
  virtual void reset() override;
  virtual int next(ObTabletRuntimeInfo &tablet_info) override;

private:
  virtual int prefetch() override;
  const static int64_t TABLET_META_TABLE_RANGE_GET_SIZE = 1500;

  bool first_check_;
  int64_t compaction_scn_;
  int64_t batch_size_;
  ObTabletID end_tablet_id_;
};

} // end namespace
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_TABLET_OB_TABLET_TABLE_ITERATOR_H
