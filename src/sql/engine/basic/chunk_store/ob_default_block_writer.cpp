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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/chunk_store/ob_default_block_writer.h"
#include "data_plane/blocksstable/ob_storage_datum.h"

namespace oceanbase
{
using namespace common;

namespace sql
{

int ObDefaultBlockWriter::add_row(const common::ObIArray<ObExpr*> &exprs, ObEvalCtx &ctx, ObChunkDatumStore::StoredRow **stored_row)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ensure_init())) {
  } else if (OB_FAIL(ensure_write(exprs, ctx))) {
  } else {
    if (OB_FAIL(inner_add_row(exprs, ctx, stored_row))) {
    }
  }

  return ret;
}

int ObDefaultBlockWriter::add_row(const ObChunkDatumStore::StoredRow &src_sr, ObChunkDatumStore::StoredRow **dst_sr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ensure_init())) {
  } else if (OB_FAIL(ensure_write(src_sr))) {
  } else {
    ObChunkDatumStore::StoredRow *sr = new (get_cur_buf())ObChunkDatumStore::StoredRow;
    sr->assign(&src_sr);
    if (OB_FAIL(advance(sr->row_size_))) {
    } else {
      if (nullptr != dst_sr) {
        *dst_sr = sr;
      }
    }
  }

  return ret;
}

int ObDefaultBlockWriter::add_row(const blocksstable::ObStorageDatum *storage_datums, const int64_t column_count,
                               const int64_t extra_size, ObChunkDatumStore::StoredRow **stored_row)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ensure_init())) {
  } else if (OB_FAIL(ensure_write(storage_datums, column_count, extra_size))) {
  } else if (OB_FAIL(inner_add_row(storage_datums, column_count, extra_size, stored_row))) {
  }
  return ret;
}

int ObDefaultBlockWriter::add_batch(const common::Datum **datums, const common::ObIArray<ObExpr *> &exprs,
                                    const uint16_t selector[], const int64_t size,
                                    ObChunkDatumStore::StoredRow **stored_rows, BatchCtx *batch_ctx)
{
  int ret = OB_SUCCESS;
  uint32_t *size_array = batch_ctx->row_size_array_;
  int64_t col_cnt = exprs.count();
  const int64_t base_row_size = sizeof(ObChunkDatumStore::StoredRow)
      + sizeof(StoredDatumCell) * col_cnt;
  if (base_row_size < 0 || base_row_size > UINT32_MAX) { ret = OB_SIZE_OVERFLOW; }
  for (int64_t i = 0; OB_SUCC(ret) && i < size; i++) {
    size_array[i] = base_row_size;
  }
  for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < col_cnt; col_idx++) {
    if (OB_ISNULL(datums[col_idx])) {
      continue;
    }
    const Datum *cur_datums = datums[col_idx];
    for (int64_t i = 0; OB_SUCC(ret) && i < size; i++) {
      int64_t payload = 0;
      if (OB_FAIL(exprs.at(col_idx)->get_datum_copy_plan().legacy_payload_size(cur_datums[selector[i]], payload))) {
        break;
      } else if (payload > UINT32_MAX - size_array[i]) {
        ret = OB_SIZE_OVERFLOW;
        break;
      } else { size_array[i] += payload; }
    }
  }

  // allocate block and assign stored rows
  int64_t idx = 0;
  OZ(ensure_init());
  while (idx < size && OB_SUCC(ret)) {
    if (OB_FAIL(ensure_write(size_array[idx]))) {
      LOG_WARN("ensure write block failed", K(ret), K(size_array[idx]), K(col_cnt), K(size));
    } else {
      int64_t rows = 0;
      for (int64_t i = idx; i < size; i++) {
        const int64_t remain = get_remain();
        if (size_array[i] <= remain) {
          char *buf = get_cur_buf();
          if (OB_ISNULL(buf)) {
            ret = OB_ERR_UNEXPECTED;
          }
          ObChunkDatumStore::StoredRow *srow = reinterpret_cast<ObChunkDatumStore::StoredRow *>(buf);
          stored_rows[i] = srow;
          srow->cnt_ = col_cnt;
          srow->row_size_ = base_row_size;
          rows += 1;
          advance(size_array[i]);
        } else {
          break;
        }
      }
      if (OB_SUCC(ret)) {
        for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < col_cnt; col_idx++) {
          if (OB_ISNULL(exprs.at(col_idx))) {
            for (int64_t i = idx; i < idx + rows; i++) {
              stored_rows[i]->stored_cells()[col_idx].set_null();
            }
            continue;
          }
          const DatumCopyPlan plan = exprs.at(col_idx)->get_datum_copy_plan();
          for (int64_t row = idx; OB_SUCC(ret) && row < idx + rows; ++row) {
            ObChunkDatumStore::StoredRow *stored = stored_rows[row];
            int64_t position = stored->row_size_;
            if (OB_FAIL(StoredDatumCodec::encode_native_copy(datums[col_idx][selector[row]],
                  stored->stored_cells()[col_idx], plan, reinterpret_cast<char *>(stored),
                  size_array[row], position))) {
            } else { stored->row_size_ = position; }
          }
        }
      }

      idx += rows;
    }
  }

  return ret;
}

