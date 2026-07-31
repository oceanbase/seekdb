/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_ENGINE_BASIC_OB_SPILL_BATCH_SPOOL_H_
#define OCEANBASE_QUERY_API_ENGINE_BASIC_OB_SPILL_BATCH_SPOOL_H_

#include <stdint.h>
#include <limits.h>
#include "common/object/ob_obj_type.h"
#include "lib/compress/ob_compress_util.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{
class ObIVector;
template <typename T> class ObIArray;
}
namespace query
{

// A storage-neutral description used to materialize spill output vectors.
struct ObSpillColumnDesc
{
  ObSpillColumnDesc()
    : type_(common::ObMaxType), scale_(0), precision_(0)
  {}

  common::ObObjType type_;
  int16_t scale_;
  int16_t precision_;
  TO_STRING_KV(K_(type), K_(scale), K_(precision));
};

// Input vectors are read only during append_batch().  Output vectors are
// borrowed from the spool and remain valid until the next next_batch() call or
// until the spool is destroyed.
struct ObSpillBatchView
{
  ObSpillBatchView() : vectors_(nullptr), row_count_(0) {}
  ObSpillBatchView(const common::ObIArray<common::ObIVector *> &vectors,
                   const int64_t row_count)
    : vectors_(&vectors), row_count_(row_count)
  {}

  const common::ObIArray<common::ObIVector *> *vectors_;
  int64_t row_count_;
};

struct ObSpillBatchSpoolOptions
{
  ObSpillBatchSpoolOptions()
    : max_batch_size_(0), resident_memory_limit_(INT64_MAX),
      rotation_threshold_(INT64_MAX), dir_id_(0),
      compressor_type_(common::NONE_COMPRESSOR), async_read_(true)
  {}

  int64_t max_batch_size_;
  int64_t resident_memory_limit_;
  int64_t rotation_threshold_;
  int64_t dir_id_;
  common::ObCompressorType compressor_type_;
  bool async_read_;
};

struct ObSpillBatchAppendResult
{
  ObSpillBatchAppendResult() : rotation_recommended_(false) {}
  bool rotation_recommended_;
};

enum ObSpillBatchSpoolState
{
  SPILL_BATCH_WRITING = 0,
  SPILL_BATCH_SEALED,
  SPILL_BATCH_READING,
  SPILL_BATCH_EXHAUSTED,
  SPILL_BATCH_FAILED
};

struct ObSpillBatchSpoolStats
{
  ObSpillBatchSpoolStats()
    : row_count_(0), resident_bytes_(0), spilled_bytes_(0),
      state_(SPILL_BATCH_WRITING), first_error_(0)
  {}

  int64_t row_count_;
  int64_t resident_bytes_;
  int64_t spilled_bytes_;
  ObSpillBatchSpoolState state_;
  int first_error_;
};

// Single-writer/single-reader spool.  append_batch() is valid only while
// WRITING.  seal() is idempotent and hands ownership from writer to reader.
// next_batch() is valid after seal; once it returns OB_ITER_END every later
// call also returns OB_ITER_END.  The first non-ITER_END operation error is
// latched and returned by all later operations.
class ObISpillBatchSpool
{
public:
  virtual ~ObISpillBatchSpool() {}
  virtual int append_batch(const ObSpillBatchView &batch,
                           ObSpillBatchAppendResult &result) = 0;
  virtual int seal() = 0;
  virtual int next_batch(ObSpillBatchView &batch) = 0;
  virtual ObSpillBatchSpoolStats get_stats() const = 0;
};

// The factory must outlive every spool it creates.  destroy() accepts nullptr
// and must clear the caller's pointer; each non-null spool is destroyed once.
class ObISpillBatchSpoolFactory
{
public:
  virtual ~ObISpillBatchSpoolFactory() {}
  virtual int create(const common::ObIArray<ObSpillColumnDesc> &columns,
                     const ObSpillBatchSpoolOptions &options,
                     ObISpillBatchSpool *&spool) = 0;
  virtual void destroy(ObISpillBatchSpool *&spool) = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_BASIC_OB_SPILL_BATCH_SPOOL_H_
