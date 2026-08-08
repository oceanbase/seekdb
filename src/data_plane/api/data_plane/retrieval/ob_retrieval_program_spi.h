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

#ifndef OCEANBASE_DATA_PLANE_RETRIEVAL_OB_RETRIEVAL_PROGRAM_SPI_H_
#define OCEANBASE_DATA_PLANE_RETRIEVAL_OB_RETRIEVAL_PROGRAM_SPI_H_

#include "data_plane/retrieval/ob_retrieval_program.h"

namespace oceanbase
{
namespace data_plane
{
namespace detail
{

// Provider-side internal seam.  An execution is coarse-grained on purpose:
// posting cursors, scorers and algorithm objects are never provider ports.
class ObIRetrievalExecution
{
public:
  virtual ~ObIRetrievalExecution() = default;
  // query and run remain valid until this execution is destroyed.  The
  // provider owns each returned batch until the next non-const execution call.
  virtual int next_batch(
      const int64_t max_rows, ObRetrievalBatchView &batch) = 0;
  // Available immediately after open(); callers may ask before the first row.
  virtual int query_max_score(double &score) = 0;
  virtual ObRetrievalResultOrder result_order() const = 0;
  virtual void destroy() = 0;
};

// Double dispatch prevents fake opacity: the binding must create an execution
// from neutral query/run views.  There is deliberately no kind(), native
// handle, RTTI hook or templated downcast escape hatch.
class ObIRetrievalCorpusBinding
{
public:
  virtual ~ObIRetrievalCorpusBinding() = default;
  // Failure-atomic staging contract: open() must not mutate or invalidate any
  // previously opened execution.  Old and staged executions may coexist until
  // the facade commits the replacement.  On failure execution remains null.
  virtual int open(
      const ObRetrievalCompileRequest &query,
      const ObRetrievalRunRequest &run,
      ObIRetrievalExecution *&execution) = 0;
  virtual void destroy() = 0;
};

} // namespace detail

// Production and in-memory providers derive from this factory.  create() is
// failure-atomic: the provider retains its resources and corpus stays empty
// unless a complete binding is returned.
class ObRetrievalCorpusFactory
{
public:
  virtual ~ObRetrievalCorpusFactory() = default;
  int create(ObRetrievalCorpus &corpus);

protected:
  virtual int create_binding(
      detail::ObIRetrievalCorpusBinding *&binding) = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_RETRIEVAL_OB_RETRIEVAL_PROGRAM_SPI_H_
