/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef OCEANBASE_DATA_PLANE_MEMTABLE_OB_WRITE_FLAG_H_
#define OCEANBASE_DATA_PLANE_MEMTABLE_OB_WRITE_FLAG_H_

#include <cstdint>

#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace concurrent_control
{

// Value passed across the query/data-plane write boundary.  The storage
// implementation may interpret it, but does not own its public layout.
struct ObWriteFlag
{
  #define OBWF_BIT_RESERVED_COMPAT_0    1
  #define OBWF_BIT_TABLE_LOCK           1
  #define OBWF_BIT_MDS                  1
  #define OBWF_BIT_DML_BATCH_OPT        1
  #define OBWF_BIT_INSERT_UP            1
  #define OBWF_BIT_WRITE_ONLY_INDEX     1
  #define OBWF_BIT_CHECK_ROW_LOCKED     1
  #define OBWF_BIT_LOB_AUX              1
  #define OBWF_BIT_SKIP_FLUSH_REDO      1
  #define OBWF_BIT_UPDATE_UK            1
  #define OBWF_BIT_UPDATE_PK_DOP        1
  #define OBWF_BIT_IMMEDIATE_CHECK      1
  #define OBWF_BIT_DELETE_INSERT        1
  #define OBWF_BIT_SNAPSHOT_OPT         1
  #define OBWF_BIT_RESERVED             50

  static const uint64_t OBWF_MASK_RESERVED_COMPAT_0 = (0x1UL << OBWF_BIT_RESERVED_COMPAT_0) - 1;
  static const uint64_t OBWF_MASK_TABLE_LOCK = (0x1UL << OBWF_BIT_TABLE_LOCK) - 1;
  static const uint64_t OBWF_MASK_MDS = (0x1UL << OBWF_BIT_MDS) - 1;
  static const uint64_t OBWF_MASK_DML_BATCH_OPT = (0x1UL << OBWF_BIT_DML_BATCH_OPT) - 1;
  static const uint64_t OBWF_MASK_INSERT_UP = (0x1UL << OBWF_BIT_INSERT_UP) - 1;
  static const uint64_t OBWF_MASK_WRITE_ONLY_INDEX = (0x1UL << OBWF_BIT_WRITE_ONLY_INDEX) - 1;
  static const uint64_t OBWF_MASK_CHECK_ROW_LOCKED = (0x1UL << OBWF_BIT_CHECK_ROW_LOCKED) - 1;

  union
  {
    uint64_t flag_;
    struct
    {
      uint64_t reserved_compat_0_    : OBWF_BIT_RESERVED_COMPAT_0;
      uint64_t is_table_lock_        : OBWF_BIT_TABLE_LOCK;
      uint64_t is_mds_               : OBWF_BIT_MDS;
      uint64_t is_dml_batch_opt_     : OBWF_BIT_DML_BATCH_OPT;
      uint64_t is_insert_up_         : OBWF_BIT_INSERT_UP;
      uint64_t is_write_only_index_  : OBWF_BIT_WRITE_ONLY_INDEX;
      uint64_t is_check_row_locked_  : OBWF_BIT_CHECK_ROW_LOCKED;
      uint64_t is_lob_aux_           : OBWF_BIT_LOB_AUX;
      uint64_t is_skip_flush_redo_   : OBWF_BIT_SKIP_FLUSH_REDO;
      uint64_t is_update_uk_         : OBWF_BIT_UPDATE_UK;
      uint64_t is_update_pk_dop_     : OBWF_BIT_UPDATE_PK_DOP;
      uint64_t immediate_row_check_  : OBWF_BIT_IMMEDIATE_CHECK;
      uint64_t is_delete_insert_     : OBWF_BIT_DELETE_INSERT;
      uint64_t use_snapshot_opt_     : OBWF_BIT_SNAPSHOT_OPT;
      uint64_t reserved_             : OBWF_BIT_RESERVED;
    };
  };

  ObWriteFlag() : flag_(0) {}
  void reset() { flag_ = 0; }
  inline bool is_table_lock() const { return is_table_lock_; }
  inline void set_is_table_lock() { is_table_lock_ = true; }
  inline bool is_mds() const { return is_mds_; }
  inline void set_is_mds() { is_mds_ = true; }
  inline bool is_dml_batch_opt() const { return is_dml_batch_opt_; }
  inline void set_is_dml_batch_opt() { is_dml_batch_opt_ = true; }
  inline bool is_insert_up() const { return is_insert_up_; }
  inline void set_is_insert_up() { is_insert_up_ = true; }
  inline bool is_write_only_index() const { return is_write_only_index_; }
  inline void set_is_write_only_index() { is_write_only_index_ = true; }
  inline bool is_check_row_locked() const { return is_check_row_locked_; }
  inline void set_check_row_locked() { is_check_row_locked_ = true; }
  inline bool is_lob_aux() const { return is_lob_aux_; }
  inline void set_lob_aux() { is_lob_aux_ = true; }
  inline bool is_skip_flush_redo() const { return is_skip_flush_redo_; }
  inline void set_skip_flush_redo() { is_skip_flush_redo_ = true; }
  inline void unset_skip_flush_redo() { is_skip_flush_redo_ = false; }
  inline void set_update_uk() { is_update_uk_ = true; }
  inline bool is_update_uk() const { return is_update_uk_; }
  inline void set_update_pk_dop() { is_update_pk_dop_ = true; }
  inline bool is_update_pk_dop() const { return is_update_pk_dop_; }
  inline void set_immediate_row_check() { immediate_row_check_ = true; }
  inline bool is_immediate_row_check() const { return immediate_row_check_; }
  inline void set_snapshot_opt() { use_snapshot_opt_ = true; }
  inline bool is_snapshot_opt() const { return use_snapshot_opt_; }
  inline void set_is_delete_insert() { is_delete_insert_ = true; }
  inline bool is_delete_insert() const { return is_delete_insert_; }

  TO_STRING_KV("is_table_lock", is_table_lock_,
               "is_mds", is_mds_,
               "is_dml_batch_opt", is_dml_batch_opt_,
               "is_insert_up", is_insert_up_,
               "is_write_only_index", is_write_only_index_,
               "is_check_row_locked", is_check_row_locked_,
               "is_lob_aux", is_lob_aux_,
               "is_skip_flush_redo", is_skip_flush_redo_,
               "is_update_uk", is_update_uk_,
               "is_update_pk_dop", is_update_pk_dop_,
               "immediate_row_check", immediate_row_check_,
               "is_delete_insert", is_delete_insert_,
               "use_snapshot_opt", use_snapshot_opt_);

  OB_UNIS_VERSION(1);
};

} // namespace concurrent_control
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_MEMTABLE_OB_WRITE_FLAG_H_
