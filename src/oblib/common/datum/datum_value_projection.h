/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include "common/datum/datum_value.h"
namespace oceanbase { namespace common {
// Native view of the historical typed Datum projection. The caller chooses
// whether NULL preserves the declared collation (the two-argument contract).
// Descriptors/payloads are borrowed from source; retaining users must clone.
inline int project_datum_value_view(const ObDatum &datum,const SqlTypeMeta &meta,
    SqlValueDatumMapType mapping,const SqlTypeMeta &previous_meta,DatumValue &value,
    bool keep_null_collation=false)
{
  int ret = OB_SUCCESS;
  DatumValue candidate(datum, meta);
  if (datum.is_null()) {
    candidate.meta_ = previous_meta;
    candidate.set_null();
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
  } else if (mapping == OBJ_DATUM_EXTEND) {
    if (meta.is_ext_sql_array() && !datum.is_ext()) {
      // Native SQL-array Datums carry the DatumParameterArray address in an
      // ordinary eight-byte Datum; unlike legacy Ext values, there is no
      // eight-byte type header to skip.
      if (datum.ptr_ == nullptr || datum.len_ != sizeof(int64_t)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        candidate.datum_ = datum;
        candidate.meta_ = meta;
      }
    } else if (datum.is_native_extend()) {
      candidate.meta_.set_ext();
      candidate.meta_.set_extend_type(datum.get_extend_type());
      candidate.meta_.set_cs_level(datum.get_extend_collation_level());
      candidate.meta_.set_cs_type(datum.get_extend_collation());
    } else if (datum.is_ext() && datum.len_ == DATUM_LEGACY_EXTEND_SIZE) {
      MEMCPY(&candidate.meta_, datum.ptr_, sizeof(candidate.meta_));
      if (candidate.meta_.is_null()) { candidate.datum_.set_null(); }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCC(ret) && candidate.meta_.is_ext_sql_array() && datum.is_ext()) {
      // Array values carry a native descriptor address, while expression frames
      // carry that same address in an extended row Datum payload.
      candidate.datum_.ptr_ = datum.ptr_ + (datum.is_native_extend() ? 0 : 8);
      candidate.datum_.pack_ = sizeof(int64_t);
    }
  } else if (mapping == OBJ_DATUM_8BYTE_DATA
             || mapping == OBJ_DATUM_4BYTE_DATA
             || mapping == OBJ_DATUM_1BYTE_DATA) {
    // Ordinary typed projection reads the indicated scalar width, even when
    // storage supplied native Ext bytes. The resulting value is a scalar,
    // while enhanced storage projection handles Ext as a distinct type.
    candidate.datum_.pack_ = ObDatum::get_reserved_size(mapping);
  } else if (mapping == OBJ_DATUM_STRING) {
    candidate.datum_.pack_ = datum.len_;
    if (datum.is_outrow()) { candidate.meta_.set_outrow(); }
  }
  if (OB_SUCC(ret)) { value = candidate; }
  return ret;
}
} }
