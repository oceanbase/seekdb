/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OB_FT_TOKEN_PROCESSOR_H_
#define OB_FT_TOKEN_PROCESSOR_H_

#include "storage/fts/ob_fts_stop_token_check.h"

namespace oceanbase
{
namespace storage
{

class ObFTParserProperty;

class ObFTTokenProcessor final
{
public:
  explicit ObFTTokenProcessor(ObIAllocator &scratch_allocator)
      : is_inited_(false), token_meta_(), token_map_(nullptr),
        non_stop_token_cnt_(0), min_token_size_(0), max_token_size_(0),
        flag_(), hash_func_(nullptr), cmp_func_(nullptr), stop_token_checker_(),
        scratch_allocator_(scratch_allocator)
  {}
  ~ObFTTokenProcessor() = default;

  int init(const ObFTParserProperty &property,
           const ObObjMeta &meta,
           const ObAddWordFlag &flag,
           ObFTTokenMap *token_map);
  void reset();
  void reuse();
  int process_token(const char *token,
                    const int64_t token_len,
                    const int64_t char_cnt,
                    const int64_t token_freq);
  OB_INLINE int64_t get_non_stop_token_count() const { return non_stop_token_cnt_; }

private:
  class UpdateTokenCallback final
  {
  public:
    explicit UpdateTokenCallback(const int64_t count) : count_(count) {}
    int operator()(ObFTTokenPair &pair)
    {
      pair.second.count_ += count_;
      return OB_SUCCESS;
    }
  private:
    int64_t count_;
  };

  bool is_min_max_token(const int64_t char_cnt) const;
  int groupby_token(const ObFTToken &token, const int64_t token_freq);

private:
  static const int64_t MAX_CHAR_COUNT_PER_TOKEN = 1024;
  bool is_inited_;
  ObObjMeta token_meta_;
  ObFTTokenMap *token_map_;
  int64_t non_stop_token_cnt_;
  int64_t min_token_size_;
  int64_t max_token_size_;
  ObAddWordFlag flag_;
  ObDatumHashFuncType hash_func_;
  ObDatumCmpFuncType cmp_func_;
  ObStopTokenChecker stop_token_checker_;
  ObIAllocator &scratch_allocator_;
};

} // namespace storage
} // namespace oceanbase

#endif // OB_FT_TOKEN_PROCESSOR_H_
