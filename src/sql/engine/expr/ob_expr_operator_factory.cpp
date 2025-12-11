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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_expr_operator_factory.h"
#include "sql/engine/expr/ob_expr_substring_index.h"
#include "sql/engine/expr/ob_expr_strcmp.h"
#include "sql/engine/expr/ob_expr_assign.h"
#include "sql/engine/expr/ob_expr_database.h"
#include "sql/engine/expr/ob_expr_and.h"
#include "sql/engine/expr/ob_expr_sin.h"
#include "sql/engine/expr/ob_expr_cos.h"
#include "sql/engine/expr/ob_expr_tan.h"
#include "sql/engine/expr/ob_expr_cot.h"
#include "sql/engine/expr/ob_expr_asin.h"
#include "sql/engine/expr/ob_expr_acos.h"
#include "sql/engine/expr/ob_expr_atan.h"
#include "sql/engine/expr/ob_expr_atan2.h"
#include "sql/engine/expr/ob_expr_arg_case.h"
#include "sql/engine/expr/ob_expr_between.h"
#include "sql/engine/expr/ob_expr_bit_and.h"
#include "sql/engine/expr/ob_expr_bit_xor.h"
#include "sql/engine/expr/ob_expr_bit_or.h"
#include "sql/engine/expr/ob_expr_bit_neg.h"
#include "sql/engine/expr/ob_expr_bit_left_shift.h"
#include "sql/engine/expr/ob_expr_bit_right_shift.h"
#include "sql/engine/expr/ob_expr_bm25.h"
#include "sql/engine/expr/ob_expr_case.h"
#include "sql/engine/expr/ob_expr_oracle_decode.h"
#include "sql/engine/expr/ob_expr_oracle_trunc.h"
#include "sql/engine/expr/ob_expr_fun_values.h"
#include "sql/engine/expr/ob_expr_fun_default.h"
#include "sql/engine/expr/ob_expr_cast.h"
#include "sql/engine/expr/ob_expr_to_type.h"
#include "sql/engine/expr/ob_expr_convert.h"
#include "sql/engine/expr/ob_expr_coalesce.h"
#include "sql/engine/expr/ob_expr_current_user.h"
#include "sql/engine/expr/ob_expr_current_user_priv.h"
#include "sql/engine/expr/ob_expr_concat.h"
#include "sql/engine/expr/ob_expr_concat_ws.h"
#include "sql/engine/expr/ob_expr_div.h"
#include "sql/engine/expr/ob_expr_effective_tenant.h"
#include "sql/engine/expr/ob_expr_effective_tenant_id.h"
#include "sql/engine/expr/ob_expr_equal.h"
#include "sql/engine/expr/ob_expr_from_unix_time.h"
#include "sql/engine/expr/ob_expr_null_safe_equal.h"
#include "sql/engine/expr/ob_expr_nullif.h"
#include "sql/engine/expr/ob_expr_get_user_var.h"
#include "sql/engine/expr/ob_expr_greater_equal.h"
#include "sql/engine/expr/ob_expr_greater_than.h"
#include "sql/engine/expr/ob_expr_greatest.h"
#include "sql/engine/expr/ob_expr_agg_param_list.h"
#include "sql/engine/expr/ob_expr_is_serving_tenant.h"
#include "sql/engine/expr/ob_expr_hex.h"
#include "sql/engine/expr/ob_expr_password.h"
#include "sql/engine/expr/ob_expr_int2ip.h"
#include "sql/engine/expr/ob_expr_ip2int.h"
#include "sql/engine/expr/ob_expr_inet.h"
#include "sql/engine/expr/ob_expr_last_exec_id.h"
#include "sql/engine/expr/ob_expr_last_trace_id.h"
#include "sql/engine/expr/ob_expr_is.h"
#include "sql/engine/expr/ob_expr_length.h"
#include "sql/engine/expr/ob_expr_less_equal.h"
#include "sql/engine/expr/ob_expr_less_than.h"
#include "sql/engine/expr/ob_expr_like.h"
#include "sql/engine/expr/ob_expr_lower.h"
#include "sql/engine/expr/ob_expr_minus.h"
#include "sql/engine/expr/ob_expr_mod.h"
#include "sql/engine/expr/ob_expr_int_div.h"
#include "sql/engine/expr/ob_expr_mul.h"
#include "sql/engine/expr/ob_expr_neg.h"
#include "sql/engine/expr/ob_expr_abs.h"
#include "sql/engine/expr/ob_expr_uuid.h"
#include "sql/engine/expr/ob_expr_not_between.h"
#include "sql/engine/expr/ob_expr_not_equal.h"
#include "sql/engine/expr/ob_expr_not.h"
#include "sql/engine/expr/ob_expr_or.h"
#include "sql/engine/expr/ob_expr_xor.h"
#include "sql/engine/expr/ob_expr_sqrt.h"
#include "sql/engine/expr/ob_expr_log2.h"
#include "sql/engine/expr/ob_expr_log10.h"
#include "sql/engine/expr/ob_expr_regexp.h"
#include "sql/engine/expr/ob_expr_regexp_substr.h"
#include "sql/engine/expr/ob_expr_regexp_instr.h"
#include "sql/engine/expr/ob_expr_regexp_replace.h"
#include "sql/engine/expr/ob_expr_regexp_like.h"
#include "sql/engine/expr/ob_expr_mid.h"
#include "sql/engine/expr/ob_expr_mid.h"
#include "sql/engine/expr/ob_expr_substrb.h"
#include "sql/engine/expr/ob_expr_insert.h"
#include "sql/engine/expr/ob_expr_trim.h"
#include "sql/engine/expr/ob_expr_inner_trim.h"
#include "sql/engine/expr/ob_expr_unhex.h"
#include "sql/engine/expr/ob_expr_user.h"
#include "sql/engine/expr/ob_expr_version.h"
#include "sql/engine/expr/ob_expr_connection_id.h"
#include "sql/engine/expr/ob_expr_sys_view_bigint_param.h"
#include "sql/engine/expr/ob_expr_date.h"
#include "sql/engine/expr/ob_expr_date_add.h"
#include "sql/engine/expr/ob_expr_date_diff.h"
#include "sql/engine/expr/ob_expr_timestamp_diff.h"
#include "sql/engine/expr/ob_expr_time_diff.h"
#include "sql/engine/expr/ob_expr_period_diff.h"
#include "sql/engine/expr/ob_expr_unix_timestamp.h"
#include "sql/engine/expr/ob_expr_maketime.h"
#include "sql/engine/expr/ob_expr_makedate.h"
#include "sql/engine/expr/ob_expr_extract.h"
#include "sql/engine/expr/ob_expr_to_days.h"
#include "sql/engine/expr/ob_expr_day_of_func.h"
#include "sql/engine/expr/ob_expr_from_days.h"
#include "sql/engine/expr/ob_expr_pad.h"
#include "sql/engine/expr/ob_expr_position.h"
#include "sql/engine/expr/ob_expr_column_conv.h"
#include "sql/engine/expr/ob_expr_date_format.h"
#include "sql/engine/expr/ob_expr_str_to_date.h"
#include "sql/engine/expr/ob_expr_cur_time.h"
#include "sql/engine/expr/ob_expr_time_to_usec.h"
#include "sql/engine/expr/ob_expr_usec_to_time.h"
#include "sql/engine/expr/ob_expr_func_round.h"
#include "sql/engine/expr/ob_expr_func_ceil.h"
#include "sql/engine/expr/ob_expr_func_dump.h"
#include "sql/engine/expr/ob_expr_func_sleep.h"
#include "sql/engine/expr/ob_expr_merging_frozen_time.h"
#include "sql/engine/expr/ob_expr_repeat.h"
#include "sql/engine/expr/ob_expr_export_set.h"
#include "sql/engine/expr/ob_expr_replace.h"
#include "sql/engine/expr/ob_expr_func_part_hash.h"
#include "sql/engine/expr/ob_expr_func_partition_key.h"
#include "sql/engine/expr/ob_expr_lnnvl.h"
#include "sql/engine/expr/ob_expr_autoinc_nextval.h"
#include "sql/engine/expr/ob_expr_last_insert_id.h"
#include "sql/engine/expr/ob_expr_instr.h"
#include "sql/engine/expr/ob_expr_locate.h"
#include "sql/engine/expr/ob_expr_subquery_ref.h"
#include "sql/engine/expr/ob_expr_exists.h"
#include "sql/engine/expr/ob_expr_not_exists.h"
#include "sql/engine/expr/ob_expr_collation.h"
#include "sql/engine/expr/ob_expr_subquery_equal.h"
#include "sql/engine/expr/ob_expr_subquery_not_equal.h"
#include "sql/engine/expr/ob_expr_subquery_greater_equal.h"
#include "sql/engine/expr/ob_expr_subquery_greater_than.h"
#include "sql/engine/expr/ob_expr_subquery_less_equal.h"
#include "sql/engine/expr/ob_expr_subquery_less_than.h"
#include "sql/engine/expr/ob_expr_sys_privilege_check.h"
#include "sql/engine/expr/ob_expr_reverse.h"
#include "sql/engine/expr/ob_expr_right.h"
#include "sql/engine/expr/ob_expr_md5.h"
#include "sql/engine/expr/ob_expr_crc32.h"
#include "sql/engine/expr/ob_expr_lrpad.h"
#include "sql/engine/expr/ob_expr_conv.h"
#include "sql/engine/expr/ob_expr_sign.h"
#include "sql/engine/expr/ob_expr_pow.h"
#include "sql/engine/expr/ob_expr_found_rows.h"
#include "sql/engine/expr/ob_expr_row_count.h"
#include "sql/engine/expr/ob_expr_char_length.h"
#include "sql/engine/expr/ob_expr_ifnull.h"
#include "sql/engine/expr/ob_expr_quote.h"
#include "sql/engine/expr/ob_expr_field.h"
#include "sql/engine/expr/ob_expr_timestamp_nvl.h"
#include "sql/engine/expr/ob_expr_subquery_ns_equal.h"
#include "sql/engine/expr/ob_expr_host_ip.h"
#include "sql/engine/expr/ob_expr_rpc_port.h"
#include "sql/engine/expr/ob_expr_mysql_port.h"
#include "sql/engine/expr/ob_expr_char.h"
#include "sql/engine/expr/ob_expr_get_sys_var.h"
#include "sql/engine/expr/ob_expr_elt.h"
#include "sql/engine/expr/ob_expr_part_id.h"
#include "sql/engine/expr/ob_expr_timestamp_add.h"
#include "sql/engine/expr/ob_expr_des_hex_str.h"
#include "sql/engine/expr/ob_expr_doc_id.h"
#include "sql/engine/expr/ob_expr_doc_length.h"
#include "sql/engine/expr/ob_expr_word_segment.h"
#include "sql/engine/expr/ob_expr_word_count.h"
#include "sql/engine/expr/ob_expr_ascii.h"
#include "sql/engine/expr/ob_expr_truncate.h"
#include "sql/engine/expr/ob_expr_bit_count.h"
#include "sql/engine/expr/ob_expr_make_set.h"
#include "sql/engine/expr/ob_expr_find_in_set.h"
#include "sql/engine/expr/ob_expr_estimate_ndv.h"
#include "sql/engine/expr/ob_expr_left.h"
#include "sql/engine/expr/ob_expr_space.h"
#include "sql/engine/expr/ob_expr_rand.h"
#include "sql/engine/expr/ob_expr_randstr.h"
#include "sql/engine/expr/ob_expr_random.h"
#include "sql/engine/expr/ob_expr_generator_func.h"
#include "sql/engine/expr/ob_expr_zipf.h"
#include "sql/engine/expr/ob_expr_normal.h"
#include "sql/engine/expr/ob_expr_uniform.h"
#include "sql/engine/expr/ob_expr_obj_access.h"
#include "sql/engine/expr/ob_expr_type_to_str.h"
#include "sql/engine/expr/ob_expr_sys_op_opnsize.h"
#include "sql/engine/expr/ob_expr_get_package_var.h"
#include "sql/engine/expr/ob_expr_get_subprogram_var.h"
#include "sql/engine/expr/ob_expr_shadow_uk_project.h"
#include "sql/engine/expr/ob_expr_time_format.h"
#include "sql/engine/expr/ob_expr_udf.h"
#include "sql/engine/expr/ob_expr_week_of_func.h"
#include "sql/engine/expr/ob_expr_dll_udf.h"
#include "sql/engine/expr/ob_expr_timestamp.h"
#include "sql/engine/expr/ob_expr_seq_nextval.h"
#include "sql/engine/expr/ob_expr_pl_integer_checker.h"
#include "sql/engine/expr/ob_expr_pl_get_cursor_attr.h"
#include "sql/engine/expr/ob_expr_pl_sqlcode_sqlerrm.h"
#include "sql/engine/expr/ob_expr_plsql_variable.h"
#include "sql/engine/expr/ob_expr_pl_associative_index.h"
#include "sql/engine/expr/ob_expr_symmetric_encrypt.h"
#include "sql/engine/expr/ob_expr_collection_construct.h"
#include "sql/engine/expr/ob_expr_nvl.h"
#include "sql/engine/expr/ob_expr_object_construct.h"
#include "sql/engine/expr/ob_expr_exp.h"
#include "sql/engine/expr/ob_expr_log.h"
#include "sql/engine/expr/ob_expr_bool.h"
#include "sql/engine/expr/ob_expr_part_id_pseudo_column.h"
#include "sql/engine/expr/ob_expr_stmt_id.h"
#include "sql/engine/expr/ob_expr_obversion.h"
#include "sql/engine/expr/ob_expr_remove_const.h"
#include "sql/engine/expr/ob_expr_wrapper_inner.h"
#include "sql/engine/expr/ob_expr_coll_pred.h"
#include "sql/engine/expr/ob_expr_radians.h"
#include "sql/engine/expr/ob_expr_pi.h"
#include "sql/engine/expr/ob_expr_to_outfile_row.h"
#include "sql/engine/expr/ob_expr_format.h"
#include "sql/engine/expr/ob_expr_quarter.h"
#include "sql/engine/expr/ob_expr_bit_length.h"
#include "sql/engine/expr/ob_expr_soundex.h"
#include "sql/engine/expr/ob_expr_output_pack.h"
#include "sql/engine/expr/ob_expr_degrees.h"
#include "sql/engine/expr/ob_expr_any_value.h"
#include "sql/engine/expr/ob_expr_validate_password_strength.h"
#include "sql/engine/expr/ob_expr_uuid_short.h"
#include "sql/engine/expr/ob_expr_benchmark.h"
#include "sql/engine/expr/ob_expr_weight_string.h"
#include "sql/engine/expr/ob_expr_convert_tz.h"
#include "sql/engine/expr/ob_expr_to_base64.h"
#include "sql/engine/expr/ob_expr_from_base64.h"
#include "sql/engine/expr/ob_expr_random_bytes.h"
#include "sql/engine/expr/ob_pl_expr_subquery.h"
#include "sql/engine/expr/ob_expr_encode_sortkey.h"
#include "sql/engine/expr/ob_expr_hash.h"
#include "sql/engine/expr/ob_expr_json_object.h"
#include "sql/engine/expr/ob_expr_json_extract.h"
#include "sql/engine/expr/ob_expr_json_schema_valid.h"
#include "sql/engine/expr/ob_expr_json_schema_validation_report.h"
#include "sql/engine/expr/ob_expr_json_contains.h"
#include "sql/engine/expr/ob_expr_json_contains_path.h"
#include "sql/engine/expr/ob_expr_json_depth.h"
#include "sql/engine/expr/ob_expr_json_keys.h"
#include "sql/engine/expr/ob_expr_json_search.h"
#include "sql/engine/expr/ob_expr_json_unquote.h"
#include "sql/engine/expr/ob_expr_json_quote.h"
#include "sql/engine/expr/ob_expr_json_array.h"
#include "sql/engine/expr/ob_expr_json_overlaps.h"
#include "sql/engine/expr/ob_expr_json_valid.h"
#include "sql/engine/expr/ob_expr_json_remove.h"
#include "sql/engine/expr/ob_expr_json_append.h"
#include "sql/engine/expr/ob_expr_json_array_insert.h"
#include "sql/engine/expr/ob_expr_json_value.h"
#include "sql/engine/expr/ob_expr_json_replace.h"
#include "sql/engine/expr/ob_expr_json_type.h"
#include "sql/engine/expr/ob_expr_json_length.h"
#include "sql/engine/expr/ob_expr_json_insert.h"
#include "sql/engine/expr/ob_expr_json_storage_size.h"
#include "sql/engine/expr/ob_expr_json_storage_free.h"
#include "sql/engine/expr/ob_expr_json_set.h"
#include "sql/engine/expr/ob_expr_json_merge.h"
#include "sql/engine/expr/ob_expr_json_merge_patch.h"
#include "sql/engine/expr/ob_expr_json_pretty.h"
#include "sql/engine/expr/ob_expr_json_member_of.h"
#include "sql/engine/expr/ob_expr_sha.h"
#include "sql/engine/expr/ob_expr_compress.h"
#include "sql/engine/expr/ob_expr_statement_digest.h"
#include "sql/engine/expr/ob_expr_timestamp_to_scn.h"
#include "sql/engine/expr/ob_expr_scn_to_timestamp.h"
#include "sql/engine/expr/ob_expr_errno.h"
#include "sql/engine/expr/ob_expr_json_query.h"
#include "sql/engine/expr/ob_expr_point.h"
#include "sql/engine/expr/ob_expr_spatial_collection.h"
#include "sql/engine/expr/ob_expr_st_area.h"
#include "sql/engine/expr/ob_expr_st_intersects.h"
#include "sql/engine/expr/ob_expr_st_x.h"
#include "sql/engine/expr/ob_expr_st_transform.h"
#include "sql/engine/expr/ob_expr_priv_st_transform.h"
#include "sql/engine/expr/ob_expr_st_covers.h"
#include "sql/engine/expr/ob_expr_st_bestsrid.h"
#include "sql/engine/expr/ob_expr_st_astext.h"
#include "sql/engine/expr/ob_expr_st_buffer.h"
#include "sql/engine/expr/ob_expr_spatial_cellid.h"
#include "sql/engine/expr/ob_expr_spatial_mbr.h"
#include "sql/engine/expr/ob_expr_st_geomfromewkb.h"
#include "sql/engine/expr/ob_expr_st_geomfromwkb.h"
#include "sql/engine/expr/ob_expr_st_geomfromewkt.h"
#include "sql/engine/expr/ob_expr_priv_st_geographyfromtext.h"
#include "sql/engine/expr/ob_expr_st_asewkt.h"
#include "sql/engine/expr/ob_expr_st_distance.h"
#include "sql/engine/expr/ob_expr_st_geometryfromtext.h"
#include "sql/engine/expr/ob_expr_priv_st_setsrid.h"
#include "sql/engine/expr/ob_expr_priv_st_point.h"
#include "sql/engine/expr/ob_expr_st_isvalid.h"
#include "sql/engine/expr/ob_expr_st_dwithin.h"
#include "sql/engine/expr/ob_expr_st_aswkb.h"
#include "sql/engine/expr/ob_expr_st_distance_sphere.h"
#include "sql/engine/expr/ob_expr_st_contains.h"
#include "sql/engine/expr/ob_expr_st_within.h"
#include "sql/engine/expr/ob_expr_priv_st_asewkb.h"
#include "sql/engine/expr/ob_expr_current_scn.h"
#include "sql/engine/expr/ob_expr_name_const.h"
#include "sql/engine/expr/ob_expr_format_bytes.h"
#include "sql/engine/expr/ob_expr_format_pico_time.h"
#include "sql/engine/expr/ob_expr_encrypt.h"
#include "sql/engine/expr/ob_expr_icu_version.h"
#include "sql/engine/expr/ob_expr_sql_mode_convert.h"
#include "sql/engine/expr/ob_expr_prefix_pattern.h"
#include "sql/engine/expr/ob_expr_extract_value.h"
#include "sql/engine/expr/ob_expr_update_xml.h"
#include "sql/engine/expr/ob_expr_sql_udt_construct.h"
#include "sql/engine/expr/ob_expr_priv_st_numinteriorrings.h"
#include "sql/engine/expr/ob_expr_priv_st_iscollection.h"
#include "sql/engine/expr/ob_expr_priv_st_equals.h"
#include "sql/engine/expr/ob_expr_priv_st_touches.h"
#include "sql/engine/expr/ob_expr_align_date4cmp.h"
#include "sql/engine/expr/ob_expr_extract_cert_expired_time.h"
#include "sql/engine/expr/ob_expr_transaction_id.h"
#include "sql/engine/expr/ob_expr_inner_row_cmp_val.h"
#include "sql/engine/expr/ob_expr_last_refresh_scn.h"
#include "sql/engine/expr/ob_expr_priv_st_makeenvelope.h"
#include "sql/engine/expr/ob_expr_priv_st_clipbybox2d.h"
#include "sql/engine/expr/ob_expr_priv_st_pointonsurface.h"
#include "sql/engine/expr/ob_expr_priv_st_geometrytype.h"
#include "sql/engine/expr/ob_expr_st_crosses.h"
#include "sql/engine/expr/ob_expr_st_overlaps.h"
#include "sql/engine/expr/ob_expr_st_union.h"
#include "sql/engine/expr/ob_expr_st_length.h"
#include "sql/engine/expr/ob_expr_st_difference.h"
#include "sql/engine/expr/ob_expr_st_asgeojson.h"
#include "sql/engine/expr/ob_expr_st_centroid.h"
#include "sql/engine/expr/ob_expr_st_symdifference.h"
#include "sql/engine/expr/ob_expr_priv_st_asmvtgeom.h"
#include "sql/engine/expr/ob_expr_priv_st_makevalid.h"
#include "sql/engine/expr/ob_expr_gtid.h"
#include "sql/engine/expr/ob_expr_array.h"
#include "sql/engine/expr/ob_expr_vec_ivf_center_id.h"
#include "sql/engine/expr/ob_expr_vec_ivf_center_vector.h"
#include "sql/engine/expr/ob_expr_vec_ivf_flat_data_vector.h"
#include "sql/engine/expr/ob_expr_vec_ivf_sq8_data_vector.h"
#include "sql/engine/expr/ob_expr_vec_ivf_meta_id.h"
#include "sql/engine/expr/ob_expr_vec_ivf_meta_vector.h"
#include "sql/engine/expr/ob_expr_vec_ivf_pq_center_id.h"
#include "sql/engine/expr/ob_expr_vec_ivf_pq_center_ids.h"
#include "sql/engine/expr/ob_expr_vec_ivf_pq_center_vector.h"
#include "sql/engine/expr/ob_expr_vec_vid.h"
#include "sql/engine/expr/ob_expr_vec_type.h"
#include "sql/engine/expr/ob_expr_vec_vector.h"
#include "sql/engine/expr/ob_expr_vec_scn.h"
#include "sql/engine/expr/ob_expr_vec_key.h"
#include "sql/engine/expr/ob_expr_vec_data.h"
#include "sql/engine/expr/ob_expr_spiv_dim.h"
#include "sql/engine/expr/ob_expr_spiv_value.h"
#include "sql/engine/expr/ob_expr_vector.h"
#include "sql/engine/expr/ob_expr_semantic_distance.h"
#include "sql/engine/expr/ob_expr_vec_chunk.h"
#include "sql/engine/expr/ob_expr_embedded_vec.h"
#include "sql/engine/expr/ob_expr_inner_table_option_printer.h"
#include "sql/engine/expr/ob_expr_rb_build_empty.h"
#include "sql/engine/expr/ob_expr_rb_is_empty.h"
#include "sql/engine/expr/ob_expr_rb_build_varbinary.h"
#include "sql/engine/expr/ob_expr_rb_to_varbinary.h"
#include "sql/engine/expr/ob_expr_rb_cardinality.h"
#include "sql/engine/expr/ob_expr_rb_calc_cardinality.h"
#include "sql/engine/expr/ob_expr_rb_calc.h"
#include "sql/engine/expr/ob_expr_rb_to_string.h"
#include "sql/engine/expr/ob_expr_rb_from_string.h"
#include "sql/engine/expr/ob_expr_rb_select.h"
#include "sql/engine/expr/ob_expr_rb_build.h"
#include "sql/engine/expr/ob_expr_array_contains.h"
#include "sql/engine/expr/ob_expr_array_to_string.h"
#include "sql/engine/expr/ob_expr_string_to_array.h"
#include "sql/engine/expr/ob_expr_array_append.h"
#include "sql/engine/expr/ob_expr_array_concat.h"
#include "sql/engine/expr/ob_expr_array_difference.h"
#include "sql/engine/expr/ob_expr_array_max.h"
#include "sql/engine/expr/ob_expr_array_avg.h"
#include "sql/engine/expr/ob_expr_array_compact.h"
#include "sql/engine/expr/ob_expr_array_sort.h"
#include "sql/engine/expr/ob_expr_array_sortby.h"
#include "sql/engine/expr/ob_expr_array_filter.h"
#include "sql/engine/expr/ob_expr_element_at.h"
#include "sql/engine/expr/ob_expr_array_cardinality.h"
#include "sql/engine/expr/ob_expr_tokenize.h"
#include "sql/engine/expr/ob_expr_lock_func.h"
#include "sql/engine/expr/ob_expr_decode_trace_id.h"
#include "sql/engine/expr/ob_expr_topn_filter.h"
#include "sql/engine/expr/ob_expr_get_path.h"
#include "sql/engine/expr/ob_expr_transaction_id.h"
#include "sql/engine/expr/ob_expr_can_access_trigger.h"
#include "sql/engine/expr/ob_expr_split_part.h"
#include "sql/engine/expr/ob_expr_inner_decode_like.h"
#include "sql/engine/expr/ob_expr_inner_double_to_int.h"
#include "sql/engine/expr/ob_expr_inner_decimal_to_year.h"
#include "sql/engine/expr/ob_expr_array_overlaps.h"
#include "sql/engine/expr/ob_expr_array_contains_all.h"
#include "sql/engine/expr/ob_expr_array_distinct.h"
#include "sql/engine/expr/ob_expr_array_remove.h"
#include "sql/engine/expr/ob_expr_array_map.h"
#include "sql/engine/expr/ob_expr_array_range.h"
#include "sql/engine/expr/ob_expr_array_first.h"
#include "sql/engine/expr/ob_expr_mysql_proc_info.h"
#include "sql/engine/expr/ob_expr_get_mysql_routine_parameter_type_str.h"
#include "sql/engine/expr/ob_expr_priv_st_geohash.h"
#include "sql/engine/expr/ob_expr_priv_st_makepoint.h"
#include "sql/engine/expr/ob_expr_to_pinyin.h"
#include "sql/engine/expr/ob_expr_url_codec.h"
#include "sql/engine/expr/ob_expr_keyvalue.h"
#include "sql/engine/expr/ob_expr_demote_cast.h"
#include "sql/engine/expr/ob_expr_array_sum.h"
#include "sql/engine/expr/ob_expr_array_length.h"
#include "sql/engine/expr/ob_expr_array_position.h"
#include "sql/engine/expr/ob_expr_array_slice.h"
#include "sql/engine/expr/ob_expr_inner_info_cols_printer.h"
#include "sql/engine/expr/ob_expr_array_except.h"
#include "sql/engine/expr/ob_expr_array_intersect.h"
#include "sql/engine/expr/ob_expr_array_union.h"
#include "sql/engine/expr/ob_expr_map.h"
#include "sql/engine/expr/ob_expr_rb_to_array.h"
#include "sql/engine/expr/ob_expr_rb_contains.h"
#include "sql/engine/expr/ob_expr_map_keys.h"
#include "sql/engine/expr/ob_expr_current_catalog.h"
#include "sql/engine/expr/ob_expr_check_catalog_access.h"
#include "sql/engine/expr/ob_expr_oracle_to_char.h"
#include "sql/engine/expr/ob_expr_semantic_distance.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_complete.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_embed.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_rerank.h"
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_prompt.h"
#include "sql/engine/expr/ob_expr_vector_similarity.h"
#include "sql/engine/expr/ob_expr_check_location_access.h"


