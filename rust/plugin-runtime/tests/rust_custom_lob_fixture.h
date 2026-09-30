// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real SQL frames, PluginCustomOp, loader and Rust spool; child rows and backing
// LOB storage are fixtures. This does not claim a live storage/MVCC regression.
#ifndef SEEKDB_TEST_RUST_CUSTOM_LOB_FIXTURE_H_
#define SEEKDB_TEST_RUST_CUSTOM_LOB_FIXTURE_H_
#include "sql/engine/basic/plugin_custom_op.h"
#include "share/lob/ob_lob_text_iter_context.h"
namespace rust_custom_lob_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
struct Cell {
  std::string bytes;
  bool null = false;
  int64_t declared = -1;
};
using Row = std::array<Cell, 2>;
class Storage final : public ObILobReadService {
  class Cursor final : public ObILobReadCursor {
  public:
    int get_next_row(ObString &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
    void reset() override {}
  } cursor_;
public:
  const Row *row = nullptr;
  int reads = 0, releases = 0, error = OB_SUCCESS;
  int get_outrow_lob_full_data(ObLobTextIterCtx &ctx, ObCollationType, bool, bool, ObIAllocator *) override
  {
    ++reads;
    CHECK(!ctx.read_cursor_); ctx.read_cursor_ = &cursor_;
    if (error) return error;
    ObLobCommon *disk = nullptr;
    CHECK(ctx.locator_.get_disk_locator(disk) == OB_SUCCESS && disk);
    const auto id = reinterpret_cast<ObLobData *>(disk->buffer_)->id_.lob_id_;
    CHECK(row && id < 2 && ctx.alloc_);
    const auto &bytes = row->at(id).bytes;
    ctx.buff_ = static_cast<char *>(ctx.alloc_->alloc(std::max(size_t(1), bytes.size())));
    CHECK(ctx.buff_);
    if (!bytes.empty()) std::memcpy(ctx.buff_, bytes.data(), bytes.size());
    ctx.content_byte_len_ = bytes.size();
    return OB_SUCCESS;
  }
  int get_delta_lob_full_data(ObLobTextIterCtx &, ObObjType, ObCollationType,
      ObLobLocatorV2 &, ObIAllocator *, ObString &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_outrow_prefix_data(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObIAllocator *, uint32_t) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_first_block(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *,
      ObString &, ObTextStringIterState &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_next_block_inner(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObString &, ObTextStringIterState &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_outrow_char_len(ObLobTextIterCtx &, ObCollationType, ObIAllocator *, int64_t &) override
  { CHECK(false); return OB_ERR_UNEXPECTED; }
  void free_lob_query_iter(ObLobTextIterCtx &ctx) override
  { CHECK(ctx.read_cursor_ == &cursor_); ctx.read_cursor_ = nullptr; ++releases; }
};
class Input final : public ObOperator {
public:
  Input(ObExecContext &ctx, const ObOpSpec &spec, Storage &storage, int format)
      : ObOperator(ctx, spec, nullptr), storage_(storage), format_(format) {}
  std::vector<Row> rows;
  int reads = 0;
  int inner_get_next_row() override
  {
    ++reads;
    if (position_ == rows.size()) return OB_ITER_END;
    clear_evaluated_flag();
    storage_.row = &rows[position_++];
    for (int i = 0; i < 2; ++i) {
      auto &expr = *spec_.output_.at(i);
      auto &datum = expr.locate_datum_for_write(eval_ctx_);
      const auto &cell = storage_.row->at(i);
      if (cell.null) datum.set_null();
      else if (format_ == 2) {
        // Memory locator referencing an out-of-row persistent value. No table
        // or IO is simulated: Storage supplies precisely the payload requested.
        const size_t size = sizeof(ObMemLobCommon) + sizeof(ObLobCommon) + sizeof(ObLobData) +
            sizeof(ObLobDataOutRowCtx) + sizeof(uint64_t);
        locators_[i].assign(size, 0);
        auto *mem = new (locators_[i].data()) ObMemLobCommon(PERSISTENT_LOB, false);
        mem->set_has_inrow_data(false);
        auto *disk = new (mem->data_) ObLobCommon(); disk->in_row_ = 0; disk->is_init_ = 1;
        auto *data = new (disk->buffer_) ObLobData(); data->id_.lob_id_ = i;
        data->byte_size_ = cell.declared < 0 ? cell.bytes.size() : cell.declared;
        datum.set_string(ObString(size, locators_[i].data()));
      } else if (format_ == 1) {
        // Persistent in-row header, not the same representation as the
        // temporary LOB returned by the custom operator.
        locators_[i].assign(sizeof(ObLobCommon) + cell.bytes.size(), 0);
        auto *disk = new (locators_[i].data()) ObLobCommon();
        if (!cell.bytes.empty()) std::memcpy(disk->buffer_, cell.bytes.data(), cell.bytes.size());
        datum.set_string(ObString(locators_[i].size(), locators_[i].data()));
      } else {
        CHECK(ObTextStringHelper::string_to_templob_result(expr, eval_ctx_, datum,
            ObString(cell.bytes.size(), cell.bytes.data())) == OB_SUCCESS);
      }
      expr.set_evaluated_projected(eval_ctx_);
    }
    return OB_SUCCESS;
  }
  int inner_rescan() override { position_ = 0; return ObOperator::inner_rescan(); }
  void destroy() override { ObOperator::destroy(); }
private:
  Storage &storage_;
  int format_;
  size_t position_ = 0;
  std::array<std::vector<char>, 2> locators_;
};
template <typename Provider>
void run(Provider &provider, ObArenaAllocator &arena)
{
  for (bool stored : {false, true}) for (int format : {0, 1, 2}) {
    std::cerr << "custom LOB stored=" << stored << " format=" << format << std::endl;
    auto session = std::make_unique<ObSQLSessionInfo>();
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
    session->set_inner_session();
    Storage storage;
    ObPhysicalPlan physical;
    ObExecContext execution(arena); execution.set_my_session(session.get()); execution.set_lob_read_service(&storage);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    execution.get_physical_plan_ctx()->set_phy_plan(&physical);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
    ObRawExprFactory factory(arena); ObRawExprUniqueSet roots(false);
    for (int i = 0; i < 2; ++i) {
      ObColumnRefRawExpr *raw = nullptr;
      CHECK(factory.create_raw_expr(T_REF_COLUMN, raw) == OB_SUCCESS && raw);
      raw->set_ref_id(100, i + 1); raw->set_data_type(ObLongTextType);
      raw->set_collation_type(CS_TYPE_BINARY); raw->set_collation_level(CS_LEVEL_IMPLICIT);
      ObAccuracy accuracy; accuracy.set_length(1000000); raw->set_accuracy(accuracy);
      CHECK(raw->formalize(session.get()) == OB_SUCCESS && roots.append(raw) == OB_SUCCESS);
    }
    ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObOpSpec child_spec(arena, PHY_EXPR_VALUES);
    PluginCustomSpec spec(arena, PHY_PLUGIN_CUSTOM);
    child_spec.plan_ = &physical; spec.plan_ = &physical;
    CHECK(child_spec.output_.init(2) == OB_SUCCESS && spec.columns_.init(2) == OB_SUCCESS);
    CHECK(spec.output_.init(2) == OB_SUCCESS && spec.type_ids_.init(2) == OB_SUCCESS);
    CHECK(spec.nullable_.init(2) == OB_SUCCESS);
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) {
      expr.obj_meta_.set_has_lob_header();
      CHECK(PluginCustomOp::supported_column(expr));
      CHECK(child_spec.output_.push_back(&expr) == OB_SUCCESS);
      CHECK(spec.columns_.push_back(&expr) == OB_SUCCESS && spec.output_.push_back(&expr) == OB_SUCCESS);
      CHECK(spec.type_ids_.push_back(ObString::make_string("core.type.bytes")) == OB_SUCCESS);
      CHECK(spec.nullable_.push_back(1) == OB_SUCCESS);
    }
    CHECK(spec.columns_.count() == 2);
    if (stored) {
      PluginExprType type;
      type.stored_ = true; type.physical_type_ = ObLongTextType;
      type.sql_name_ = ObString::make_string("rust_stored_utf8");
      type.logical_id_ = ObString::make_string(rust_stored_type_test::TYPE_ID);
      type.owner_ = ObString::make_string("org.seekdb.rust-text");
      type.format_ = ObString::make_string("org.seekdb.rust-text.stored-utf8.v1"); type.format_version_ = 1;
      spec.type_ids_.at(0) = type.logical_id_;
      CHECK(spec.bind_stored_column(arena, 0, type) == OB_SUCCESS);
      CHECK(spec.codecs_.count() == 2 && !spec.codecs_.at(0).empty() && spec.codecs_.at(1).empty());
      const auto saved = spec.codecs_.at(0);
      ++type.format_version_;
      CHECK(spec.bind_stored_column(arena, 0, type) == OB_STATE_NOT_MATCH && spec.codecs_.at(0) == saved);
      --type.format_version_;
      const int before = provider.resolves_;
      auto invalid = type; invalid.physical_type_ = ObMaxType;
      CHECK(spec.bind_stored_column(arena, 0, invalid) == OB_INVALID_ARGUMENT);
      invalid = type; invalid.sql_name_ = ObString(-1, "x");
      CHECK(spec.bind_stored_column(arena, 0, invalid) == OB_INVALID_ARGUMENT);
      CHECK(spec.bind_stored_column(arena, 2, type) == OB_INVALID_ARGUMENT);
      CHECK(provider.resolves_ == before && spec.codecs_.at(0) == saved);
    }
    oceanbase::share::plugin::CustomExecutorBinding binding;
    CHECK(provider.bind_plugin_custom_executor("org.seekdb.rust-candidate.spool", 1, 0, binding) == OB_SUCCESS);
    CHECK(spec.set_binding(arena, binding, {}) == OB_SUCCESS);
    Input child(execution, child_spec, storage, format);
    std::vector<Row> rows = {
      Row{Cell{std::string("a\0b", 3)}, Cell{"🙂"}},
      Row{Cell{"", true}, Cell{""}},
      Row{Cell{std::string(70000, 'x')}, Cell{"tail"}},
      Row{Cell{"next"}, Cell{std::string(70000, 'y')}}};
    if (stored) rows.push_back(Row{Cell{""}, Cell{"", true}});
    const auto encoded = [&](std::vector<Row> input) {
      if (stored) for (auto &row : input) if (!row[0].null) {
        std::string value("RUT\1", 4);
        for (const unsigned char byte : row[0].bytes) value += static_cast<char>(~byte);
        row[0].bytes = std::move(value);
      }
      return input;
    };
    child.rows = encoded(rows);
    PluginCustomOp op(execution, spec, nullptr);
    ObOperator *children[] = {&child}; CHECK(op.set_children_pointer(children, 1) == OB_SUCCESS);
    CHECK(child.init() == OB_SUCCESS && op.init() == OB_SUCCESS);
    if (stored) {
      const auto saved = spec.codecs_.at(0);
      const int before = provider.custom_opens_;
      std::string trailing(saved.ptr(), saved.length()); trailing.push_back('x');
      for (int fault : {0, 1, 2}) {
        PluginStoredArgument decoded; int64_t position = 0;
        CHECK(decoded.deserialize(saved.ptr(), saved.length(), position) == OB_SUCCESS && position == saved.length());
        std::vector<char> altered(decoded.get_serialize_size() + 16);
        if (fault == 2) {
          decoded.index_ = 1; position = 0;
          CHECK(decoded.serialize(altered.data(), altered.size(), position) == OB_SUCCESS);
          spec.codecs_.at(0) = ObString(position, altered.data());
        } else spec.codecs_.at(0) = fault == 0 ? ObString(saved.length() - 1, saved.ptr()) :
            ObString(trailing.size(), trailing.data());
        CHECK(op.inner_open() == OB_INVALID_DATA && provider.custom_opens_ == before);
      }
      spec.codecs_.at(0) = saved;
    }
    const int opens = provider.custom_opens_, nexts = provider.custom_nexts_;
    const int resolves = provider.resolves_, decodes = provider.decodes_, encodes = provider.encodes_;
    int described = 0;
    provider.custom_schema_observer_ = [&](const seekdb_plugin_custom_context_v2_t &context) {
      ++described;
      CHECK(context.v1.input_count == 1 && context.inputs && context.output);
      CHECK(context.inputs[0].column_count == 2 && context.output->column_count == 2);
      for (uint32_t i = 0; i < 2; ++i) {
        const auto &column = context.inputs[0].columns[i];
        CHECK(column.sql_type == ObLongTextType && column.collation == CS_TYPE_BINARY);
        CHECK(column.encoding == SEEKDB_PLUGIN_CUSTOM_ENCODING_BYTES && column.precision == -1 && column.scale == -1);
        CHECK(column.flags == (uint32_t(spec.nullable_.at(i)) |
            (stored && i == 0 ? SEEKDB_PLUGIN_CUSTOM_COLUMN_STORED : 0)));
        CHECK(ObString::make_string(column.type_id) == spec.type_ids_.at(i));
      }
    };
    CHECK(op.open() == OB_SUCCESS);
    const auto verify = [&] {
      for (const auto &row : rows) {
        CHECK(op.get_next_row() == OB_SUCCESS);
        for (int i = 0; i < 2; ++i) {
          const auto &expr = *spec.columns_.at(i); ObDatum *datum = nullptr;
          CHECK(expr.eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum && datum->is_null() == row[i].null);
          if (!row[i].null) {
            ObString bytes;
            ObArenaAllocator temporary;
            CHECK(ObTextStringHelper::read_real_string_data_with_copy(execution, temporary, *datum,
                expr.datum_meta_, true, bytes) == OB_SUCCESS);
            CHECK(std::string(bytes.ptr() ? bytes.ptr() : "", bytes.length()) == row[i].bytes);
            CHECK(datum->get_string().length() > bytes.length()); // SQL header reconstructed.
          }
        }
      }
      CHECK(op.get_next_row() == OB_ITER_END);
    };
    // Returned SQL content must have the persistent codec restored, not merely
    // the decoded Rust bytes wrapped in a LOB. verify() uses the encoded oracle.
    rows = encoded(rows);
    verify(); CHECK(op.rescan() == OB_SUCCESS); verify();
    CHECK(provider.decodes_ == decodes + (stored ? 8 : 0));
    CHECK(provider.encodes_ == encodes + (stored ? 8 : 0));
    CHECK(provider.resolves_ == resolves);
    if (format == 2) {
      CHECK(storage.reads == (stored ? 16 : 14));
      child.rows = encoded({Row{Cell{"", false, SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES + 1}, Cell{""}}});
      CHECK(op.rescan() == OB_SUCCESS);
      const int before = storage.reads;
      CHECK(op.get_next_row() == OB_SIZE_OVERFLOW && storage.reads == before);
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH);
      child.rows = encoded({Row{Cell{"ok"}, Cell{"", false, SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES}}});
      CHECK(op.rescan() == OB_SUCCESS);
      CHECK(op.get_next_row() == OB_SIZE_OVERFLOW && storage.reads == before + 1);
      child.rows = rows; storage.error = OB_TIMEOUT;
      CHECK(op.rescan() == OB_SUCCESS && op.get_next_row() == OB_TIMEOUT);
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH);
      storage.error = OB_SUCCESS;
      child.rows = encoded({Row{Cell{"longer", false, 1}, Cell{""}}});
      CHECK(op.rescan() == OB_SUCCESS && op.get_next_row() == OB_INVALID_DATA);
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH);
      child.rows = rows;
      CHECK(op.rescan() == OB_SUCCESS); verify();
      CHECK(storage.releases == storage.reads);
    }
    if (stored) {
      child.rows = rows; child.rows[0][0].bytes[3] = 2; // Actual Rust decoder rejects this format version.
      CHECK(op.rescan() == OB_SUCCESS && op.get_next_row() == OB_INVALID_ARGUMENT);
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH);
      child.rows = rows; CHECK(op.rescan() == OB_SUCCESS); verify();
      if (format == 0) {
        CHECK(op.inner_close() == OB_SUCCESS);
        child.rows = {rows[0]}; spec.nullable_.at(0) = 0;
        CHECK(op.inner_open() == OB_SUCCESS);
        for (bool encode : {false, true}) for (int mode : {1, 2, 3, 4, 5, 6, 8, 9, 10}) {
          auto &fault = encode ? provider.custom_encode_fault_ : provider.custom_decode_fault_;
          fault = mode;
          CHECK(op.rescan() == OB_SUCCESS);
          const int expected = mode == 5 ? OB_SIZE_OVERFLOW : mode == 8 ? OB_TIMEOUT : OB_INVALID_DATA;
          CHECK(op.get_next_row() == expected);
          CHECK(op.get_next_row() == OB_STATE_NOT_MATCH);
          fault = 0;
        }
        CHECK(op.inner_close() == OB_SUCCESS);
        spec.nullable_.at(0) = 1; child.rows = rows;
        CHECK(op.inner_open() == OB_SUCCESS);
        CHECK(op.rescan() == OB_SUCCESS); verify();
      }
    }
    CHECK(described > 0); provider.custom_schema_observer_ = {};
    CHECK(op.close() == OB_SUCCESS);
    CHECK(provider.custom_opens_ == opens + (stored && format == 0 ? 3 : 1) && provider.custom_nexts_ > nexts);
    CHECK(provider.custom_closes_ == provider.custom_opens_);
    oceanbase::share::plugin::ObPluginStatusSnapshot status;
    CHECK(provider.candidate_loader_->get_status("org.seekdb.rust-candidate", status) == OB_SUCCESS && !status.lease_count_);
    std::cerr << "custom LOB stored=" << stored << " format=" << format << " passed" << std::endl;
  }
}
} // namespace rust_custom_lob_test
#endif
