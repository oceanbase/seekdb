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

#ifndef OB_STORAGE_BLOCKSSTABLE_OB_IMACRO_BLOCK_FLUSH_CALLBACK_H_
#define OB_STORAGE_BLOCKSSTABLE_OB_IMACRO_BLOCK_FLUSH_CALLBACK_H_

namespace oceanbase
{
namespace blocksstable
{
class ObStorageObjectHandle;
struct ObLogicMacroBlockId;

class ObIMacroBlockFlushCallback
{
public:
  ObIMacroBlockFlushCallback() {}
  virtual ~ObIMacroBlockFlushCallback() {}
  /*
   * for some reason， some sub class set write into three parts
   * 1. write()       : write data into local buffer
   * 2. do_write_io() : optional, some sub class start write data in do_write_io(), while other directly write in write()
   * 3. wait()        : wait async write finish
  */
  virtual int write(const ObStorageObjectHandle &macro_handle,
                    const ObLogicMacroBlockId &logic_id,
                    char *buf,
                    const int64_t buf_len,
                    const int64_t row_count) = 0;
  virtual int do_write_io() { return OB_SUCCESS;}
  
  virtual int wait() = 0;
  virtual void reset() {}
  virtual int64_t get_ddl_start_row_offset() const { return -1; };
};

}  // end namespace blocksstable
}  // end namespace oceanbase

#endif //OB_STORAGE_BLOCKSSTABLE_OB_IMACRO_BLOCK_FLUSH_CALLBACK_H_