#include "sql/engine/expr/ob_expr_lock_func.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{
static AllocFunc OP_ALLOC[T_MAX_OP];
static AllocFunc                                                                           OP_ALLOC_ORCL[T_MAX_OP];

#define REG_OP(OpClass)                             \
  do {                                              \
    [&]() {                                         \
      OpClass op(alloc);                            \
      if (OB_UNLIKELY(i >= EXPR_OP_NUM)) {          \
        LOG_ERROR_RET(common::OB_ERR_UNEXPECTED, "out of the max expr");           \
      } else {                                      \
        NAME_TYPES[i].name_ = op.get_name();        \
        NAME_TYPES[i].type_ = op.get_type();        \
        NAME_TYPES[i].is_internal_ = op.is_internal_for_mysql(); \
        OP_ALLOC[op.get_type()] = ObExprOperatorFactory::alloc<OpClass>; \
        i++;                                        \
      }                                             \
    }();                                            \
  } while(0)
// When developing two functionally identical expressions (e.g., mid and substr expressions) you can use this macro
// OriOp is the existing expression, now we want to develop NewOp with the same functionality, using this macro can avoid duplicate code
// But require OriOp has already registered
#define REG_SAME_OP(OriOpType, NewOpType, NewOpName, idx_mysql)        \
  do {                                                                 \
    [&]() {                                                            \
      if (OB_UNLIKELY((idx_mysql) >= EXPR_OP_NUM)) {                   \
        LOG_ERROR_RET(common::OB_ERR_UNEXPECTED, "out of the max expr");                              \
      } else if (OB_ISNULL(OP_ALLOC[OriOpType])) {                     \
        LOG_ERROR_RET(common::OB_ERR_UNEXPECTED, "OriOp is not registered yet", K(OriOpType), K(NewOpType)); \
      } else {                                                         \
        NAME_TYPES[(idx_mysql)].name_ = NewOpName;                     \
        NAME_TYPES[(idx_mysql)].type_ = NewOpType;                     \
        OP_ALLOC[NewOpType] = OP_ALLOC[OriOpType];                     \
        (idx_mysql)++;                                                 \
      }                                                                \
    }();                                                               \
  } while(0)

