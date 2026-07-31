/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_DDL_OB_DIRECT_LOAD_CONTROL_H_
#define OCEANBASE_DATA_PLANE_API_DDL_OB_DIRECT_LOAD_CONTROL_H_

#include "data_plane/ddl/ob_direct_load_type.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace data_plane
{

// Value state shared by PX orchestration and direct-load execution.  It is a
// protocol value; DAGs, managers, and writer implementations remain private
// to the data plane.
class ObDirectLoadControl final
{
public:
  ObDirectLoadControl()
    : direct_load_type_(storage::ObDirectLoadType::DIRECT_LOAD_INVALID),
      context_id_(0),
      in_progress_(false)
  {}

  bool is_in_progress() const { return in_progress_; }
  TO_STRING_KV(K_(direct_load_type), K_(context_id), K_(in_progress));

  storage::ObDirectLoadType direct_load_type_;
  int64_t context_id_;
  bool in_progress_;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_DDL_OB_DIRECT_LOAD_CONTROL_H_
