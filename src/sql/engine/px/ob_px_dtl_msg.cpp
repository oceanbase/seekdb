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

#include "ob_px_dtl_msg.h"
#include "common/datum/datum_parameter_codec.h"
#include "sql/engine/ob_physical_plan_ctx.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;

OB_SERIALIZE_MEMBER(ObPxUserErrorMsg, rcode_, msg_, warnings_);
OB_SERIALIZE_MEMBER(ObPxTabletInfo,
                    tablet_id_,
                    logical_row_count_,
                    physical_row_count_);
OB_SERIALIZE_MEMBER((ObPxTaskChSet, dtl::ObDtlChSet), sqc_id_, task_id_);
OB_SERIALIZE_MEMBER(ObPxPartChMapItem, first_, second_, third_);
OB_SERIALIZE_MEMBER(ObPxReceiveDataChannelMsg, child_dfo_id_, ch_sets_, ch_total_info_, has_filled_channel_);
OB_SERIALIZE_MEMBER(ObPxTransmitDataChannelMsg, ch_sets_, part_affinity_map_, ch_total_info_, has_filled_channel_);
OB_SERIALIZE_MEMBER(ObPxInitSqcResultMsg, dfo_id_, sqc_id_, rc_, task_count_, err_msg_);
OB_SERIALIZE_MEMBER(ObPxFinishSqcResultMsg, dfo_id_, sqc_id_, rc_, trans_result_,
                    sqc_affected_rows_, dml_row_info_, temp_table_id_,
                    interm_result_ids_, fb_info_, err_msg_, das_retry_rc_,
                    sqc_memstore_row_read_count_, sqc_ssstore_row_read_count_);
OB_SERIALIZE_MEMBER(ObPxFinishTaskResultMsg, dfo_id_, sqc_id_, task_id_, rc_);
OB_SERIALIZE_MEMBER(ObPxDmlRowInfo, row_match_count_, row_duplicated_count_, row_deleted_count_);
OB_SERIALIZE_MEMBER(ObPxTabletRange, tablet_id_, range_cut_, range_weights_);

int ObPxTaskChSet::assign(const ObPxTaskChSet &other)
{
  int ret = OB_SUCCESS;
  sqc_id_ = other.sqc_id_;
  task_id_ = other.task_id_;
  sm_group_id_ = other.sm_group_id_;
  if (OB_FAIL(dtl::ObDtlChSet::assign(other))) {
  }
  return ret;
}

void ObPxDmlRowInfo::set_px_dml_row_info(const ObPhysicalPlanCtx &plan_ctx)
{
  row_match_count_ = plan_ctx.get_row_matched_count();
  row_duplicated_count_ = plan_ctx.get_row_duplicated_count();
  row_deleted_count_ = plan_ctx.get_row_deleted_count();
}

int ObPxRangeCell::assign_stored_key_borrowed(const ObDatum &source)
{
  int ret = OB_SUCCESS;
  ObPxRangeCell candidate;
  if ((source.len_ > 0 && source.ptr_ == nullptr)
      || (source.is_ext() && source.len_ != DATUM_EXTEND_PAYLOAD_SIZE
                          && source.len_ != DATUM_LEGACY_EXTEND_SIZE)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!source.is_null() || source.len_ > 0) {
    ret = candidate.value_.set_reference(source.ptr_, source.len_);
  }
  if (OB_SUCC(ret)) {
    candidate.stored_pack_ = source.pack_;
    *this = candidate;
  }
  return ret;
}

int ObPxRangeCell::assign_stored_key_owned(const ObDatum &source, ObIAllocator &owner)
{
  ObPxRangeCell borrowed;
  int ret = borrowed.assign_stored_key_borrowed(source);
  if (OB_SUCC(ret)) { ret = deep_copy(borrowed, owner); }
  return ret;
}

int ObPxRangeCell::assign_expr_view(const Datum &source, const DatumCopyPlan &plan,
                                  void *scratch, int64_t capacity)
{
  int ret = OB_SUCCESS;
  ObDatum view;
  ObPxRangeCell candidate;
  if (capacity < 0 || capacity > UINT32_MAX) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(plan.legacy_view(source, view, scratch, static_cast<uint32_t>(capacity)))) {
  } else {
    candidate.meta_ = plan.get_type_meta();
    // Sentinels/resources are physical Extend values even when the SQL
    // comparison column has an ordinary scalar declaration.
    if (view.is_ext()) {
      candidate.meta_.set_ext();
      candidate.meta_.set_extend_type(view.get_extend_type());
      candidate.meta_.set_collation_level(static_cast<ObCollationLevel>(view.get_extend_collation_level()));
      candidate.meta_.set_collation_type(static_cast<ObCollationType>(view.get_extend_collation()));
    }
    if (OB_FAIL(DatumParameterCodec::borrow_native_value(view, candidate.meta_, candidate.value_))) {
    } else {
      candidate.opaque_ = false;
      *this = candidate;
    }
  }
  return ret;
}

int ObPxRangeCell::assign_expr_owned(const Datum &source, const DatumCopyPlan &plan,
                                   ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  alignas(8) char scratch[64];
  ObPxRangeCell borrowed;
  if (OB_FAIL(borrowed.assign_expr_view(source, plan, scratch, sizeof(scratch)))) {
  } else { ret = deep_copy(borrowed, owner); }
  return ret;
}