#define REG_OP_ORCL(OpClass)                        \
  do {                                              \
    [&]() {                                         \
      OpClass op(alloc);                            \
      if (OB_UNLIKELY(j >= EXPR_OP_NUM)) {          \
        LOG_ERROR_RET(common::OB_ERR_UNEXPECTED, "out of the max expr");           \
      } else {                                      \
        NAME_TYPES_ORCL[j].name_ = op.get_name();   \
        NAME_TYPES_ORCL[j].type_ = op.get_type();   \
        NAME_TYPES_ORCL[j].is_internal_ = op.is_internal_for_oracle();\
        OP_ALLOC_ORCL[op.get_type()] = ObExprOperatorFactory::alloc<OpClass>; \
        j++;                                        \
      }                                             \
    }();                                            \
  } while(0)
// Used for registering the same function expression in Oracle mode
#define REG_SAME_OP_ORCL(OriOpType, NewOpType, NewOpName, idx_oracle)      \
  do {                                                                     \
    [&]() {                                                                \
      if (OB_UNLIKELY((idx_oracle) >= EXPR_OP_NUM)) {                      \
        LOG_ERROR_RET(common::OB_ERR_UNEXPECTED, "out of the max expr");                                  \
      } else if (OB_ISNULL(OP_ALLOC_ORCL[OriOpType])) {                    \
        LOG_ERROR_RET(common::OB_ERR_UNEXPECTED, "OriOp is not registered yet", K(OriOpType), K(NewOpType)); \
      } else {                                                             \
        NAME_TYPES_ORCL[(idx_oracle)].name_ = NewOpName;                   \
        NAME_TYPES_ORCL[(idx_oracle)].type_ = NewOpType;                   \
        OP_ALLOC_ORCL[NewOpType] = OP_ALLOC_ORCL[OriOpType];               \
        (idx_oracle)++;                                                    \
      }                                                                    \
    }();                                                                   \
  } while(0)

