/*
 * Copyright (c) 2025 OceanBase.
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

#ifndef OCEANBASE_SQL_CODE_GENERATOR_OB_STATIC_ENGINE_EXPR_CG_
#define OCEANBASE_SQL_CODE_GENERATOR_OB_STATIC_ENGINE_EXPR_CG_

#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "share/ob_cluster_version.h"
#include "lib/container/ob_fixed_array.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}

namespace sql
{

class ObPhysicalPlan;
class ObDMLStmt;
class ObRawExprUniqueSet;
class ObSQLSessionInfo;
class ObRawExprFactory;

class ObExprCGCtx
{
public:
  ObExprCGCtx(common::ObIAllocator &allocator,
              ObSQLSessionInfo *session,
              share::schema::ObSchemaGetterGuard *schema_guard,
              const uint64_t cur_cluster_version)
    : allocator_(&allocator), session_(session),
      schema_guard_(schema_guard), cur_cluster_version_(cur_cluster_version)
  {}

private:
DISALLOW_COPY_AND_ASSIGN(ObExprCGCtx);

public:
  common::ObIAllocator *allocator_;
  ObSQLSessionInfo *session_;
  share::schema::ObSchemaGetterGuard *schema_guard_;
  uint64_t cur_cluster_version_;
};

class ObRawExpr;
class ObRawExprFactory;
class RowDesc;
struct ObHiddenColumnItem;
class ObStaticEngineExprCG
{
public:
  static const int64_t STACK_OVERFLOW_CHECK_DEPTH = 16;
  static const int64_t OLTP_WORKLOAD_CARDINALITY = 16;
  static const int64_t DATUM_EVAL_INFO_SIZE = sizeof(ObDatum) + sizeof(ObEvalInfo);
  friend class ObRawExpr;
  struct TmpFrameInfo {
    TmpFrameInfo() : expr_start_pos_(0), frame_info_() {}
    TmpFrameInfo(uint64_t start_pos,
                 uint64_t expr_cnt,
                 uint32_t frame_idx,
                 uint32_t frame_size,
                 uint32_t zero_init_pos,
                 uint32_t zero_init_size,
                 bool use_rich_format)
      : expr_start_pos_(start_pos),
        frame_info_(expr_cnt, frame_idx, frame_size, zero_init_pos, zero_init_size, use_rich_format)
    {}
    TO_STRING_KV(K_(expr_start_pos), K_(frame_info));
  public:
    uint64_t expr_start_pos_; // The offset value of the first expr in the current frame within this ObExpr array
    ObFrameInfo frame_info_;
  };

  ObStaticEngineExprCG(common::ObIAllocator &allocator,
                       ObSQLSessionInfo *session,
                       share::schema::ObSchemaGetterGuard *schema_guard,
                       const int64_t original_param_cnt,
                       int64_t param_cnt,
                       const uint64_t cur_cluster_version)
    : allocator_(allocator),
      original_param_cnt_(original_param_cnt),
      param_cnt_(param_cnt),
      op_cg_ctx_(allocator_, session, schema_guard, cur_cluster_version),
      flying_param_cnt_(0),
      batch_size_(0),
      rt_question_mark_eval_(false),
      need_flatten_gen_col_(true),
      cur_cluster_version_(cur_cluster_version),
      gen_questionmarks_(allocator, param_cnt),
      contain_dynamic_eval_rt_qm_(false)
  {
  }
  virtual ~ObStaticEngineExprCG() {}
  // Expand all raw exprs and generate ObExpr
  // @param [in] all_raw_exprs raw expr collection before expansion
  // @param [out] expr_info information related to frame and rt_exprs
  int generate(const ObRawExprUniqueSet &all_raw_exprs, ObExprFrameInfo &expr_info);

  int generate(ObRawExpr *expr,
               ObRawExprUniqueSet &flattened_raw_exprs,
               ObExprFrameInfo &expr_info);

  int generate_calculable_exprs(const common::ObIArray<ObHiddenColumnItem> &calculable_exprs,
                                ObPreCalcExprFrameInfo &pre_calc_frame);

  int generate_calculable_expr(ObRawExpr *raw_expr,
                               ObPreCalcExprFrameInfo &pre_calc_frame,
                               ObExpr *&rt_expr);

  static int generate_rt_expr(const ObRawExpr &src,
                              common::ObIArray<ObRawExpr *> &exprs,
                              ObExpr *&dst);


  // Attention : Please think over before you have to use this function.
  // This function is different from generate_rt_expr.
  // It won't put raw_expr into cur_op_exprs_ because it doesn't need to be calculated.
  static void *get_left_value_rt_expr(const ObRawExpr &raw_expr);

  int detect_batch_size(const ObRawExprUniqueSet &exprs, int64_t &batch_size,
                        int64_t config_maxrows, int64_t config_target_maxsize,
                        const double scan_cardinality, int64_t lob_rowsets_max_rows);

  // TP workload VS AP workload:
  // TableScan cardinality(accessed rows) is used to determine TP load so far
  // More sophisticated rules would be added when optimizer support new
  // vectorized cost model
  bool is_oltp_workload(const double scan_cardinality) const {
    bool is_oltp_workload = false;
    if (scan_cardinality < OLTP_WORKLOAD_CARDINALITY) {
      is_oltp_workload = true;
    }
    return is_oltp_workload;
  }

  void set_batch_size(const int64_t v) { batch_size_ = v; }

  void set_rt_question_mark_eval(const bool v) { rt_question_mark_eval_ = v; }
  void set_contain_dynamic_eval_rt_qm(const bool v) { contain_dynamic_eval_rt_qm_ = v; }

  static int generate_partial_expr_frame(const ObPhysicalPlan &plan,
                                         ObExprFrameInfo &partial_expr_frame_info,
                                         ObIArray<ObRawExpr *> &raw_exprs,
                                         const bool use_rich_format);

  void set_need_flatten_gen_col(const bool v) { need_flatten_gen_col_ = v; }

  static int gen_expr_with_row_desc(const ObRawExpr *expr,
                                    const RowDesc &row_desc,
                                    ObIAllocator &alloctor,
                                    ObSQLSessionInfo *session,
                                    share::schema::ObSchemaGetterGuard *schema_guard,
                                    ObTempExpr *&temp_expr,
                                    bool contain_dynamic_eval_rt_qm_ = false);

  static int init_temp_expr_mem_size(ObTempExpr &temp_expr);
  static int64_t frame_max_offset(const ObExpr &e, const int64_t batch_size, const bool use_rich_format);

private:
  static ObExpr *get_rt_expr(const ObRawExpr &raw_expr);
  // Construct ObExpr, and set the corresponding ObExpr to the corresponding ObRawExpr
  // @param [in]  raw_exprs
  // @param [out] rt_exprs, constructed physical expressions
  int construct_exprs(const common::ObIArray<ObRawExpr *> &raw_exprs,
                      common::ObIArray<ObExpr> &rt_exprs);
  // Initialize the corresponding rt_expr in raw_expr, and return frame information
  // @param [in/out] raw_exprs expressions used to generate rt_exprs
  // @param [out] expr_info frame and rt_exprs related information
  int cg_exprs(const common::ObIArray<ObRawExpr *> &raw_exprs,
               ObExprFrameInfo &expr_info);

  // init type_, datum_meta_, obj_meta_, obj_datum_map_, args_, arg_cnt_
  // row_dimension_, op_
  int cg_expr_basic(const common::ObIArray<ObRawExpr *> &raw_exprs);

  int init_attr_expr(ObExpr *rt_expr, ObRawExpr *raw_expr);

  // init parent_cnt_, parents_
  int cg_expr_parents(const common::ObIArray<ObRawExpr *> &raw_exprs);

  // init eval_func_, inner_eval_func_, expr_ctx_id_, extra_
  int cg_expr_by_operator(const common::ObIArray<ObRawExpr *> &raw_exprs,
                          int64_t &total_ctx_cnt);

  // init res_buf_len_, frame_idx_, datum_off_, res_buf_off_
  // @param [in/out] raw_exprs
  // @param [out] expr_info frame information
  int cg_all_frame_layout(const common::ObIArray<ObRawExpr *> &raw_exprs,
                          ObExprFrameInfo &expr_info);

  int cg_expr_basic_funcs(const common::ObIArray<ObRawExpr *> &raw_exprs);

  // alloc stack overflow check exprs.
  int alloc_so_check_exprs(const common::ObIArray<ObRawExpr *> &raw_exprs,
                           ObExprFrameInfo &expr_info);

  // calculate res_buf_len_ for exprs' datums
  int calc_exprs_res_buf_len(const common::ObIArray<ObRawExpr *> &raw_exprs);

  // create tmp frameinfo based on expr info
  int create_tmp_frameinfo(const common::ObIArray<ObRawExpr *> &raw_exprs,
                           common::ObIArray<TmpFrameInfo> &tmp_frame_infos,
                           int64_t &frame_index_pos);
  // Divide the expression into 4 categories based on its frame type
  // @param [in] raw_exprs all expressions to be classified
  // @param [out] const_exprs all expressions belonging to the const frame
  // @param [out] param_exprs all expressions belonging to the param frame
  // @param [out] dynamic_param_exprs all expressions belonging to the dynamic frame
  // @param [out] no_const_param_exprs all expressions belonging to the datum frame
  int classify_exprs(const common::ObIArray<ObRawExpr *> &raw_exprs,
                     common::ObIArray<ObRawExpr *> &const_exprs,
                     common::ObIArray<ObRawExpr *> &param_exprs,
                     common::ObIArray<ObRawExpr *> &dynamic_param_exprs,
                     common::ObIArray<ObRawExpr *> &no_const_param_exprs) const;
  // Initialize const expr layout in frame
  // @param [in/out] const_exprs all expressions of the const frame
  // @param [in/out] frame_index_pos offset of generated frame, used for calculating the current generated frame
  //                 index in all frames
  // @param [out]    frame_info_arr all const frame related information
  int cg_const_frame_layout(const common::ObIArray<ObRawExpr *> &const_exprs,
                            int64_t &frame_index_pos,
                            common::ObIArray<ObFrameInfo> &frame_info_arr);
  // Initialize param expr layout in frame
  // @param [in/out] param_exprs all expressions of const frame
  // @param [in/out] frame_index_pos offset of generated frame, used for calculating the current generated frame
  //                 index in all frames
  // @param [out]    frame_info_arr all const frame related information
  int cg_param_frame_layout(const common::ObIArray<ObRawExpr *> &param_exprs,
                            int64_t &frame_index_pos,
                            common::ObIArray<ObFrameInfo> &frame_info_arr);
  // Initialize dynamic param expr layout in frame
  // @param [in/out] const_exprs all expressions of the const frame
  // @param [in/out] frame_index_pos offset of generated frame, used for calculating the current generated frame
  //                 index in all frames
  // @param [out]    frame_info_arr all const frame related information
  int cg_dynamic_frame_layout(const common::ObIArray<ObRawExpr *> &exprs,
                              int64_t &frame_index_pos,
                              common::ObIArray<ObFrameInfo> &frame_info_arr);
  // Initialize non-const and param expr layout in frame
  // @param [in/out] exprs all expressions of non-const and param expr
  // @param [in/out] frame_index_pos offset of generated frame, used for calculating the current generated frame
  //                 index in all frames
  // @param [out]    frame_info_arr all datum frame related information
  int cg_datum_frame_layouts(const common::ObIArray<ObRawExpr *> &exprs,
                            int64_t &frame_index_pos,
                            common::ObIArray<ObFrameInfo> &frame_info_arr);
  // Initialize frame layout
  // @param [in/out] exprs all expressions to be calculated for frame layout
  // @param [in] reserve_empty_string Whether to allocate reserved memory for string type in frame
  // @param [in] continuous_datum ObDatum + ObEvalInfo should be continuous
  // @param [in/out] frame_index_pos offset of generated frame, used for calculating the current generated frame
  //                 index in all frames
  // @param [out]    frame_info_arr all frame related information
  int cg_frame_layout(const common::ObIArray<ObRawExpr *> &exprs,
                      const bool reserve_empty_string,
                      const bool continuous_datum,
                      int64_t &frame_index_pos,
                      common::ObIArray<ObFrameInfo> &frame_info_arr);

  // new frame layout: the vector version
  int cg_frame_layout_vector_version(const common::ObIArray<ObRawExpr *> &exprs,
                      const bool continuous_datum,
                      int64_t &frame_index_pos,
                      common::ObIArray<ObFrameInfo> &frame_info_arr);
  // Allocate constant expression frame memory, and initialize
  // @param [in] exprs all constant expressions
  // @param [in] const_frames all const frame related information
  // @param [out] frame_ptrs initialized frame pointer list
  int alloc_const_frame(const common::ObIArray<ObRawExpr *> &exprs,
                        const common::ObIArray<ObFrameInfo> &const_frames,
                        common::ObIArray<char *> &frame_ptrs);

  // called after res_buf_len_ assigned to get the buffer size with dynamic reserved buffer.
  int64_t reserve_data_consume(const ObExpr &expr)
  {
    const bool need_dyn_buf = ObDynReserveBuf::supported(expr.datum_meta_.type_);
    return expr.res_buf_len_
        + (need_dyn_buf && expr.res_buf_len_ > 0 ? sizeof(ObDynReserveBuf) : 0);
  }

  // called after res_buf_len_ assigned to get the buffer size with dynamic reserved buffer.
  int64_t reserve_data_consume(const common::ObObjType &type, const int16_t prec)
  {
    const bool need_dyn_buf = ObDynReserveBuf::supported(type);
    // ObObjDatumMapType datum_map_type = ObDatum::get_obj_datum_map_type(type);
    auto res_buf_len =
        ObDatum::get_reserved_size(ObDatum::get_obj_datum_map_type(type), prec);
    return res_buf_len +
           +(need_dyn_buf && res_buf_len > 0 ? sizeof(ObDynReserveBuf) : 0);
  }

  inline int64_t get_expr_datums_count(const ObExpr &expr)
  {
    return get_expr_datums_count(expr, batch_size_);
  }

  static inline int64_t get_expr_datums_count(const ObExpr &expr, int64_t batch_size)
  {
    OB_ASSERT(!(expr.is_batch_result() && batch_size == 0));
    return expr.is_batch_result() ? batch_size : 1;
  }

  inline int64_t get_expr_skip_vector_size(const ObExpr &expr)
  {
    return get_expr_skip_vector_size(expr, batch_size_);
  }

  static inline int64_t get_expr_skip_vector_size(const ObExpr &expr, int64_t batch_size)
  {
    return expr.is_batch_result() ? ObBitVector::memory_size(batch_size) : 1;
  }

  inline int64_t get_expr_bitmap_vector_size(const ObExpr &expr)
  {
    return get_expr_bitmap_vector_size(expr, batch_size_);
  }

  static inline int64_t get_expr_bitmap_vector_size(const ObExpr &expr, int64_t batch_size)
  {
    batch_size = expr.is_batch_result() ? batch_size : 1;
    return ObBitVector::memory_size(batch_size);
  }

  int64_t dynamic_buf_header_size(const ObExpr &expr)
  {
    return dynamic_buf_header_size(expr, batch_size_);
  }

  static int64_t dynamic_buf_header_size(const ObExpr &expr, int64_t batch_size)
  {
    const bool need_dyn_buf = ObDynReserveBuf::supported(expr.datum_meta_.type_);
    batch_size = expr.is_batch_result() ? batch_size : 1;
    return (need_dyn_buf ? batch_size * sizeof(ObDynReserveBuf) : 0);
  }

  // vector version of reserve_data_consume
  int64_t reserve_datums_buf_len(const ObExpr &expr)
  {
    const bool need_dyn_buf = ObDynReserveBuf::supported(expr.datum_meta_.type_);
    return expr.res_buf_len_ *
               get_expr_datums_count(expr) /* reserve datums part */
           + (need_dyn_buf ? get_expr_datums_count(expr) * sizeof(ObDynReserveBuf)
                           : 0) /* dynamic datums part */;
  }

  inline int64_t get_datums_header_size(const ObExpr &expr)
  {
    return sizeof(ObDatum) * get_expr_datums_count(expr);
  }

  int arrange_datum_data(common::ObIArray<ObRawExpr *> &exprs,
                         const ObFrameInfo &frame,
                         const bool continuous_datum);

  // vector version of arrange_datum_data
  int arrange_datums_data(common::ObIArray<ObRawExpr *> &exprs,
                         const ObFrameInfo &frame,
                         const bool continuous_datum);

  int inner_generate_calculable_exprs(
                                const common::ObIArray<ObHiddenColumnItem> &calculable_exprs,
                                ObPreCalcExprFrameInfo &expr_info);

  // total datums size: header + reserved data
  int64_t get_expr_datums_size(const ObExpr &expr) {
    int64_t size = get_expr_datums_header_size(expr) + reserve_datums_buf_len(expr);
    if (use_rich_format()) {
      size += get_rich_format_size(expr);
    }

    return size;
  }

  // datums meta/header size vector version.
  // four parts:
  // - datum instance vector
  // - EvalInfo instance
  // - EvalFlag(BitVector) instance + BitVector data
  // - SkipBitmap(BitVector) + BitVector data
  int64_t get_expr_datums_header_size(const ObExpr &expr) {
    int64_t size = get_datums_header_size(expr)
                   + sizeof(ObEvalInfo)
                   + get_expr_skip_vector_size(expr) /*skip*/
                   + get_expr_bitmap_vector_size(expr); /*eval flags*/

    return size;
  }

  static int64_t get_vector_header_size() {
    return sizeof(VectorHeader);
  }

  // ptrs
  inline int64_t get_ptrs_size(const ObExpr &expr) {
    return get_ptrs_size(expr, batch_size_);
  }

  static inline int64_t get_ptrs_size(const ObExpr &expr, int64_t batch_size) {
    return expr.is_fixed_length_data_ ? 0 : sizeof(char *) * get_expr_datums_count(expr, batch_size);
  }

  // cont dynamic buf header size
  int64_t cont_dynamic_buf_header_size(const ObExpr &expr) {
    return expr.is_fixed_length_data_
           ? 0
           : sizeof(ObDynReserveBuf);
  }

  // lens / offset
  inline int64_t get_offsets_size(const ObExpr &expr) {
    return get_offsets_size(expr, batch_size_);
  }

  static inline int64_t get_offsets_size(const ObExpr &expr, int64_t batch_size) {
    return expr.is_fixed_length_data_ ? 0 : sizeof(uint32_t) * (get_expr_datums_count(expr, batch_size) + 1);
  }

  int64_t get_rich_format_size(const ObExpr &expr) {
    int64_t size = 0;
    size += get_offsets_size(expr);
    size += get_ptrs_size(expr);
    size += get_vector_header_size();
    size += get_expr_bitmap_vector_size(expr); /* null bitmaps*/
    size += cont_dynamic_buf_header_size(expr);

    return size;
  }

  // datum meta/header size non-vector version.
  // two parts:
  // - datum instance
  // - EvalInfo instance
  int get_expr_datum_fixed_header_size() {
    return sizeof(ObDatum) + sizeof(ObEvalInfo);
  }
  // Fetch non const exprs
  // @param [in] raw_exprs all raw exprs
  // @param [out] no_const_param_exprs exprs using datum frame
  int get_vectorized_exprs(const common::ObIArray<ObRawExpr *> &raw_exprs,
                     common::ObIArray<ObRawExpr *> &no_const_param_exprs) const;
  bool is_vectorized_expr(const ObRawExpr *raw_expr) const;
  int compute_max_batch_size(const ObRawExpr *raw_expr);

  enum class ObExprBatchSize {
    one = 1,
    small = 8,
    full = 65535
  };
  // Certain exprs can NOT be executed vectorizely. Check the exps within this
  // routine
  ObExprBatchSize
  get_expr_execute_size(const common::ObIArray<ObRawExpr *> &raw_exprs, int64_t lob_rowsets_max_rows);
  inline bool is_large_data(ObObjType type) {
    bool b_large_data = false;
    if (type == ObLongTextType
        || type == ObMediumTextType
        || type == ObTextType
        || type == ObJsonType
        || type == ObUserDefinedSQLType
        || type == ObCollectionSQLType) {
      b_large_data = true;
    }
    return b_large_data;
  }

  // get the frame idx of param frames and datum index (in frame) by index of param store.
  void get_param_frame_idx(const int64_t idx, int64_t &frame_idx, int64_t &datum_idx);

  int divide_probably_local_exprs(common::ObIArray<ObRawExpr *> &exprs);

  bool use_rich_format() const;

