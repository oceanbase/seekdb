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

#include "common/rowkey/ob_rowkey.h"
#include "lib/ob_define.h"
#include "lib/utility/utility.h"

namespace oceanbase
{
namespace sql
{

static const int64_t GRAPH_PATH_MAX_HOPS = 16;
static const int64_t GRAPH_INVALID_PATH_STATE_ID = -1;

// Controls which repeated graph elements are legal inside one matched path.
// WALK and TRAIL are implemented in P2; SIMPLE and ACYCLIC reserve the public
// path-mode vocabulary for later vertex-history support.
enum class GraphPathMode : int8_t
{
  WALK = 0,
  TRAIL = 1,
  SIMPLE = 2,
  ACYCLIC = 3
};

enum class GraphPathDirection : int8_t
{
  OUT = 0,
  IN = 1
};

// Controls how one matched path is exposed as relational rows and how path
// variables are bound. COLUMNS expressions still determine the actual output
// column types.
enum class GraphPathRowShape : int8_t
{
  // One row for the complete path; the quantified edge is a group variable.
  PER_MATCH = 0,
  // One row for each traversed edge; iteration variables bind that step.
  PER_STEP = 1
};

// Compile-time description of the one quantified edge segment supported by P2.
// Element IDs are mapping identities, not labels or table IDs.
struct GraphPathDesc
{
  bool is_valid() const
  {
    return graph_id_ != common::OB_INVALID_ID
        && source_element_id_ != common::OB_INVALID_ID
        && edge_element_id_ != common::OB_INVALID_ID
        && target_element_id_ != common::OB_INVALID_ID
        && lower_bound_ >= 0
        && lower_bound_ <= upper_bound_
        && upper_bound_ <= GRAPH_PATH_MAX_HOPS
        && (direction_ == GraphPathDirection::OUT || direction_ == GraphPathDirection::IN)
        && (path_mode_ == GraphPathMode::WALK || path_mode_ == GraphPathMode::TRAIL)
        && (row_shape_ == GraphPathRowShape::PER_MATCH
            || row_shape_ == GraphPathRowShape::PER_STEP);
  }

  uint64_t graph_id_ = common::OB_INVALID_ID;
  int64_t graph_version_ = 0;
  uint64_t source_element_id_ = common::OB_INVALID_ID;
  uint64_t edge_element_id_ = common::OB_INVALID_ID;
  uint64_t target_element_id_ = common::OB_INVALID_ID;
  // Inclusive hop range after normalizing the edge quantifier {n}, {n,m}, or {,m}.
  int64_t lower_bound_ = 0;
  int64_t upper_bound_ = 0;
  GraphPathDirection direction_ = GraphPathDirection::OUT;
  // WALK permits repeated edges and vertices. TRAIL rejects an edge identity
  // already present in the same path while still permitting repeated vertices.
  GraphPathMode path_mode_ = GraphPathMode::WALK;
  // Chooses per-path/per-step row cardinality and graph-variable binding shape;
  // COLUMNS still defines the output column count, names, and SQL data types.
  GraphPathRowShape row_shape_ = GraphPathRowShape::PER_MATCH;
  bool need_path_ = false;
  TO_STRING_KV(K_(graph_id), K_(graph_version), K_(source_element_id),
               K_(edge_element_id), K_(target_element_id), K_(lower_bound),
               K_(upper_bound), K_(direction), K_(path_mode), K_(row_shape),
               K_(need_path));
};

// Internal typed identity of one mapped vertex or edge. graph_id_ and
// element_id_ select the graph element mapping; rowkey_ contains that mapping's
// complete base-table primary key in declaration order. The Oracle-compatible
// VERTEX_ID() and EDGE_ID() SQL functions expose equivalent identity
// information as JSON; execution compares these typed fields, never the
// serialized JSON text.
struct GraphElementIdentity
{
  void reset()
  {
    graph_id_ = common::OB_INVALID_ID;
    element_id_ = common::OB_INVALID_ID;
    rowkey_.reset();
  }