int ObDefaultBlockWriter::inner_add_row(const blocksstable::ObStorageDatum *storage_datums, const int64_t column_count,
                                        const int64_t extra_size, ObChunkDatumStore::StoredRow **dst_sr)
{
  int ret = OB_SUCCESS;
  int64_t head_size = sizeof(ObChunkDatumStore::StoredRow);
  int64_t datum_size = sizeof(StoredDatumCell) * column_count;
  int64_t pos = head_size + datum_size + extra_size;
  ObChunkDatumStore::StoredRow *sr = static_cast<ObChunkDatumStore::StoredRow *>((void*)get_cur_buf());
  if (OB_ISNULL(sr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    sr->cnt_ = column_count;
    for (int64_t i = 0; OB_SUCC(ret) && i < column_count; ++i) {
      // ObStorageDatum still embeds the legacy pointer Datum. Adapt only at
      // this input boundary; its prefix is not the StoredRow's cell format.
      const ObDatum &legacy = static_cast<const ObDatum &>(storage_datums[i]);
      const int64_t payload_position = pos;
      ret = StoredDatumCodec::encode_legacy_copy(legacy, sr->stored_cells()[i],
                                                get_cur_buf(), get_remain(), pos);
      if (OB_SUCC(ret)) {
        // Preserve this writer's row-relative reference even for NULL cells.
        sr->stored_cells()[i].ptr_ = get_cur_buf() + payload_position;
      }
    }
    if (OB_SUCC(ret)) {
      sr->row_size_ = pos;
      if (OB_FAIL(advance(sr->row_size_))) {
      } else if (OB_NOT_NULL(dst_sr)) {
        *dst_sr = sr;
      }
    }
  }
  return ret;
}

// before call this function -- we need to ensure the size if enough.
int ObDefaultBlockWriter::inner_add_row(const common::ObIArray<ObExpr*> &exprs, ObEvalCtx &ctx, ObChunkDatumStore::StoredRow **stored_row)
{
  int ret = OB_SUCCESS;
  ObChunkDatumStore::StoredRow *sr = nullptr;
  char *buffer = get_cur_buf();
  if (OB_ISNULL(buffer)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObChunkDatumStore::StoredRow::build(sr, exprs, ctx, buffer, get_remain()))) {
  } else if (OB_FAIL(advance(sr->row_size_))) {
  } else if (OB_NOT_NULL(stored_row)) {
    *stored_row = sr;
  }
  return ret;
}

