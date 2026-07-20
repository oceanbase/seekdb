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

#include "ob_expr_eval_functions.h"
#include "ob_expr_bit_count.h"
#include "ob_expr_bit_neg.h"
#include "ob_expr_column_conv.h"
#include "ob_expr_concat.h"
#include "ob_expr_connection_id.h"
#include "ob_expr_conv.h"
#include "ob_expr_current_user.h"
#include "ob_expr_current_user_priv.h"
#include "ob_expr_cur_time.h"
#include "ob_expr_database.h"
#include "ob_expr_date.h"
#include "ob_expr_date_diff.h"
#include "ob_expr_day_of_func.h"
#include "ob_expr_div.h"
#include "ob_expr_exists.h"
#include "ob_expr_extract.h"
#include "ob_expr_export_set.h"
#include "ob_expr_found_rows.h"
#include "ob_expr_from_unix_time.h"
#include "ob_expr_func_partition_key.h"
#include "ob_expr_greatest.h"
#include "ob_expr_host_ip.h"
#include "ob_expr_trim.h"
#include "ob_expr_tokenize.h"
#include "ob_expr_insert.h"
#include "ob_expr_int2ip.h"
#include "ob_expr_int_div.h"
#include "ob_expr_ip2int.h"
#include "ob_expr_inet.h"
#include "ob_expr_is.h"
#include "ob_expr_last_exec_id.h"
#include "ob_expr_last_trace_id.h"
#include "ob_expr_length.h"
#include "ob_expr_like.h"
#include "ob_expr_lower.h"
#include "ob_expr_md5.h"
#include "ob_expr_crc32.h"
#include "src/sql/engine/expr/ob_expr_substr.h"
#include "ob_expr_minus.h"
#include "ob_expr_mod.h"
#include "ob_expr_mul.h"
#include "ob_expr_mysql_port.h"
#include "ob_expr_not.h"
#include "ob_expr_not_exists.h"
#include "ob_expr_null_safe_equal.h"
#include "ob_expr_nullif.h"
#include "ob_expr_nvl.h"
#include "ob_expr_pow.h"
#include "ob_expr_regexp.h"
#include "ob_expr_regexp_instr.h"
#include "ob_expr_regexp_like.h"
#include "ob_expr_regexp_replace.h"
#include "ob_expr_regexp_substr.h"
#include "ob_expr_repeat.h"
#include "ob_expr_replace.h"
#include "ob_expr_func_dump.h"
#include "ob_expr_func_part_hash.h"
#include "ob_expr_autoinc_nextval.h"
#include "ob_expr_sys_privilege_check.h"
#include "ob_expr_field.h"
#include "ob_expr_elt.h"
#include "ob_expr_des_hex_str.h"
#include "ob_expr_lnnvl.h"
#include "ob_expr_row_count.h"
#include "ob_expr_rpc_port.h"
#include "ob_expr_space.h"
#include "ob_expr_subquery_ref.h"
#include "ob_expr_substring_index.h"
#include "ob_expr_timestamp.h"
#include "ob_expr_trim.h"
#include "ob_expr_unhex.h"
#include "ob_expr_user.h"
#include "ob_expr_uuid.h"
#include "ob_expr_version.h"
#include "ob_expr_xor.h"
#include "ob_expr_estimate_ndv.h"
#include "ob_expr_find_in_set.h"
#include "ob_expr_get_sys_var.h"
#include "ob_expr_seq_nextval.h"
#include "ob_expr_ifnull.h"
#include "ob_expr_ascii.h"
#include "ob_expr_instr.h"
#include "ob_expr_concat_ws.h"
#include "ob_expr_make_set.h"
#include "ob_expr_sys_op_opnsize.h"
#include "ob_expr_quote.h"
#include "ob_expr_date_add.h"
#include "ob_expr_date_format.h"
#include "ob_expr_from_days.h"
#include "ob_expr_period_diff.h"
#include "ob_expr_time_diff.h"
#include "ob_expr_timestamp_nvl.h"
#include "ob_expr_week_of_func.h"
#include "ob_expr_fun_default.h"
#include "ob_expr_substrb.h"
#include "ob_expr_rand.h"
#include "ob_expr_randstr.h"
#include "ob_expr_random.h"
#include "ob_expr_lrpad.h"
#include "ob_expr_pad.h"
#include "ob_expr_fun_values.h"
#include "ob_expr_part_id.h"
#include "ob_expr_hex.h"
#include "ob_expr_shadow_uk_project.h"
#include "ob_expr_char_length.h"
#include "ob_expr_unix_timestamp.h"
#include "ob_expr_symmetric_encrypt.h"
#include "ob_expr_case.h"
#include "ob_expr_remove_const.h"
#include "ob_expr_wrapper_inner.h"
#include "ob_expr_func_sleep.h"
#include "ob_expr_errno.h"
#include "ob_expr_get_package_var.h"
#include "ob_expr_timestamp_diff.h"
#include "ob_expr_timestamp_add.h"
#include "ob_expr_get_user_var.h"
#include "ob_expr_cot.h"
#include "ob_expr_convert.h"
#include "ob_expr_type_to_str.h"
#include "ob_expr_date_format.h"
#include "ob_expr_last_insert_id.h"
#include "ob_expr_part_id_pseudo_column.h"
#include "ob_expr_radians.h"
#include "ob_expr_pi.h"
#include "ob_expr_maketime.h"
#include "ob_expr_makedate.h"
#include "ob_expr_to_outfile_row.h"
#include "ob_expr_format.h"
#include "ob_expr_and.h"
#include "ob_expr_or.h"
#include "ob_expr_quarter.h"
#include "ob_expr_bit_length.h"
#include "ob_expr_time_format.h"
#include "ob_expr_dll_udf.h"
#include "ob_expr_collection_construct.h"
#include "ob_expr_obj_access.h"
#include "ob_expr_pl_associative_index.h"
#include "ob_expr_udf.h"
#include "ob_expr_object_construct.h"
#include "ob_expr_pl_get_cursor_attr.h"
#include "ob_expr_pl_integer_checker.h"
#include "ob_expr_get_subprogram_var.h"
#include "ob_expr_pl_sqlcode_sqlerrm.h"
#include "ob_expr_coll_pred.h"
#include "ob_expr_stmt_id.h"
#include "ob_expr_output_pack.h"
#include "ob_expr_obversion.h"
#include "ob_expr_plsql_variable.h"
#include "ob_expr_degrees.h"
#include "ob_expr_any_value.h"
#include "ob_expr_uuid_short.h"
#include "ob_expr_func_round.h"
#include "ob_expr_validate_password_strength.h"
#include "ob_expr_soundex.h"
#include "ob_expr_benchmark.h"
#include "ob_expr_weight_string.h"
#include "ob_expr_convert_tz.h"
#include "ob_expr_to_base64.h"
#include "ob_expr_from_base64.h"
#include "ob_expr_random_bytes.h"
#include "ob_pl_expr_subquery.h"
#include "ob_expr_encode_sortkey.h"
#include "ob_expr_hash.h"
#include "ob_expr_json_object.h"
#include "ob_expr_json_extract.h"
#include "ob_expr_json_contains.h"
#include "ob_expr_json_schema_valid.h"
#include "ob_expr_json_schema_validation_report.h"
#include "ob_expr_json_contains_path.h"
#include "ob_expr_json_depth.h"
#include "ob_expr_json_keys.h"
#include "ob_expr_json_search.h"
#include "ob_expr_json_array.h"
#include "ob_expr_json_quote.h"
#include "ob_expr_json_unquote.h"
#include "ob_expr_json_overlaps.h"
#include "ob_expr_json_valid.h"
#include "ob_expr_json_remove.h"
#include "ob_expr_json_append.h"
#include "ob_expr_json_array_insert.h"
#include "ob_expr_json_value.h"
#include "ob_expr_json_replace.h"
#include "ob_expr_json_type.h"
#include "ob_expr_json_length.h"
#include "ob_expr_json_insert.h"
#include "ob_expr_json_storage_size.h"
#include "ob_expr_json_storage_free.h"
#include "ob_expr_json_set.h"
#include "ob_expr_json_merge.h"
#include "ob_expr_json_merge_patch.h"
#include "ob_expr_json_pretty.h"
#include "ob_expr_json_member_of.h"
#include "ob_expr_sha.h"
#include "ob_expr_compress.h"
#include "ob_expr_statement_digest.h"
#include "ob_expr_json_query.h"
#include "ob_expr_point.h"
#include "ob_expr_spatial_collection.h"
#include "ob_expr_st_area.h"
#include "ob_expr_st_intersects.h"
#include "ob_expr_st_x.h"
#include "ob_expr_st_transform.h"
#include "ob_expr_priv_st_transform.h"
#include "ob_expr_st_covers.h"
#include "ob_expr_st_bestsrid.h"
#include "ob_expr_st_astext.h"
#include "ob_expr_st_buffer.h"
#include "ob_expr_spatial_cellid.h"
#include "ob_expr_spatial_mbr.h"
#include "ob_expr_st_geomfromewkb.h"
#include "ob_expr_st_geomfromwkb.h"
#include "ob_expr_st_geomfromewkt.h"
#include "ob_expr_st_asewkt.h"
#include "ob_expr_st_distance.h"
#include "ob_expr_st_geometryfromtext.h"
#include "ob_expr_priv_st_setsrid.h"
#include "ob_expr_priv_st_point.h"
#include "ob_expr_priv_st_geographyfromtext.h"
#include "ob_expr_st_isvalid.h"
#include "ob_expr_st_dwithin.h"
#include "ob_expr_st_aswkb.h"
#include "ob_expr_st_distance_sphere.h"
#include "ob_expr_st_contains.h"
#include "ob_expr_st_within.h"
#include "ob_expr_priv_st_asewkb.h"
#include "ob_expr_name_const.h"
#include "ob_expr_format_bytes.h"
#include "ob_expr_format_pico_time.h"
#include "ob_expr_encrypt.h"
#include "ob_expr_coalesce.h"
#include "ob_expr_cast.h"
#include "ob_expr_current_scn.h"
#include "ob_expr_icu_version.h"
#include "ob_expr_sql_mode_convert.h"
#include "ob_expr_extract_value.h"
#include "ob_expr_update_xml.h"
#include "ob_expr_generator_func.h"
#include "ob_expr_random.h"
#include "ob_expr_randstr.h"
#include "ob_expr_zipf.h"
#include "ob_expr_normal.h"
#include "ob_expr_uniform.h"
#include "ob_expr_prefix_pattern.h"
#include "ob_expr_sin.h"
#include "ob_expr_between.h"
#include "ob_expr_align_date4cmp.h"
#include "ob_expr_word_count.h"
#include "ob_expr_word_segment.h"
#include "ob_expr_doc_id.h"
#include "ob_expr_doc_length.h"
#include "ob_expr_bm25.h"
#include "ob_expr_lock_func.h"
#include "ob_expr_extract_cert_expired_time.h"
#include "ob_expr_transaction_id.h"
#include "ob_expr_inner_row_cmp_val.h"
#include "ob_expr_sql_udt_construct.h"
#include "ob_expr_priv_st_numinteriorrings.h"
#include "ob_expr_priv_st_iscollection.h"
#include "ob_expr_priv_st_equals.h"
#include "ob_expr_priv_st_touches.h"
#include "ob_expr_align_date4cmp.h"
#include "ob_expr_priv_st_makeenvelope.h"
#include "ob_expr_priv_st_clipbybox2d.h"
#include "ob_expr_priv_st_pointonsurface.h"
#include "ob_expr_priv_st_geometrytype.h"
#include "ob_expr_st_crosses.h"
#include "ob_expr_st_overlaps.h"
#include "ob_expr_st_union.h"
#include "ob_expr_st_length.h"
#include "ob_expr_st_difference.h"
#include "ob_expr_st_asgeojson.h"
#include "ob_expr_st_centroid.h"
#include "ob_expr_st_symdifference.h"
#include "ob_expr_priv_st_asmvtgeom.h"
#include "ob_expr_priv_st_makevalid.h"
#include "ob_expr_array.h"
#include "ob_expr_vec_vector.h"
#include "ob_expr_vec_key.h"
#include "ob_expr_vec_scn.h"
#include "ob_expr_vec_vid.h"
#include "ob_expr_vec_data.h"
#include "ob_expr_vec_type.h"
#include "ob_expr_vec_chunk.h"
#include "ob_expr_embedded_vec.h"
#include "ob_expr_spiv_dim.h"
#include "ob_expr_spiv_value.h"
#include "ob_expr_vector.h"
#include "ob_expr_func_ceil.h"
#include "ob_expr_topn_filter.h"
#include "ob_expr_gtid.h"
#include "ob_expr_inner_table_option_printer.h"
#include "ob_expr_password.h"
#include "ob_expr_decode_trace_id.h"
#include "ob_expr_array_contains.h"
#include "ob_expr_array_to_string.h"
#include "ob_expr_string_to_array.h"
#include "ob_expr_array_append.h"
#include "ob_expr_array_concat.h"
#include "ob_expr_array_difference.h"
#include "ob_expr_array_max.h"
#include "ob_expr_array_avg.h"
#include "ob_expr_array_compact.h"
#include "ob_expr_array_sort.h"
#include "ob_expr_array_sortby.h"
#include "ob_expr_array_filter.h"
#include "ob_expr_element_at.h"
#include "ob_expr_array_cardinality.h"
#include "ob_expr_can_access_trigger.h"
#include "ob_expr_split_part.h"
#include "ob_expr_inner_decode_like.h"
#include "ob_expr_inner_double_to_int.h"
#include "ob_expr_inner_decimal_to_year.h"
#include "ob_expr_array_overlaps.h"
#include "ob_expr_array_contains_all.h"
#include "ob_expr_array_distinct.h"
#include "ob_expr_array_remove.h"
#include "ob_expr_array_map.h"
#include "ob_expr_array_range.h"
#include "ob_expr_array_first.h"
#include "ob_expr_mysql_proc_info.h"
#include "ob_expr_get_mysql_routine_parameter_type_str.h"
#include "ob_expr_keyvalue.h"
#include "ob_expr_url_codec.h"
#include "ob_expr_priv_st_geohash.h"
#include "ob_expr_priv_st_makepoint.h"
#include "ob_expr_to_pinyin.h"
#include "ob_expr_demote_cast.h"
#include "ob_expr_array_sum.h"
#include "ob_expr_array_length.h"
#include "ob_expr_array_position.h" 
#include "ob_expr_array_slice.h"
#include "ob_expr_vec_ivf_center_id.h"
#include "ob_expr_vec_ivf_center_vector.h"
#include "ob_expr_vec_ivf_flat_data_vector.h"
#include "ob_expr_vec_ivf_meta_id.h"
#include "ob_expr_vec_ivf_meta_vector.h"
#include "ob_expr_vec_ivf_sq8_data_vector.h"
#include "ob_expr_vec_ivf_pq_center_id.h"
#include "ob_expr_vec_ivf_pq_center_ids.h"
#include "ob_expr_vec_ivf_pq_center_vector.h"
#include "ob_expr_bool.h"
#include "ob_expr_not_between.h"
#include "ob_expr_inner_info_cols_printer.h"
#include "ob_expr_array_except.h"
#include "ob_expr_array_intersect.h"
#include "ob_expr_array_union.h"
#include "ob_expr_map.h"
#include "ob_expr_map_keys.h"
#include "ob_expr_current_catalog.h"
#include "ob_expr_check_catalog_access.h"
#include "ob_expr_semantic_distance.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_complete.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_embed.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_rerank.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_prompt.h"
#include "ob_expr_vector_similarity.h"
#include "ob_expr_check_location_access.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

