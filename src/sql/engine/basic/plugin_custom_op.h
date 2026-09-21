// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_SQL_PLUGIN_CUSTOM_OP_H_
#define SEEKDB_SQL_PLUGIN_CUSTOM_OP_H_
#include "sql/engine/ob_operator.h"
#include "share/plugin/custom_executor.h"
#include <memory>
namespace oceanbase { namespace sql {
struct PluginExprType;
class PluginCustomSpec : public ObOpSpec {
  OB_UNIS_VERSION_V(1);
public:
  PluginCustomSpec(common::ObIAllocator &alloc, ObPhyOperatorType type)
      : ObOpSpec(alloc, type), columns_(alloc), type_ids_(alloc), nullable_(alloc), codecs_(alloc),
        input_columns_(alloc), input_type_ids_(alloc), input_nullable_(alloc), input_codecs_(alloc), input_offsets_(alloc),
        input_bindings_(alloc), binding_sources_(alloc), binding_inputs_(alloc), binding_targets_(alloc) {}
  int set_binding(common::ObIAllocator &allocator, const share::plugin::CustomExecutorBinding &binding,
                  const std::string &parameters);
  int bind_stored_column(common::ObIAllocator &allocator, uint32_t column, const PluginExprType &type);
  int bind_stored_input(common::ObIAllocator &allocator, uint32_t column, const PluginExprType &type);
  // Output slots. The legacy path also uses these as the input description.
  ExprFixedArray columns_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> type_ids_;
  common::ObFixedArray<uint8_t, common::ObIAllocator> nullable_;
  // Empty means no codecs. Otherwise one entry per column, with empty entries
  // for runtime values. Each nonempty blob uses the existing versioned stored
  // binding serializer with its local argument index fixed to zero. No native
  // pointers or duplicate type-binding wire format are stored in the plan.
  common::ObFixedArray<common::ObString, common::ObIAllocator> codecs_;
  common::ObString service_, owner_, incarnation_, parameters_;
  uint64_t generation_ = 0;
  uint32_t major_ = 0, minor_ = 0, patch_ = 0;
  // Explicit zero-column input is distinct from the legacy shared layout.
  // Independent descriptions allow projection/reordering and future computed
  // outputs without treating an input Datum as the output's storage slot.
  bool explicit_input_ = false;
  ExprFixedArray input_columns_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> input_type_ids_;
  common::ObFixedArray<uint8_t, common::ObIAllocator> input_nullable_;
  common::ObFixedArray<common::ObString, common::ObIAllocator> input_codecs_;
  // Optional explicit child partition of the flattened input arrays. Exactly
  // child_count+1 monotonically increasing offsets, starting at zero and ending
  // at input_columns_.count(). Equal offsets describe a zero-column child.
  // Empty retains the legacy single-child layout, not an implicit broadcast.
  common::ObFixedArray<uint32_t, common::ObIAllocator> input_offsets_;
  // Owned NestedLoop parameters transferred from removed JOINs. Each flattened
  // source slot belongs to binding_inputs_[i]; binding_targets_[i] is the child
  // that must be bound before execution. These are physical indexes, not IDs.
  common::ObFixedArray<ObDynamicParamSetter, common::ObIAllocator> input_bindings_;
  common::ObFixedArray<uint32_t, common::ObIAllocator> binding_sources_;
  common::ObFixedArray<uint32_t, common::ObIAllocator> binding_inputs_;
  common::ObFixedArray<uint32_t, common::ObIAllocator> binding_targets_;
};
class PluginCustomOp : public ObOperator {
public:
  PluginCustomOp(ObExecContext &ctx, const ObOpSpec &spec, ObOpInput *input);
  ~PluginCustomOp() override;
  int inner_open() override;
  int inner_get_next_row() override;
  int get_next_row() override;
  int get_next_batch(const int64_t max_row_cnt, const ObBatchRows *&batch_rows) override;
  int rescan() override;
  int inner_rescan() override;
  int inner_close() override;
  void destroy() override;
  // Shared codegen/runtime admission; unsupported physical representations are
  // rejected, never passed as a native C++ Datum or interpreted as raw bytes.
  static bool supported_column(const ObExpr &expr);
private:
  struct State;
  std::unique_ptr<State> state_;
  int read_input(uint32_t input, seekdb_plugin_custom_row_v1_t &row);
  int read_input_values(uint32_t input, seekdb_plugin_custom_row_v1_t &row);
  int begin_input(uint32_t operation, uint32_t input, uint64_t &ticket);
  int finish_input(uint64_t ticket, uint32_t outcome, int result);
  void invalidate_input_state(uint64_t rows, uint64_t bindings);
  int receive(const seekdb_plugin_execution_value_v1_t *values, uint32_t count);
  int publish();
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL input(void *, uint32_t,
      seekdb_plugin_custom_row_v1_t *, int32_t *);
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(void *, const seekdb_plugin_execution_value_v1_t *, uint32_t, int32_t *);
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL poll(void *, int32_t *);
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL rewind(void *, uint32_t, int32_t *);
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL bind_rewind(void *, uint32_t, int32_t *);
  void clear_owned_parameters(bool reusable = false);
};
} }
#endif
