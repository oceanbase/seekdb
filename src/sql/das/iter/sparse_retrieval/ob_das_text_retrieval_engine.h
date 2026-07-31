/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef OCEANBASE_SQL_DAS_TEXT_RETRIEVAL_ENGINE_H_
#define OCEANBASE_SQL_DAS_TEXT_RETRIEVAL_ENGINE_H_

#include <stdint.h>

#include "sql/das/iter/sparse_retrieval/ob_das_tr_merge_iter.h"

namespace oceanbase
{
namespace sql
{

// Query-owned quarantine boundary for the existing physical retrieval
// implementation.  Its first purpose is dependency isolation: SQL owns the
// request, result expressions and DAS lifecycle, while the final composition
// root owns every posting-list, scorer, scan-param and merge implementation.
//
// The lifecycle is intentionally coarse grained.  It mirrors the existing DAS
// contract while modes are moved behind ObRetrievalProgram one by one; no
// storage implementation type is allowed to appear here.
class ObIDASTextRetrievalEngine
{
public:
  virtual ~ObIDASTextRetrievalEngine() = default;

  virtual int init(ObDASTRMergeIterParam &param) = 0;
  virtual int bind_source_tree(ObDASIter **children, const uint32_t child_count) = 0;
  virtual int set_related_tablet_ids(
      const ObDASFTSTabletID &related_tablet_ids) = 0;
  virtual int do_table_scan() = 0;
  virtual int reuse() = 0;
  virtual int rescan() = 0;
  virtual int get_next_row() = 0;
  virtual int get_next_rows(int64_t &count, const int64_t capacity) = 0;
  virtual int set_lookup_keys(
      const common::ObIArray<std::pair<ObDocIdExt, int>> &virtual_rangekeys,
      const int64_t batch_size) = 0;
  virtual bool is_taat_mode() const = 0;
  virtual int get_query_max_score(double &score) = 0;

  // Implementations are allocator-owned and must detach the borrowed DAS
  // source tree before releasing their physical state.
  virtual void destroy() = 0;
};

typedef int (*ObDASTextRetrievalEngineFactory)(
    common::ObIAllocator &allocator,
    ObIDASTextRetrievalEngine *&engine);
typedef int (*ObDASTextRetrievalQueryBuilder)(
    const ObDASIRScanCtDef *ir_ctdef,
    ObDASIRScanRtDef *ir_rtdef,
    common::ObIAllocator &allocator,
    ObArray<ObString> &query_tokens,
    ObArray<double> &boost_values,
    ObFtsEvalNode *&root_node,
    bool &has_duplicate_boolean_tokens);

// The dispatcher is defined by query.  The final composition root installs
// exactly one provider during process initialization, which keeps the link
// dependency pointing from composition to query.  Missing registration fails
// closed; there is no hidden legacy fallback in the query module.
int install_das_text_retrieval_engine_factory(
    ObDASTextRetrievalEngineFactory factory,
    ObDASTextRetrievalQueryBuilder query_builder);
int create_das_text_retrieval_engine(
    common::ObIAllocator &allocator,
    ObIDASTextRetrievalEngine *&engine);
int build_das_text_retrieval_query(
    const ObDASIRScanCtDef *ir_ctdef,
    ObDASIRScanRtDef *ir_rtdef,
    common::ObIAllocator &allocator,
    ObArray<ObString> &query_tokens,
    ObArray<double> &boost_values,
    ObFtsEvalNode *&root_node,
    bool &has_duplicate_boolean_tokens);

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_DAS_TEXT_RETRIEVAL_ENGINE_H_