  bool is_valid() const
  {
    bool valid = graph_id_ != common::OB_INVALID_ID
        && element_id_ != common::OB_INVALID_ID
        && rowkey_.is_valid()
        && rowkey_.get_obj_cnt() <= common::OB_USER_MAX_ROWKEY_COLUMN_NUMBER;
    for (int64_t i = 0; valid && i < rowkey_.get_obj_cnt(); ++i) {
      valid = !rowkey_.get_obj_ptr()[i].is_null();
    }
    return valid;
  }

  bool operator==(const GraphElementIdentity &other) const
  {
    bool equal = graph_id_ == other.graph_id_
              && element_id_ == other.element_id_
              && rowkey_ == other.rowkey_;
    return equal;
  }

  uint64_t hash(uint64_t seed = 0) const
  {
    uint64_t value = common::do_hash(graph_id_, seed);
    value = common::do_hash(element_id_, value);
    return rowkey_.murmurhash(value);
  }

  uint64_t graph_id_ = common::OB_INVALID_ID;
  uint64_t element_id_ = common::OB_INVALID_ID;
  // ObRowkey is a non-owning typed key view. The DAS page, GraphExpand, or
  // GraphPathStateStore that produces an identity owns its backing objects.
  common::ObRowkey rowkey_{};
  TO_STRING_KV(K_(graph_id), K_(element_id), K_(rowkey));
};

// Represents one node in a query-local, parent-linked path-state store.
// path_state_id_ is a store key, not an encoded path: following
// parent_path_state_id_ back to the root and reversing the nodes reconstructs
// the full path. All descendants of one anchor/outer row retain its opaque
// binding_id_; that row may remain in the outer operator or be materialized in
// a separate row store. The frontier owner allocates these IDs and nodes;
// GraphExpand only preserves their association.
struct GraphPathState
{
  bool is_valid() const
  {
    return binding_id_ >= 0
        && path_state_id_ >= 0
        && hop_ >= 0
        && hop_ <= GRAPH_PATH_MAX_HOPS
        && current_identity_.is_valid()
        && ((hop_ == 0 && parent_path_state_id_ == GRAPH_INVALID_PATH_STATE_ID)
            || (hop_ > 0 && parent_path_state_id_ >= 0
                && incoming_edge_identity_.is_valid()));
  }
  int64_t binding_id_ = 0;
  int64_t path_state_id_ = GRAPH_INVALID_PATH_STATE_ID;
  int64_t parent_path_state_id_ = GRAPH_INVALID_PATH_STATE_ID;
  int64_t hop_ = 0;
  GraphElementIdentity current_identity_{};
  GraphElementIdentity incoming_edge_identity_{};
  TO_STRING_KV(K_(binding_id), K_(path_state_id), K_(parent_path_state_id),
               K_(hop), K_(current_identity), K_(incoming_edge_identity));
};

// One current frontier path. binding_id_ is the owner's correlation handle for
// the anchor/outer input row; it does not require a particular BindingStore
// representation. path_state_id_ identifies this path prefix in the owner's
// state store. source_identity_ repeats that state's current vertex so
// GraphExpand can batch equal adjacency reads without dereferencing owner state.
// Neither ID is a persistent graph or table identifier.
struct GraphExpandInput
{
  int64_t binding_id_ = 0;
  int64_t path_state_id_ = GRAPH_INVALID_PATH_STATE_ID;
  GraphElementIdentity source_identity_{};
  TO_STRING_KV(K_(binding_id), K_(path_state_id), K_(source_identity));
};

// One candidate extension of an input path. binding_id_ and path_state_id_ are
// copied unchanged from the input; in particular, path_state_id_ still names
// the parent prefix. The frontier owner combines it with edge_identity_ and
// target_identity_ to allocate the child GraphPathState for the next hop.
struct GraphExpandOutput
{
  int64_t binding_id_ = 0;
  int64_t path_state_id_ = GRAPH_INVALID_PATH_STATE_ID;
  GraphElementIdentity edge_identity_{};
  GraphElementIdentity target_identity_{};
  TO_STRING_KV(K_(binding_id), K_(path_state_id), K_(edge_identity),
               K_(target_identity));
};

// One physical adjacency row before association multiplicity is restored.
// Implementations must return pages in stable edge-identity order.
struct GraphExpandEdge
{
  GraphElementIdentity source_identity_{};
  GraphElementIdentity edge_identity_{};
  GraphElementIdentity target_identity_{};
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
