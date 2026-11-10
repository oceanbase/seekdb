/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include "common/datum/datum_value.h"
#include "common/datum/datum_copy_plan.h"
namespace oceanbase { namespace common {
// Native view of the historical typed Datum projection. The caller chooses
// whether NULL preserves the declared collation (the two-argument contract).
// Descriptors/payloads are borrowed from source; retaining users must clone.
inline int project_datum_value_view(const ObDatum &datum,const SqlTypeMeta &meta,
    SqlValueDatumMapType mapping,const SqlTypeMeta &previous_meta,DatumValue &value,
    bool keep_null_collation=false)
{
  int ret = OB_SUCCESS;
  DatumValue candidate;
  candidate.meta_ = meta;
  ObDatum projected = datum;
  if (datum.is_null()) {
    candidate.meta_ = previous_meta;
    candidate.set_null();
    projected.set_null();
    if (keep_null_collation) {
      if (meta.is_null()) {
        // A NULL value can still carry non-default type metadata. Preserve
        // the complete declared metadata in this case.
        candidate.meta_ = meta;
      } else {
        candidate.meta_.set_collation_level(meta.get_collation_level());
        candidate.meta_.set_collation_type(meta.get_collation_type());
      }
    }
  } else if (mapping == OBJ_DATUM_MAPPING_MAX) {
    ret = OB_ERR_UNEXPECTED;
  } else if (mapping == OBJ_DATUM_DECIMALINT && meta.get_scale() == SCALE_UNKNOWN_YET) {
    // A negative scale is valid for DecimalInt values (for example, -9).
    // Reject only the sentinel that means the scale was not specified.
    ret = OB_ERR_UNEXPECTED;
  } else if (mapping == OBJ_DATUM_NULL) {
    candidate.set_null();
    projected.set_null();
  } else if (mapping == OBJ_DATUM_EXTEND) {
    if (meta.is_ext_sql_array() && !datum.is_ext()) {
      // Native SQL-array Datums carry the DatumParameterArray address in an
      // ordinary eight-byte Datum; unlike legacy Ext values, there is no
      // eight-byte type header to skip.
      if (datum.ptr_ == nullptr || datum.len_ != sizeof(int64_t)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        candidate.meta_ = meta;
      }
    } else if (datum.is_native_extend()) {
      candidate.meta_.set_ext();
      candidate.meta_.set_extend_type(datum.get_extend_type());
      candidate.meta_.set_cs_level(datum.get_extend_collation_level());
      candidate.meta_.set_cs_type(datum.get_extend_collation());
    } else if (datum.is_ext() && datum.len_ == DATUM_LEGACY_EXTEND_SIZE) {
      MEMCPY(&candidate.meta_, datum.ptr_, sizeof(candidate.meta_));
      if (candidate.meta_.is_null()) { projected.set_null(); }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCC(ret) && candidate.meta_.is_ext_sql_array() && datum.is_ext()) {
      // Array values carry a native descriptor address, while expression frames
      // carry that same address in an extended row Datum payload.
      projected.ptr_ = datum.ptr_ + (datum.is_native_extend() ? 0 : 8);
      projected.pack_ = sizeof(int64_t);
    }
  } else if (mapping == OBJ_DATUM_8BYTE_DATA
             || mapping == OBJ_DATUM_4BYTE_DATA
             || mapping == OBJ_DATUM_1BYTE_DATA) {
    // Ordinary typed projection reads the indicated scalar width, even when
    // storage supplied native Ext bytes. The resulting value is a scalar,
    // while enhanced storage projection handles Ext as a distinct type.
    projected.pack_ = ObDatum::get_reserved_size(mapping);
  } else if (mapping == OBJ_DATUM_STRING) {
    projected.pack_ = datum.len_;
    if (datum.is_outrow()) { candidate.meta_.set_outrow(); }
  }
  if (OB_SUCC(ret)) { ret = candidate.assign_legacy_borrowed(projected, candidate.meta_); }
  if (OB_SUCC(ret)) { value = candidate; }
  return ret;
}

// The source type selects the native representation; result_meta retains the
// historical projection (including FK target metadata and NULL collation).
// Ordinary values borrow source bytes. A required legacy wrapper/coefficient
// normalization instead lives in wrapper_owner, never in a local scratch slot.
// Neither path retains a referenced resource; its existing owner must outlive
// the view. Failure preserves value, although failed allocations remain owned
// by wrapper_owner until that owner's normal reset.
inline int project_native_datum_value_view(const Datum &datum, const SqlType &source_type,
    const SqlTypeMeta &result_meta, SqlValueDatumMapType mapping,
    const SqlTypeMeta &previous_meta, DatumValue &value, ObIAllocator &wrapper_owner,
    bool keep_null_collation = false)
{
  const DatumCopyPlan plan(source_type);
  if (datum.is_inline_extended()) {
    // Projection may change the physical interpretation or metadata. Preserve
    // that result, not the original bits, without retaining a scratch view.
    alignas(8) char scratch[64];
    ObDatum legacy;
    DatumValue projected, candidate;
    int ret = plan.legacy_view(datum, legacy, scratch, sizeof(scratch));
    if (OB_SUCC(ret)) {
      ret = project_datum_value_view(legacy, result_meta, mapping, previous_meta,
                                    projected, keep_null_collation);
    }
    if (OB_SUCC(ret)) { ret = candidate.deep_copy(projected, wrapper_owner); }
    if (OB_SUCC(ret)) { value = candidate; }
    return ret;
  }
  ObDatum legacy;
  int ret = plan.borrow_legacy(datum, legacy);
  if (ret == OB_NOT_SUPPORTED) {
    int64_t bytes = 0;
    ret = plan.legacy_payload_size(datum, bytes);
    if (OB_SUCC(ret)) {
      char *buffer = bytes == 0 ? nullptr : static_cast<char *>(wrapper_owner.alloc(bytes));
      if (bytes > 0 && buffer == nullptr) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        int64_t position = 0;
        ret = plan.export_legacy(datum, legacy, buffer, bytes, position);
      }
    }
  }
  if (OB_SUCC(ret)) {
    ret = project_datum_value_view(legacy, result_meta, mapping, previous_meta,
                                  value, keep_null_collation);
  }
  return ret;
}
} }
