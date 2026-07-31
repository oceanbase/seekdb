/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_DAS_OB_BLOCK_MAX_SPEC_ACCESS_H_
#define OCEANBASE_QUERY_API_DAS_OB_BLOCK_MAX_SPEC_ACCESS_H_

#include <stdint.h>
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace sql
{
struct ObDASIRScanCtDef;
struct ObDASVecAuxScanCtDef;
}
namespace query
{

struct ObBlockMaxColumnView
{
  int32_t store_index_;
  uint8_t statistic_type_;
  int32_t projector_;
};

struct ObTextBlockMaxSpecView
{
  int64_t column_count_;
  int32_t min_domain_id_index_;
  int32_t max_domain_id_index_;
  int32_t token_frequency_index_;
  int32_t document_length_index_;
  common::ObObjMeta domain_id_meta_;
  common::ObObjMeta dimension_meta_;
};

struct ObVectorBlockMaxSpecView
{
  int64_t column_count_;
  int32_t min_domain_id_index_;
  int32_t max_domain_id_index_;
  int32_t score_index_;
  common::ObObjMeta domain_id_meta_;
  common::ObObjMeta dimension_meta_;
};

int get_text_block_max_spec(
    const sql::ObDASIRScanCtDef &ctdef,
    ObTextBlockMaxSpecView &view);
int get_text_block_max_column(
    const sql::ObDASIRScanCtDef &ctdef,
    int64_t index,
    ObBlockMaxColumnView &view);
int get_vector_block_max_spec(
    const sql::ObDASVecAuxScanCtDef &ctdef,
    ObVectorBlockMaxSpecView &view);
int get_vector_block_max_column(
    const sql::ObDASVecAuxScanCtDef &ctdef,
    int64_t index,
    ObBlockMaxColumnView &view);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_DAS_OB_BLOCK_MAX_SPEC_ACCESS_H_
