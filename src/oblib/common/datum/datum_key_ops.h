/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include "common/datum/datum_value.h"
#include "lib/checksum/ob_crc64.h"
namespace oceanbase { namespace common {
// Key operations deliberately retain their own contracts. Key equality is
// type-class/representation aware and is not SQL equality or cache equality.
int validate_key_value(const DatumValue &value);
int compare_key_value(const DatumValue &left, const DatumValue &right, int &result,
                      bool collation_free = false);
int equal_key_value(const DatumValue &left, const DatumValue &right, bool &equal);
// SQL null-safe equality used by prefix extraction, distinct from raw key equality.
int sql_equal_key_value(const DatumValue &left, const DatumValue &right, bool &equal);
int hash_key_value(const DatumValue &value, uint64_t seed, uint64_t &hash);
int checksum_key_value(const DatumValue &value, ObBatchChecksum &checksum);
int transform_key_sortkey(const DatumValue &source, DatumValue &output,
                          bool &valid_sortkey, ObIAllocator &owner);
} }
