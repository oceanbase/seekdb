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
#pragma once

#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_external_fragment.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadExternalTable;

struct ObDirectLoadExternalTableCompactParam
{
public:
  ObDirectLoadExternalTableCompactParam();
  ~ObDirectLoadExternalTableCompactParam();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(table_data_desc));
public:
  common::ObTabletID tablet_id_;
  ObDirectLoadTableDataDesc table_data_desc_;
};

class ObDirectLoadExternalTableCompactor : public ObIDirectLoadTabletTableCompactor
{
public:
  ObDirectLoadExternalTableCompactor();
  virtual ~ObDirectLoadExternalTableCompactor();
  int init(const ObDirectLoadExternalTableCompactParam &param);
  int add_table(const ObDirectLoadTableHandle &table_handle) override;
  int compact() override;
  int get_table(ObDirectLoadTableHandle &table_handle,
                ObDirectLoadTableManager *table_manager) override;
  void stop() override;
private:
  int check_table_compactable(ObDirectLoadExternalTable *external_table);
private:
  ObDirectLoadExternalTableCompactParam param_;
  int64_t row_count_;
  int64_t max_data_block_size_;
  ObDirectLoadExternalFragmentArray fragments_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