int ObPxRangeCell::bind_for_execution(const DatumCopyPlan &plan, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  alignas(8) char scratch[64];
  ObDatum physical;
  Datum semantic;
  if (OB_FAIL(export_stored_key(physical, scratch, sizeof(scratch)))) {
  } else if (OB_FAIL(plan.import_legacy(physical, semantic))) {
  } else { ret = assign_expr_owned(semantic, plan, owner); }
  return ret;
}

int ObPxRangeCell::export_stored_key(ObDatum &output, void *scratch, int64_t capacity) const
{
  int ret = OB_SUCCESS;
  if (opaque_) {
    ObDatum candidate;
    candidate.pack_ = stored_pack_;
    candidate.ptr_ = value_.is_present() ? static_cast<const char *>(value_.get_reference()) : nullptr;
    if (candidate.len_ > 0 && candidate.ptr_ == nullptr) { ret = OB_INVALID_ARGUMENT; }
    else { output = candidate; }
  } else { ret = DatumParameterCodec::export_native_value(value_, meta_, output, scratch, capacity); }
  return ret;
}

int ObPxRangeCell::payload_size(int64_t &bytes) const
{
  int ret = OB_SUCCESS;
  if (opaque_) {
    ObDatumDesc desc; desc.pack_ = stored_pack_;
    if (desc.len_ > 0 && value_.get_reference() == nullptr) { ret = OB_INVALID_ARGUMENT; }
    else { bytes = desc.len_; }
  } else { ret = DatumParameterCodec::native_span_size(value_, meta_, bytes); }
  return ret;
}

int ObPxRangeCell::deep_copy(const ObPxRangeCell &source, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  ObPxRangeCell candidate = source;
  int64_t bytes = 0;
  if (OB_FAIL(source.payload_size(bytes))) {
  } else if (!source.opaque_) {
    ret = DatumParameterCodec::copy_native_value_span(source.value_, source.meta_, owner, candidate.value_);
  } else if (!source.is_null() || bytes > 0) {
    char *memory = static_cast<char *>(owner.alloc(bytes > 0 ? bytes : 1));
    if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      if (bytes > 0) { MEMCPY(memory, source.value_.get_reference(), bytes); }
      ret = candidate.value_.set_reference(memory, bytes);
    }
  }
  if (OB_SUCC(ret)) { *this = candidate; }
  return ret;
}

int ObPxRangeCell::deep_copy(const ObPxRangeCell &source, char *buffer,
                           int64_t capacity, int64_t &position)
{
  int ret = OB_SUCCESS;
  ObPxRangeCell candidate = source;
  int64_t bytes = 0;
  int64_t staged_pos = position;
  if (position < 0 || capacity < position) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(source.payload_size(bytes))) {
  } else if (!source.opaque_) {
    ret = DatumParameterCodec::copy_native_span(source.value_, source.meta_, buffer,
                                               capacity, staged_pos, candidate.value_);
  } else if (bytes > capacity - position) { ret = OB_BUF_NOT_ENOUGH; }
  else if ((!source.is_null() || bytes > 0) && buffer == nullptr) { ret = OB_INVALID_ARGUMENT; }
  else if (!source.is_null() || bytes > 0) {
    if (bytes > 0) { MEMMOVE(buffer + position, source.value_.get_reference(), bytes); }
    ret = candidate.value_.set_reference(buffer + position, bytes);
    staged_pos += bytes;
  }
  if (OB_SUCC(ret)) { *this = candidate; position = staged_pos; }
  return ret;
}

int ObPxRangeCell::serialize(char *buf, int64_t length, int64_t &pos) const
{
  alignas(8) char scratch[64];
  ObDatum physical;
  int ret = export_stored_key(physical, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) { ret = physical.serialize(buf, length, pos); }
  return ret;
}

int ObPxRangeCell::deserialize(const char *buf, int64_t length, int64_t &pos)
{
  ObDatum physical;
  // Preserve the historical decoder's error cursor/prefix behavior.
  int ret = physical.deserialize(buf, length, pos);
  if (OB_SUCC(ret)) { ret = assign_stored_key_borrowed(physical); }
  return ret;
}

int64_t ObPxRangeCell::get_serialize_size() const
{
  alignas(8) char scratch[64];
  ObDatum physical;
  const int ret = export_stored_key(physical, scratch, sizeof(scratch));
  OB_ASSERT(ret == OB_SUCCESS);
  return physical.get_serialize_size();
}

ObPxTabletRange::ObPxTabletRange()
  : tablet_id_(OB_INVALID_ID), range_weights_(0), range_cut_()
{
  range_weights_ = 1;
}

void ObPxTabletRange::reset()
{
  tablet_id_ = OB_INVALID_ID;
  range_cut_.reset();
  range_weights_ = 1;
}

bool ObPxTabletRange::is_valid() const
{
  return tablet_id_ >= 0 && range_cut_.count() >= 0;
}

int ObPxTabletRange::assign(const ObPxTabletRange &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(range_cut_.assign(other.range_cut_))) {
  } else {
    tablet_id_ = other.tablet_id_;
    range_weights_ = other.range_weights_;
  }
  return ret;
}