ObExprOperatorFactory::NameType ObExprOperatorFactory::NAME_TYPES[EXPR_OP_NUM] = { };
ObExprOperatorFactory::NameType ObExprOperatorFactory::NAME_TYPES_ORCL[EXPR_OP_NUM] = { };


ObExprOperatorType ObExprOperatorFactory::get_type_by_name(const ObString &name)
{
  ObExprOperatorType type = T_INVALID;
  ObString real_func_name;
  get_function_alias_name(name, real_func_name);
  if (real_func_name.empty()) {
    real_func_name.assign_ptr(name.ptr(), name.length());
  }
  for (uint32_t i = 0; i < ARRAYSIZEOF(NAME_TYPES); i++) {
    if (NAME_TYPES[i].type_ <= T_MIN_OP || NAME_TYPES[i].type_ >= T_MAX_OP) {
      break;
    }
    if (static_cast<int32_t>(strlen(NAME_TYPES[i].name_)) == real_func_name.length()
        && strncasecmp(NAME_TYPES[i].name_, real_func_name.ptr(), real_func_name.length()) == 0) {
      type = NAME_TYPES[i].type_;
      break;
    }
  }
  return type;
}

void ObExprOperatorFactory::get_internal_info_by_name(const ObString &name, bool &exist, bool &is_internal)
{
  exist = false;
  ObString real_func_name;
  get_function_alias_name(name, real_func_name);
  if (real_func_name.empty()) {
    real_func_name.assign_ptr(name.ptr(), name.length());
  }
  for (uint32_t i = 0; i < ARRAYSIZEOF(NAME_TYPES); i++) {
    if (NAME_TYPES[i].type_ <= T_MIN_OP || NAME_TYPES[i].type_ >= T_MAX_OP) {
      break;
    }
    if (static_cast<int32_t>(strlen(NAME_TYPES[i].name_)) == real_func_name.length()
        && strncasecmp(NAME_TYPES[i].name_, real_func_name.ptr(), real_func_name.length()) == 0) {
      exist = true;
      is_internal = NAME_TYPES[i].is_internal_;
      break;
    }
  }
}


