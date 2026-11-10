/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include "common/datum/datum_value.h"
namespace oceanbase { namespace common {
// Matches the explicit Murmur variant used by TopK. Character values use the
// Murmur charset callback; the default typed-value hash retains its own contract.
int hash_datum_value_murmur(const DatumValue &value, uint64_t seed, uint64_t &hash);
} }
