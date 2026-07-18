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

#include "storage/direct_load/ob_direct_load_external_fragment.h"
#include "storage/direct_load/ob_direct_load_i_table.h"

namespace oceanbase
{
namespace storage
{

struct ObDirectLoadExternalTableCreateParam
{
public:
  ObDirectLoadExternalTableCreateParam();
  ~ObDirectLoadExternalTableCreateParam();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(data_block_size), K_(row_count), K_(max_data_block_size),
               K_(fragments));
public:
  common::ObTabletID tablet_id_;
  int64_t data_block_size_;
  int64_t row_count_;
  int64_t max_data_block_size_;
  ObDirectLoadExternalFragmentArray fragments_;
};

struct ObDirectLoadExternalTableMeta
{
public:
  ObDirectLoadExternalTableMeta();
  ~ObDirectLoadExternalTableMeta();
  void reset();
  TO_STRING_KV(K_(tablet_id), K_(data_block_size), K_(row_count), K_(max_data_block_size));
public:
  common::ObTabletID tablet_id_;
  int64_t data_block_size_;
  int64_t row_count_;
  int64_t max_data_block_size_;
};

class ObDirectLoadExternalTable : public ObDirectLoadITable
{
public:
  ObDirectLoadExternalTable();
  virtual ~ObDirectLoadExternalTable();
  void reset();
  int init(const ObDirectLoadExternalTableCreateParam &param);
  const common::ObTabletID &get_tablet_id() const override { return meta_.tablet_id_; }
  int64_t get_row_count() const override { return meta_.row_count_; }
  bool is_valid() const override { return is_inited_; }
  const ObDirectLoadExternalTableMeta &get_meta() const { return meta_; }
  const ObDirectLoadExternalFragmentArray &get_fragments() const { return fragments_; }
  TO_STRING_KV(K_(meta), K_(fragments));
private:
  ObDirectLoadExternalTableMeta meta_;
  ObDirectLoadExternalFragmentArray fragments_;
  bool is_inited_;
  DISABLE_COPY_ASSIGN(ObDirectLoadExternalTable);
};

} // namespace storage
} // namespace oceanbase