void ObExprOperatorFactory::register_expr_operators()
{
  memset(NAME_TYPES, 0, sizeof(NAME_TYPES));
  memset(NAME_TYPES_ORCL, 0, sizeof(NAME_TYPES_ORCL));
  ObArenaAllocator alloc;
  int64_t i = 0;
  int64_t j = 0;
  /*
  --REG_OP is used for mysql tenant registration, REG_OP_ORCL is used for oracle tenant system function registration
  --If the same function needs to be used under both mysql tenant and oracle, and compatibility has been implemented
  --Please use REG_OP() and REG_OP_ORCL() respectively for registration
  For formatting, please register in the oracle system function section at the end of the function
  */
  [&]() {
    REG_OP(ObExprAdd);
    REG_OP(ObExprAggAdd);
    REG_OP(ObExprAnd);
    REG_OP(ObExprArgCase);
    REG_OP(ObExprAssign);
    REG_OP(ObExprBetween);
    REG_OP(ObExprBitAnd);
    REG_OP(ObExprCase);
    REG_OP(ObExprCast);
    REG_OP(ObExprTimeStampAdd);
    REG_OP(ObExprToType);
    REG_OP(ObExprChar);
    REG_OP(ObExprToChar);
    REG_OP(ObExprConvert);
    REG_OP(ObExprCoalesce);
    REG_OP(ObExprNvl);
    REG_OP(ObExprConcat);
    REG_OP(ObExprCurrentUser);
    REG_OP(ObExprCurrentUserPriv);
    REG_OP(ObExprYear);
    REG_OP(ObExprOracleDecode);
    REG_OP(ObExprOracleTrunc);
    REG_OP(ObExprDiv);
    REG_OP(ObExprAggDiv);
    REG_OP(ObExprEffectiveTenant);
    REG_OP(ObExprEffectiveTenantId);
    REG_OP(ObExprEqual);
    REG_OP(ObExprNullSafeEqual);
    REG_OP(ObExprGetUserVar);
    REG_OP(ObExprGreaterEqual);
    REG_OP(ObExprGreaterThan);
    REG_OP(ObExprGreatest);
    REG_OP(ObExprHex);
    REG_OP(ObExprPassword);
    REG_OP(ObExprIn);
    REG_OP(ObExprNotIn);
    REG_OP(ObExprInt2ip);
    REG_OP(ObExprIp2int);
    REG_OP(ObExprInetAton);
    REG_OP(ObExprInet6Ntoa);
    REG_OP(ObExprInet6Aton);
    REG_OP(ObExprIsIpv4);
    REG_OP(ObExprIsIpv6);
    REG_OP(ObExprIsIpv4Mapped);
    REG_OP(ObExprIsIpv4Compat);
    REG_OP(ObExprInsert);
    REG_OP(ObExprIs);
    REG_OP(ObExprIsNot);
    REG_OP(ObExprLeast);
    REG_OP(ObExprLength);
    REG_OP(ObExprLessEqual);
    REG_OP(ObExprLessThan);
    REG_OP(ObExprLike);
    REG_OP(ObExprLower);
    REG_OP(ObExprMinus);
    REG_OP(ObExprAggMinus);
    REG_OP(ObExprMod);
    REG_OP(ObExprMd5);
    REG_OP(ObExprTime);
    REG_OP(ObExprHour);
    REG_OP(ObExprRpad);
    REG_OP(ObExprLpad);
    REG_OP(ObExprColumnConv);
    REG_OP(ObExprFunValues);
    REG_OP(ObExprFunDefault);
    REG_OP(ObExprIntDiv);
    REG_OP(ObExprMul);
    REG_OP(ObExprAggMul);
    REG_OP(ObExprAbs);
    REG_OP(ObExprUuid);
    REG_OP(ObExprNeg);
    REG_OP(ObExprFromUnixTime);
    REG_OP(ObExprNotBetween);
    REG_OP(ObExprNotEqual);
    REG_OP(ObExprNot);
    REG_OP(ObExprOr);
    REG_OP(ObExprXor);
    REG_OP(ObExprRegexp);
    REG_OP(ObExprRegexpSubstr);
    REG_OP(ObExprRegexpInstr);
    REG_OP(ObExprRegexpReplace);
    REG_OP(ObExprRegexpLike);
    REG_OP(ObExprSleep);
    REG_OP(ObExprStrcmp);
    REG_OP(ObExprSubstr);
    REG_OP(ObExprMid);
    REG_OP(ObExprSubstringIndex);
    REG_OP(ObExprMid);
    REG_OP(ObExprSysViewBigintParam);
    REG_OP(ObExprInnerTrim);
    REG_OP(ObExprTrim);
    REG_OP(ObExprLtrim);
    REG_OP(ObExprSpace);
    REG_OP(ObExprRtrim);
    REG_OP(ObExprUnhex);
    REG_OP(ObExprUpper);
    REG_OP(ObExprConv);
    REG_OP(ObExprUser);
    REG_OP(ObExprDate);
    REG_OP(ObExprMonth);
    REG_OP(ObExprMonthName);
    REG_OP(ObExprSoundex);
    REG_OP(ObExprDateAdd);
    REG_OP(ObExprDateSub);
    REG_OP(ObExprSubtime);
    REG_OP(ObExprAddtime);
    REG_OP(ObExprDateDiff);
    REG_OP(ObExprTimeStampDiff);
    REG_OP(ObExprTimeDiff);
    REG_OP(ObExprPeriodDiff);
    REG_OP(ObExprPeriodAdd);
    REG_OP(ObExprUnixTimestamp);
    REG_OP(ObExprMakeTime);
    REG_OP(ObExprMakedate);
    REG_OP(ObExprExtract);
    REG_OP(ObExprToDays);
    REG_OP(ObExprPosition);
    REG_OP(ObExprFromDays);
    REG_OP(ObExprDateFormat);
    REG_OP(ObExprGetFormat);
    REG_OP(ObExprStrToDate);
    REG_OP(ObExprCurDate);
    REG_OP(ObExprCurTime);
    REG_OP(ObExprSysdate);
    REG_OP(ObExprCurTimestamp);
    REG_OP(ObExprUtcTimestamp);
    REG_OP(ObExprUtcTime);
    REG_OP(ObExprUtcDate);
    REG_OP(ObExprTimeToUsec);
    REG_OP(ObExprUsecToTime);
    REG_OP(ObExprMergingFrozenTime);
    REG_OP(ObExprFuncRound);
    REG_OP(ObExprFuncFloor);
    REG_OP(ObExprFuncCeil);
    REG_OP(ObExprFuncCeiling);
    REG_OP(ObExprFuncDump);
    REG_OP(ObExprRepeat);
    REG_OP(ObExprExportSet);
    REG_OP(ObExprReplace);
    REG_OP(ObExprFuncPartHash);
    REG_OP(ObExprFuncPartKey);
    REG_OP(ObExprDatabase);
    REG_OP(ObExprAutoincNextval);
    REG_OP(ObExprLastInsertID);
    REG_OP(ObExprInstr);
    REG_OP(ObExprFuncLnnvl);
    REG_OP(ObExprLocate);
    REG_OP(ObExprVersion);
    REG_OP(ObExprObVersion);
    REG_OP(ObExprConnectionId);
    REG_OP(ObExprCharset);
    REG_OP(ObExprCollation);
    REG_OP(ObExprCoercibility);
    REG_OP(ObExprConvertTZ);
    REG_OP(ObExprSetCollation);
    REG_OP(ObExprReverse);
    REG_OP(ObExprRight);
    REG_OP(ObExprSign);
    REG_OP(ObExprBitXor);
    REG_OP(ObExprSqrt);
    REG_OP(ObExprLog2);
    REG_OP(ObExprLog10);
    REG_OP(ObExprPow);
    REG_OP(ObExprRowCount);
    REG_OP(ObExprFoundRows);
    REG_OP(ObExprAggParamList);
    REG_OP(ObExprIsServingTenant);
    REG_OP(ObExprSysPrivilegeCheck);
    REG_OP(ObExprField);
    REG_OP(ObExprElt);
    REG_OP(ObExprNullif);
    REG_OP(ObExprTimestampNvl);
    REG_OP(ObExprDesHexStr);
    REG_OP(ObExprAscii);
    REG_OP(ObExprOrd);
    REG_OP(ObExprBitCount);
    REG_OP(ObExprFindInSet);
    REG_OP(ObExprLeft);
    REG_OP(ObExprRand);
    REG_OP(ObExprMakeSet);
    REG_OP(ObExprEstimateNdv);
    REG_OP(ObExprSysOpOpnsize);
    REG_OP(ObExprDayOfMonth);
    REG_OP(ObExprDayOfWeek);
    REG_OP(ObExprDayOfYear);
    REG_OP(ObExprSecond);
    REG_OP(ObExprMinute);
    REG_OP(ObExprMicrosecond);
    REG_OP(ObExprToSeconds);
    REG_OP(ObExprTimeToSec);
    REG_OP(ObExprSecToTime);
    REG_OP(ObExprInterval);
    REG_OP(ObExprTruncate);
    REG_OP(ObExprDllUdf);
    REG_OP(ObExprExp);
    REG_OP(ObExprAnyValue);
    REG_OP(ObExprUuidShort);
    REG_OP(ObExprRandomBytes);
    /* subquery comparison experator */
    REG_OP(ObExprSubQueryRef);
    REG_OP(ObExprSubQueryEqual);
    REG_OP(ObExprSubQueryNotEqual);
    REG_OP(ObExprSubQueryNSEqual);
    REG_OP(ObExprSubQueryGreaterEqual);
    REG_OP(ObExprSubQueryGreaterThan);
    REG_OP(ObExprSubQueryLessEqual);
    REG_OP(ObExprSubQueryLessThan);
    REG_OP(ObExprRemoveConst);
    REG_OP(ObExprExists);
    REG_OP(ObExprNotExists);
    REG_OP(ObExprCharLength);
    REG_OP(ObExprBitAnd);
    REG_OP(ObExprBitOr);
    REG_OP(ObExprBitNeg);
    REG_OP(ObExprBitLeftShift);
    REG_OP(ObExprBitLength);
    REG_OP(ObExprBitRightShift);
    REG_OP(ObExprIfNull);
    REG_OP(ObExprConcatWs);
    REG_OP(ObExprCmpMeta);
    REG_OP(ObExprQuote);
    REG_OP(ObExprPad);
    REG_OP(ObExprHostIP);
    REG_OP(ObExprRpcPort);
    REG_OP(ObExprMySQLPort);
    REG_OP(ObExprGetSysVar);
    REG_OP(ObExprPartId);
    REG_OP(ObExprLastTraceId);
    REG_OP(ObExprLastExecId);
    REG_OP(ObExprDocID);
    REG_OP(ObExprDocLength);
    REG_OP(ObExprWordSegment);
    REG_OP(ObExprWordCount);
    REG_OP(ObExprObjAccess);
    REG_OP(ObExprEnumToStr);
    REG_OP(ObExprSetToStr);
    REG_OP(ObExprEnumToInnerType);
    REG_OP(ObExprSetToInnerType);
    REG_OP(ObExprGetPackageVar);
    REG_OP(ObExprGetSubprogramVar);
    REG_OP(ObExprShadowUKProject);
    REG_OP(ObExprUDF);
    REG_OP(ObExprWeekOfYear);
    REG_OP(ObExprWeekDay);
    REG_OP(ObExprYearWeek);
    REG_OP(ObExprWeek);
    REG_OP(ObExprQuarter);
    REG_OP(ObExprSeqNextval);
    REG_OP(ObExprAesDecrypt);
    REG_OP(ObExprAesEncrypt);
    REG_OP(ObExprBool);
    REG_OP(ObExprSin);
    REG_OP(ObExprCos);
    REG_OP(ObExprTan);
    REG_OP(ObExprCot);
    REG_OP(ObExprCalcPartitionId);
    REG_OP(ObExprCalcTabletId);
    REG_OP(ObExprCalcPartitionTabletId);
    REG_OP(ObExprPartIdPseudoColumn);
    REG_OP(ObExprStmtId);
    REG_OP(ObExprRadians);
    REG_OP(ObExprJoinFilter);
    REG_OP(ObExprAsin);
    REG_OP(ObExprAcos);
    REG_OP(ObExprAtan);
    REG_OP(ObExprAtan2);
    REG_OP(ObExprToOutfileRow);
    REG_OP(ObExprFormat);
    REG_OP(ObExprLastDay);
    REG_OP(ObExprPi);
    REG_OP(ObExprLog);
    REG_OP(ObExprTimeFormat);
    REG_OP(ObExprTimestamp);
    REG_OP(ObExprOutputPack);
    REG_OP(ObExprWrapperInner);
    REG_OP(ObExprDegrees);
    REG_OP(ObExprValidatePasswordStrength);
    REG_OP(ObExprDay);
    REG_OP(ObExprBenchmark);
    REG_OP(ObExprWeightString);
    REG_OP(ObExprCrc32);
    REG_OP(ObExprToBase64);
    REG_OP(ObExprFromBase64);
    REG_OP(ObExprOpSubQueryInPl);
    REG_OP(ObExprEncodeSortkey);
    REG_OP(ObExprHash);
    REG_OP(ObExprJsonObject);
    REG_OP(ObExprJsonExtract);
    REG_OP(ObExprJsonSchemaValid);
    REG_OP(ObExprJsonSchemaValidationReport);
    REG_OP(ObExprJsonContains);
    REG_OP(ObExprJsonContainsPath);
    REG_OP(ObExprJsonDepth);
    REG_OP(ObExprJsonKeys);
    REG_OP(ObExprJsonQuote);
    REG_OP(ObExprJsonUnquote);
    REG_OP(ObExprJsonArray);
    REG_OP(ObExprJsonOverlaps);
    REG_OP(ObExprJsonRemove);
    REG_OP(ObExprJsonSearch);
    REG_OP(ObExprJsonValid);
    REG_OP(ObExprJsonArrayAppend);
    REG_OP(ObExprJsonAppend);
    REG_OP(ObExprJsonArrayInsert);
    REG_OP(ObExprJsonValue);
    REG_OP(ObExprJsonReplace);
    REG_OP(ObExprJsonType);
    REG_OP(ObExprJsonLength);
    REG_OP(ObExprJsonInsert);
    REG_OP(ObExprJsonStorageSize);
    REG_OP(ObExprJsonStorageFree);
    REG_OP(ObExprJsonSet);
    REG_OP(ObExprJsonMergePreserve);
    REG_OP(ObExprJsonMerge);
    REG_OP(ObExprJsonMergePatch);
    REG_OP(ObExprJsonPretty);
    REG_OP(ObExprJsonMemberOf);
    REG_OP(ObExprExtractValue);
    REG_OP(ObExprUpdateXml);
    REG_OP(ObExprSha);
    REG_SAME_OP(T_FUN_SYS_SHA ,T_FUN_SYS_SHA, N_SHA1, i);
    REG_OP(ObExprSha2);
    REG_OP(ObExprCompress);
    REG_OP(ObExprUncompress);
    REG_OP(ObExprUncompressedLength);
    REG_OP(ObExprStatementDigest);
    REG_OP(ObExprStatementDigestText);
    REG_OP(ObExprTimestampToScn);
    REG_OP(ObExprScnToTimestamp);
    REG_OP(ObExprSqlModeConvert);
    REG_OP(ObExprCanAccessTrigger);
    REG_OP(ObExprMysqlProcInfo);
    REG_OP(ObExprInnerTypeToEnumSet);
#if  defined(ENABLE_DEBUG_LOG) || !defined(NDEBUG)
    // convert input value into an OceanBase error number and throw out as exception
    REG_OP(ObExprErrno);
#endif
    REG_OP(ObExprPoint);
    REG_OP(ObExprLineString);
    REG_OP(ObExprMultiPoint);
    REG_OP(ObExprMultiLineString);
    REG_OP(ObExprPolygon);
    REG_OP(ObExprMultiPolygon);
    REG_OP(ObExprGeomCollection);
    REG_OP(ObExprGeometryCollection);
    REG_OP(ObExprSTGeomFromText);
    REG_OP(ObExprSTArea);
    REG_OP(ObExprSTIntersects);
    REG_OP(ObExprSTX);
    REG_OP(ObExprSTY);
    REG_OP(ObExprSTLatitude);
    REG_OP(ObExprSTLongitude);
    REG_OP(ObExprSTTransform);
    REG_OP(ObExprPrivSTTransform);
    REG_OP(ObExprPrivSTCovers);
    REG_OP(ObExprPrivSTBestsrid);
    REG_OP(ObExprSTAsText);
    REG_OP(ObExprSTAsWkt);
    REG_OP(ObExprSTBufferStrategy);
    REG_OP(ObExprSTBuffer);
    REG_OP(ObExprSpatialCellid);
    REG_OP(ObExprSpatialMbr);
    REG_OP(ObExprPrivSTGeomFromEWKB);
    REG_OP(ObExprSTGeomFromWKB);
    REG_OP(ObExprSTGeometryFromWKB);
    REG_OP(ObExprPrivSTGeomFromEwkt);
    REG_OP(ObExprPrivSTAsEwkt);
    REG_OP(ObExprSTSRID);
    REG_OP(ObExprSTDistance);
    REG_OP(ObExprPrivSTGeogFromText);
    REG_OP(ObExprPrivSTGeographyFromText);
    REG_OP(ObExprPrivSTSetSRID);
    REG_OP(ObExprSTGeometryFromText);
    REG_OP(ObExprPrivSTPoint);
    REG_OP(ObExprSTIsValid);
    REG_OP(ObExprPrivSTBuffer);
    REG_OP(ObExprPrivSTDWithin);
    REG_OP(ObExprSTAsWkb);
    REG_OP(ObExprStPrivAsEwkb);
    REG_OP(ObExprSTAsBinary);
    REG_OP(ObExprSTDistanceSphere);
    REG_OP(ObExprSTContains);
    REG_OP(ObExprSTWithin);
    REG_OP(ObExprFormatBytes);
    REG_OP(ObExprFormatPicoTime);
    REG_OP(ObExprUuid2bin);
    REG_OP(ObExprIsUuid);
    REG_OP(ObExprBin2uuid);
    REG_OP(ObExprNameConst);
    REG_OP(ObExprDayName);
    REG_OP(ObExprDesDecrypt);
    REG_OP(ObExprDesEncrypt);
    REG_OP(ObExprEncrypt);
    REG_OP(ObExprCurrentScn);
    REG_OP(ObExprEncode);
    REG_OP(ObExprDecode);
    REG_OP(ObExprICUVersion);
    REG_OP(ObExprGeneratorFunc);
    REG_OP(ObExprZipf);
    REG_OP(ObExprNormal);
    REG_OP(ObExprUniform);
    REG_OP(ObExprRandom);
    REG_OP(ObExprRandstr);
    REG_OP(ObExprPrefixPattern);
    REG_OP(ObExprPrivSTNumInteriorRings);
    REG_OP(ObExprPrivSTIsCollection);
    REG_OP(ObExprPrivSTEquals);
    REG_OP(ObExprPrivSTTouches);
    REG_OP(ObExprAlignDate4Cmp);
    REG_OP(ObExprJsonQuery);
    REG_OP(ObExprBM25);
    REG_OP(ObExprGetLock);
    REG_OP(ObExprIsFreeLock);
    REG_OP(ObExprIsUsedLock);
    REG_OP(ObExprReleaseLock);
    REG_OP(ObExprReleaseAllLocks);
    REG_OP(ObExprExtractExpiredTime);
    REG_OP(ObExprTransactionId);
    REG_OP(ObExprInnerRowCmpVal);
    REG_OP(ObExprLastRefreshScn);
    REG_OP(ObExprTopNFilter);
    REG_OP(ObExprPrivSTMakeEnvelope);
    REG_OP(ObExprPrivSTClipByBox2D);
    REG_OP(ObExprPrivSTPointOnSurface);
    REG_OP(ObExprPrivSTGeometryType);
    REG_OP(ObExprSTCrosses);
    REG_OP(ObExprSTOverlaps);
    REG_OP(ObExprSTUnion);
    REG_OP(ObExprSTLength);
    REG_OP(ObExprSTDifference);
    REG_OP(ObExprSTAsGeoJson);
    REG_OP(ObExprSTCentroid);
    REG_OP(ObExprSTSymDifference);
    REG_OP(ObExprPrivSTAsMVTGeom);
    REG_OP(ObExprPrivSTMakeValid);
    REG_OP(ObExprPrivSTGeoHash);
    REG_OP(ObExprPrivSTMakePoint);
    REG_OP(ObExprCurrentRole);
    REG_OP(ObExprArray);
    REG_OP(ObExprDemoteCast);
    REG_OP(ObExprRangePlacement);
    REG_OP(ObExprMap);
    /* vector index */
    REG_OP(ObExprVecIVFCenterID);
    REG_OP(ObExprVecIVFCenterVector);
    REG_OP(ObExprVecIVFFlatDataVector);
    REG_OP(ObExprVecIVFSQ8DataVector);
    REG_OP(ObExprVecIVFMetaID);
    REG_OP(ObExprVecIVFMetaVector);
    REG_OP(ObExprVecIVFPQCenterId);
    REG_OP(ObExprVecIVFPQCenterIds);
    REG_OP(ObExprVecIVFPQCenterVector);
    REG_OP(ObExprVecVid);
    REG_OP(ObExprVecType);
    REG_OP(ObExprVecVector);
    REG_OP(ObExprVecScn);
    REG_OP(ObExprVecKey);
    REG_OP(ObExprVecData);
    REG_OP(ObExprVecChunk);
    REG_OP(ObExprEmbeddedVec);
    REG_OP(ObExprSpivDim);
    REG_OP(ObExprSpivValue);
    REG_OP(ObExprVectorL2Distance);
    REG_OP(ObExprVectorCosineDistance);
    REG_OP(ObExprVectorIPDistance);
    REG_OP(ObExprVectorNegativeIPDistance);
    REG_OP(ObExprVectorL1Distance);
    REG_OP(ObExprVectorDims);
    REG_OP(ObExprVectorNorm);
    REG_OP(ObExprVectorDistance);
    REG_OP(ObExprSemanticDistance);
    REG_OP(ObExprSemanticVectorDistance);
    REG_OP(ObExprVectorL2Similarity);
    REG_OP(ObExprVectorCosineSimilarity);
    REG_OP(ObExprVectorIPSimilarity);
    REG_OP(ObExprVectorSimilarity);
    REG_OP(ObExprInnerTableOptionPrinter);
    REG_OP(ObExprInnerTableSequenceGetter);
    REG_OP(ObExprRbBuildEmpty);
    REG_OP(ObExprRbIsEmpty);
    REG_OP(ObExprRbBuildVarbinary);
    REG_OP(ObExprRbToVarbinary);
    REG_OP(ObExprRbCardinality);
    REG_OP(ObExprRbAndCardinality);
    REG_OP(ObExprRbOrCardinality);
    REG_OP(ObExprRbXorCardinality);
    REG_OP(ObExprRbAndnotCardinality);
    REG_OP(ObExprRbAndNull2emptyCardinality);
    REG_OP(ObExprRbOrNull2emptyCardinality);
    REG_OP(ObExprRbAndnotNull2emptyCardinality);
    REG_OP(ObExprRbAnd);
    REG_OP(ObExprRbOr);
    REG_OP(ObExprRbXor);
    REG_OP(ObExprRbAndnot);
    REG_OP(ObExprRbAndNull2empty);
    REG_OP(ObExprRbOrNull2empty);
    REG_OP(ObExprRbAndnotNull2empty);
    REG_OP(ObExprRbToString);
    REG_OP(ObExprRbFromString);
    REG_OP(ObExprRbSelect);
    REG_OP(ObExprRbBuild);
    REG_OP(ObExprRbToArray);
    REG_OP(ObExprRbContains);
    REG_OP(ObExprGetPath);
    REG_OP(ObExprGTIDSubset);
    REG_OP(ObExprGTIDSubtract);
    REG_OP(ObExprWaitForExecutedGTIDSet);
    REG_OP(ObExprWaitUntilSQLThreadAfterGTIDs);
    REG_OP(ObExprArrayContains);
    REG_OP(ObExprArrayToString);
    REG_OP(ObExprStringToArray);
    REG_OP(ObExprArrayAppend);
    REG_OP(ObExprArrayLength);
    REG_OP(ObExprArrayPrepend);
    REG_OP(ObExprArrayConcat);
    REG_OP(ObExprArrayDifference);
    REG_OP(ObExprArrayCompact);
    REG_OP(ObExprArraySort);
    REG_OP(ObExprArraySortby);
    REG_OP(ObExprArrayFilter);
    REG_OP(ObExprElementAt);
    REG_OP(ObExprArrayCardinality);
    REG_OP(ObExprArrayMax);
    REG_OP(ObExprArrayMin);
    REG_OP(ObExprArrayAvg);
    REG_OP(ObExprArrayFirst);
    REG_OP(ObExprDecodeTraceId);
    REG_OP(ObExprIsEnabledRole);
    REG_OP(ObExprSm3);
    REG_OP(ObExprSm4Encrypt);
    REG_OP(ObExprSm4Decrypt);
    REG_OP(ObExprSplitPart);
    REG_OP(ObExprInnerIsTrue);
    REG_OP(ObExprInnerDecodeLike);
    REG_OP(ObExprInnerDoubleToInt);
    REG_OP(ObExprInnerDecimalToYear);
    REG_OP(ObExprTokenize);
    REG_OP(ObExprArrayOverlaps);
    REG_OP(ObExprArrayContainsAll);
    REG_OP(ObExprArrayDistinct);
    REG_OP(ObExprArrayRemove);
    REG_OP(ObExprArrayMap);
    REG_OP(ObExprArraySum);
    REG_OP(ObExprArrayPosition);
    REG_OP(ObExprArraySlice);
    REG_OP(ObExprArrayRange);
    REG_OP(ObExprArrayExcept);
    REG_OP(ObExprArrayIntersect);
    REG_OP(ObExprArrayUnion);
    REG_OP(ObExprGetMySQLRoutineParameterTypeStr);
    REG_OP(ObExprToPinyin);
    REG_OP(ObExprURLEncode);
    REG_OP(ObExprURLDecode);
    REG_OP(ObExprKeyValue);
    REG_OP(ObExprMapKeys);
    REG_OP(ObExprMapValues);
    REG_OP(ObExprInnerInfoColsColumnDefPrinter);
    REG_OP(ObExprInnerInfoColsCharLenPrinter);
    REG_OP(ObExprInnerInfoColsCharNamePrinter);
    REG_OP(ObExprInnerInfoColsCollNamePrinter);
    REG_OP(ObExprInnerInfoColsPrivPrinter);
    REG_OP(ObExprInnerInfoColsExtraPrinter);
    REG_OP(ObExprInnerInfoColsDataTypePrinter);
    REG_OP(ObExprInnerInfoColsColumnTypePrinter);
    REG_OP(ObExprCurrentCatalog);
    REG_OP(ObExprCheckCatalogAccess);
    REG_OP(ObExprInnerInfoColsColumnKeyPrinter);
    REG_OP(ObExprVectorL2Squared);
    REG_OP(ObExprAIComplete);
    REG_OP(ObExprAIEmbed);
    REG_OP(ObExprAIRerank);
    REG_OP(ObExprAIPrompt);
    REG_OP(ObExprCheckLocationAccess);
  }();
}

