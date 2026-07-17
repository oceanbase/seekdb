/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OB_FTS_STOP_TOKEN_CHECK_H_
#define OB_FTS_STOP_TOKEN_CHECK_H_

#include "lib/allocator/page_arena.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/hash/ob_hashset.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "storage/fts/ob_fts_struct.h"

namespace oceanbase
{
namespace storage
{

static const int64_t FTS_STOP_TOKEN_MAX_LENGTH = 10;
static const char OB_STOP_TOKEN_TABLE_UTF8[][FTS_STOP_TOKEN_MAX_LENGTH] = {
  u8"a", u8"about", u8"an", u8"are", u8"as", u8"at", u8"be", u8"by",
  u8"com", u8"de", u8"en", u8"for", u8"from", u8"how", u8"i", u8"in",
  u8"is", u8"it", u8"la", u8"of", u8"on", u8"or", u8"that", u8"the",
  u8"this", u8"to", u8"was", u8"what", u8"when", u8"where", u8"who",
  u8"will", u8"with", u8"und", u8"www"
};

typedef common::hash::ObHashSet<
    ObFTToken,
    common::hash::NoPthreadDefendMode,
    common::hash::hash_func<ObFTToken>,
    common::hash::equal_to<ObFTToken>,
    common::hash::SimpleAllocer<
        typename common::hash::HashSetTypes<ObFTToken>::AllocType,
        common::hash::NodeNumTraits<
            typename common::hash::HashSetTypes<ObFTToken>::AllocType>::NODE_NUM,
        common::hash::NoPthreadDefendMode>> ObStopTokenTable;

class ObStopTokenChecker final
{
public:
  ObStopTokenChecker()
      : is_inited_(false), collation_type_(CS_TYPE_INVALID), stop_token_table_(nullptr)
  {}
  ~ObStopTokenChecker() { reset(); }
  int init(const ObCollationType coll, ObStopTokenTable *stop_token_table);
  void reset()
  {
    is_inited_ = false;
    collation_type_ = CS_TYPE_INVALID;
    stop_token_table_ = nullptr;
  }
  int check_is_stop_token(const ObFTToken &token, bool &is_stop_token) const;

private:
  bool is_inited_;
  ObCollationType collation_type_;
  ObStopTokenTable *stop_token_table_;
};

class ObStopTokenCheckerGen final
{
public:
  ObStopTokenCheckerGen()
      : is_inited_(false), allocator_("FTStopToken"), lock_(), stop_token_tables_()
  {}
  ~ObStopTokenCheckerGen() { reset(); }

  int init();
  void reset();
  int get_stop_token_checker_by_coll(const ObCollationType coll,
                                     ObStopTokenChecker &stop_token_checker);

private:
  static const int64_t DEFAULT_STOP_TOKEN_TABLE_CAPACITY = 64;
  typedef common::hash::ObHashMap<uint64_t, ObStopTokenTable *> StopTokenHashMap;

  int generate_stop_token_table(const ObCollationType coll);
  int convert_charset(const ObString &src,
                      const ObCollationType from_coll,
                      const ObCollationType to_coll,
                      ObString &converted);

private:
  bool is_inited_;
  ObArenaAllocator allocator_;
  common::TCRWLock lock_;
  StopTokenHashMap stop_token_tables_;
};

} // namespace storage
} // namespace oceanbase

#endif // OB_FTS_STOP_TOKEN_CHECK_H_
