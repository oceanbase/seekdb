/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include "common/object/ob_object.h"
#include "lib/ob_define.h"
#include "lib/utility/utility.h"

namespace oceanbase
{
namespace sql
{

static const int64_t GRAPH_WALK_MAX_HOPS = 16;
static const int64_t GRAPH_IDENTITY_MAX_KEYS = 2;
static const int64_t GRAPH_INVALID_PATH_STATE_ID = -1;

enum class GraphPathDirection : int8_t
{
  OUT = 0,
  IN = 1
};

enum class GraphPathRowShape : int8_t
{
  PER_MATCH = 0,
  PER_STEP = 1
};

// Compile-time description of the one quantified edge segment supported by
// P2-WALK. Element IDs are mapping identities, not labels or table IDs.
struct GraphPathSpec
{
  GraphPathSpec()
    : graph_id_(common::OB_INVALID_ID),
      graph_version_(0),
      source_element_id_(common::OB_INVALID_ID),
      edge_element_id_(common::OB_INVALID_ID),
      target_element_id_(common::OB_INVALID_ID),
      lower_bound_(0),
      upper_bound_(0),
      direction_(GraphPathDirection::OUT),
      row_shape_(GraphPathRowShape::PER_MATCH),
      need_path_(false)
  {}

  bool is_valid() const
  {
    return graph_id_ != common::OB_INVALID_ID
        && source_element_id_ != common::OB_INVALID_ID
        && edge_element_id_ != common::OB_INVALID_ID
        && target_element_id_ != common::OB_INVALID_ID
        && lower_bound_ >= 0
        && lower_bound_ <= upper_bound_
        && upper_bound_ <= GRAPH_WALK_MAX_HOPS
        && (direction_ == GraphPathDirection::OUT || direction_ == GraphPathDirection::IN)
        && (row_shape_ == GraphPathRowShape::PER_MATCH
            || row_shape_ == GraphPathRowShape::PER_STEP);
  }

  uint64_t graph_id_;
  int64_t graph_version_;
  uint64_t source_element_id_;
  uint64_t edge_element_id_;
  uint64_t target_element_id_;
  int64_t lower_bound_;
  int64_t upper_bound_;
  GraphPathDirection direction_;
  GraphPathRowShape row_shape_;
  bool need_path_;
  TO_STRING_KV(K_(graph_id), K_(graph_version), K_(source_element_id),
               K_(edge_element_id), K_(target_element_id), K_(lower_bound),
               K_(upper_bound), K_(direction), K_(row_shape), K_(need_path));
};

// JSON identities are presentation values. Internal comparison always uses
// this typed identity so collation or JSON text formatting cannot change it.
struct GraphElementIdentity
{
  GraphElementIdentity()
    : graph_id_(common::OB_INVALID_ID),
      element_id_(common::OB_INVALID_ID),
      key_count_(0)
  {}

  void reset()
  {
    graph_id_ = common::OB_INVALID_ID;
    element_id_ = common::OB_INVALID_ID;
    key_count_ = 0;
    keys_[0].reset();
    keys_[1].reset();
  }

  bool is_valid() const
  {
    bool valid = graph_id_ != common::OB_INVALID_ID
        && element_id_ != common::OB_INVALID_ID
        && key_count_ > 0
        && key_count_ <= GRAPH_IDENTITY_MAX_KEYS;
    for (int64_t i = 0; valid && i < key_count_; ++i) {
      valid = !keys_[i].is_null();
    }
    return valid;
  }

  bool operator==(const GraphElementIdentity &other) const
  {
    bool equal = graph_id_ == other.graph_id_
              && element_id_ == other.element_id_
              && key_count_ == other.key_count_;
    for (int64_t i = 0; equal && i < key_count_; ++i) {
      equal = keys_[i] == other.keys_[i];
    }
    return equal;
  }

  uint64_t hash(uint64_t seed = 0) const
  {
    uint64_t value = common::do_hash(graph_id_, seed);
    value = common::do_hash(element_id_, value);
    value = common::do_hash(key_count_, value);
    for (int64_t i = 0; i < key_count_; ++i) {
      (void)keys_[i].hash(value, value);
    }
    return value;
  }

  uint64_t graph_id_;
  uint64_t element_id_;
  int64_t key_count_;
  common::ObObj keys_[GRAPH_IDENTITY_MAX_KEYS];
  TO_STRING_KV(K_(graph_id), K_(element_id), K_(key_count),
               "key0", keys_[0], "key1", keys_[1]);
};

// A parent link lets GraphBuildPath retain the path only when a projection
// actually consumes it.
struct GraphPathState
{
  GraphPathState()
    : binding_id_(0),
      path_state_id_(GRAPH_INVALID_PATH_STATE_ID),
      parent_path_state_id_(GRAPH_INVALID_PATH_STATE_ID),
      hop_(0)
  {}
  bool is_valid() const
  {
    return binding_id_ >= 0
        && path_state_id_ >= 0
        && hop_ >= 0
        && hop_ <= GRAPH_WALK_MAX_HOPS
        && current_identity_.is_valid()
        && ((hop_ == 0 && parent_path_state_id_ == GRAPH_INVALID_PATH_STATE_ID)
            || (hop_ > 0 && parent_path_state_id_ >= 0
                && incoming_edge_identity_.is_valid()));
  }
  int64_t binding_id_;
  int64_t path_state_id_;
  int64_t parent_path_state_id_;
  int64_t hop_;
  GraphElementIdentity current_identity_;
  GraphElementIdentity incoming_edge_identity_;
  TO_STRING_KV(K_(binding_id), K_(path_state_id), K_(parent_path_state_id),
               K_(hop), K_(current_identity), K_(incoming_edge_identity));
};

// Association IDs survive adjacency sharing. Source reads may be deduplicated,
// but output multiplicity is restored from these fields.
struct GraphExpandInput
{
  int64_t binding_id_ = 0;
  int64_t path_state_id_ = GRAPH_INVALID_PATH_STATE_ID;
  GraphElementIdentity source_identity_;
  TO_STRING_KV(K_(binding_id), K_(path_state_id), K_(source_identity));
};

struct GraphExpandOutput
{
  int64_t binding_id_ = 0;
  int64_t path_state_id_ = GRAPH_INVALID_PATH_STATE_ID;
  GraphElementIdentity edge_identity_;
  GraphElementIdentity target_identity_;
  TO_STRING_KV(K_(binding_id), K_(path_state_id), K_(edge_identity),
               K_(target_identity));
};

// One physical adjacency row before association multiplicity is restored.
// Implementations must return pages in stable edge-identity order.
struct GraphExpandEdge
{
  GraphElementIdentity source_identity_;
  GraphElementIdentity edge_identity_;
  GraphElementIdentity target_identity_;
  TO_STRING_KV(K_(source_identity), K_(edge_identity), K_(target_identity));
};

struct GraphExpandStats
{
  int64_t input_states_ = 0;
  int64_t distinct_sources_ = 0;
  int64_t scanned_edges_ = 0;
  int64_t looked_up_vertices_ = 0;
  int64_t orphan_edges_ = 0;
  int64_t output_states_ = 0;
  int64_t peak_path_memory_ = 0;
  TO_STRING_KV(K_(input_states), K_(distinct_sources), K_(scanned_edges),
               K_(looked_up_vertices), K_(orphan_edges), K_(output_states),
               K_(peak_path_memory));
};

} // namespace sql
} // namespace oceanbase