bool ObExprOperatorFactory::is_expr_op_type_valid(ObExprOperatorType type)
{
  bool bret = false;
  if (type > T_REF_COLUMN && type < T_MAX_OP) {
    bret = true;
  }
  return bret;
}

int ObExprOperatorFactory::alloc(ObExprOperatorType type, ObExprOperator *&expr_op)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_expr_op_type_valid(type))) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(ret), K(type));
  } else if (OB_ISNULL(OP_ALLOC[type])) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "unexpectd expr item type", K(ret), K(type));
  } else if (OB_FAIL(OP_ALLOC[type](alloc_, expr_op))) {
    OB_LOG(WARN, "fail to alloc expr_op", K(ret), K(type));
  } else if (OB_ISNULL(expr_op)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    OB_LOG(ERROR, "fail to alloc expr_op", K(ret), K(type));
  } else if (NULL != next_expr_id_) {
    expr_op->set_id((*next_expr_id_)++);
  }
  return ret;
}

int ObExprOperatorFactory::alloc_fast_expr(ObExprOperatorType type, ObFastExprOperator *&fast_op)
{
#define BEGIN_ALLOC \
  switch (type) {
#define END_ALLOC \
    default: \
      ret = OB_ERR_UNEXPECTED; \
      LOG_WARN("invalid operator type", K(type)); \
      break; \
  }
#define ALLOC_FAST_EXPR(op_type, OpClass) \
  case op_type : { \
    void *ptr = alloc_.alloc(sizeof(OpClass)); \
    if (OB_ISNULL(ptr)) { \
      ret = OB_ALLOCATE_MEMORY_FAILED; \
      LOG_WARN("allocate operator failed", K(op_type)); \
    } else { \
      fast_op = new(ptr) OpClass(alloc_); \
    } \
    break; \
  }

  int ret = OB_SUCCESS;
  BEGIN_ALLOC
  ALLOC_FAST_EXPR(T_FUN_COLUMN_CONV, ObFastColumnConvExpr);
  END_ALLOC
  return ret;
#undef BEGIN_ALLOC
#undef END_ALLOC
#undef ALLOC_FAST_EXPR
}

