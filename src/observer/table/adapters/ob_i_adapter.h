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

#ifndef _OB_I_ADAPTER_H
#define _OB_I_ADAPTER_H

#include "observer/table/common/ob_table_common_struct.h"
#include "lib/container/ob_iarray.h"
#include "share/table/ob_table.h"
#include "common/ob_range.h"
#include "ob_hbase_cell_iter.h"

namespace oceanbase
{   
namespace table
{

class ObIHbaseAdapter
{
public:
  ObIHbaseAdapter() 
      : allocator_("HbaseAdapAloc", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID())
  {}

  virtual ~ObIHbaseAdapter() {}
  virtual int put(ObTableExecCtx &ctx, const ObITableEntity &cell) = 0;
  virtual int multi_put(ObTableExecCtx &ctx, const ObIArray<ObITableEntity *> &cells) = 0;
  virtual int del(ObTableExecCtx &ctx, const ObITableEntity &cell) = 0;
  virtual int scan(ObIAllocator &alloc, ObTableExecCtx &ctx, const ObTableQuery &query, ObHbaseICellIter *&iter) = 0;
  virtual void reuse();

protected:
  int init_table_ctx(ObTableExecCtx &exec_ctx,
                     const ObITableEntity &cell, 
                     ObTableOperationType::Type op_type,
                     ObTableCtx &tb_ctx);

  int init_scan(ObTableExecCtx &exec_ctx, const ObTableQuery &query, ObTableCtx &tb_ctx);

protected:
  common::ObArenaAllocator allocator_;

  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObIHbaseAdapter);
};

} // end of namespace table
} // end of namespace oceanbase

#endif
