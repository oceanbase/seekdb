/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ROW_TO_EXPR_PROJECTOR_H_
#define OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ROW_TO_EXPR_PROJECTOR_H_

#include "common/datum/ob_datum.h"
#include "lib/container/ob_se_array.h"
#include "query/engine/basic/ob_pushdown_filter.h"

namespace oceanbase
{
namespace blocksstable
{
struct ObStorageDatum;
}
namespace storage
{

// Query-owned projection protocol invoked by the data plane after a scan.
// The storage namespace is retained temporarily for source compatibility;
// ownership is defined by this query API header.
class ObRow2ExprsProjector
{
public:
  explicit ObRow2ExprsProjector(common::ObIAllocator &allocator)
    : other_idx_(0),
      has_virtual_(false),
      op_(nullptr),
      outputs_(common::OB_MALLOC_NORMAL_BLOCK_SIZE, common::ModulePageAllocator(allocator))
  {}
  ~ObRow2ExprsProjector() { destroy(); }

  int init(const sql::ObExprPtrIArray &exprs,
           sql::ObPushdownOperator &op,
           const common::ObIArray<int32_t> &projector);
  int project(const sql::ObExprPtrIArray &exprs,
              const blocksstable::ObStorageDatum *datums,
              int16_t *nop_pos,
              int64_t &nop_cnt);
  void destroy() { outputs_.reset(); }
  bool has_virtual() const { return has_virtual_; }

private:
  struct Item
  {
    int32_t obj_idx_;
    int32_t expr_idx_;
    sql::ObDatum *datum_;
    sql::ObEvalInfo *eval_info_;
    sql::ObBitVector *eval_flags_;
    const char *data_;

    Item() = default;
    DECLARE_TO_STRING;
  };

  template <common::ObObjDatumMapType OBJ_DATUM_MAP_TYPE, bool NEED_RESET_PTR>
  struct MapConvert
  {
    int32_t start_;
    int32_t end_;
    static const common::ObObjDatumMapType map_type_ = OBJ_DATUM_MAP_TYPE;

    MapConvert() : start_(0), end_(0) {}
    OB_INLINE void project(const Item *items,
                           const blocksstable::ObStorageDatum *datums,
                           int16_t *nop_pos,
                           int64_t &nop_cnt) const;
    OB_INLINE void project_batch_datum(const Item *items,
                                       const blocksstable::ObStorageDatum *datums,
                                       int16_t *nop_pos,
                                       int64_t &nop_cnt,
                                       int64_t idx) const;
  };

  MapConvert<common::OBJ_DATUM_NUMBER, true> num_;
  MapConvert<common::OBJ_DATUM_STRING, false> str_;
  MapConvert<common::OBJ_DATUM_8BYTE_DATA, true> int_;
  int32_t other_idx_;
  bool has_virtual_;
  sql::ObPushdownOperator *op_;
  common::ObSEArray<Item, 4> outputs_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ROW_TO_EXPR_PROJECTOR_H_
