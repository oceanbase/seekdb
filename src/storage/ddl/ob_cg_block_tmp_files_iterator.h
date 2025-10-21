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

#ifndef OCEANBASE_STORAGE_OB_CG_BLOCK_TMP_FILES_ITERATOR_H_
#define OCEANBASE_STORAGE_OB_CG_BLOCK_TMP_FILES_ITERATOR_H_

#include "storage/ddl/ob_cg_block_tmp_file.h"

namespace oceanbase
{
namespace storage
{
class ObCGBlockFilesIterator
{
public:
  ObCGBlockFilesIterator() :
    can_put_cg_block_back_(true),
    total_data_size_(0),
    allocator_(ObMemAttr(MTL_ID(), "CGBFIter")),
    cg_block_files_(allocator_) { }
  ~ObCGBlockFilesIterator()
  {
    reset();
  }
  void reset();
  int push_back_cg_block_files(ObIArray<ObCGBlockFile *> &cg_block_files);
  int push_back_cg_block_file(ObCGBlockFile *cg_block_file);
  int get_next_cg_block(ObCGBlock &cg_block);
  int put_cg_block_back(const ObCGBlock &cg_block);
  int get_remain_block_files(ObIArray<ObCGBlockFile *> &block_files);
  OB_INLINE int64_t get_total_data_size() const { return total_data_size_; }
  TO_STRING_KV(K(can_put_cg_block_back_), K(total_data_size_), K(cg_block_files_));

private:
  bool can_put_cg_block_back_;
  int64_t total_data_size_;
  ObMalloc allocator_;
  ObList<ObCGBlockFile *, ObMalloc> cg_block_files_;
};
} //end storage
} // end oceanbase

#endif //OCEANBASE_STORAGE_OB_CG_BLOCK_TMP_FILES_ITERATOR_H_