//int ObExprOperatorFactory::free(ObExprOperator *&expr_op)
//{
//  int ret = OB_SUCCESS;
//  if (OB_ISNULL(expr_op)) {
//    ret = OB_INVALID_ARGUMENT;
//    OB_LOG(WARN, "invalid argument", K(ret), K(expr_op));
//  } else {
//    expr_op->~ObExprOperator();
//    alloc_.free(expr_op);
//    expr_op = NULL;
//  }
//  return ret;
//}

template <typename ClassT>
int ObExprOperatorFactory::alloc(common::ObIAllocator &alloc, ObExprOperator *&expr_op)
{
  int ret = common::OB_SUCCESS;
  void *buf = NULL;
  if (OB_ISNULL(buf = alloc.alloc(sizeof(ClassT)))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    OB_LOG(ERROR, "fail to alloc expr_operator", K(ret));
  } else {
    expr_op = new(buf) ClassT(alloc);
  }
  return ret;
}


void ObExprOperatorFactory::get_function_alias_name(const ObString &origin_name, ObString &alias_name) {
  if (is_mysql_mode()) {
    //for synonyms in mysql mode
    if (0 == origin_name.case_compare("bin")) {
      // bin(N) is equivalent to CONV(N,10,2)
      alias_name = ObString::make_string(N_CONV);
    } else if (0 == origin_name.case_compare("oct")) {
      // oct(N) is equivalent to CONV(N,10,8)
      alias_name = ObString::make_string(N_CONV);
    } else if (0 == origin_name.case_compare("lcase")) {
      // lcase is synonym for lower
      alias_name = ObString::make_string(N_LOWER);
    } else if (0 == origin_name.case_compare("ucase")) {
      // ucase is synonym for upper
      alias_name = ObString::make_string(N_UPPER);
    } else if (0 == origin_name.case_compare("power")) {
      // don't alias "power" to "pow" in oracle mode, because oracle has no
      // "pow" function.
      alias_name = ObString::make_string(N_POW);
    } else if (0 == origin_name.case_compare("VEC_IVF_CENTER_ID")) {
      alias_name = ObString::make_string(N_VEC_IVF_CENTER_ID);
    } else if (0 == origin_name.case_compare("VEC_IVF_CENTER_VECTOR")) {
      alias_name = ObString::make_string(N_VEC_IVF_CENTER_VECTOR);
    } else if (0 == origin_name.case_compare("VEC_IVF_SQ8_DATA_VECTOR")) {
      alias_name = ObString::make_string(N_VEC_IVF_SQ8_DATA_VECTOR);
    } else if (0 == origin_name.case_compare("VEC_IVF_FLAT_DATA_VECTOR")) {
      alias_name = ObString::make_string(N_VEC_IVF_FLAT_DATA_VECTOR);
    } else if (0 == origin_name.case_compare("VEC_IVF_META_ID")) {
      alias_name = ObString::make_string(N_VEC_IVF_META_ID);
    } else if (0 == origin_name.case_compare("VEC_IVF_META_VECTOR")) {
      alias_name = ObString::make_string(N_VEC_IVF_META_VECTOR);
    } else if (0 == origin_name.case_compare("VEC_IVF_PQ_CENTER_ID")) {
      alias_name = ObString::make_string(N_VEC_IVF_PQ_CENTER_ID);
    } else if (0 == origin_name.case_compare("VEC_IVF_PQ_CENTER_IDS")) {
      alias_name = ObString::make_string(N_VEC_IVF_PQ_CENTER_IDS);
    } else if (0 == origin_name.case_compare("VEC_IVF_PQ_CENTER_VECTOR")) {
      alias_name = ObString::make_string(N_VEC_IVF_PQ_CENTER_VECTOR);
    } else if (0 == origin_name.case_compare("VEC_VID")) {
      alias_name = ObString::make_string(N_VEC_VID);
    } else if (0 == origin_name.case_compare("VEC_TYPE")) {
      alias_name = ObString::make_string(N_VEC_TYPE);
    } else if (0 == origin_name.case_compare("VEC_VECTOR")) {
      alias_name = ObString::make_string(N_VEC_VECTOR);
    } else if (0 == origin_name.case_compare("EMBEDDED_VEC")) {
      alias_name = ObString::make_string(N_EMBEDDED_VEC);
    } else if (0 == origin_name.case_compare("VEC_SCN")) {
      alias_name = ObString::make_string(N_VEC_SCN);
    } else if (0 == origin_name.case_compare("VEC_KEY")) {
      alias_name = ObString::make_string(N_VEC_KEY);
    } else if (0 == origin_name.case_compare("VEC_DATA")) {
      alias_name = ObString::make_string(N_VEC_DATA);
    } else if (0 == origin_name.case_compare("VEC_CHUNK")) {
      alias_name = ObString::make_string(N_VEC_CHUNK);
    } else if (0 == origin_name.case_compare("SPIV_DIM")) {
      alias_name = ObString::make_string(N_SPIV_DIM); 
    } else if (0 == origin_name.case_compare("SPIV_VALUE")) {
      alias_name = ObString::make_string(N_SPIV_VALUE);  
    } else if (0 == origin_name.case_compare("DOC_ID")) {
      alias_name = ObString::make_string(N_DOC_ID);
    } else if (0 == origin_name.case_compare("ws")) {
      // ws is synonym for word_segment
      alias_name = ObString::make_string(N_WORD_SEGMENT);
    } else if (0 == origin_name.case_compare("WORD_COUNT")) {
      alias_name = ObString::make_string(N_WORD_COUNT);
    } else if (0 == origin_name.case_compare("DOC_LENGTH")) {
      alias_name = ObString::make_string(N_DOC_LENGTH);
    } else if (0 == origin_name.case_compare("inet_ntoa")) {
      // inet_ntoa is synonym for int2ip
      alias_name = ObString::make_string(N_INT2IP);
    } else if (0 == origin_name.case_compare("octet_length")) {
      // octet_length is synonym for length
      alias_name = ObString::make_string(N_LENGTH);
    } else if (0 == origin_name.case_compare("character_length")) {
      // character_length is synonym for char_length
      alias_name = ObString::make_string(N_CHAR_LENGTH);
    } else if (0 == origin_name.case_compare("area")) {
      // area is synonym for st_area
      alias_name = ObString::make_string(N_ST_AREA);
    } else if (0 == origin_name.case_compare("centroid")) {
      // centroid is synonym for st_centroid
      alias_name = ObString::make_string(N_ST_CENTROID);
    } else if (0 == origin_name.case_compare("semantic_distance")) {
      alias_name = ObString::make_string(N_SEMANTIC_DISTANCE);
    } else {
      //do nothing
    }
  } else {
    //for synonyms in oracle mode
  }
}

} //end sql
} //end oceanbase

