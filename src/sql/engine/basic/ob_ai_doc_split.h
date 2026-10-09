/**
 * Copyright (c) 2026 OceanBase and/or its affiliates. All rights reserved.
 * MulanPubL v2.
 */

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_AI_DOC_SPLIT_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_AI_DOC_SPLIT_H_

#include "lib/string/ob_string.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace sql
{

// Parameters of AI_SPLIT_DOCUMENT(content[, parameters]).
// parameters is a flat json object, e.g. {"type":"text","by":"sentence","max":1,"overlap":0}
struct ObAiSplitParams
{
  enum DocType { DOC_TEXT = 0, DOC_MARKDOWN = 1 };
  enum SplitBy { BY_SENTENCE = 0, BY_WORD = 1 };

  ObAiSplitParams() : type_(DOC_MARKDOWN), by_(BY_SENTENCE), max_(1), overlap_(0) {}

  DocType type_;
  SplitBy by_;
  int64_t max_;      // max sentences per chunk (by=sentence) / window size in words (by=word)
  int64_t overlap_;  // words shared by adjacent windows (by=word only)

  TO_STRING_EMPTY();
};

struct ObAiSplitChunk
{
  ObAiSplitChunk() : chunk_id_(0), offset_(0), length_(0), text_() {}

  int64_t chunk_id_;
  int64_t offset_;    // byte offset of the chunk content within the original document
  int64_t length_;    // byte length of text_
  common::ObString text_;

  TO_STRING_EMPTY();
};

// Plain C-array container; buffer memory comes from the row allocator of the scan ctx.
struct ObAiSplitChunks
{
  ObAiSplitChunks() : count_(0), chunks_(NULL) {}

  int64_t count_;
  ObAiSplitChunk *chunks_;

  TO_STRING_EMPTY();
};

class ObAiDocSplitter
{
public:
  // Parse the optional parameters json. Missing keys keep their defaults.
  static int parse_params(const common::ObString &params_json, ObAiSplitParams &params);
  // Split content into chunks. Chunk text memory is allocated from alloc.
  static int split(const common::ObString &content,
                   const ObAiSplitParams &params,
                   common::ObIAllocator &alloc,
                   ObAiSplitChunks &out);

private:
  struct Span
  {
    Span() : start_(0), end_(0) {}
    Span(int64_t start, int64_t end) : start_(start), end_(end) {}
    int64_t start_;
    int64_t end_;

    TO_STRING_EMPTY();
  };

  static bool is_ws(char c);
  static bool is_terminator(char c);
  static bool is_heading_line(const common::ObString &content, int64_t start, int64_t end);
  // Split content[start,end) into sentence spans; a sentence keeps its terminator,
  // trailing whitespace belongs to no chunk.
  static int split_sentences(const common::ObString &content,
                             int64_t start, int64_t end,
                             common::ObIArray<Span> &sentences);
  // Group sentence spans into chunks of at most max sentences; chunk span =
  // [first.start_, last.end_) of the original content.
  static int emit_sentence_chunks(const common::ObString &content,
                                  const common::ObIArray<Span> &sentences,
                                  int64_t max_group,
                                  const common::ObString &heading,
                                  common::ObIAllocator &alloc,
                                  common::ObIArray<ObAiSplitChunk> &chunks);
  // Sliding word window over content[start,end): window = max_ words,
  // step = max_ - overlap_; the tail window emits the remaining words.
  static int emit_word_chunks(const common::ObString &content,
                              int64_t start, int64_t end,
                              const ObAiSplitParams &params,
                              const common::ObString &heading,
                              common::ObIAllocator &alloc,
                              common::ObIArray<ObAiSplitChunk> &chunks);
  static int build_chunk_text(const common::ObString &content,
                              int64_t start, int64_t end,
                              const common::ObString &heading,
                              common::ObIAllocator &alloc,
                              ObAiSplitChunk &chunk);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_BASIC_OB_AI_DOC_SPLIT_H_ */
