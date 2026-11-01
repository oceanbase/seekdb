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

#include "ob_values_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

ObValuesSpec::ObValuesSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type), row_store_(alloc)
{
}

OB_UNIS_DEF_SERIALIZE((ObValuesSpec, ObOpSpec), row_store_);
OB_UNIS_DEF_SERIALIZE_SIZE((ObValuesSpec, ObOpSpec), row_store_);

namespace {
// RowStore v2 carries logical values. Version 1 selects its bounded legacy
// image reader; neither format can be interpreted as the current C++ layout.
int decode_row_store_header(const char *buf, int64_t size, int64_t &pos,
                            int64_t &body_length)
{
  if (buf == nullptr || pos < 0 || size < pos) { return OB_DESERIALIZE_ERROR; }
  int64_t cursor = pos;
  int64_t fields[2] = {};
  for (int field = 0; field < 2; ++field) {
    uint64_t value = 0;
    bool complete = false;
    for (int byte = 0; byte < 10 && !complete; ++byte) {
      if (cursor >= size) { return OB_DESERIALIZE_ERROR; }
      const uint8_t part = static_cast<uint8_t>(buf[cursor++]);
      if (byte == 9 && (part & 0xfe) != 0) { return OB_DESERIALIZE_ERROR; }
      value |= static_cast<uint64_t>(part & 0x7f) << (7 * byte);
      complete = (part & 0x80) == 0;
    }
    if (!complete) { return OB_DESERIALIZE_ERROR; }
    fields[field] = static_cast<int64_t>(value);
  }
  if (fields[0] != 1 && fields[0] != 2) { return OB_NOT_SUPPORTED; }
  if (fields[1] < 0 || fields[1] > size - cursor) { return OB_DESERIALIZE_ERROR; }
  body_length = fields[1];
  pos = cursor;
  return OB_SUCCESS;
}
} // namespace

int ObValuesSpec::deserialize(DESERIAL_PARAMS)
{
  int64_t body_start = pos;
  int64_t body_length = 0;
  int ret = decode_row_store_header(buf, data_len, body_start, body_length);
  if (OB_SUCC(ret)) {
    int64_t body_pos = 0;
    ret = deserialize_(buf + body_start, body_length, body_pos);
    if (OB_SUCC(ret)) { pos = body_start + body_length; }
  }
  return ret;
}

OB_DESERIALIZE_NOHEADER(ObValuesSpec, ObOpSpec, _, row_store_);


ObValuesOp::ObValuesOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input)
{
}

int ObValuesOp::inner_open()
{
  int ret = OB_SUCCESS;
  row_store_it_ = MY_SPEC.row_store_.begin();
  DatumValue *cells = nullptr;
  if (OB_FAIL(DatumValueBuffer::allocate(ctx_.get_allocator(), MY_SPEC.output_.count(), cells))) {
  } else {
    cur_row_.cells_ = cells;
    cur_row_.count_ = MY_SPEC.output_.count();
  }
  return ret;
}

int ObValuesOp::inner_rescan()
{
  row_store_it_ = MY_SPEC.row_store_.begin();
  return ObOperator::inner_rescan();
}

int ObValuesOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(row_store_it_.get_next_row(cur_row_))) {
    if (OB_ITER_END != ret) {
    }
  } else {
    clear_evaluated_flag();
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.output_.count(); i++) {
      const DatumValue &cell = cur_row_.cells_[i];
      Datum &datum = MY_SPEC.output_.at(i)->locate_datum_for_write(eval_ctx_);
      ObExpr *expr = MY_SPEC.output_.at(i);
      char extended_payload[DATUM_EXTEND_PAYLOAD_SIZE];
      ObDatum projected;
      if (cell.is_null()) {
        datum.set_null();
      } else if (cell.get_type() != expr->datum_meta_.type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("type mismatch", K(ret), K(i), K(cell.get_type()), K(*expr));
      } else if (OB_FAIL(cell.to_row_datum(projected, extended_payload))) {
      } else {
        // Iterator values live until its next advance. Extend normalization
        // uses stack bytes, so publish those bytes in the expression frame.
        Datum value;
        if (OB_FAIL(expr->get_datum_copy_plan().import_legacy(projected, value))) {
        } else if (cell.is_ext()) {
          ret = expr->deep_copy_datum(eval_ctx_, value);
        } else { datum = value; }
        if (OB_SUCC(ret) && is_lob_storage(cell.get_type())) {
          ret = ob_adjust_lob_datum(get_exec_ctx(), value, cell.get_meta(),
              expr->obj_meta_, get_exec_ctx().get_allocator(), datum);
        }
        if (OB_SUCC(ret)) { expr->set_evaluated_projected(eval_ctx_); }
      }
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