private:
  int generate_extra_questionmarks(ObRawExprUniqueSet &flattened_raw_exprs, ObRawExprFactory &factory);
  bool is_dynamic_eval_qm(const ObRawExpr &raw_expr) const;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObStaticEngineExprCG);

private:
  // Used to allocate memory for the expr object generated by cg
  common::ObIAllocator &allocator_;
  // All parameterized constant objects
  DatumParamStore *param_store_;

  // original param cnt, see comment of ObPhysicalPlanCtx
  int64_t original_param_cnt_;

  //count of param store
  int64_t param_cnt_;
  // operator cg's context
  ObExprCGCtx op_cg_ctx_;
  // Count of param store in generating, for calculable expressions CG.
  int64_t flying_param_cnt_;
  // batch size detected in generate()
  int64_t batch_size_;

  // evaluate question mark expression at run time, used in PL which has no param frames.
  bool rt_question_mark_eval_;
  //is code generate temp expr witch used in table location
  bool need_flatten_gen_col_;
  uint64_t cur_cluster_version_;
  common::ObFixedArray<ObRawExpr *, common::ObIAllocator> gen_questionmarks_;
  bool contain_dynamic_eval_rt_qm_;
};

} // end namespace sql
} // end namespace oceanbase
#endif /*OCEANBASE_SQL_CODE_GENERATOR_OB_STATIC_ENGINE_EXPR_CG_*/