int ObDefaultBlockWriter::get_row_stored_size(const common::ObIArray<ObExpr*> &exprs, ObEvalCtx &ctx, uint64_t &size)
{
  int ret = OB_SUCCESS;
  size = 0;
  ObExpr *expr = nullptr;
  common::Datum *datum = nullptr;
  size = sizeof(StoredDatumCell) * exprs.count() + sizeof(ObChunkDatumStore::StoredRow);
  for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); ++i) {
    expr = exprs.at(i);
    if (OB_ISNULL(expr)) {
    } else if (OB_FAIL(expr->eval(ctx, datum))) {
    } else {
      int64_t payload = 0;
      if (OB_FAIL(expr->get_datum_copy_plan().legacy_payload_size(*datum, payload))) {
      } else { size += payload; }
    }
  }
  return ret;
}

int ObDefaultBlockWriter::close()
{
  int ret = OB_SUCCESS;
  inited_ = false;
  return ret;
}

int ObDefaultBlockWriter::ensure_write(const common::ObIArray<ObExpr*> &exprs, ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  uint64_t row_size;
  if (OB_FAIL(get_row_stored_size(exprs, ctx, row_size))) {
  } else if (OB_FAIL(ensure_write(row_size))) {
  }
  return ret;
}

int ObDefaultBlockWriter::ensure_write(const ObChunkDatumStore::StoredRow &sr)
{
  int ret = OB_SUCCESS;
  uint64_t row_size;
  if (OB_FAIL(ensure_write(sr.row_size_))) {
  }
  return ret;
}

int ObDefaultBlockWriter::block_unswizzling(ObTempBlockStore::Block *blk)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(blk)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t cur_row = 0;
    int64_t cur_pos = 0;
    const int64_t buf_size = blk->raw_size_ - sizeof(ObTempBlockStore::Block);
    while (cur_row < blk->cnt_ && cur_pos < buf_size) {
      ObChunkDatumStore::StoredRow *sr = reinterpret_cast<ObChunkDatumStore::StoredRow*>(blk->payload_ + cur_pos);
      sr->unswizzling();
      cur_pos += sr->row_size_;
      cur_row++;
    }
  }

  return ret;
}

int ObDefaultBlockWriter::prepare_blk_for_write(ObTempBlockStore::Block *blk)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(block_unswizzling(blk))) {
  }
  return ret;
}

int ObDefaultBlockWriter::ensure_write(const blocksstable::ObStorageDatum *storage_datums,
                                       const int64_t column_count,
                                       const int64_t extra_size)
{
  int ret = OB_SUCCESS;
  uint64_t row_size;
  if (OB_FAIL(get_row_stored_size(storage_datums, column_count, extra_size, row_size))) {
  } else if (OB_FAIL(ensure_write(row_size))) {
  }

  return ret;
}

int ObDefaultBlockWriter::get_row_stored_size(const blocksstable::ObStorageDatum *storage_datums,
                                              const int64_t column_count,
                                              const int64_t extra_size, uint64_t &size)
{
  int ret = OB_SUCCESS;
  int64_t head_size = sizeof(ObChunkDatumStore::StoredRow);
  int64_t datum_size = sizeof(StoredDatumCell) * column_count;
  int64_t data_size = 0;
  for (int64_t i = 0; i < column_count; ++i) {
    data_size += storage_datums[i].len_;
  }
  size = head_size + datum_size + extra_size + data_size;
  return ret;
}

int ObDefaultBlockWriter::ensure_write(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (is_overflow(size)) {
    // need to alloc a new block to write.
    int64_t new_blk_size = size < DEFAULT_BUF_SIZE ? DEFAULT_BUF_SIZE : size;
    ObTempBlockStore::Block *tmp_blk = nullptr;
    if (OB_FAIL(store_->new_block(new_blk_size, tmp_blk, true))) {
    } else if (OB_ISNULL(tmp_blk)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      cur_blk_ = tmp_blk;
    }
  }

  return ret;
}

}
}
