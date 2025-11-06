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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_DATA_MACRO_BLOCK_MERGE_WRITER_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_DATA_MACRO_BLOCK_MERGE_WRITER_H_
#include "ob_macro_block_writer.h"
#include "ob_macro_block.h"
#include "index_block/ob_index_block_macro_iterator.h"
#include "storage/blocksstable/index_block/ob_index_block_dual_meta_iterator.h"
#include "ob_macro_block.h"
#include "ob_datum_row.h"


namespace oceanbase
{
namespace blocksstable
{

class ObSSTablePrivateObjectCleaner;

class ObDataMacroBlockMergeWriter : public ObMacroBlockWriter
{
public:
  ObDataMacroBlockMergeWriter();
  virtual ~ObDataMacroBlockMergeWriter();
  virtual void reset() override;
  virtual int open(
      const ObDataStoreDesc &data_store_desc, 
      const int64_t parallel_idx,
      const blocksstable::ObMacroSeqParam &macro_seq_param,
      const share::ObPreWarmerParam &pre_warm_param,
      ObSSTablePrivateObjectCleaner &object_cleaner,
      ObIMacroBlockFlushCallback *callback = nullptr,
      ObIMacroBlockValidator *validator = nullptr,
      ObIODevice *device_handle = nullptr) override;
  virtual int append_row(const ObDatumRow &row, const ObMacroBlockDesc *curr_macro_desc = nullptr) override;
  virtual int append_micro_block(const ObMicroBlock &micro_block, const ObMacroBlockDesc *curr_macro_desc = nullptr) override;
  virtual int append_macro_block(
      const ObMacroBlockDesc &macro_desc,
      const ObMicroBlockData *micro_block_data) override;
protected:
  virtual int build_micro_block() override;
  virtual int try_switch_macro_block() override;
  virtual bool is_keep_freespace() const override {return !is_use_freespace_; }

private:
  void adjust_freespace(const ObMacroBlockDesc *curr_macro_desc);
  bool check_need_switch_macro_block();
private:
  ObLogicMacroBlockId curr_macro_logic_id_;
  bool is_use_freespace_;
  bool next_block_use_freespace_;
};

}//end namespace blocksstable
}//end namespace oceanbase
#endif
