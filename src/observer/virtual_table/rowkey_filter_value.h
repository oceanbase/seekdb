/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once

#include "share/datum/typed_datum_compare.h"
#include "lib/utility/ob_hang_fatal_error.h"

namespace oceanbase { namespace observer {
// Match the independent null-safe EQ/NE predicates used by key filters. Keep
// the old fatal-error policy when the producer supplies incomparable values.
inline bool key_filter_equal(const common::DatumValue &left, const common::DatumValue &right)
{
  bool equal = false;
  const int ret = common::datum_values_equal(left, right, equal);
  if (ret != common::OB_SUCCESS) {
    COMMON_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "key filter values cannot compare", K(ret), K(left), K(right));
    common::right_to_die_or_duty_to_live();
    return false;
  }
  return equal;
}
inline bool key_filter_not_equal(const common::DatumValue &left, const common::DatumValue &right)
{
  bool equal = false;
  const int ret = common::datum_values_equal(left, right, equal);
  if (ret != common::OB_SUCCESS) {
    COMMON_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "key filter values cannot compare", K(ret), K(left), K(right));
    common::right_to_die_or_duty_to_live();
    return false;
  }
  return !equal;
}
} }
