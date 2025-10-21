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
#include "storage/blocksstable/ob_shared_macro_block_manager.h"
#include "storage/ddl/ob_cg_block_tmp_file.h"

#ifndef OB_STORAGE_BLOCKSSTABLE_OB_MACRO_BLOCK_FLUSHER_H_
#define OB_STORAGE_BLOCKSSTABLE_OB_MACRO_BLOCK_FLUSHER_H_

namespace oceanbase
{
namespace blocksstable
{
class ObMacroBlock;
/**
 * -----------------------------------------------------------------ObIMacroBlockFlusher-------------------------------------------------------------------
 */
class ObIMacroBlockFlusher
{
public:
  ObIMacroBlockFlusher();
  virtual ~ObIMacroBlockFlusher();
  virtual int write_disk(ObMacroBlock& macro_block, const bool is_close_flush) = 0;
};

/**
 * --------------------------------------------------------------ObDagTempMacroFlusher------------------------------------------------------------
 */
class ObDagTempMacroFlusher : public ObIMacroBlockFlusher
{
public:
  ObDagTempMacroFlusher();
  virtual ~ObDagTempMacroFlusher();
  void reset();
  virtual int write_disk(ObMacroBlock& macro_block, const bool is_close_flush) override;
  void set_temp_file_writer(ObCGBlockFileWriter &temp_file_writer) { temp_file_writer_ = &temp_file_writer; }
private:
  ObCGBlockFileWriter *temp_file_writer_;
};


/**
 * --------------------------------------------------------------ObDagSliceMacroFlusher------------------------------------------------------------
 */
class ObDagSliceMacroFlusher : public ObIMacroBlockFlusher
{
public:
  explicit ObDagSliceMacroFlusher(ObCGBlockFileWriter &temp_file_writer);
  virtual ~ObDagSliceMacroFlusher();
  void reset();
  virtual int write_disk(ObMacroBlock& macro_block, const bool is_close_flush) override;
private:
  ObCGBlockFileWriter *temp_file_writer_;
};

}//end namespace blocksstable
}//end namespace oceanbase
#endif //OB_STORAGE_BLOCKSSTABLE_OB_MACRO_BLOCK_FLUSHER_H_