//
// this file is for function serialization
// Without maps defined here, you can not get correct function ptr
// when serialize between different observer versions
//
extern int cast_eval_arg(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int anytype_to_varchar_char_explicit(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int anytype_anytype_explicit(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_acos_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_and_exprN(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_asin_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_assign_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_atan2_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_atan_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_between_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_bool_expr_for_integer_type(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_bool_expr_for_float_type(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_bool_expr_for_double_type(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_bool_expr_for_other_type(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_bool_expr_for_decint_type(const ObExpr &, ObEvalCtx &, ObDatum &);

extern int calc_char_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_coalesce_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_cos_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_exp_expr_double(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_exp_expr_number(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_ceil_floor(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_round_expr_datetime1(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_round_expr_numeric2(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_round_expr_numeric1(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_left_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_log10_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_log2_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_log_expr_double(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_log_expr_number(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_not_between_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_or_exprN(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_right_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_sign_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_sin_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_sqrt_expr_mysql(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_str_to_date_expr_date(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_str_to_date_expr_time(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_str_to_date_expr_datetime(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_tan_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_timestampadd_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_time_to_usec_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_todays_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_to_temporal_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_usec_to_time_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_charset_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_collation_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_coercibility_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_set_collation_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_cmp_meta_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_truncate_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_reverse_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);
extern int calc_convert_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);
extern int eval_question_mark_func(EVAL_FUNC_ARG_DECL);
extern int cast_eval_arg_batch(const ObExpr &, ObEvalCtx &, const ObBitVector &, const int64_t);
extern int eval_batch_ceil_floor(const ObExpr &, ObEvalCtx &, const ObBitVector &, const int64_t);
extern int eval_assign_question_mark_func(EVAL_FUNC_ARG_DECL);
extern int calc_timestamp_to_scn_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_scn_to_timestamp_expr(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int calc_sqrt_expr_mysql_in_batch(const ObExpr &, ObEvalCtx &, const ObBitVector &, const int64_t);
extern int eval_questionmark_decint2nmb(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int eval_questionmark_nmb2decint_eqcast(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int eval_questionmark_decint2decint_eqcast(const ObExpr &, ObEvalCtx &, ObDatum &);
extern int eval_questionmark_decint2decint_normalcast(const ObExpr &, ObEvalCtx &, ObDatum &);

// Function serialization table. Keep entries and indices contiguous.
static ObExpr::EvalFunc g_expr_eval_functions[] = {
  cast_eval_arg,                                                      /* 0 */
  anytype_to_varchar_char_explicit,                                   /* 1 */
  anytype_anytype_explicit,                                           /* 2 */
  calc_acos_expr,                                                     /* 3 */
  ObExprAdd::add_datetime_datetime,                                   /* 4 */
  NULL,                                                               /* 5 */
  NULL,                                                               /* 6 */
  ObExprAdd::add_datetime_number,                                     /* 7 */
  ObExprAdd::add_double_double,                                       /* 8 */
  ObExprAdd::add_float_float,                                         /* 9 */
  NULL,                                                               /* 10 */
  NULL,                                                               /* 11 */
  NULL,                                                               /* 12 */
  NULL,                                                               /* 13 */
  NULL,                                                               /* 14 */
  NULL,                                                               /* 15 */
  NULL,                                                               /* 16 */
  NULL,                                                               /* 17 */
  NULL,                                                               /* 18 */
  ObExprAdd::add_int_int,                                             /* 19 */
  ObExprAdd::add_int_uint,                                            /* 20 */
  ObExprAdd::add_number_datetime,                                     /* 21 */
  ObExprAdd::add_number_number,                                       /* 22 */
  NULL,                                                               /* 23 */
  NULL,                                                               /* 24 */
  NULL,                                                               /* 25 */
  NULL,                                                               /* 26 */
  NULL,                                                               /* 27 */
  NULL,                                                               /* 28 */
  NULL,                                                               /* 29 */
  NULL,                                                               /* 30 */
  NULL,                                                               /* 31 */
  NULL,                                                               /* 32 */
  ObExprAdd::add_uint_int,                                            /* 33 */
  ObExprAdd::add_uint_uint,                                           /* 34 */
  calc_and_exprN,                                                     /* 35 */
  calc_asin_expr,                                                     /* 36 */
  calc_assign_expr,                                                   /* 37 */
  calc_atan2_expr,                                                    /* 38 */
  calc_atan_expr,                                                     /* 39 */
  calc_between_expr,                                                  /* 40 */
  ObExprBitCount::calc_bitcount_expr,                                 /* 41 */
  ObExprBitNeg::calc_bitneg_expr,                                     /* 42 */
  calc_bool_expr_for_integer_type,                                    /* 43 */
  calc_bool_expr_for_float_type,                                      /* 44 */
  calc_bool_expr_for_double_type,                                     /* 45 */
  calc_bool_expr_for_other_type,                                      /* 46 */
  calc_char_expr,                                                     /* 47 */
  calc_coalesce_expr,                                                 /* 48 */
  ObExprColumnConv::column_convert,                                   /* 49 */
  ObExprConcat::eval_concat,                                          /* 50 */
  ObExprConnectionId::eval_connection_id,                             /* 51 */
  ObExprConv::eval_conv,                                              /* 52 */
  calc_cos_expr,                                                      /* 53 */
  NULL,                                                               /* 54 */
  ObExprCurrentUser::eval_current_user,                               /* 55 */
  ObExprUtcTimestamp::eval_utc_timestamp,                             /* 56 */
  ObExprCurTimestamp::eval_cur_timestamp,                             /* 57 */
  ObExprSysdate::eval_sysdate,                                        /* 58 */
  ObExprCurDate::eval_cur_date,                                       /* 59 */
  ObExprCurTime::eval_cur_time,                                       /* 60 */
  ObExprDatabase::eval_database,                                      /* 61 */
  ObExprDate::eval_date,                                              /* 62 */
  ObExprDateDiff::eval_date_diff,                                     /* 63 */
  NULL, // ObExprMonthsBetween::eval_months_between,                  /* 64 */
  ObExprToSeconds::calc_toseconds,                                    /* 65 */
  ObExprSecToTime::calc_sectotime,                                    /* 66 */
  ObExprTimeToSec::calc_timetosec,                                    /* 67 */
  ObExprSubtime::subaddtime_datetime,                                    /* 68 */
  ObExprSubtime::subaddtime_varchar,                                     /* 69 */
  ObExprDiv::div_float,                                               /* 70 */
  ObExprDiv::div_double,                                              /* 71 */
  ObExprDiv::div_number,                                              /* 72 */
  NULL,                                                               /* 73 */
  NULL,                                                               /* 74 */
  NULL,                       /* 75 */
  NULL,                  /* 76 */
  ObExprExists::exists_eval,                                          /* 77 */
  calc_exp_expr_double,                                               /* 78 */
  calc_exp_expr_number,                                               /* 79 */
  NULL,                                                               /* 80 */
  ObExprExtract::calc_extract_mysql,                                  /* 81 */
  ObExprFoundRows::eval_found_rows,                                   /* 82 */
  ObExprFromUnixTime::eval_one_temporal_fromtime,                     /* 83 */
  ObExprFromUnixTime::eval_one_param_fromtime,                        /* 84 */
  ObExprFromUnixTime::eval_fromtime_normal,                           /* 85 */
  ObExprFromUnixTime::eval_fromtime_special,                          /* 86 */
  calc_ceil_floor,                                                    /* 87 */
  ObExprFuncPartKey::calc_partition_key,                              /* 88 */
  calc_round_expr_datetime1,                                          /* 89 */
  NULL, // calc_round_expr_datetime2,                                 /* 90 */
  calc_round_expr_numeric2,                                           /* 91 */
  calc_round_expr_numeric1,                                           /* 92 */
  ObExprGreatest::calc_greatest,                                      /* 93 */
  ObExprHostIP::eval_host_ip,                                         /* 94 */
  NULL,                                                               /* 95 */
  ObExprTrim::eval_trim,                                              /* 96 */
  ObExprInsert::calc_expr_insert,                                     /* 97 */
  ObExprInt2ip::int2ip_varchar,                                       /* 98 */
  ObExprIntDiv::div_int_int,                                          /* 99 */
  ObExprIntDiv::div_int_uint,                                         /* 100 */
  ObExprIntDiv::div_uint_int,                                         /* 101 */
  ObExprIntDiv::div_uint_uint,                                        /* 102 */
  ObExprIntDiv::div_number,                                           /* 103 */
  ObExprIp2int::ip2int_varchar,                                       /* 104 */
  ObExprIs::calc_is_date_int_null,                                    /* 105 */
  ObExprIs::calc_is_null,                                             /* 106 */
  ObExprIs::int_is_true,                                              /* 107 */
  ObExprIs::int_is_false,                                             /* 108 */
  ObExprIs::float_is_true,                                            /* 109 */
  ObExprIs::float_is_false,                                           /* 110 */
  ObExprIs::double_is_true,                                           /* 111 */
  ObExprIs::double_is_false,                                          /* 112 */
  ObExprIs::number_is_true,                                           /* 113 */
  ObExprIs::number_is_false,                                          /* 114 */
  ObExprIsNot::calc_is_not_null,                                      /* 115 */
  ObExprIsNot::int_is_not_true,                                       /* 116 */
  ObExprIsNot::int_is_not_false,                                      /* 117 */
  ObExprIsNot::float_is_not_true,                                     /* 118 */
  ObExprIsNot::float_is_not_false,                                    /* 119 */
  ObExprIsNot::double_is_not_true,                                    /* 120 */
  ObExprIsNot::double_is_not_false,                                   /* 121 */
  ObExprIsNot::number_is_not_true,                                    /* 122 */
  ObExprIsNot::number_is_not_false,                                   /* 123 */
  ObExprLastExecId::eval_last_exec_id,                                /* 124 */
  ObExprLastTraceId::eval_last_trace_id,                              /* 125 */
  ObExprLeast::calc_least,                                            /* 126 */
  calc_left_expr,                                                     /* 127 */
  ObExprLength::calc_null,                                            /* 128 */
  NULL, /* retired slot */                                           /* 129 */
  ObExprLength::calc_mysql_mode,                                      /* 130 */
  ObExprLike::like_varchar,                                           /* 131 */
  NULL, // calc_ln_expr_mysql,                                        /* 132 */
  NULL, /* retired slot */                                           /* 133 */
  NULL, /* retired slot */                                           /* 134 */
  calc_log10_expr,                                                    /* 135 */
  calc_log2_expr,                                                     /* 136 */
  calc_log_expr_double,                                               /* 137 */
  calc_log_expr_number,                                               /* 138 */
  ObExprLower::calc_lower,                                            /* 139 */
  ObExprUpper::calc_upper,                                            /* 140 */
  ObExprMd5::calc_md5,                                                /* 141 */
  ObExprMinus::minus_datetime_datetime,                               /* 142 */
  NULL, /* retired slot */                   /* 143 */
  NULL,                                                               /* 144 */
  NULL,                                                               /* 145 */
  ObExprMinus::minus_datetime_number,                                 /* 146 */
  ObExprMinus::minus_double_double,                                   /* 147 */
  ObExprMinus::minus_float_float,                                     /* 148 */
  NULL,                                                               /* 149 */
  NULL,                                                               /* 150 */
  ObExprMinus::minus_int_int,                                         /* 151 */
  ObExprMinus::minus_int_uint,                                        /* 152 */
  ObExprMinus::minus_number_number,                                   /* 153 */
  NULL,                                                               /* 154 */
  NULL,                                                               /* 155 */
  NULL,                                                               /* 156 */
  NULL,                                                               /* 157 */
  NULL,                                                               /* 158 */
  NULL,                                                               /* 159 */
  ObExprMinus::minus_uint_int,                                        /* 160 */
  ObExprMinus::minus_uint_uint,                                       /* 161 */
  ObExprMod::mod_double,                                              /* 162 */
  ObExprMod::mod_float,                                               /* 163 */
  ObExprMod::mod_int_int,                                             /* 164 */
  ObExprMod::mod_int_uint,                                            /* 165 */
  ObExprMod::mod_number,                                              /* 166 */
  ObExprMod::mod_uint_int,                                            /* 167 */
  ObExprMod::mod_uint_uint,                                           /* 168 */
  ObExprMul::mul_double,                                              /* 169 */
  ObExprMul::mul_float,                                               /* 170 */
  NULL,                                                               /* 171 */
  NULL,                                                               /* 172 */
  ObExprMul::mul_int_int,                                             /* 173 */
  ObExprMul::mul_int_uint,                                            /* 174 */
  ObExprMul::mul_number,                                              /* 175 */
  NULL,                                                               /* 176 */
  NULL,                                                               /* 177 */
  ObExprMul::mul_uint_int,                                            /* 178 */
  ObExprMul::mul_uint_uint,                                           /* 179 */
  ObExprMySQLPort::eval_mysql_port,                                   /* 180 */
  NULL, // ObExprNeg::eval_tinyint is deleted                         /* 181 */
  calc_not_between_expr,                                              /* 182 */
  ObExprNot::eval_not,                                                /* 183 */
  ObExprNotExists::not_exists_eval,                                   /* 184 */
  ObExprNullSafeEqual::ns_equal_eval,                                 /* 185 */
  ObExprNullSafeEqual::row_ns_equal_eval,                             /* 186 */
  ObExprNvlUtil::calc_nvl_expr,                                       /* 187 */
  ObExprNvlUtil::calc_nvl_expr2,                                      /* 188 */
  ObSubQueryRelationalExpr::subquery_cmp_eval,                        /* 189 */
  NULL,                                                               /* 190 */
  ObBitwiseExprOperator::calc_result2_mysql,                          /* 191 */
  ObRelationalExprOperator::row_eval,                                 /* 192 */
  calc_or_exprN,                                                      /* 193 */
  ObExprPow::calc_pow_expr,                                           /* 194 */
  NULL, /* retired slot */                                           /* 195 */
  ObExprRegexp::eval_regexp,                                          /* 196 */
  NULL,                                                               /* 197 */
  ObExprRegexpInstr::eval_regexp_instr,                               /* 198 */
  ObExprRegexpLike::eval_regexp_like,                                 /* 199 */
  ObExprRegexpReplace::eval_regexp_replace,                           /* 200 */
  ObExprRegexpSubstr::eval_regexp_substr,                             /* 201 */
  ObExprRepeat::eval_repeat,                                          /* 202 */
  ObExprReplace::eval_replace,                                        /* 203 */
  ObExprFuncDump::eval_dump,                                          /* 204 */
  NULL,//ObExprFuncPartOldKey::eval_part_old_key is deleted           /* 205 */
  ObExprFuncPartHash::eval_part_hash,                                 /* 206 */
  NULL,//ObExprFuncAddrToPartId::eval_addr_to_part_id is deleted      /* 207 */
  ObExprAutoincNextval::eval_nextval,                                 /* 208 */
  ObExprFuncLnnvl::eval_lnnvl,                                        /* 209 */
  NULL,                      /* 210 */
  ObExprSysPrivilegeCheck::eval_sys_privilege_check,                  /* 211 */
  ObExprField::eval_field,                                            /* 212 */
  ObExprElt::eval_elt,                                                /* 213 */
  ObExprDesHexStr::eval_des_hex_str,                                  /* 214 */
  calc_right_expr,                                                    /* 215 */
  ObExprRowCount::eval_row_count,                                     /* 216 */
  NULL,                                                               /* 217 */
  ObExprRpcPort::eval_rpc_port,                                       /* 218 */
  ObExprCot::calc_cot_expr,                                           /* 219 */
  calc_sign_expr,                                                     /* 220 */
  calc_sin_expr,                                                      /* 221 */
  NULL,                                                               /* 222 */
  ObExprSpace::eval_space,                                            /* 223 */
  calc_sqrt_expr_mysql,                                               /* 224 */
  NULL,                                                               /* 225 */
  NULL,                                                               /* 226 */
  calc_str_to_date_expr_time,                                         /* 227 */
  calc_str_to_date_expr_date,                                         /* 228 */
  calc_str_to_date_expr_datetime,                                     /* 229 */
  ObExprSubQueryRef::expr_eval,                                       /* 230 */
  ObExprSubstr::eval_substr,                                          /* 231 */
  ObExprSubstringIndex::eval_substring_index,                         /* 232 */
  calc_tan_expr,                                                      /* 233 */
  NULL,                                                               /* 234 */
  ObExprDayOfMonth::calc_dayofmonth,                                  /* 235 */
  ObExprDayOfWeek::calc_dayofweek,                                    /* 236 */
  ObExprDayOfYear::calc_dayofyear,                                    /* 237 */
  ObExprHour::calc_hour,                                              /* 238 */
  ObExprMicrosecond::calc_microsecond,                                /* 239 */
  ObExprMinute::calc_minute,                                          /* 240 */
  ObExprMonth::calc_month,                                            /* 241 */
  ObExprSecond::calc_second,                                          /* 242 */
  ObExprTime::calc_time,                                              /* 243 */
  ObExprYear::calc_year,                                              /* 244 */
  calc_timestampadd_expr,                                             /* 245 */
  NULL,                                                               /* 246 */
  NULL,                                                               /* 247 */
  calc_time_to_usec_expr,                                             /* 248 */
  NULL, //ObExprDbtimezone::eval_db_timezone,                         /* 249 */
  NULL, //ObExprSessiontimezone::eval_session_timezone,               /* 250 */
  calc_todays_expr,                                                   /* 251 */
  calc_to_temporal_expr,                                              /* 252 */
  NULL,                                                               /* 253 */
  ObExprTrim::eval_trim,                                              /* 254 */
  NULL,                                                               /* 255 */
  ObExprUnhex::eval_unhex,                                            /* 256 */
  calc_usec_to_time_expr,                                             /* 257 */
  ObExprUser::eval_user,                                              /* 258 */
  ObExprUuid::eval_uuid,                                              /* 259 */
  NULL, // ObExprSysGuid::eval_sys_guid,                              /* 260 */
  ObExprVersion::eval_version,                                        /* 261 */
  ObExprXor::eval_xor,                                                /* 262 */
  calc_charset_expr,                                                  /* 263 */
  calc_collation_expr,                                                /* 264 */
  calc_coercibility_expr,                                             /* 265 */
  calc_set_collation_expr,                                            /* 266 */
  calc_cmp_meta_expr,                                                 /* 267 */
  NULL, /* calc_trunc_expr_datetime removed */                         /* 268 */
  NULL, /* calc_trunc_expr_numeric removed */                          /* 269 */
  calc_truncate_expr,                                                 /* 270 */
  ObExprEstimateNdv::calc_estimate_ndv_expr,                          /* 271 */
  ObExprFindInSet::calc_find_in_set_expr,                             /* 272 */
  ObExprGetSysVar::calc_get_sys_val_expr,                             /* 273 */
  NULL, //ObExprToNumber::calc_tonumber_expr,                         /* 274 */
  NULL, //ObExprToBinaryFloat::calc_to_binaryfloat_expr,              /* 275 */
  NULL, //ObExprToBinaryDouble::calc_to_binarydouble_expr,            /* 276 */
  NULL, //ObExprHextoraw::calc_hextoraw_expr,                         /* 277 */
  NULL, //ObExprRawtohex::calc_rawtohex_expr,                         /* 278 */
  NULL, //ObExprChr::calc_chr_expr,                                   /* 279 */
  ObExprIfNull::calc_ifnull_expr,                                     /* 280 */
  NULL,                                                               /* 281 */
  ObExprAscii::calc_ascii_expr,                                       /* 282 */
  ObExprOrd::calc_ord_expr,                                           /* 283 */
  ObExprInstr::calc_mysql_instr_expr,                                 /* 284 */
  NULL,                                                               /* 285 */
  ObLocationExprOperator::calc_location_expr,                         /* 286 */
  ObExprCalcPartitionBase::calc_no_partition_location,                /* 287 */
  ObExprCalcPartitionBase::calc_partition_level_one,                  /* 288 */
  ObExprCalcPartitionBase::calc_partition_level_two,                  /* 289 */
  NULL,                                                               /* 290 */
  ObExprSeqNextval::calc_sequence_nextval,                            /* 291 */
  calc_reverse_expr,                                                  /* 292 */
  NULL,                                                               /* 293 */
  ObExprConcatWs::calc_concat_ws_expr,                                /* 294 */
  ObExprMakeSet::calc_make_set_expr,                                  /* 295 */
  ObExprInterval::calc_interval_expr,                                 /* 296 */
  ObExprSysOpOpnsize::calc_sys_op_opnsize_expr,                       /* 297 */
  ObExprQuote::calc_quote_expr,                                       /* 298 */
  ObExprDateAdd::calc_date_add,                                       /* 299 */
  ObExprDateSub::calc_date_sub,                                       /* 300 */
  NULL, // ObExprAddMonths::calc_add_months,                          /* 301 */
  ObExprLastDay::calc_last_day,                                       /* 302 */
  NULL, // ObExprNextDay::calc_next_day,                              /* 303 */
  ObExprFromDays::calc_fromdays,                                      /* 304 */
  ObExprPeriodDiff::calc_perioddiff,                                  /* 305 */
  ObExprTimeDiff::calc_timediff,                                      /* 306 */
  ObExprTimestampNvl::calc_timestampnvl,                              /* 307 */
  NULL, // ObExprToYMInterval::calc_to_yminterval,                    /* 308 */
  NULL, // ObExprToDSInterval::calc_to_dsinterval,                    /* 309 */
  NULL, // ObExprNumToYMInterval::calc_num_to_yminterval,             /* 310 */
  NULL, // ObExprNumToDSInterval::calc_num_to_dsinterval,             /* 311 */
  ObExprWeekOfYear::calc_weekofyear,                                  /* 312 */
  ObExprWeekDay::calc_weekday,                                        /* 313 */
  ObExprYearWeek::calc_yearweek,                                      /* 314 */
  ObExprWeek::calc_week,                                              /* 315 */
  ObExprInOrNotIn::eval_in_with_row,                                  /* 316 */
  ObExprInOrNotIn::eval_in_without_row,                               /* 317 */
  ObExprInOrNotIn::eval_in_with_row_fallback,                         /* 318 */
  ObExprInOrNotIn::eval_in_without_row_fallback,                      /* 319 */
  ObExprInOrNotIn::eval_in_with_subquery,                             /* 320 */
  ObExprFunDefault::calc_default_expr,                                /* 321 */
  ObExprSubstrb::calc_substrb_expr,                                   /* 322 */
  NULL,                                                               /* 323 */
  ObExprRand::calc_random_expr_const_seed,                            /* 324 */
  ObExprRand::calc_random_expr_nonconst_seed,                         /* 325 */
  NULL, //ObExprWidthBucket::calc_width_bucket_expr,                  /* 326 */
  NULL, // ObExprSysExtractUtc::calc_sys_extract_utc,                 /* 327 */
  NULL, //ObExprToClob::calc_to_clob_expr,                            /* 328 */
  NULL, //ObExprUserEnv::calc_user_env_expr,                          /* 329 */
  NULL, // ObExprVsize::calc_vsize_expr,                              /* 330 */
  NULL, /* retired slot */                                           /* 331 */
  NULL, /* retired slot */                                           /* 332 */
  ObExprLpad::calc_mysql_lpad_expr,                                   /* 333 */
  ObExprRpad::calc_mysql_rpad_expr,                                   /* 334 */
  ObExprPad::calc_pad_expr,                                           /* 335 */
  ObExprFunValues::eval_values,                                       /* 336 */
  NULL,                                                               /* 337 */
  NULL, /* retired slot */                                           /* 338 */
  ObExprPartId::eval_part_id,                                         /* 339 */
  ObExprHex::eval_hex,                                                /* 340 */
  ObExprShadowUKProject::shadow_uk_project,                           /* 341 */
  ObExprCharLength::eval_char_length,                                 /* 342 */
  ObExprUnixTimestamp::eval_unix_timestamp,                           /* 343 */
  ObExprAesDecrypt::eval_aes_decrypt,                                 /* 344 */
  ObExprAesEncrypt::eval_aes_encrypt,                                 /* 345 */
  ObExprCase::calc_case_expr,                                         /* 346 */
  NULL, /* retired slot */                                           /* 347 */
  ObExprRemoveConst::eval_remove_const,                               /* 348 */
  ObExprSleep::eval_sleep,                                            /* 349 */
  NULL,                                                               /* 350 */
  ObExprGetPackageVar::eval_get_package_var,                          /* 351 */
  ObExprTimeStampDiff::eval_timestamp_diff,                           /* 352 */
  NULL, // ObExprFromTz::eval_from_tz,                                /* 353 */
  NULL, // ObExprTzOffset::eval_tz_offset,                            /* 354 */
  NULL, /* retired slot */                                           /* 355 */
  ObExprGetUserVar::eval_get_user_var,                                /* 356 */
  NULL, //ObExprUtil::eval_generated_column,                          /* 357 */
  NULL, //ObExprCalcPartitionBase::calc_opt_route_hash_one            /* 358 */
  calc_convert_expr,                                                  /* 359 */
  ObExprSetToStr::calc_to_str_expr,                                   /* 360 */
  ObExprEnumToStr::calc_to_str_expr,                                  /* 361 */
  ObExprSetToInnerType::calc_to_inner_expr,                           /* 362 */
  ObExprEnumToInnerType::calc_to_inner_expr,                          /* 363 */
  ObExprDateFormat::calc_date_format_invalid,                         /* 364 */
  ObExprDateFormat::calc_date_format,                                 /* 365 */
  NULL, // ObExprCalcURowID::calc_urowid,                             /* 366 */
  NULL,//ObExprFuncPartOldHash::eval_old_part_hash is deleted         /* 367 */
  NULL,//ObExprFuncPartNewKey::calc_new_partition_key is deleted      /* 368 */
  ObExprUtil::eval_stack_overflow_check,                              /* 369 */
  NULL,                                                               /* 370 */
  ObExprLastInsertID::eval_last_insert_id,                            /* 371 */
  ObExprPartIdPseudoColumn::eval_part_id,                             /* 372 */
  ObExprNullif::eval_nullif,                                          /* 373 */
  NULL, /* retired slot */                                           /* 374 */
  NULL, // ObExprUserCanAccessObj::eval_user_can_access_obj,          /* 375 */
  NULL, // ObExprEmptyClob::eval_empty_clob,                          /* 376 */
  NULL, // ObExprEmptyBlob::eval_empty_blob,                          /* 377 */
  ObExprRadians::calc_radians_expr,                                   /* 378 */
  ObExprMakeTime::eval_maketime,                                      /* 379 */
  ObExprMonthName::calc_month_name,                                   /* 380 */
  NULL,                                                               /* 381 */
  ObExprJoinFilter::eval_bloom_filter,                                /* 382 */
  NULL,                                                               /* 383 */
  NULL,                                                               /* 384 */
  ObExprToOutfileRow::to_outfile_str,                                 /* 385 */
  ObExprIs::calc_is_infinite,                                         /* 386 */
  ObExprIs::calc_is_nan,                                              /* 387 */
  ObExprIsNot::calc_is_not_infinite,                                  /* 388 */
  ObExprIsNot::calc_is_not_nan,                                       /* 389 */
  NULL, /* retired slot */                                           /* 390 */
  NULL, // ObExprNaNvl::eval_nanvl,                                   /* 391 */
  ObExprFormat::calc_format_expr,                                     /* 392 */
  NULL,                                                               /* 393 */
  ObExprQuarter::calc_quater,                                         /* 394 */
  ObExprBitLength::calc_bit_length,                                   /* 395 */
  NULL, /* retired slot */                                           /* 396 */
  NULL, // ObExprUnistr::calc_unistr_expr,                            /* 397 */
  NULL, // ObExprAsciistr::calc_asciistr_expr,                        /* 398 */
  NULL, // ObExprAtTimeZone::eval_at_time_zone,                       /* 399 */
  NULL, //ObExprAtLocal::eval_at_local,                               /* 400 */
  NULL, // ObExprToSingleByte::calc_to_single_byte,                   /* 401 */
  NULL, // ObExprToMultiByte::calc_to_multi_byte,                     /* 402 */
  ObExprDllUdf::eval_dll_udf,                                         /* 403 */
  NULL, // ObExprRawtonhex::calc_rawtonhex_expr,                      /* 404 */
  ObExprPi::eval_pi,                                                  /* 405 */
  ObExprOutputPack::eval_output_pack,                                 /* 406 */
  NULL, //ObExprReturningLob::eval_lob,                               /* 407 */
  eval_question_mark_func,                                            /* 408 */
  ObExprUtcTime::eval_utc_time,                                       /* 409 */
  ObExprUtcDate::eval_utc_date,                                       /* 410 */
  ObExprGetFormat::calc_get_format,                                   /* 411 */
  ObExprCollectionConstruct::eval_collection_construct,               /* 412 */
  ObExprObjAccess::eval_obj_access,                                   /* 413 */
  ObExprTimeFormat::calc_time_format,                                 /* 414 */
  ObExprMakedate::calc_makedate,                                      /* 415 */
  ObExprPeriodAdd::calc_periodadd,                                    /* 416 */
  ObExprPLAssocIndex::eval_assoc_idx,                                 /* 417 */
  ObExprUDF::eval_udf,                                                /* 418 */
  ObExprObjectConstruct::eval_object_construct,                       /* 419 */
  ObRelationalExprOperator::eval_pl_udt_compare,                      /* 420 */
  ObExprInOrNotIn::eval_pl_udt_in,                                    /* 421 */
  ObExprPLGetCursorAttr::calc_pl_get_cursor_attr,                     /* 422 */
  ObExprPLIntegerChecker::calc_pl_integer_checker,                    /* 423 */
  ObExprGetSubprogramVar::calc_get_subprogram_var,                    /* 424 */
  ObExprPLSQLCodeSQLErrm::eval_pl_sql_code_errm,                      /* 425 */
  ObExprMultiSet::eval_multiset,                                      /* 426 */
  NULL,// ObExprCardinality::eval_card,                               /* 427 */
  ObExprCollPred::eval_coll_pred,                                     /* 428 */
  ObExprStmtId::eval_stmt_id,                                         /* 429 */
  NULL,//ObExprWordSegment::eval_word_segment is deleted              /* 430 */
  NULL,// ObExprPLSeqNextval::eval_pl_seq_next_val,                   /* 431 */
  NULL,// ObExprSet::calc_set,                                        /* 432 */
  ObExprWrapperInner::eval_wrapper_inner,                             /* 433 */
  ObExprObVersion::eval_version,                                      /* 434 */
  NULL, // ObExprOLSLabelCmpLE::eval_cmple,                           /* 435 */
  NULL, // ObExprOLSLabelCheck::eval_label_check,                     /* 436 */
  NULL, // ObExprOLSCharToLabel::eval_char_to_label,                  /* 437 */
  NULL, // ObExprOLSLabelToChar::eval_label_to_char,                  /* 438 */
  ObExprPLSQLVariable::eval_plsql_variable,                           /* 439 */
  ObExprDegrees::calc_degrees_expr,                                   /* 440 */
  ObExprAnyValue::eval_any_value,                                     /* 441 */
  NULL, // ObExprIs::calc_collection_is_null,                         /* 442 */
  NULL, // ObExprIsNot::calc_collection_is_not_null,                  /* 443 */
  NULL, // ObExprOLSSessionRowLabel::eval_row_label,                  /* 444 */
  NULL, // ObExprOLSSessionLabel::eval_label,                         /* 445 */
  ObExprTimestamp::calc_timestamp1,                                   /* 446 */
  ObExprTimestamp::calc_timestamp2,                                   /* 447 */
  ObExprValidatePasswordStrength::eval_password_strength,             /* 448 */
  ObExprSoundex::eval_soundex,                                        /* 449 */
  NULL, // ObExprRowIDToChar::eval_rowid_to_char,                     /* 450 */
  NULL, // ObExprRowIDToNChar::eval_rowid_to_nchar,                   /* 451 */
  NULL, // ObExprCharToRowID::eval_char_to_rowid,                     /* 452 */
  ObExprUuidShort::eval_uuid_short,                                   /* 453 */
  ObExprBenchmark::eval_benchmark,                                    /* 454 */
  ObExprExportSet::eval_export_set,                                   /* 455 */
  ObExprInet6Aton::calc_inet6_aton,                                   /* 456 */
  ObExprIsIpv4::calc_is_ipv4,                                         /* 457 */
  ObExprIsIpv6::calc_is_ipv6,                                         /* 458 */
  ObExprIsIpv4Mapped::calc_is_ipv4_mapped,                            /* 459 */
  ObExprIsIpv4Compat::calc_is_ipv4_compat,                            /* 460 */
  ObExprInetAton::calc_inet_aton,                                     /* 461 */
  ObExprInet6Ntoa::calc_inet6_ntoa,                                   /* 462 */
  ObExprWeightString::eval_weight_string,                             /* 463 */
  ObExprConvertTZ::eval_convert_tz,                                   /* 464 */
  ObExprCrc32::calc_crc32_expr,                                       /* 465 */
  NULL,//ObExprDmlEvent::calc_dml_event,                              /* 466 */
  ObExprToBase64::eval_to_base64,                                     /* 467 */
  ObExprFromBase64::eval_from_base64,                                 /* 468 */
  ObExprRandomBytes::generate_random_bytes,                           /* 469 */
  ObExprOpSubQueryInPl::eval_subquery,                                /* 470 */
  ObExprEncodeSortkey::eval_encode_sortkey,                           /* 471 */
  NULL, //ObExprNLSSort::eval_nlssort,                                /* 472 */
  eval_assign_question_mark_func,                                     /* 473 */
  ObExprEncodeSortkey::eval_encode_sortkey,                           /* 474 */
  ObExprJsonObject::eval_json_object,                                 /* 475 */
  ObExprJsonExtract::eval_json_extract,                               /* 476 */
  ObExprJsonContains::eval_json_contains,                             /* 477 */
  ObExprJsonContainsPath::eval_json_contains_path,                    /* 478 */
  ObExprJsonDepth::eval_json_depth,                                   /* 479 */
  ObExprJsonKeys::eval_json_keys,                                     /* 480 */
  ObExprJsonArray::eval_json_array,                                   /* 481 */
  ObExprJsonQuote::eval_json_quote,                                   /* 482 */
  ObExprJsonUnquote::eval_json_unquote,                               /* 483 */
  ObExprJsonOverlaps::eval_json_overlaps,                             /* 484 */
  ObExprJsonRemove::eval_json_remove,                                 /* 485 */
  ObExprJsonSearch::eval_json_search,                                 /* 486 */
  ObExprJsonValid::eval_json_valid,                                   /* 487 */
  ObExprJsonArrayAppend::eval_json_array_append,                      /* 488 */
  ObExprJsonArrayInsert::eval_json_array_insert,                      /* 489 */
  ObExprJsonReplace::eval_json_replace,                               /* 490 */
  ObExprJsonType::eval_json_type,                                     /* 491 */
  ObExprJsonLength::eval_json_length,                                 /* 492 */
  ObExprJsonInsert::eval_json_insert,                                 /* 493 */
  ObExprJsonStorageSize::eval_json_storage_size,                      /* 494 */
  ObExprJsonStorageFree::eval_json_storage_free,                      /* 495 */
  ObExprJsonMergePreserve::eval_json_merge_preserve,                  /* 496 */
  ObExprJsonMerge::eval_json_merge_preserve,                          /* 497 */
  ObExprJsonMergePatch::eval_json_merge_patch,                        /* 498 */
  ObExprJsonPretty::eval_json_pretty,                                 /* 499 */
  ObExprJsonSet::eval_json_set,                                       /* 500 */
  ObExprJsonValue::eval_json_value,                                   /* 501 */
  ObExprJsonMemberOf::eval_json_member_of,                            /* 502 */
  ObExprJsonExtract::eval_json_extract_null,                          /* 503 */
  ObExprSha::eval_sha,                                                /* 504 */
  ObExprSha2::eval_sha2,                                              /* 505 */
  ObExprCompress::eval_compress,                                      /* 506 */
  ObExprUncompress::eval_uncompress,                                  /* 507 */
  ObExprUncompressedLength::eval_uncompressed_length,                 /* 508 */
  ObExprStatementDigest::eval_statement_digest,                       /* 509 */
  ObExprStatementDigestText::eval_statement_digest_text,              /* 510 */
  ObExprHash::calc_hash_value_expr,                                   /* 511 */
  calc_timestamp_to_scn_expr,                                         /* 512 */
  calc_scn_to_timestamp_expr,                                         /* 513 */
#if defined(ENABLE_DEBUG_LOG) || !defined(NDEBUG)
  ObExprErrno::eval_errno,                                            /* 514 */
#else
  NULL,                                                               /* 515 */
#endif
  ObExprDayName::calc_dayname,                                        /* 516 */
  ObExprNullif::eval_nullif_enumset,                                  /* 517 */
  ObExprSTIntersects::eval_st_intersects,                             /* 518 */
  ObExprSTX::eval_st_x,                                               /* 519 */
  ObExprSTY::eval_st_y,                                               /* 520 */
  ObExprSTLatitude::eval_st_latitude,                                 /* 521 */
  ObExprSTLongitude::eval_st_longitude,                               /* 522 */
  ObExprSTTransform::eval_st_transform,                               /* 523 */
  ObExprPoint::eval_point,                                            /* 524 */
  ObExprLineString::eval_linestring,                                  /* 525 */
  ObExprMultiPoint::eval_multipoint,                                  /* 526 */
  ObExprMultiLineString::eval_multilinestring,                        /* 527 */
  ObExprPolygon::eval_polygon,                                        /* 528 */
  ObExprMultiPolygon::eval_multipolygon,                              /* 529 */
  ObExprGeomCollection::eval_geomcollection,                          /* 530 */
  ObExprPrivSTCovers::eval_st_covers,                                 /* 531 */
  ObExprPrivSTBestsrid::eval_st_bestsrid,                             /* 532 */
  ObExprSTAsText::eval_st_astext,                                     /* 533 */
  ObExprSTAsWkt::eval_st_astext,                                      /* 534 */
  ObExprSTBufferStrategy::eval_st_buffer_strategy,                    /* 535 */
  ObExprSTBuffer::eval_st_buffer,                                     /* 536 */
  ObExprSpatialCellid::eval_spatial_cellid,                           /* 537 */
  ObExprSpatialMbr::eval_spatial_mbr,                                 /* 538 */
  ObExprPrivSTGeomFromEWKB::eval_st_geomfromewkb,                     /* 539 */
  ObExprSTGeomFromWKB::eval_st_geomfromwkb,                           /* 540 */
  ObExprSTGeometryFromWKB::eval_st_geometryfromwkb,                   /* 541 */
  ObExprPrivSTGeomFromEwkt::eval_st_geomfromewkt,                     /* 542 */
  ObExprPrivSTAsEwkt::eval_priv_st_asewkt,                            /* 543 */
  ObExprGeometryCollection::eval_geometrycollection,                  /* 544 */
  ObExprSTSRID::eval_st_srid,                                         /* 545 */
  ObExprSTDistance::eval_st_distance,                                 /* 546 */
  ObExprPrivSTSetSRID::eval_priv_st_setsrid,                          /* 547 */
  ObExprSTGeometryFromText::eval_st_geometryfromtext,                 /* 548 */
  ObExprPrivSTPoint::eval_priv_st_point,                              /* 549 */
  ObExprPrivSTGeogFromText::eval_priv_st_geogfromtext,                /* 550 */
  ObExprPrivSTGeographyFromText::eval_priv_st_geographyfromtext,      /* 551 */
  ObExprSTIsValid::eval_st_isvalid,                                   /* 552 */
  ObExprPrivSTBuffer::eval_priv_st_buffer,                            /* 553 */
  ObExprSTAsWkb::eval_st_aswkb,                                       /* 554 */
  ObExprStPrivAsEwkb::eval_priv_st_as_ewkb,                           /* 555 */
  ObExprSTAsBinary::eval_st_asbinary,                                 /* 556 */
  ObExprSTDistanceSphere::eval_st_distance_sphere,                    /* 557 */
  ObExprPrivSTDWithin::eval_st_dwithin,                               /* 558 */
  ObExprSTContains::eval_st_contains,                                 /* 559 */
  ObExprSTWithin::eval_st_within,                                     /* 560 */
  ObExprPrivSTTransform::eval_priv_st_transform,                      /* 561 */
  ObExprSTGeomFromText::eval_st_geomfromtext,                         /* 562 */
  ObExprSTArea::eval_st_area,                                         /* 563 */
  ObExprCurrentUserPriv::eval_current_user_priv,                      /* 564 */
  ObExprSqlModeConvert::sql_mode_convert,                             /* 565 */
  NULL, // ObExprJsonValue::eval_ora_json_value,                      /* 566 */
  NULL, // ObExprIsJson::eval_is_json,                                /* 567 */
  NULL, // ObExprJsonEqual::eval_json_equal,                          /* 568 */
  ObExprJsonQuery::eval_json_query,                                   /* 569 */
  ObExprJsonMergePatch::eval_ora_json_merge_patch,                    /* 570 */
  NULL, //ObExprJsonExists::eval_json_exists,                         /* 571 */
  NULL, // ObExprJsonArray::eval_ora_json_array,                      /* 572 */
  NULL, // ObExprJsonObject::eval_ora_json_object,                    /* 573 */
  NULL, // ObExprTreat::eval_treat,                                   /* 574 */
  ObExprUuid2bin::uuid2bin,                                           /* 575 */
  ObExprIsUuid::is_uuid,                                              /* 576 */
  ObExprBin2uuid::bin2uuid,                                           /* 577 */
  ObExprNameConst::eval_name_const,                                   /* 578 */
  ObExprFormatBytes::eval_format_bytes,                               /* 579 */
  ObExprFormatPicoTime::eval_format_pico_time,                        /* 580 */
  ObExprDesEncrypt::eval_des_encrypt_with_key,                        /* 581 */
  ObExprDesEncrypt::eval_des_encrypt_with_default,                    /* 582 */
  ObExprDesDecrypt::eval_des_decrypt,                                 /* 583 */
  ObExprEncrypt::eval_encrypt,                                        /* 584 */
  ObExprEncode::eval_encode,                                          /* 585 */
  ObExprDecode::eval_decode,                                          /* 586 */
  ObExprICUVersion::eval_version,                                     /* 587 */
  ObExprCast::eval_cast_multiset,                                     /* 588 */
  ObExprGeneratorFunc::eval_next_value,                               /* 589 */
  ObExprZipf::eval_next_value,                                        /* 590 */
  ObExprNormal::eval_next_value,                                      /* 591 */
  ObExprUniform::eval_next_int_value,                                 /* 592 */
  ObExprUniform::eval_next_real_value,                                /* 593 */
  ObExprUniform::eval_next_number_value,                              /* 594 */
  ObExprRandom::calc_random_expr_const_seed,                          /* 595 */
  ObExprRandom::calc_random_expr_nonconst_seed,                       /* 596 */
  ObExprRandstr::calc_random_str,                                     /* 597 */
  NULL,                                                               /* 598 */
  ObExprPrefixPattern::eval_prefix_pattern,                           /* 599 */
  NULL, // ObExprSysMakeXML::eval_sys_makexml,                        /* 600 */
  NULL, // ObExprPrivXmlBinary::eval_priv_xml_binary,                 /* 601 */
  NULL, // ObExprXmlparse::eval_xmlparse,                             /* 602 */
  NULL, // ObExprXmlElement::eval_xml_element,                        /* 603 */
  NULL, // ObExprXmlAttributes::eval_xml_attributes,                  /* 604 */
  NULL, // ObExprExtractValue::eval_extract_value,                    /* 605 */
  NULL, // ObExprExtractXml::eval_extract_xml,                        /* 606 */
  NULL, // ObExprXmlSerialize::eval_xml_serialize,                    /* 607 */
  NULL, // ObExprXmlcast::eval_xmlcast,                               /* 608 */
  NULL, // ObExprUpdateXml::eval_update_xml,                                   /* 609 */
  ObExprJoinFilter::eval_range_filter,                                /* 610 */
  ObExprJoinFilter::eval_in_filter,                                   /* 611 */
  ObExprCurrentScn::eval_current_scn,                                 /* 612 */
  NULL, // ObExprTempTableSSID::calc_temp_table_ssid,                 /* 613 */
  ObExprAlignDate4Cmp::eval_align_date4cmp,                           /* 614 */
  NULL, // ObExprJsonObjectStar::eval_ora_json_object_star,           /* 615 */
  calc_bool_expr_for_decint_type,                                     /* 616 */
  ObExprIs::decimal_int_is_true,                                      /* 617 */
  ObExprIs::decimal_int_is_false,                                     /* 618 */
  ObExprIsNot::decimal_int_is_not_true,                               /* 619 */
  ObExprIsNot::decimal_int_is_not_false,                              /* 620 */
  ObExprInnerIsTrue::int_is_true_start,                               /* 621 */
  ObExprInnerIsTrue::int_is_true_end,                                 /* 622 */
  ObExprInnerIsTrue::float_is_true_start,                             /* 623 */
  ObExprInnerIsTrue::float_is_true_end,                               /* 624 */
  ObExprInnerIsTrue::double_is_true_start,                            /* 625 */
  ObExprInnerIsTrue::double_is_true_end,                              /* 626 */
  ObExprInnerIsTrue::number_is_true_start,                            /* 627 */
  ObExprInnerIsTrue::number_is_true_end,                              /* 628 */
  ObExprInnerDecodeLike::eval_inner_decode_like,                      /* 629 */
  ObExprJsonSchemaValid::eval_json_schema_valid,                      /* 630 */
  ObExprJsonSchemaValidationReport::eval_json_schema_validation_report, /* 631 */
  NULL, // ObExprInsertChildXml::eval_insert_child_xml,               /* 632 */
  NULL, // ObExprDeleteXml::eval_delete_xml,                          /* 633 */
  ObExprExtractValue::eval_mysql_extract_value,                       /* 634 */
  ObExprUpdateXml::eval_mysql_update_xml,                             /* 635 */
  NULL, //ObExprXmlSequence::eval_xml_sequence,                       /* 636 */
  ObExprJsonAppend::eval_json_array_append,                           /* 637 */
  NULL, //unused                                                      /* 638 */
  ObExprUdtConstruct::eval_udt_construct,                             /* 639 */
  NULL, //ObExprUDTAttributeAccess::eval_attr_access,                 /* 640 */
  ObExprPrivSTNumInteriorRings::eval_priv_st_numinteriorrings,        /* 641 */
  ObExprPrivSTIsCollection::eval_priv_st_iscollection,                /* 642 */
  ObExprPrivSTEquals::eval_priv_st_equals,                            /* 643 */
  ObExprPrivSTTouches::eval_priv_st_touches,                          /* 644 */
  ObExprPrivSTMakeEnvelope::eval_priv_st_makeenvelope,                /* 645 */
  ObExprPrivSTClipByBox2D::eval_priv_st_clipbybox2d,                  /* 646 */
  ObExprPrivSTPointOnSurface::eval_priv_st_pointonsurface,            /* 647 */
  ObExprPrivSTGeometryType::eval_priv_st_geometrytype,                /* 648 */
  ObExprSTCrosses::eval_st_crosses,                                   /* 649 */
  ObExprSTOverlaps::eval_st_overlaps,                                 /* 650 */
  ObExprSTUnion::eval_st_union,                                       /* 651 */
  ObExprSTLength::eval_st_length,                                     /* 652 */
  ObExprSTDifference::eval_st_difference,                             /* 653 */
  ObExprSTAsGeoJson::eval_st_asgeojson,                               /* 654 */
  ObExprSTCentroid::eval_st_centroid,                                 /* 655 */
  ObExprSTSymDifference::eval_st_symdifference,                       /* 656 */
  ObExprPrivSTAsMVTGeom::eval_priv_st_asmvtgeom,                      /* 657 */
  ObExprPrivSTMakeValid::eval_priv_st_makevalid,                      /* 658 */
  NULL, //unused                                                      /* 659 */
  NULL, //unused                                                      /* 660 */
  NULL, //unused                                                      /* 661 */
  NULL, //unused                                                      /* 662 */
  eval_questionmark_decint2nmb,                                       /* 663 */
  eval_questionmark_nmb2decint_eqcast,                                /* 664 */
  eval_questionmark_decint2decint_eqcast,                             /* 665 */
  eval_questionmark_decint2decint_normalcast,                         /* 666 */
  ObExprExtractExpiredTime::eval_extract_cert_expired_time,           /* 667 */
  NULL, // ObExprXmlConcat::eval_xml_concat,                          /* 668 */
  NULL, // ObExprXmlForest::eval_xml_forest,                          /* 669 */
  NULL, // ObExprExistsNodeXml::eval_existsnode_xml,                  /* 670 */
  ObExprPassword::eval_password,                                      /* 671 */
  ObExprDocID::generate_doc_id,                                       /* 672 */
  ObExprWordSegment::generate_fulltext_column,                        /* 673 */
  ObExprWordCount::generate_word_count,                               /* 674 */
  ObExprBM25::eval_bm25_relevance_expr,                               /* 675 */
  ObExprTransactionId::eval_transaction_id,                           /* 676 */
  ObExprInnerTableOptionPrinter::eval_inner_table_option_printer,     /* 677 */
  ObExprInnerTableSequenceGetter::eval_inner_table_sequence_getter,   /* 678 */
  ObExprDecodeTraceId::calc_decode_trace_id_expr,                     /* 679 */
  ObExprInnerRowCmpVal::eval_inner_row_cmp_val,                       /* 680 */
  ObExprIs::json_is_true,                                             /* 681 */
  ObExprIs::json_is_false,                                            /* 682 */
  ObExprCurrentRole::eval_current_role,                               /* 683 */
  ObExprMod::mod_decimalint,                                          /* 684 */
  ObExprPrivSTGeoHash::eval_priv_st_geohash,                          /* 685 */
  ObExprPrivSTMakePoint::eval_priv_st_makepoint,                      /* 686 */
  ObExprGetLock::get_lock,                                            /* 687 */
  ObExprIsFreeLock::is_free_lock,                                     /* 688 */
  ObExprIsUsedLock::is_used_lock,                                     /* 689 */
  ObExprReleaseLock::release_lock,                                    /* 690 */
  ObExprReleaseAllLocks::release_all_locks,                           /* 691 */
  ObExprGTIDSubset::eval_subset,                                      /* 692 */
  ObExprGTIDSubtract::eval_subtract,                                  /* 693 */
  ObExprWaitForExecutedGTIDSet::eval_wait_for_executed_gtid_set,      /* 694 */
  ObExprWaitUntilSQLThreadAfterGTIDs::eval_wait_until_sql_thread_after_gtids, /* 695 */
  ObExprDocLength::generate_doc_length,                               /* 696 */
  ObExprTopNFilter::eval_topn_filter,                                 /* 697 */
  ObExprIsEnabledRole::eval_is_enabled_role,                          /* 698 */
  ObExprCanAccessTrigger::can_access_trigger,                         /* 699 */
  NULL, //   ObExprSdoRelate::eval_sdo_relate,                        /* 700 */
  ObExprArray::eval_array,                                            /* 701 */
  ObExprVectorL1Distance::calc_l1_distance,                           /* 702 */
  ObExprVectorL2Distance::calc_l2_distance,                           /* 703 */
  ObExprVectorCosineDistance::calc_cosine_distance,                   /* 704 */
  ObExprVectorIPDistance::calc_inner_product,                         /* 705 */
  ObExprVectorDims::calc_dims,                                        /* 706 */
  ObExprVectorNorm::calc_norm,                                        /* 707 */
  ObExprVectorDistance::calc_distance,                                /* 708 */
  ObExprInnerDoubleToInt::eval_inner_double_to_int,                   /* 709 */
  ObExprInnerDecimalToYear::eval_inner_decimal_to_year,               /* 710 */
  ObExprSm3::eval_sm3,                                                /* 711 */
  ObExprSm4Encrypt::eval_sm4_encrypt,                                 /* 712 */
  ObExprSm4Decrypt::eval_sm4_decrypt,                                 /* 713 */
  NULL, // ObExprAdd::add_vec_vec,                                    /* 714 */
  NULL, // ObExprMinus::minus_vec_vec,                                /* 715 */
  ObExprMul::mul_vec_vec,                                             /* 716 */
  ObExprDiv::div_vec,                                                 /* 717 */
  ObExprVecKey::generate_vec_key,                                     /* 718 */
  ObExprVecScn::generate_vec_scn,                                     /* 719 */
  ObExprVecVid::generate_vec_id,                                      /* 720 */
  ObExprVecData::generate_vec_data,                                   /* 721 */
  ObExprVecType::generate_vec_type,                                   /* 722 */
  ObExprVecVector::generate_vec_vector,                               /* 723 */
  ObExprRegexp::eval_regexp,                                          /* 724 */
  NULL,                                                               /* 725 */
  ObExprRegexpInstr::eval_regexp_instr,                              /* 726 */
  ObExprRegexpLike::eval_regexp_like,                                 /* 727 */
  ObExprRegexpReplace::eval_regexp_replace,                           /* 728 */
  ObExprRegexpSubstr::eval_regexp_substr,                             /* 729 */
  NULL, /* retired slot */                                           /* 730 */
  ObExprArrayContains::eval_array_contains_int64_t,                   /* 731 */
  ObExprArrayContains::eval_array_contains_float,                     /* 732 */
  ObExprArrayContains::eval_array_contains_double,                    /* 733 */
  ObExprArrayContains::eval_array_contains_ObString,                  /* 734 */
  ObExprArrayContains::eval_array_contains_array,                     /* 735 */
  ObExprSplitPart::calc_split_part_expr,                              /* 736 */
  ObExprVectorNegativeIPDistance::calc_negative_inner_product,        /* 737 */
  ObExprTokenize::eval_tokenize,                                      /* 738 */
  NULL,                                                               /* 739 */
  NULL,                                                               /* 740 */
  ObExprMysqlProcInfo::eval_mysql_proc_info,                          /* 741 */
  ObExprArrayOverlaps::eval_array_overlaps,                           /* 742 */
  ObExprArrayContainsAll::eval_array_contains_all,                    /* 743 */
  ObExprInnerIsTrue::decimal_int_is_true_start,                       /* 744 */
  ObExprInnerIsTrue::decimal_int_is_true_end,                         /* 745 */
  ObExprInnerIsTrue::json_is_true_start,                              /* 746 */
  ObExprInnerIsTrue::json_is_true_end,                                /* 747 */
  ObExprGetMySQLRoutineParameterTypeStr::get_mysql_routine_parameter_type_str, /* 748 */
  ObExprArrayDistinct::eval_array_distinct,                           /* 749 */
  ObExprArrayRemove::eval_array_remove_int64_t,                       /* 750 */
  ObExprArrayRemove::eval_array_remove_float,                         /* 751 */
  ObExprArrayRemove::eval_array_remove_double,                        /* 752 */
  ObExprArrayRemove::eval_array_remove_ObString,                      /* 753 */
  ObExprArrayRemove::eval_array_remove_array,                         /* 754 */
  ObExprArrayMap::eval_array_map,                                     /* 755 */
  NULL, //  ObExprOraLoginUser::eval_ora_login_user,                  /* 756 */
  ObExprArrayToString::eval_array_to_string,                          /* 757 */
  ObExprStringToArray::eval_string_to_array,                          /* 758 */
  ObExprArrayAppend::eval_array_append,                               /* 759 */
  ObExprElementAt::eval_element_at,                                   /* 760 */
  ObExprArrayCardinality::eval_array_cardinality,                     /* 761 */
  ObExprArrayPrepend::eval_array_prepend,                             /* 762 */
  ObExprArrayConcat::eval_array_concat,                               /* 763 */
  ObExprArrayDifference::eval_array_difference,                       /* 764 */
  ObExprArrayFirst::eval_array_first,                                 /* 765 */
  NULL, // ObExprCalcPartitionName::get_partition_name,               /* 766 */
  NULL, // ObExprCalcSubPartitionName::get_sub_partition_name,        /* 767 */
  NULL, // ObExprCalcPartitionIdx::get_partition_idx,                 /* 768 */
  NULL, // ObExprCalcSubPartitionIdx::get_sub_partition_idx,          /* 769 */
  NULL, // ObExprCalcOdpsSize::calc_odps_size,                        /* 770 */
  ObExprVecIVFCenterID::calc_center_id,                               /* 771 */
  ObExprVecIVFCenterVector::generate_center_vector,                   /* 772 */
  ObExprVecIVFFlatDataVector::generate_data_vector,                   /* 773 */
  ObExprVecIVFSQ8DataVector::generate_data_vector,                    /* 774 */
  ObExprVecIVFMetaID::generate_meta_id,                               /* 775 */
  ObExprVecIVFMetaVector::generate_meta_vector,                       /* 776 */
  ObExprVecIVFPQCenterId::generate_pq_center_id,                      /* 777 */
  ObExprVecIVFPQCenterIds::calc_pq_center_ids,                        /* 778 */
  ObExprArrayMax::eval_array_max,                                     /* 779 */
  ObExprArrayMin::eval_array_min,                                     /* 780 */
  ObExprArrayAvg::eval_array_avg,                                     /* 781 */
  ObExprArraySum::eval_array_sum,                                     /* 782 */
  ObExprArrayCompact::eval_array_compact,                             /* 783 */
  ObExprArraySort::eval_array_sort,                                   /* 784 */
  ObExprKeyValue::calc_key_value_expr,                                /* 785 */
  NULL, /* ObExprToChar::eval_to_char removed */                       /* 786 */
  ObExprToPinyin::eval_to_pinyin,                                     /* 787 */
  ObExprArraySlice::eval_array_slice,                                 /* 788 */
  ObExprArraySortby::eval_array_sortby,                               /* 789 */
  ObExprArrayFilter::eval_array_filter,                               /* 790 */
  ObExprArrayLength::eval_array_length,                               /* 791 */
  ObExprArrayRange::eval_array_range,                                 /* 792 */ // FARM COMPAT WHITELIST
  ObExprArrayPosition::eval_array_position,                           /* 793 */
  ObExprURLEncode::eval_url_encode,                                   /* 794 */
  ObExprURLDecode::eval_url_decode,                                   /* 795 */
  ObExprVecIVFPQCenterVector::generate_pq_center_vector,              /* 796 */
  ObExprDemoteCast::eval_demoted_val,                                 /* 797 */
  ObExprRangePlacement::eval_range_placement,                         /* 798 */
  ObExprInnerTypeToEnumSet::eval_inner_type_to_enumset,               /* 799 */
  ObExprIsNot::json_is_not_false,                                     /* 800 */
  ObExprIsNot::json_is_not_true,                                      /* 801 */
  ObExprArrayExcept::eval_array_except,                               /* 802 */
  ObExprArrayIntersect::eval_array_intersect,                         /* 803 */
  ObExprArrayUnion::eval_array_union,                                 /* 804 */
  NULL, // ObExprArrayReplace::eval_array_replace,                    /* 805 */
  NULL, // ObExprArrayPopfront::eval_array_popfront,                  /* 806 */
  NULL, // ObExprCurrentCatalog::eval_current_catalog,                /* 807 */
  ObExprInnerInfoColsColumnDefPrinter::eval_column_def,               /* 808 */
  ObExprInnerInfoColsCharLenPrinter::eval_column_char_len,            /* 809 */
  ObExprInnerInfoColsCharNamePrinter::eval_column_char_name,          /* 810 */
  ObExprInnerInfoColsCollNamePrinter::eval_column_collation_name,     /* 811 */
  ObExprInnerInfoColsPrivPrinter::eval_column_priv,                   /* 812 */
  ObExprInnerInfoColsExtraPrinter::eval_column_extra,                 /* 813 */
  ObExprInnerInfoColsDataTypePrinter::eval_column_data_type,          /* 814 */
  ObExprInnerInfoColsColumnTypePrinter::eval_column_column_type,      /* 815 */
  ObExprCurrentCatalog::eval_current_catalog,                          /* 816 */
  ObExprCheckCatalogAccess::eval_check_catalog_access,                 /* 817 */
  ObExprMap::eval_map,                                                 /* 818 */
  ObExprSpivValue::generate_spiv_value,                                /* 819 */
  ObExprMapKeys::eval_map_keys,                                        /* 820 */
  ObExprMapValues::eval_map_values,                                    /* 821 */
  ObExprSpivDim::generate_spiv_dim,                                    /* 822 */
  ObExprInnerInfoColsColumnKeyPrinter::eval_column_column_key,         /* 823 */
  ObExprCheckLocationAccess::eval_check_location_access,               /* 824 */
  NULL, // ObExprUDF::eval_external_udf,                               /* 825 */
  NULL, // ObExprStartUpMode::eval_startup_mode,                       /* 826 */
  ObExprVectorL2Squared::calc_l2_squared,                              /* 827 */
  ObExprVecChunk::generate_vec_chunk,                                  /* 828 */
  ObExprEmbeddedVec::generate_embedded_vec,                            /* 829 */
  ObExprSemanticDistance::calc_semantic_distance,                      /* 830 */
  ObExprSemanticVectorDistance::calc_semantic_vector_distance,         /* 831 */
  ObExprAIComplete::eval_ai_complete,                                  /* 832 */
  ObExprAIEmbed::eval_ai_embed,                                        /* 833 */
  ObExprAIRerank::eval_ai_rerank,                                      /* 834 */
  ObExprAIPrompt::eval_ai_prompt,                                      /* 835 */
  ObExprVectorL2Similarity::calc_l2_similarity,                        /* 836 */
  ObExprVectorCosineSimilarity::calc_cosine_similarity,                /* 837 */
  ObExprVectorIPSimilarity::calc_ip_similarity,                        /* 838 */
  ObExprVectorSimilarity::calc_similarity,                             /* 839 */
};

static ObExpr::EvalBatchFunc g_expr_eval_batch_functions[] = {
  expr_default_eval_batch_func,                                       /* 0 */
  ObExprUtil::eval_batch_stack_overflow_check,                        /* 1 */
  ObExprAdd::add_datetime_datetime_batch,                             /* 2 */
  NULL,                                                               /* 3 */
  NULL,                                                               /* 4 */
  ObExprAdd::add_datetime_number_batch,                               /* 5 */
  ObExprAdd::add_double_double_batch,                                 /* 6 */
  ObExprAdd::add_float_float_batch,                                   /* 7 */
  NULL,                                                               /* 8 */
  NULL,                                                               /* 9 */
  NULL,                                                               /* 10 */
  NULL,                                                               /* 11 */
  NULL,                                                               /* 12 */
  NULL,                                                               /* 13 */
  NULL,                                                               /* 14 */
  NULL,                                                               /* 15 */
  NULL,                                                               /* 16 */
  ObExprAdd::add_int_int_batch,                                       /* 17 */
  ObExprAdd::add_int_uint_batch,                                      /* 18 */
  ObExprAdd::add_number_datetime_batch,                               /* 19 */
  ObExprAdd::add_number_number_batch,                                 /* 20 */
  NULL,                                                               /* 21 */
  NULL,                                                               /* 22 */
  NULL,                                                               /* 23 */
  NULL,                                                               /* 24 */
  NULL,                                                               /* 25 */
  NULL,                                                               /* 21 */
  NULL,                                                               /* 22 */
  NULL,                                                               /* 23 */
  NULL,                                                               /* 24 */
  NULL,                                                               /* 25 */
  ObExprAdd::add_uint_int_batch,                                      /* 26 */
  ObExprAdd::add_uint_uint_batch,                                     /* 27 */
  ObExprMinus::minus_datetime_datetime_batch,                         /* 28 */
  NULL, /* retired slot */             /* 29 */
  NULL,                                                               /* 30 */
  NULL,                                                               /* 31 */
  ObExprMinus::minus_datetime_number_batch,                           /* 32 */
  ObExprMinus::minus_double_double_batch,                             /* 33 */
  ObExprMinus::minus_float_float_batch,                               /* 34 */
  NULL,                                                               /* 35 */
  NULL,                                                               /* 36 */
  ObExprMinus::minus_int_int_batch,                                   /* 37 */
  ObExprMinus::minus_int_uint_batch,                                  /* 38 */
  ObExprMinus::minus_number_number_batch,                             /* 39 */
  NULL,                                                               /* 40 */
  NULL,                                                               /* 41 */
  NULL,                                                               /* 42 */
  NULL,                                                               /* 43 */
  NULL,                                                               /* 44 */
  NULL,                                                               /* 45 */
  NULL,                                                               /* 40 */
  NULL,                                                               /* 41 */
  NULL,                                                               /* 42 */
  NULL,                                                               /* 43 */
  NULL,                                                               /* 44 */
  NULL,                                                               /* 45 */
  ObExprMinus::minus_uint_int_batch,                                  /* 46 */
  ObExprMinus::minus_uint_uint_batch,                                 /* 47 */
  ObExprMul::mul_double_batch,                                        /* 48 */
  ObExprMul::mul_float_batch,                                         /* 49 */
  NULL,                                                               /* 50 */
  NULL,                                                               /* 51 */
  ObExprMul::mul_int_int_batch,                                       /* 52 */
  ObExprMul::mul_int_uint_batch,                                      /* 53 */
  ObExprMul::mul_number_batch,                                        /* 54 */
  NULL,                                                               /* 55 */
  NULL,                                                               /* 56 */
  ObExprMul::mul_uint_int_batch,                                      /* 57 */
  ObExprMul::mul_uint_uint_batch,                                     /* 58 */
  ObExprDiv::div_float_batch,                                         /* 59 */
  ObExprDiv::div_double_batch,                                        /* 60 */
  ObExprDiv::div_number_batch,                                        /* 61 */
  NULL,                                                               /* 62 */
  NULL,                                                               /* 63 */
  ObExprMakeTime::eval_batch_maketime,                                /* 64 */
  ObExprAnd::eval_and_batch_exprN,                                    /* 65 */
  ObExprOr::eval_or_batch_exprN,                                      /* 66 */
  ObExprFuncPartKey::calc_partition_key_batch,                        /* 67 */
  NULL,//ObExprFuncPartNewKey::calc_new_partition_key_batch is deleted/* 68 */
  ObExprInOrNotIn::eval_batch_in_without_row_fallback,                /* 69 */
  ObExprInOrNotIn::eval_batch_in_without_row,                         /* 70 */
  ObExprLike::eval_like_expr_batch_only_text_vectorized,              /* 71 */
  ObExprCase::eval_case_batch,                                        /* 72 */
  ObExprSubstr::eval_substr_batch,                                    /* 73 */
  ObExprJoinFilter::eval_bloom_filter_batch,                          /* 74 */
  NULL,                                                               /* 75 */
  NULL, /* retired slot */                        /* 76 */
  NULL,                                                               /* 77 */
  ObExprExtract::calc_extract_mysql_batch,                            /* 78 */
  cast_eval_arg_batch,                                                /* 79 */
  ObExprOutputPack::eval_output_pack_batch,                           /* 80 */
  eval_batch_ceil_floor,                                              /* 81 */
  ObExprFuncRound::calc_round_expr_numeric1_batch,                    /* 82 */
  ObExprFuncRound::calc_round_expr_numeric2_batch,                    /* 83 */
  ObExprFuncRound::calc_round_expr_datetime1_batch,                   /* 84 */
  NULL, // ObExprFuncRound::calc_round_expr_datetime2_batch,          /* 85 */
  ObExprNot::eval_not_batch,                                          /* 86 */
  NULL,    // ObExprCalcPartitionBase::calc_opt_route_hash_one_vec,   /* 87 */
  ObExprBenchmark::eval_benchmark_batch,                              /* 88 */
  ObExprToBase64::eval_to_base64_batch,                               /* 89 */
  ObExprFromBase64::eval_from_base64_batch,                           /* 90 */
  ObExprEncodeSortkey::eval_encode_sortkey_batch,                     /* 91 */
  ObExprHash::calc_hash_value_expr_batch,                             /* 92 */
  ObExprSubstringIndex::eval_substring_index_batch,                   /* 93 */
  NULL, // ObExprInstrb::calc_instrb_expr_batch,                      /* 94 */
  NULL, // ObExprNaNvl::eval_nanvl_batch,                             /* 95 */
  ObExprNvlUtil::calc_nvl_expr_batch,                                 /* 96 */
  NULL,                                                               /* 97 */
  ObExprUuid2bin::uuid2bin_batch,                                     /* 98 */
  ObExprIsUuid::is_uuid_batch,                                        /* 99 */
  ObExprBin2uuid::bin2uuid_batch,                                     /* 100 */
  ObExprFormatBytes::eval_format_bytes_batch,                         /* 101 */
  ObExprFormatPicoTime::eval_format_pico_time_batch,                  /* 102 */
  ObExprDesEncrypt::eval_des_encrypt_batch_with_default,              /* 103 */
  ObExprDesEncrypt::eval_des_encrypt_batch_with_key,                  /* 104 */
  ObExprDesDecrypt::eval_des_decrypt_batch,                           /* 105 */
  ObExprEncrypt::eval_encrypt_batch,                                  /* 106 */
  ObExprEncode::eval_encode_batch,                                    /* 107 */
  ObExprDecode::eval_decode_batch,                                    /* 108 */
  ObExprCoalesce::calc_batch_coalesce_expr,                           /* 109 */
  ObExprIsNot::calc_batch_is_not_null,                                /* 110 */
  NULL,                                                               /* 111 */
  ObExprJoinFilter::eval_range_filter_batch,                          /* 112 */
  ObExprJoinFilter::eval_in_filter_batch,                             /* 113 */
  calc_sqrt_expr_mysql_in_batch,                                      /* 114 */
  NULL,                                                               /* 115 */
  NULL,                                                               /* 116 */
  ObBatchCast::explicit_batch_cast<ObDecimalIntTC, ObDecimalIntTC>,   /* 117 */
  ObBatchCast::implicit_batch_cast<ObDecimalIntTC, ObDecimalIntTC>,   /* 118 */
  ObBatchCast::explicit_batch_cast<ObIntTC, ObDecimalIntTC>,          /* 119 */
  ObBatchCast::implicit_batch_cast<ObIntTC, ObDecimalIntTC>,          /* 120 */
  ObBatchCast::explicit_batch_cast<ObUIntTC, ObDecimalIntTC>,         /* 121 */
  ObBatchCast::implicit_batch_cast<ObUIntTC, ObDecimalIntTC>,         /* 122 */
  ObBatchCast::explicit_batch_cast<ObDecimalIntTC, ObIntTC>,          /* 123 */
  ObBatchCast::implicit_batch_cast<ObDecimalIntTC, ObIntTC>,          /* 124 */
  ObBatchCast::explicit_batch_cast<ObDecimalIntTC, ObUIntTC>,         /* 125 */
  ObBatchCast::implicit_batch_cast<ObDecimalIntTC, ObUIntTC>,         /* 126 */
  ObBatchCast::explicit_batch_cast<ObDecimalIntTC, ObNumberTC>,       /* 127 */
  ObBatchCast::implicit_batch_cast<ObDecimalIntTC, ObNumberTC>,       /* 128 */
  ObExprDecodeTraceId::calc_decode_trace_id_expr_batch,               /* 129 */
  ObExprTopNFilter::eval_topn_filter_batch,                           /* 130 */
  ObExprBM25::eval_batch_bm25_relevance_expr,                  /* 132 */
  NULL,// ObExprAdd::add_vec_vec_batch,                               /* 133 */
  NULL,// ObExprMinus::minus_vec_vec_batch,                           /* 134 */
  ObExprMul::mul_vec_vec_batch,                                       /* 135 */
  ObExprDiv::div_vec_batch,                                           /* 136 */
  ObExprColumnConv::column_convert_batch,                             /* 137 */
  NULL, /* retired slot */                                            /* 138 */
  ObExprArrayContains::eval_array_contains_batch_int64_t,             /* 139 */
  ObExprArrayContains::eval_array_contains_batch_float,               /* 140 */
  ObExprArrayContains::eval_array_contains_batch_double,              /* 141 */
  ObExprArrayContains::eval_array_contains_batch_ObString,            /* 142 */
  ObExprArrayContains::eval_array_contains_array_batch,               /* 143 */
  ObExprArrayOverlaps::eval_array_overlaps_batch,                     /* 144 */
  ObExprArrayContainsAll::eval_array_contains_all_batch,              /* 145 */
  ObExprArrayDistinct::eval_array_distinct_batch,                     /* 146 */
  ObExprArrayRemove::eval_array_remove_batch_int64_t,                 /* 147 */
  ObExprArrayRemove::eval_array_remove_batch_float,                   /* 148 */
  ObExprArrayRemove::eval_array_remove_batch_double,                  /* 149 */
  ObExprArrayRemove::eval_array_remove_batch_ObString,                /* 150 */
  ObExprArrayRemove::eval_array_remove_array_batch,                   /* 151 */
  ObExprArrayToString::eval_array_to_string_batch,                    /* 152 */
  ObExprStringToArray::eval_string_to_array_batch,                    /* 153 */
  ObExprArrayAppend::eval_array_append_batch,                         /* 154 */
  ObExprElementAt::eval_element_at_batch,                             /* 155 */
  ObExprArrayCardinality::eval_array_cardinality_batch,               /* 156 */
  ObExprArrayPrepend::eval_array_prepend_batch,                       /* 157 */
  ObExprArrayConcat::eval_array_concat_batch,                         /* 158 */
  ObExprArrayDifference::eval_array_difference_batch,                 /* 159 */
  ObExprArrayMax::eval_array_max_batch,                               /* 160 */
  ObExprArrayMin::eval_array_min_batch,                               /* 161 */
  ObExprArrayAvg::eval_array_avg_batch,                               /* 162 */
  ObExprArraySum::eval_array_sum_batch,                               /* 163 */
  ObExprArrayCompact::eval_array_compact_batch,                       /* 164 */
  ObExprArraySort::eval_array_sort_batch,                             /* 165 */
  ObExprToPinyin::eval_to_pinyin_batch,                               /* 166 */
  ObExprArraySlice::eval_array_slice_batch,                           /* 167 */
  ObExprArrayLength::eval_array_length_batch,                         /* 168 */
  NULL,// ObExprRange::eval_range_batch,                              /* 169 */
  ObExprArrayPosition::eval_array_position_batch,                     /* 170*/
  ObExprURLEncode::eval_url_encode_batch,                             /* 171 */
  ObExprURLDecode::eval_url_decode_batch,                             /* 172 */
  ObExprArrayExcept::eval_array_except_batch,                         /* 173 */
  ObExprArrayIntersect::eval_array_intersect_batch,                   /* 174 */
  ObExprArrayUnion::eval_array_union_batch,                           /* 175 */
  NULL, // ObExprArrayReplace::eval_array_replace_batch,              /* 176 */
  NULL, // ObExprArrayPopfront::eval_array_popfront_batch,            /* 177 */
  NULL, // ObExprUDF::eval_udf_batch                                  /* 178 */
};

static ObExpr::EvalVectorFunc g_expr_eval_vector_functions[] = {
  expr_default_eval_vector_func,                                /* 0 */
  ObExprSin::eval_double_sin_vector,                            /* 1 */
  ObExprSin::eval_number_sin_vector,                            /* 2 */
  ObExprFuncPartKey::calc_partition_key_vector,                 /* 3 */
  ObExprAdd::add_int_int_vector,                                /* 4 */
  ObExprAdd::add_int_uint_vector,                               /* 5 */
  ObExprAdd::add_uint_int_vector,                               /* 6 */
  ObExprAdd::add_uint_uint_vector,                              /* 7 */
  ObExprAdd::add_float_float_vector,                            /* 8 */
  ObExprAdd::add_double_double_vector,                          /* 9 */
  NULL, /* retired slot */             /* 10 */
  NULL, /* retired slot */             /* 11 */
  NULL, /* retired slot */            /* 12 */
  ObExprAdd::add_number_number_vector,                          /* 13 */
  ObExprAdd::add_decimalint32_vector,                           /* 14 */
  ObExprAdd::add_decimalint64_vector,                           /* 15 */
  ObExprAdd::add_decimalint128_vector,                          /* 16 */
  ObExprAdd::add_decimalint256_vector,                          /* 17 */
  ObExprAdd::add_decimalint512_vector,                          /* 18 */
  ObExprAdd::add_decimalint512_with_check_vector,               /* 19 */
  ObExprMinus::minus_int_int_vector,                            /* 20 */
  ObExprMinus::minus_int_uint_vector,                           /* 21 */
  ObExprMinus::minus_uint_uint_vector,                          /* 22 */
  ObExprMinus::minus_uint_int_vector,                           /* 23 */
  ObExprMinus::minus_float_float_vector,                        /* 24 */
  ObExprMinus::minus_double_double_vector,                      /* 25 */
  ObExprMinus::minus_number_number_vector,                      /* 26 */
  ObExprMinus::minus_decimalint32_vector,                       /* 27 */
  ObExprMinus::minus_decimalint64_vector,                       /* 28 */
  ObExprMinus::minus_decimalint128_vector,                      /* 29 */
  ObExprMinus::minus_decimalint256_vector,                      /* 30 */
  ObExprMinus::minus_decimalint512_vector,                      /* 31 */
  ObExprMinus::minus_decimalint512_with_check_vector,           /* 32 */
  NULL, /* retired slot */           /* 33 */
  NULL, /* retired slot */           /* 34 */
  NULL, /* retired slot */          /* 35 */
  ObExprMul::mul_int_int_vector,                                /* 36 */
  ObExprMul::mul_int_uint_vector,                               /* 37 */
  ObExprMul::mul_uint_int_vector,                               /* 38 */
  ObExprMul::mul_uint_uint_vector,                              /* 39 */
  ObExprMul::mul_float_vector,                                  /* 40 */
  ObExprMul::mul_double_vector,                                 /* 41 */
  ObExprMul::mul_number_vector,                                 /* 42 */
  ObExprMul::mul_decimalint32_int32_int32_vector,               /* 43 */
  ObExprMul::mul_decimalint64_int32_int32_vector,               /* 44 */
  ObExprMul::mul_decimalint64_int32_int64_vector,               /* 45 */
  ObExprMul::mul_decimalint64_int64_int32_vector,               /* 46 */
  ObExprMul::mul_decimalint128_int32_int64_vector,              /* 47 */
  ObExprMul::mul_decimalint128_int64_int32_vector,              /* 48 */
  ObExprMul::mul_decimalint128_int32_int128_vector,             /* 49 */
  ObExprMul::mul_decimalint128_int128_int32_vector,             /* 50 */
  ObExprMul::mul_decimalint128_int64_int64_vector,              /* 51 */
  ObExprMul::mul_decimalint128_int64_int128_vector,             /* 52 */
  ObExprMul::mul_decimalint128_int128_int64_vector,             /* 53 */
  ObExprMul::mul_decimalint128_int128_int128_vector,            /* 54 */
  ObExprMul::mul_decimalint256_int32_int128_vector,             /* 55 */
  ObExprMul::mul_decimalint256_int128_int32_vector,             /* 56 */
  ObExprMul::mul_decimalint256_int32_int256_vector,             /* 57 */
  ObExprMul::mul_decimalint256_int256_int32_vector,             /* 58 */
  ObExprMul::mul_decimalint256_int64_int128_vector,             /* 59 */
  ObExprMul::mul_decimalint256_int128_int64_vector,             /* 60 */
  ObExprMul::mul_decimalint256_int64_int256_vector,             /* 61 */
  ObExprMul::mul_decimalint256_int256_int64_vector,             /* 62 */
  ObExprMul::mul_decimalint256_int128_int128_vector,            /* 63 */
  ObExprMul::mul_decimalint256_int128_int256_vector,            /* 64 */
  ObExprMul::mul_decimalint256_int256_int128_vector,            /* 65 */
  ObExprMul::mul_decimalint512_int512_int512_vector,            /* 66 */
  ObExprMul::mul_decimalint512_with_check_vector,               /* 67 */
  ObExprMul::mul_decimalint64_round_vector,                     /* 68 */
  ObExprMul::mul_decimalint128_round_vector,                    /* 69 */
  ObExprMul::mul_decimalint256_round_vector,                    /* 70 */
  ObExprMul::mul_decimalint512_round_vector,                    /* 71 */
  ObExprMul::mul_decimalint512_round_with_check_vector,         /* 72 */
  NULL, /* retired slot */ /* 73 */
  NULL, /* retired slot */ /* 74 */
  NULL, /* retired slot */ /* 75 */
  NULL, /* retired slot */ /* 76 */
  NULL, /* retired slot */ /* 77 */
  NULL, /* retired slot */ /* 78 */
  NULL, /* retired slot */ /* 79 */
  NULL, /* retired slot */ /* 80 */
  NULL, /* retired slot */ /* 81 */
  NULL, /* retired slot */ /* 82 */
  NULL, /* retired slot */ /* 83 */
  NULL, /* retired slot */ /* 84 */
  ObExprDiv::div_float_vector,                                  /* 85 */
  ObExprDiv::div_double_vector,                                 /* 86 */
  ObExprDiv::div_number_vector,                                 /* 87 */
  ObExprAnd::eval_and_vector,                                   /* 88 */
  ObExprOr::eval_or_vector,                                     /* 89 */
  ObExprJoinFilter::eval_bloom_filter_vector,                   /* 90 */
  ObExprJoinFilter::eval_range_filter_vector,                   /* 91 */
  ObExprJoinFilter::eval_in_filter_vector,                      /* 92 */
  ObExprCalcPartitionBase::calc_partition_level_one_vector,     /* 93 */
  ObExprBetween::eval_between_vector,                           /* 94 */
  ObExprLength::calc_mysql_length_vector,                       /* 95 */
  ObExprJoinFilter::eval_in_filter_vector,                      /* 96 */
  ObExprSubstr::eval_substr_vector,                             /* 97 */
  ObExprLower::eval_lower_vector,                               /* 98 */
  ObExprUpper::eval_upper_vector,                               /* 99 */
  ObExprCase::eval_case_vector,                                 /* 100 */
  ObExprFuncRound::calc_round_expr_numeric1_vector,             /* 101 */
  ObExprFuncRound::calc_round_expr_numeric2_vector,             /* 102 */
  ObExprFuncRound::calc_round_expr_datetime1_vector,            /* 103 */
  NULL, // ObExprFuncRound::calc_round_expr_datetime2_vector,   /* 104 */
  ObExprLike::eval_like_expr_vector_only_text_vectorized,       /* 105 */
  NULL,                                                         /* 106 */
  ObExprExtract::calc_extract_mysql_vector,                     /* 107 */
  ObExprRegexpReplace::eval_regexp_replace_vector,              /* 108 */
  ObExprInOrNotIn::eval_vector_in_without_row_fallback,         /* 109 */
  ObExprInOrNotIn::eval_vector_in_without_row,                  /* 110 */
  NULL,//ObExprDecodeTraceId::calc_decode_trace_id_expr_vector  /* 111 */
  ObExprTopNFilter::eval_topn_filter_vector,                    /* 112 */
  ObExprCeilFloor::calc_ceil_floor_vector,                      /* 113 */
  ObExprRepeat::eval_repeat_vector,                             /* 114 */
  ObExprRegexpReplace::eval_regexp_replace_vector,              /* 115 */
  ObExprArrayContains::eval_array_contains_vector_int64_t,      /* 116 */
  ObExprArrayContains::eval_array_contains_vector_float,        /* 117 */
  ObExprArrayContains::eval_array_contains_vector_double,       /* 118 */
  ObExprArrayContains::eval_array_contains_vector_ObString,     /* 119 */
  ObExprArrayContains::eval_array_contains_array_vector,        /* 120 */
  ObExprCalcPartitionBase::fast_calc_partition_level_one_vector,/* 121 */
  ObExprTrim::eval_trim_vector,                                 /* 122 */
  NULL, // ObExprEncodeSortkey::eval_encode_sortkey_vector      /* 123 */
  ObExprArrayOverlaps::eval_array_overlaps_vector,              /* 124 */
  ObExprArrayContainsAll::eval_array_contains_all_vector,       /* 125 */
  ObBitwiseExprOperator::calc_bitwise_result2_mysql_vector,     /* 126 */
  NULL,                                                         /* 127 */
  ObExprDiv::decint_div_mysql_vec_fn<int32_t, int32_t>,         /* 128 */
  ObExprDiv::decint_div_mysql_vec_fn<int64_t, int32_t>,         /* 129 */
  ObExprDiv::decint_div_mysql_vec_fn<int64_t, int64_t>,         /* 130 */
  ObExprDiv::decint_div_mysql_vec_fn<int128_t, int32_t>,        /* 131 */
  ObExprDiv::decint_div_mysql_vec_fn<int128_t, int64_t>,        /* 132 */
  ObExprDiv::decint_div_mysql_vec_fn<int128_t, int128_t>,       /* 133 */
  ObExprDiv::decint_div_mysql_vec_fn<int256_t, int32_t>,        /* 134 */
  ObExprDiv::decint_div_mysql_vec_fn<int256_t, int64_t>,        /* 135 */
  ObExprDiv::decint_div_mysql_vec_fn<int256_t, int128_t>,       /* 136 */
  ObExprDiv::decint_div_mysql_vec_fn<int256_t, int256_t>,       /* 137 */
  ObExprDiv::decint_div_mysql_vec_fn<int512_t, int32_t>,        /* 138 */
  ObExprDiv::decint_div_mysql_vec_fn<int512_t, int64_t>,        /* 139 */
  ObExprDiv::decint_div_mysql_vec_fn<int512_t, int128_t>,       /* 140 */
  ObExprDiv::decint_div_mysql_vec_fn<int512_t, int256_t>,       /* 141 */
  ObExprDiv::decint_div_mysql_vec_fn<int512_t, int512_t>,       /* 142 */
  ObExprArrayRemove::eval_array_remove_vector_int64_t,          /* 143 */
  ObExprArrayRemove::eval_array_remove_vector_float,            /* 144 */
  ObExprArrayRemove::eval_array_remove_vector_double,           /* 145 */
  ObExprArrayRemove::eval_array_remove_vector_ObString,         /* 146 */
  ObExprArrayRemove::eval_array_remove_array_vector,            /* 147 */
  ObExprArrayDistinct::eval_array_distinct_vector,              /* 148 */
  ObExprDateFormat::calc_date_format_vector,                    /* 149 */
  ObExprYear::calc_year_vector,                                 /* 150 */
  ObExprMonth::calc_month_vector,                               /* 151 */
  ObExprMonthName::calc_month_name_vector,                      /* 152 */
  ObExprHour::calc_hour_vector,                                 /* 153 */
  ObExprMinute::calc_minute_vector,                             /* 154 */
  ObExprDayOfYear::calc_dayofyear_vector,                       /* 155 */
  ObExprDayOfMonth::calc_dayofmonth_vector,                     /* 156 */
  ObExprDayOfWeek::calc_dayofweek_vector,                       /* 157 */
  ObExprDayName::calc_dayname_vector,                           /* 158 */
  ObExprWeek::calc_week_vector,                                 /* 159 */
  ObExprWeekOfYear::calc_weekofyear_vector,                     /* 160 */
  ObExprDate::eval_date_vector,                                 /* 161 */
  ObExprDateDiff::eval_date_diff_vector,                        /* 162 */
  ObExprDateAdd::calc_date_add_vector,                          /* 163 */
  ObExprDateSub::calc_date_sub_vector,                          /* 164 */
  ObExprFromDays::calc_fromdays_vector,                         /* 165 */
  ObExprTimeStampDiff::eval_timestamp_diff_vector,              /* 166 */
  ObExprTimeStampAdd::calc_timestamp_add_vector,                /* 167 */
  ObExprArrayToString::eval_array_to_string_vector,             /* 168 */
  ObExprStringToArray::eval_string_to_array_vector,             /* 169 */
  ObExprArrayAppend::eval_array_append_vector,                  /* 170 */
  ObExprElementAt::eval_element_at_vector,                      /* 171 */
  ObExprArrayCardinality::eval_array_cardinality_vector,        /* 172 */
  ObExprArrayPrepend::eval_array_prepend_vector,                /* 173 */
  ObExprArrayConcat::eval_array_concat_vector,                  /* 174 */
  ObExprArrayDifference::eval_array_difference_vector,          /* 175 */
  ObExprArrayMax::eval_array_max_vector,                        /* 176 */
  ObExprArrayMin::eval_array_min_vector,                        /* 177 */
  ObExprArrayAvg::eval_array_avg_vector,                        /* 178 */
  ObExprArraySum::eval_array_sum_vector,                        /* 179 */
  ObExprArrayCompact::eval_array_compact_vector,                 /* 180 */
  ObExprArraySort::eval_array_sort_vector,                       /* 181 */
  ObExprSplitPart::calc_split_part_expr_vec,                             /* 182 */
  ObExprKeyValue::calc_key_value_expr_vector,                            /* 183 */
  NULL, /* retired slot */                                             /* 184 */
  NULL, /* ObExprToChar::eval_to_char_vector removed */                   /* 185 */
  ObExprArrayPosition::eval_array_position_vector,                       /* 186 */
  ObExprArraySlice::eval_array_slice_vector,                             /* 187 */
  ObExprArrayLength::eval_array_length_vector,                           /* 188 */
  NULL, // ObExprRange::eval_range_vector,                               /* 189 */
  ObExprURLEncode::eval_url_encode_vector,                      /* 190 */
  ObExprURLDecode::eval_url_decode_vector,                      /* 191 */
  ObExprIs::calc_vector_is_null,                                /* 192 */
  ObExprIs::calc_vector_is_true,                                /* 193 */
  ObExprIs::calc_vector_is_false,                               /* 194 */
  ObExprIsNot::calc_vector_is_not_null,                         /* 195 */
  ObExprIsNot::calc_vector_is_not_true,                         /* 196 */
  ObExprIsNot::calc_vector_is_not_false,                        /* 197 */
  ObExprBool::calc_vector_bool_expr,                            /* 198 */
  ObExprNotBetween::eval_not_between_vector,                    /* 199 */
  ObExprNot::eval_not_vector,                                   /* 200 */
  ObExprArrayExcept::eval_array_except_vector,                  /* 201 */
  ObExprArrayIntersect::eval_array_intersect_vector,            /* 202 */
  ObExprArrayUnion::eval_array_union_vector,                    /* 203 */
  NULL, // ObExprArrayReplace::eval_array_replace_vector,                /* 204 */
  NULL, // ObExprArrayPopfront::eval_array_popfront_vector,              /* 205 */
  ObExprColumnConv::column_convert_vector,                               /* 206 */
  NULL, /* retired slot */                                              /* 207 */
  NULL, // ObExprConcat::eval_concat_vector,                             /* 208 */
  NULL, // ObExprLpad::calc_mysql_lpad_expr_vector,                      /* 209 */
  NULL, // ObExprRpad::calc_mysql_rpad_expr_vector,                      /* 210 */
  NULL, /* retired slot */                                             /* 211 */
  NULL, /* retired slot */                                             /* 212 */
  ObExprFindInSet::calc_find_in_set_vector,                              /* 213 */
  NULL, // ObExprSubstringIndex::eval_substring_index_vector,            /* 214 */
  NULL, // ObExprConcatWs::calc_concat_ws_expr_vector,                   /* 215 */
  ObExprMapKeys::eval_map_keys_vector,                                   /* 216 */
  ObExprMapValues::eval_map_values_vector,                               /* 217 */
  NULL, // ObExprUDF::eval_udf_vector                                    /* 218 */
  NULL, // ObExprUDF::eval_external_udf_vector,                          /* 219 */
  NULL, // ObExprInstr::calc_mysql_instr_expr_vector,                    /* 220 */
  NULL, /* retired slot */                                             /* 221 */
  NULL, // ObLocationExprOperator::calc_location_expr_vector,            /* 222 */
  ObExprConvertTZ::calc_convert_tz_vector,                               /* 223 */
  ObExprAIComplete::eval_ai_complete_vector,                             /* 224 */
  ObExprAIEmbed::eval_ai_embed_vector,                                   /* 225 */
  NULL, // ObExprAIRerank::eval_ai_rerank_vector,                        /* 226 */
};

static ObExpr::EvalFunc g_decimal_int_eval_functions[] = {
  ObExprAdd::add_decimalint32,
  ObExprAdd::add_decimalint64,
  ObExprAdd::add_decimalint128,
  ObExprAdd::add_decimalint256,
  ObExprAdd::add_decimalint512,
  ObExprAdd::add_decimalint512_with_check,
  ObExprMinus::minus_decimalint32,
  ObExprMinus::minus_decimalint64,
  ObExprMinus::minus_decimalint128,
  ObExprMinus::minus_decimalint256,
  ObExprMinus::minus_decimalint512,
  ObExprMinus::minus_decimalint512_with_check,
  ObExprMul::mul_decimalint32_int32_int32,
  ObExprMul::mul_decimalint64_int32_int32,
  ObExprMul::mul_decimalint64_int32_int64,
  ObExprMul::mul_decimalint64_int64_int32,
  ObExprMul::mul_decimalint128_int32_int64,
  ObExprMul::mul_decimalint128_int64_int32,
  ObExprMul::mul_decimalint128_int32_int128,
  ObExprMul::mul_decimalint128_int128_int32,
  ObExprMul::mul_decimalint128_int64_int64,
  ObExprMul::mul_decimalint128_int64_int128,
  ObExprMul::mul_decimalint128_int128_int64,
  ObExprMul::mul_decimalint128_int128_int128,
  ObExprMul::mul_decimalint256_int32_int128,
  ObExprMul::mul_decimalint256_int128_int32,
  ObExprMul::mul_decimalint256_int32_int256,
  ObExprMul::mul_decimalint256_int256_int32,
  ObExprMul::mul_decimalint256_int64_int128,
  ObExprMul::mul_decimalint256_int128_int64,
  ObExprMul::mul_decimalint256_int64_int256,
  ObExprMul::mul_decimalint256_int256_int64,
  ObExprMul::mul_decimalint256_int128_int128,
  ObExprMul::mul_decimalint256_int128_int256,
  ObExprMul::mul_decimalint256_int256_int128,
  ObExprMul::mul_decimalint512_int512_int512,
  ObExprMul::mul_decimalint512_with_check,
  ObExprMul::mul_decimalint64_round,
  ObExprMul::mul_decimalint128_round,
  ObExprMul::mul_decimalint256_round,
  ObExprMul::mul_decimalint512_round,
  ObExprMul::mul_decimalint512_round_with_check,
  ObExprDiv::div_decimalint_32_32,
  ObExprDiv::div_decimalint_32_64,
  ObExprDiv::div_decimalint_32_128,
  ObExprDiv::div_decimalint_32_256,
  ObExprDiv::div_decimalint_32_512,
  ObExprDiv::div_decimalint_64_32,
  ObExprDiv::div_decimalint_64_64,
  ObExprDiv::div_decimalint_64_128,
  ObExprDiv::div_decimalint_64_256,
  ObExprDiv::div_decimalint_64_512,
  ObExprDiv::div_decimalint_128_32,
  ObExprDiv::div_decimalint_128_64,
  ObExprDiv::div_decimalint_128_128,
  ObExprDiv::div_decimalint_128_256,
  ObExprDiv::div_decimalint_128_512,
  ObExprDiv::div_decimalint_256_32,
  ObExprDiv::div_decimalint_256_64,
  ObExprDiv::div_decimalint_256_128,
  ObExprDiv::div_decimalint_256_256,
  ObExprDiv::div_decimalint_256_512,
  ObExprDiv::div_decimalint_512_32,
  ObExprDiv::div_decimalint_512_64,
  ObExprDiv::div_decimalint_512_128,
  ObExprDiv::div_decimalint_512_256,
  ObExprDiv::div_decimalint_512_512,
  ObExprDiv::div_decimalint_512_32_with_check,
  ObExprDiv::div_decimalint_512_64_with_check,
  ObExprDiv::div_decimalint_512_128_with_check,
  ObExprDiv::div_decimalint_512_256_with_check,
  ObExprDiv::div_decimalint_512_512_with_check,
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  // div functions of decimal int types in mysql mode
  ObExprDiv::decint_div_mysql_fn<int32_t, int32_t>,
  ObExprDiv::decint_div_mysql_fn<int64_t, int32_t>,
  ObExprDiv::decint_div_mysql_fn<int64_t, int64_t>,
  ObExprDiv::decint_div_mysql_fn<int128_t, int32_t>,
  ObExprDiv::decint_div_mysql_fn<int128_t, int64_t>,
  ObExprDiv::decint_div_mysql_fn<int128_t, int128_t>,
  ObExprDiv::decint_div_mysql_fn<int256_t, int32_t>,
  ObExprDiv::decint_div_mysql_fn<int256_t, int64_t>,
  ObExprDiv::decint_div_mysql_fn<int256_t, int128_t>,
  ObExprDiv::decint_div_mysql_fn<int256_t, int256_t>,
  ObExprDiv::decint_div_mysql_fn<int512_t, int32_t>,
  ObExprDiv::decint_div_mysql_fn<int512_t, int64_t>,
  ObExprDiv::decint_div_mysql_fn<int512_t, int128_t>,
  ObExprDiv::decint_div_mysql_fn<int512_t, int256_t>,
  ObExprDiv::decint_div_mysql_fn<int512_t, int512_t>,
};

static ObExpr::EvalBatchFunc g_decimal_int_eval_batch_functions[] = {
  ObExprAdd::add_decimalint32_batch,
  ObExprAdd::add_decimalint64_batch,
  ObExprAdd::add_decimalint128_batch,
  ObExprAdd::add_decimalint256_batch,
  ObExprAdd::add_decimalint512_batch,
  ObExprAdd::add_decimalint512_with_check_batch,
  ObExprMinus::minus_decimalint32_batch,
  ObExprMinus::minus_decimalint64_batch,
  ObExprMinus::minus_decimalint128_batch,
  ObExprMinus::minus_decimalint256_batch,
  ObExprMinus::minus_decimalint512_batch,
  ObExprMinus::minus_decimalint512_with_check_batch,
  ObExprMul::mul_decimalint32_int32_int32_batch,
  ObExprMul::mul_decimalint64_int32_int32_batch,
  ObExprMul::mul_decimalint64_int32_int64_batch,
  ObExprMul::mul_decimalint64_int64_int32_batch,
  ObExprMul::mul_decimalint128_int32_int64_batch,
  ObExprMul::mul_decimalint128_int64_int32_batch,
  ObExprMul::mul_decimalint128_int32_int128_batch,
  ObExprMul::mul_decimalint128_int128_int32_batch,
  ObExprMul::mul_decimalint128_int64_int64_batch,
  ObExprMul::mul_decimalint128_int64_int128_batch,
  ObExprMul::mul_decimalint128_int128_int64_batch,
  ObExprMul::mul_decimalint128_int128_int128_batch,
  ObExprMul::mul_decimalint256_int32_int128_batch,
  ObExprMul::mul_decimalint256_int128_int32_batch,
  ObExprMul::mul_decimalint256_int32_int256_batch,
  ObExprMul::mul_decimalint256_int256_int32_batch,
  ObExprMul::mul_decimalint256_int64_int128_batch,
  ObExprMul::mul_decimalint256_int128_int64_batch,
  ObExprMul::mul_decimalint256_int64_int256_batch,
  ObExprMul::mul_decimalint256_int256_int64_batch,
  ObExprMul::mul_decimalint256_int128_int128_batch,
  ObExprMul::mul_decimalint256_int128_int256_batch,
  ObExprMul::mul_decimalint256_int256_int128_batch,
  ObExprMul::mul_decimalint512_int512_int512_batch,
  ObExprMul::mul_decimalint512_with_check_batch,
  ObExprMul::mul_decimalint64_round_batch,
  ObExprMul::mul_decimalint128_round_batch,
  ObExprMul::mul_decimalint256_round_batch,
  ObExprMul::mul_decimalint512_round_batch,
  ObExprMul::mul_decimalint512_round_with_check_batch,
  ObExprDiv::div_decimalint_32_32_batch,
  ObExprDiv::div_decimalint_32_32_batch,
  ObExprDiv::div_decimalint_32_64_batch,
  ObExprDiv::div_decimalint_32_128_batch,
  ObExprDiv::div_decimalint_32_256_batch,
  ObExprDiv::div_decimalint_32_512_batch,
  ObExprDiv::div_decimalint_64_32_batch,
  ObExprDiv::div_decimalint_64_64_batch,
  ObExprDiv::div_decimalint_64_128_batch,
  ObExprDiv::div_decimalint_64_256_batch,
  ObExprDiv::div_decimalint_64_512_batch,
  ObExprDiv::div_decimalint_128_32_batch,
  ObExprDiv::div_decimalint_128_64_batch,
  ObExprDiv::div_decimalint_128_128_batch,
  ObExprDiv::div_decimalint_128_256_batch,
  ObExprDiv::div_decimalint_128_512_batch,
  ObExprDiv::div_decimalint_256_32_batch,
  ObExprDiv::div_decimalint_256_64_batch,
  ObExprDiv::div_decimalint_256_128_batch,
  ObExprDiv::div_decimalint_256_256_batch,
  ObExprDiv::div_decimalint_256_512_batch,
  ObExprDiv::div_decimalint_512_32_batch,
  ObExprDiv::div_decimalint_512_64_batch,
  ObExprDiv::div_decimalint_512_128_batch,
  ObExprDiv::div_decimalint_512_256_batch,
  ObExprDiv::div_decimalint_512_512_batch,
  ObExprDiv::div_decimalint_512_32_with_check_batch,
  ObExprDiv::div_decimalint_512_64_with_check_batch,
  ObExprDiv::div_decimalint_512_128_with_check_batch,
  ObExprDiv::div_decimalint_512_256_with_check_batch,
  ObExprDiv::div_decimalint_512_512_with_check_batch,
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  NULL, /* retired slot */
  // div functions of decimal int types in mysql mode
  ObExprDiv::decint_div_mysql_batch_fn<int32_t, int32_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int64_t, int32_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int64_t, int64_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int128_t, int32_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int128_t, int64_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int128_t, int128_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int256_t, int32_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int256_t, int64_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int256_t, int128_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int256_t, int256_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int512_t, int32_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int512_t, int64_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int512_t, int128_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int512_t, int256_t>,
  ObExprDiv::decint_div_mysql_batch_fn<int512_t, int512_t>,
};

static ObExpr::EvalFunc g_collection_eval_functions[] = {
  ObExprAdd::add_collection_collection_int8_t,
  ObExprAdd::add_collection_collection_int16_t,
  ObExprAdd::add_collection_collection_int32_t,
  ObExprAdd::add_collection_collection_int64_t,
  ObExprAdd::add_collection_collection_float,
  ObExprAdd::add_collection_collection_double,
  ObExprMinus::minus_collection_collection_int8_t,
  ObExprMinus::minus_collection_collection_int16_t,
  ObExprMinus::minus_collection_collection_int32_t,
  ObExprMinus::minus_collection_collection_int64_t,
  ObExprMinus::minus_collection_collection_float,
  ObExprMinus::minus_collection_collection_double,
  ObExprAdd::add_collection_collection_uint64_t,
  ObExprMinus::minus_collection_collection_uint64_t,
};

static ObExpr::EvalBatchFunc g_collection_eval_batch_functions[] = {
  ObExprAdd::add_collection_collection_int8_t_batch,
  ObExprAdd::add_collection_collection_int16_t_batch,
  ObExprAdd::add_collection_collection_int32_t_batch,
  ObExprAdd::add_collection_collection_int64_t_batch,
  ObExprAdd::add_collection_collection_float_batch,
  ObExprAdd::add_collection_collection_double_batch,
  ObExprMinus::minus_collection_collection_int8_t_batch,
  ObExprMinus::minus_collection_collection_int16_t_batch,
  ObExprMinus::minus_collection_collection_int32_t_batch,
  ObExprMinus::minus_collection_collection_int64_t_batch,
  ObExprMinus::minus_collection_collection_float_batch,
  ObExprMinus::minus_collection_collection_double_batch,
  ObExprAdd::add_collection_collection_uint64_t_batch,
  ObExprMinus::minus_collection_collection_uint64_t_batch,
};

static ObExpr::EvalVectorFunc g_collection_expr_eval_vector_functions[] = {
  ObExprAdd::add_collection_collection_int8_t_vector,
  ObExprAdd::add_collection_collection_int16_t_vector,
  ObExprAdd::add_collection_collection_int32_t_vector,
  ObExprAdd::add_collection_collection_int64_t_vector,
  ObExprAdd::add_collection_collection_float_vector,
  ObExprAdd::add_collection_collection_double_vector,
  ObExprMinus::minus_collection_collection_int8_t_vector,
  ObExprMinus::minus_collection_collection_int16_t_vector,
  ObExprMinus::minus_collection_collection_int32_t_vector,
  ObExprMinus::minus_collection_collection_int64_t_vector,
  ObExprMinus::minus_collection_collection_float_vector,
  ObExprMinus::minus_collection_collection_double_vector,
  ObExprAdd::add_collection_collection_uint64_t_vector,
  ObExprMinus::minus_collection_collection_uint64_t_vector,
};

} // end namespace sql
} // end namespace oceanbase
