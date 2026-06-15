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

#define USING_LOG_PREFIX STORAGE

#include "ob_sstable_private_object_cleaner.h"
#include "storage/blocksstable/index_block/ob_index_block_builder.h"

namespace oceanbase
{
namespace blocksstable
{

int ObSSTablePrivateObjectCleaner::get_cleaner_from_data_store_desc(ObDataStoreDesc &data_store_desc, ObSSTablePrivateObjectCleaner *&cleaner)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!data_store_desc.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to get private object cleaner, invalid data store desc", K(ret), K(data_store_desc));
  } else if (OB_ISNULL(data_store_desc.sstable_index_builder_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get private object cleaner from data store desc, unexpected sstable index builder", K(ret), K(data_store_desc));
  } else {
    cleaner = &(data_store_desc.sstable_index_builder_->get_private_object_cleaner());
  }
  return ret;
}

ObSSTablePrivateObjectCleaner::ObSSTablePrivateObjectCleaner()
    : new_macro_block_ids_(),
      lock_(),
      task_succeed_(false)
{
  new_macro_block_ids_.set_attr(ObMemAttr(MTL_ID(), "MaWriterCleaner"));
}

ObSSTablePrivateObjectCleaner::~ObSSTablePrivateObjectCleaner()
{
  reset();
}

void ObSSTablePrivateObjectCleaner::reset()
{
  if (OB_UNLIKELY(!ATOMIC_LOAD(&task_succeed_))) {
    clean();
  }
  ATOMIC_SET(&task_succeed_, false);
  new_macro_block_ids_.reset();
}

int ObSSTablePrivateObjectCleaner::add_new_macro_block_id(const MacroBlockId &macro_id)
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObSSTablePrivateObjectCleaner::mark_succeed()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ATOMIC_LOAD(&task_succeed_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("double mark", K(ret), K(ATOMIC_LOAD(&task_succeed_)));
  } else {
    ATOMIC_SET(&task_succeed_, true);
  }
  return ret;
}

void ObSSTablePrivateObjectCleaner::clean()
{
  int ret = OB_SUCCESS;
}

} // namespace blocksstable
} // namespace oceanbase
