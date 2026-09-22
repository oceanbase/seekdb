/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "seekdb/plugin/sql_spi.h"
#include "seekdb/plugin/extension_spi.h"
#include "seekdb/plugin/optimizer_spi.h"
#include "seekdb/plugin/server_dev_planner.h"
#include "seekdb/plugin/server_dev.h"
#include "seekdb/plugin/server_dev_executor.h"
#include "seekdb/plugin/catalog_spi.h"
#include "seekdb/plugin/memory_spi.h"
#include <stdio.h>
#include <stdalign.h>
#define TYPE(n, t) printf(#n ".size=%zu\n" #n ".align=%zu\n", sizeof(t), alignof(t))
#define FIELD(n, t, f) printf(#n "." #f "=%zu\n", offsetof(t, f))
#define FIELD_LIST_V1(n, t) \
 FIELD(n,t,struct_size); FIELD(n,t,abi_major); FIELD(n,t,abi_minor); FIELD(n,t,host_handle); \
 FIELD(n,t,alloc); FIELD(n,t,free); FIELD(n,t,log); FIELD(n,t,acquire_service); \
 FIELD(n,t,release_service); FIELD(n,t,begin_registration); FIELD(n,t,register_service); \
 FIELD(n,t,commit_registration); FIELD(n,t,abort_registration); FIELD(n,t,reserved)
int main(void) {
  TYPE(CustomRow,seekdb_plugin_custom_row_v1_t);
#define X(f) FIELD(CustomRow,seekdb_plugin_custom_row_v1_t,f)
  X(struct_size); X(column_count); X(values); X(reserved);
#undef X
  TYPE(CustomContext,seekdb_plugin_custom_context_v1_t);
#define X(f) FIELD(CustomContext,seekdb_plugin_custom_context_v1_t,f)
  X(struct_size); X(input_count); X(output_column_count); X(reserved_word); X(host_context);
  X(next_input); X(emit); X(check_interrupt); X(reserved);
#undef X
  TYPE(CustomExecutor,seekdb_plugin_custom_executor_v1_t);
  TYPE(CustomColumn,seekdb_plugin_custom_column_v1_t);
#define S(f) FIELD(CustomColumn,seekdb_plugin_custom_column_v1_t,f)
  S(struct_size); S(flags); S(encoding); S(sql_type); S(collation); S(precision); S(scale); S(reserved_word); S(type_id); S(reserved);
#undef S
  TYPE(CustomSchema,seekdb_plugin_custom_schema_v1_t);
#define S(f) FIELD(CustomSchema,seekdb_plugin_custom_schema_v1_t,f)
  S(struct_size); S(column_count); S(columns); S(reserved);
#undef S
  TYPE(CustomContextV2,seekdb_plugin_custom_context_v2_t);
#define S(f) FIELD(CustomContextV2,seekdb_plugin_custom_context_v2_t,f)
  S(v1); S(inputs); S(output); S(reserved);
#undef S
  TYPE(CustomContextV3,seekdb_plugin_custom_context_v3_t);
#define S(f) FIELD(CustomContextV3,seekdb_plugin_custom_context_v3_t,f)
  S(v2); S(rescan_input); S(reserved);
#undef S
  TYPE(CustomContextV4,seekdb_plugin_custom_context_v4_t);
#define S(f) FIELD(CustomContextV4,seekdb_plugin_custom_context_v4_t,f)
  S(v3); S(bind_rescan_input); S(reserved);
#undef S
  TYPE(InputBinding,seekdb_plugin_input_binding_v1_t);
#define S(f) FIELD(InputBinding,seekdb_plugin_input_binding_v1_t,f)
  S(parameter); S(source_input); S(source_column); S(target_input);
#undef S
  TYPE(CustomPathRequestV4,seekdb_plugin_custom_path_request_v4_t);
#define S(f) FIELD(CustomPathRequestV4,seekdb_plugin_custom_path_request_v4_t,f)
  S(v3); S(bindings); S(binding_count); S(reserved_word); S(reserved);
#undef S
#define X(f) FIELD(CustomExecutor,seekdb_plugin_custom_executor_v1_t,f)
  X(struct_size); X(spi_major); X(spi_minor); X(reserved_word); X(open); X(next); X(rescan); X(close); X(reserved);
#undef X
  TYPE(ServerDevManifest,seekdb_plugin_server_dev_manifest_v1_t);
#define SD(f) FIELD(ServerDevManifest,seekdb_plugin_server_dev_manifest_v1_t,f)
  SD(v1); SD(bridge_version); SD(host_build_id_size); SD(host_build_id); SD(reserved);
#undef SD
  printf("cap.server_dev=%llu\n", (unsigned long long)SEEKDB_PLUGIN_CAPABILITY_SERVER_DEV);
  TYPE(PathRequest,seekdb_plugin_path_request_v1_t);
#define P(f) FIELD(PathRequest,seekdb_plugin_path_request_v1_t,f)
  P(struct_size); P(kind); P(input_index); P(reserved_word); P(reserved);
#undef P
  TYPE(CandidateContextV2,seekdb_plugin_candidate_context_v2_t);
  TYPE(CandidateContextV3,seekdb_plugin_candidate_context_v3_t);
  TYPE(CandidateContextV4,seekdb_plugin_candidate_context_v4_t);
  TYPE(CandidateContextV5,seekdb_plugin_candidate_context_v5_t);
#define S(f) FIELD(CandidateContextV5,seekdb_plugin_candidate_context_v5_t,f)
  S(v4); S(plan_semantics); S(expression_semantics); S(scope); S(column_count); S(column); S(reserved);
#undef S
  TYPE(CandidateContextV6,seekdb_plugin_candidate_context_v6_t);
#define S(f) FIELD(CandidateContextV6,seekdb_plugin_candidate_context_v6_t,f)
  S(v5); S(binding_count); S(binding); S(reserved);
#undef S
  TYPE(CandidateContextV7,seekdb_plugin_candidate_context_v7_t);
#define S(f) FIELD(CandidateContextV7,seekdb_plugin_candidate_context_v7_t,f)
  S(v6); S(sort_info); S(sort_key); S(reserved);
#undef S
  TYPE(CandidateContextV8,seekdb_plugin_candidate_context_v8_t);
#define S(f) FIELD(CandidateContextV8,seekdb_plugin_candidate_context_v8_t,f)
  S(v7); S(value_info); S(reserved);
#undef S
  TYPE(ValueInfo,seekdb_plugin_value_info_v1_t);
#define S(f) FIELD(ValueInfo,seekdb_plugin_value_info_v1_t,f)
  S(struct_size); S(flags); S(reserved);
#undef S
  TYPE(SortInfo,seekdb_plugin_sort_info_v1_t);
#define S(f) FIELD(SortInfo,seekdb_plugin_sort_info_v1_t,f)
  S(struct_size); S(flags); S(key_count); S(prefix_key_count); S(partition_key_count);
  S(topn_expression); S(topk_limit_expression); S(topk_offset_expression); S(hash_expression);
  S(reserved_word); S(reserved);
#undef S
  TYPE(PlanSemantics,seekdb_plugin_plan_semantics_v1_t);
#define S(f) FIELD(PlanSemantics,seekdb_plugin_plan_semantics_v1_t,f)
  S(struct_size); S(relation_kind); S(flags); S(reserved_word); S(reserved);
#undef S
  TYPE(ExprSemantics,seekdb_plugin_expr_semantics_v1_t);
#define S(f) FIELD(ExprSemantics,seekdb_plugin_expr_semantics_v1_t,f)
  S(struct_size); S(comparison_kind); S(value_kind); S(flags); S(reserved);
#undef S
#define Q(f) FIELD(CandidateContextV4,seekdb_plugin_candidate_context_v4_t,f)
  Q(v3); Q(query); Q(target); Q(reserved);
#undef Q
  TYPE(QueryInfo,seekdb_plugin_query_info_v1_t);
#define Q(f) FIELD(QueryInfo,seekdb_plugin_query_info_v1_t,f)
  Q(struct_size); Q(statement_type); Q(flags); Q(target_count); Q(reserved);
#undef Q
#define G(f) FIELD(CandidateContextV3,seekdb_plugin_candidate_context_v3_t,f)
  G(v2); G(root); G(plan); G(child); G(expression); G(describe_expression); G(argument); G(reserved);
#undef G
  TYPE(PlanInfo,seekdb_plugin_plan_info_v1_t);
#define G(f) FIELD(PlanInfo,seekdb_plugin_plan_info_v1_t,f)
  G(struct_size); G(operator_type); G(child_count); G(join_type); G(expression_counts); G(reserved_word); G(cost); G(rows); G(width); G(reserved);
#undef G
  TYPE(ExprInfo,seekdb_plugin_expr_info_v1_t);
#define G(f) FIELD(ExprInfo,seekdb_plugin_expr_info_v1_t,f)
  G(struct_size); G(expression_type); G(sql_type); G(flags); G(argument_count); G(collation); G(precision); G(scale); G(table_id); G(column_id); G(type_id); G(reserved);
#undef G
  TYPE(CustomPathRequest,seekdb_plugin_custom_path_request_v1_t);
  TYPE(CustomPathRequestV2,seekdb_plugin_custom_path_request_v2_t);
  TYPE(CustomPathRequestV3,seekdb_plugin_custom_path_request_v3_t);
#define W(f) FIELD(CustomPathRequestV3,seekdb_plugin_custom_path_request_v3_t,f)
  W(v2); W(input_plans); W(plan_count); W(execution); W(input_offsets); W(reserved);
#undef W
#define V(f) FIELD(CustomPathRequestV2,seekdb_plugin_custom_path_request_v2_t,f)
  V(v1); V(inputs); V(input_count); V(output_count); V(outputs); V(reserved);
#undef V
#define C(f) FIELD(CustomPathRequest,seekdb_plugin_custom_path_request_v1_t,f)
  C(v1); C(service_id); C(service_major); C(minimum_minor); C(plan); C(plan_size); C(flags); C(operator_cost); C(reserved);
#undef C
#define P(f) FIELD(CandidateContextV2,seekdb_plugin_candidate_context_v2_t,f)
  P(v1); P(current_count); P(build); P(get_error); P(reserved);
#undef P
  TYPE(CandidateInfo,seekdb_plugin_candidate_info_v1_t);
#define P(f) FIELD(CandidateInfo,seekdb_plugin_candidate_info_v1_t,f)
  P(struct_size); P(operator_type); P(cost); P(rows); P(width); P(reserved);
#undef P
  TYPE(CandidateContext,seekdb_plugin_candidate_context_v1_t);
#define P(f) FIELD(CandidateContext,seekdb_plugin_candidate_context_v1_t,f)
  P(struct_size); P(candidate_count); P(host_context); P(get); P(select); P(continuation); P(next); P(reserved);
#undef P
  TYPE(CandidateService,seekdb_plugin_candidate_service_v1_t);
#define P(f) FIELD(CandidateService,seekdb_plugin_candidate_service_v1_t,f)
  P(struct_size); P(spi_major); P(spi_minor); P(mode); P(invoke); P(reserved);
#undef P
  TYPE(CatalogBuildContext,seekdb_plugin_catalog_build_context_v1_t);
#define B(f) FIELD(CatalogBuildContext,seekdb_plugin_catalog_build_context_v1_t,f)
  B(struct_size); B(reserved_word); B(tenant_id); B(database_id); B(owner_id);
  B(extension_name); B(extension_version); B(host_context); B(create_routine); B(reserved);
#undef B
  TYPE(CatalogServiceV2,seekdb_plugin_catalog_service_v2_t);
#define B(f) FIELD(CatalogServiceV2,seekdb_plugin_catalog_service_v2_t,f)
  B(v1); B(build); B(reserved);
#undef B
  TYPE(CatalogBuildContextV2,seekdb_plugin_catalog_build_context_v2_t);
#define B(f) FIELD(CatalogBuildContextV2,seekdb_plugin_catalog_build_context_v2_t,f)
  B(v1); B(lookup_routine); B(reserved);
#undef B
  TYPE(CatalogContext,seekdb_plugin_catalog_context_v1_t);
#define C(f) FIELD(CatalogContext,seekdb_plugin_catalog_context_v1_t,f)
  C(struct_size); C(reserved_word); C(tenant_id); C(database_id); C(owner_id);
  C(extension_name); C(extension_version); C(host_context); C(emit_sql); C(reserved);
#undef C
  TYPE(CatalogService,seekdb_plugin_catalog_service_v1_t);
#define C(f) FIELD(CatalogService,seekdb_plugin_catalog_service_v1_t,f)
  C(struct_size); C(spi_major); C(spi_minor); C(reserved_word); C(prepare); C(reserved);
#undef C
  TYPE(OptimizerHookDescriptor,seekdb_plugin_optimizer_hook_descriptor_v1_t);
#define H(f) FIELD(OptimizerHookDescriptor,seekdb_plugin_optimizer_hook_descriptor_v1_t,f)
  H(struct_size); H(object_id); H(hook_point); H(priority); H(reserved_word); H(flags); H(implementation); H(reserved);
#undef H
  TYPE(OptimizerInfo,seekdb_plugin_optimizer_info_v1_t);
#define H(f) FIELD(OptimizerInfo,seekdb_plugin_optimizer_info_v1_t,f)
  H(struct_size); H(statement_kind); H(database_id); H(user_id); H(reserved);
#undef H
  TYPE(OptimizerContext,seekdb_plugin_optimizer_context_v1_t);
#define H(f) FIELD(OptimizerContext,seekdb_plugin_optimizer_context_v1_t,f)
  H(struct_size); H(info); H(continuation); H(next); H(reserved);
#undef H
  TYPE(OptimizerService,seekdb_plugin_optimizer_service_v1_t);
#define H(f) FIELD(OptimizerService,seekdb_plugin_optimizer_service_v1_t,f)
  H(struct_size); H(spi_major); H(spi_minor); H(reserved_word); H(invoke); H(reserved);
#undef H
  TYPE(Version, seekdb_plugin_semantic_version_t);
  FIELD(Version,seekdb_plugin_semantic_version_t,major); FIELD(Version,seekdb_plugin_semantic_version_t,minor); FIELD(Version,seekdb_plugin_semantic_version_t,patch);
  TYPE(VersionRange,seekdb_plugin_version_range_t);
  FIELD(VersionRange,seekdb_plugin_version_range_t,struct_size); FIELD(VersionRange,seekdb_plugin_version_range_t,minimum_inclusive);
  FIELD(VersionRange,seekdb_plugin_version_range_t,maximum_exclusive); FIELD(VersionRange,seekdb_plugin_version_range_t,reserved);
  TYPE(ServiceProvide,seekdb_plugin_service_provide_descriptor_t);
  FIELD(ServiceProvide,seekdb_plugin_service_provide_descriptor_t,struct_size); FIELD(ServiceProvide,seekdb_plugin_service_provide_descriptor_t,service_id);
  FIELD(ServiceProvide,seekdb_plugin_service_provide_descriptor_t,version); FIELD(ServiceProvide,seekdb_plugin_service_provide_descriptor_t,service);
  FIELD(ServiceProvide,seekdb_plugin_service_provide_descriptor_t,capabilities); FIELD(ServiceProvide,seekdb_plugin_service_provide_descriptor_t,reserved);
  TYPE(ServiceRequire,seekdb_plugin_service_require_descriptor_t);
  FIELD(ServiceRequire,seekdb_plugin_service_require_descriptor_t,struct_size); FIELD(ServiceRequire,seekdb_plugin_service_require_descriptor_t,service_id);
  FIELD(ServiceRequire,seekdb_plugin_service_require_descriptor_t,version_range); FIELD(ServiceRequire,seekdb_plugin_service_require_descriptor_t,service_slot);
  FIELD(ServiceRequire,seekdb_plugin_service_require_descriptor_t,optional); FIELD(ServiceRequire,seekdb_plugin_service_require_descriptor_t,reserved_bytes);
  FIELD(ServiceRequire,seekdb_plugin_service_require_descriptor_t,required_capabilities); FIELD(ServiceRequire,seekdb_plugin_service_require_descriptor_t,reserved);
  TYPE(HostApiV1,seekdb_plugin_host_api_v1_t); FIELD_LIST_V1(HostApiV1,seekdb_plugin_host_api_v1_t);
  TYPE(HostApiV2,seekdb_plugin_host_api_v2_t);
  FIELD(HostApiV2,seekdb_plugin_host_api_v2_t,host); FIELD(HostApiV2,seekdb_plugin_host_api_v2_t,registration_spi_major);
  FIELD(HostApiV2,seekdb_plugin_host_api_v2_t,registration_spi_minor); FIELD(HostApiV2,seekdb_plugin_host_api_v2_t,register_extension);
  FIELD(HostApiV2,seekdb_plugin_host_api_v2_t,registration_reserved);
  TYPE(OwnedBytesV1,seekdb_plugin_owned_bytes_v1_t);
  FIELD(OwnedBytesV1,seekdb_plugin_owned_bytes_v1_t,struct_size); FIELD(OwnedBytesV1,seekdb_plugin_owned_bytes_v1_t,alignment);
  FIELD(OwnedBytesV1,seekdb_plugin_owned_bytes_v1_t,size); FIELD(OwnedBytesV1,seekdb_plugin_owned_bytes_v1_t,data);
  FIELD(OwnedBytesV1,seekdb_plugin_owned_bytes_v1_t,owner); FIELD(OwnedBytesV1,seekdb_plugin_owned_bytes_v1_t,release);
  FIELD(OwnedBytesV1,seekdb_plugin_owned_bytes_v1_t,reserved);
  TYPE(HostApiV3,seekdb_plugin_host_api_v3_t);
  FIELD(HostApiV3,seekdb_plugin_host_api_v3_t,v2); FIELD(HostApiV3,seekdb_plugin_host_api_v3_t,memory_spi_major);
  FIELD(HostApiV3,seekdb_plugin_host_api_v3_t,memory_spi_minor); FIELD(HostApiV3,seekdb_plugin_host_api_v3_t,allocate_owned_bytes);
  FIELD(HostApiV3,seekdb_plugin_host_api_v3_t,memory_reserved);
  TYPE(Manifest,seekdb_plugin_manifest_v1_t);
#define M(f) FIELD(Manifest,seekdb_plugin_manifest_v1_t,f)
  M(struct_size); M(abi_major); M(abi_minor); M(plugin_id); M(vendor); M(version); M(build_id);
  M(catalog_version); M(data_format_version); M(capabilities); M(provides); M(provides_count);
  M(required_services); M(required_services_count); M(init); M(start); M(stop); M(deinit); M(reserved);
  TYPE(Implementation,seekdb_plugin_implementation_ref_v1_t);
#define I(f) FIELD(Implementation,seekdb_plugin_implementation_ref_v1_t,f)
  I(struct_size); I(service_id); I(version_range); I(required_capabilities); I(reserved);
  TYPE(TypeDescriptor,seekdb_plugin_type_descriptor_v1_t);
#define TD(f) FIELD(TypeDescriptor,seekdb_plugin_type_descriptor_v1_t,f)
  TD(struct_size); TD(object_id); TD(sql_name); TD(physical_format_id); TD(physical_format_version);
  TD(reserved_word); TD(flags); TD(codec_service); TD(reserved);
  TYPE(CastDescriptor,seekdb_plugin_cast_descriptor_v1_t);
#define CD(f) FIELD(CastDescriptor,seekdb_plugin_cast_descriptor_v1_t,f)
  CD(struct_size); CD(object_id); CD(source_type_id); CD(target_type_id); CD(context);
  CD(cost); CD(flags); CD(implementation); CD(reserved);
  printf("kind.type=%d\nkind.cast=%d\ncast.explicit=%d\ncast.assignment=%d\ncast.implicit=%d\n",
    SEEKDB_PLUGIN_EXTENSION_TYPE, SEEKDB_PLUGIN_EXTENSION_CAST, SEEKDB_PLUGIN_CAST_EXPLICIT,
    SEEKDB_PLUGIN_CAST_ASSIGNMENT, SEEKDB_PLUGIN_CAST_IMPLICIT);
  TYPE(FunctionV1,seekdb_plugin_function_descriptor_v1_t);
#define F(f) FIELD(FunctionV1,seekdb_plugin_function_descriptor_v1_t,f)
  F(struct_size); F(object_id); F(sql_name); F(minimum_arity); F(maximum_arity); F(static_result_type_id); F(flags); F(implementation); F(reserved);
  TYPE(FunctionV2,seekdb_plugin_function_descriptor_v2_t);
#define G(f) FIELD(FunctionV2,seekdb_plugin_function_descriptor_v2_t,f)
  G(descriptor); G(argument_type_ids); G(argument_type_count); G(signature_flags); G(signature_reserved);
  TYPE(Value,seekdb_plugin_execution_value_v1_t);
#define V(f) FIELD(Value,seekdb_plugin_execution_value_v1_t,f)
  V(struct_size); V(type_id); V(data); V(data_size); V(is_null); V(reserved_bytes); V(reserved);
  TYPE(Result,seekdb_plugin_execution_result_v1_t);
#define R(f) FIELD(Result,seekdb_plugin_execution_result_v1_t,f)
  R(struct_size); R(type_id); R(data); R(data_size); R(is_null); R(reserved_bytes); R(reserved);
  TYPE(ContextV1,seekdb_plugin_execution_context_v1_t);
#define C(f) FIELD(ContextV1,seekdb_plugin_execution_context_v1_t,f)
  C(struct_size); C(host); C(emit_result); C(reserved);
  TYPE(ContextV2,seekdb_plugin_execution_context_v2_t);
#define D(f) FIELD(ContextV2,seekdb_plugin_execution_context_v2_t,f)
  D(v1); D(sql_api); D(sql_context); D(reserved);
  TYPE(FunctionService,seekdb_plugin_function_service_v1_t);
#define S(f) FIELD(FunctionService,seekdb_plugin_function_service_v1_t,f)
  S(struct_size); S(spi_major); S(spi_minor); S(reserved_word); S(execute); S(reserved);
  TYPE(ResolvedType,seekdb_plugin_resolved_type_v1_t);
  FIELD(ResolvedType,seekdb_plugin_resolved_type_v1_t,struct_size);
  FIELD(ResolvedType,seekdb_plugin_resolved_type_v1_t,type_id);
  FIELD(ResolvedType,seekdb_plugin_resolved_type_v1_t,reserved);
  TYPE(FunctionServiceV2,seekdb_plugin_function_service_v2_t);
  FIELD(FunctionServiceV2,seekdb_plugin_function_service_v2_t,v1);
  FIELD(FunctionServiceV2,seekdb_plugin_function_service_v2_t,resolve_result);
  FIELD(FunctionServiceV2,seekdb_plugin_function_service_v2_t,resolution_reserved);
  TYPE(BatchRow,seekdb_plugin_batch_row_v1_t);
#define B(f) FIELD(BatchRow,seekdb_plugin_batch_row_v1_t,f)
  B(struct_size); B(argument_count); B(arguments); B(reserved);
#undef B
  TYPE(BatchContext,seekdb_plugin_batch_context_v1_t);
#define B(f) FIELD(BatchContext,seekdb_plugin_batch_context_v1_t,f)
  B(struct_size); B(reserved_word); B(query_context); B(host); B(emit_result); B(reserved);
#undef B
  TYPE(FunctionServiceV3,seekdb_plugin_function_service_v3_t);
  FIELD(FunctionServiceV3,seekdb_plugin_function_service_v3_t,v2);
  FIELD(FunctionServiceV3,seekdb_plugin_function_service_v3_t,execute_batch);
  FIELD(FunctionServiceV3,seekdb_plugin_function_service_v3_t,batch_reserved);
  printf("batch.minor=%u\nbatch.max_rows=%u\nbatch.max_bytes=%zu\n",
      SEEKDB_PLUGIN_EXECUTION_BATCH_MINOR, SEEKDB_PLUGIN_MAX_BATCH_ROWS,
      (size_t)SEEKDB_PLUGIN_MAX_BATCH_BYTES);
  printf("resolution.minor=%u\nresolution.max_identifier=%u\n",
    SEEKDB_PLUGIN_EXECUTION_RESULT_TYPE_MINOR, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES);
  TYPE(TypeCodecService,seekdb_plugin_type_codec_service_v1_t);
#define TC(f) FIELD(TypeCodecService,seekdb_plugin_type_codec_service_v1_t,f)
  TC(struct_size); TC(spi_major); TC(spi_minor); TC(reserved_word); TC(decode); TC(encode); TC(reserved);
  TYPE(TypeCodecServiceV2,seekdb_plugin_type_codec_service_v2_t);
  FIELD(TypeCodecServiceV2,seekdb_plugin_type_codec_service_v2_t,v1);
  FIELD(TypeCodecServiceV2,seekdb_plugin_type_codec_service_v2_t,compare);
  FIELD(TypeCodecServiceV2,seekdb_plugin_type_codec_service_v2_t,comparison_reserved);
  TYPE(TypeComparison,seekdb_plugin_type_comparison_v1_t);
  FIELD(TypeComparison,seekdb_plugin_type_comparison_v1_t,struct_size);
  FIELD(TypeComparison,seekdb_plugin_type_comparison_v1_t,ordering);
  FIELD(TypeComparison,seekdb_plugin_type_comparison_v1_t,reserved);
  TYPE(SqlValue,seekdb_plugin_sql_value_v1_t);
#define Q(f) FIELD(SqlValue,seekdb_plugin_sql_value_v1_t,f)
  Q(struct_size); Q(kind); Q(data); Q(data_size); Q(reserved);
  TYPE(SqlResult,seekdb_plugin_sql_result_v1_t);
#define T(f) FIELD(SqlResult,seekdb_plugin_sql_result_v1_t,f)
  T(struct_size); T(reserved_word); T(database_error); T(affected_rows); T(returned_rows); T(reserved);
  TYPE(SqlApi,seekdb_plugin_sql_api_v1_t);
#define A(f) FIELD(SqlApi,seekdb_plugin_sql_api_v1_t,f)
  A(struct_size); A(spi_major); A(spi_minor); A(reserved_word); A(execute); A(reserved);
  TYPE(SqlApiV2,seekdb_plugin_sql_api_v2_t);
  TYPE(SqlApiV3,seekdb_plugin_sql_api_v3_t);
  FIELD(SqlApiV3,seekdb_plugin_sql_api_v3_t,v2);
  FIELD(SqlApiV3,seekdb_plugin_sql_api_v3_t,lookup_routine);
  FIELD(SqlApiV3,seekdb_plugin_sql_api_v3_t,reserved);
  TYPE(SqlApiV4,seekdb_plugin_sql_api_v4_t);
  FIELD(SqlApiV4,seekdb_plugin_sql_api_v4_t,v3);
  FIELD(SqlApiV4,seekdb_plugin_sql_api_v4_t,mutate_routine);
  FIELD(SqlApiV4,seekdb_plugin_sql_api_v4_t,reserved);
  TYPE(RoutineMutationResult,seekdb_plugin_routine_mutation_result_v1_t);
#define M(f) FIELD(RoutineMutationResult,seekdb_plugin_routine_mutation_result_v1_t,f)
  M(struct_size); M(outcome); M(database_error); M(object_id); M(close_error); M(identity_error);
  M(data_rollback_error); M(view_rollback_error); M(poison_error); M(reserved);
  TYPE(RoutineLookupResult,seekdb_plugin_routine_lookup_result_v1_t);
  FIELD(RoutineLookupResult,seekdb_plugin_routine_lookup_result_v1_t,struct_size);
  FIELD(RoutineLookupResult,seekdb_plugin_routine_lookup_result_v1_t,reserved_word);
  FIELD(RoutineLookupResult,seekdb_plugin_routine_lookup_result_v1_t,database_error);
  FIELD(RoutineLookupResult,seekdb_plugin_routine_lookup_result_v1_t,object_id);
  FIELD(RoutineLookupResult,seekdb_plugin_routine_lookup_result_v1_t,reserved);
  TYPE(TableContextV3,seekdb_plugin_table_execution_context_v3_t);
  FIELD(TableContextV3,seekdb_plugin_table_execution_context_v3_t,v2);
  FIELD(TableContextV3,seekdb_plugin_table_execution_context_v3_t,sql_api);
  FIELD(TableContextV3,seekdb_plugin_table_execution_context_v3_t,reserved);
  TYPE(TableContextV4,seekdb_plugin_table_execution_context_v4_t);
  FIELD(TableContextV4,seekdb_plugin_table_execution_context_v4_t,v3);
  FIELD(TableContextV4,seekdb_plugin_table_execution_context_v4_t,column_count);
  FIELD(TableContextV4,seekdb_plugin_table_execution_context_v4_t,reserved_word);
  FIELD(TableContextV4,seekdb_plugin_table_execution_context_v4_t,requested_columns);
  FIELD(TableContextV4,seekdb_plugin_table_execution_context_v4_t,reserved);
  TYPE(TableContextV2,seekdb_plugin_table_execution_context_v2_t);
  FIELD(TableContextV2,seekdb_plugin_table_execution_context_v2_t,v1);
  FIELD(TableContextV2,seekdb_plugin_table_execution_context_v2_t,query_context);
  FIELD(TableContextV2,seekdb_plugin_table_execution_context_v2_t,poll_query);
  FIELD(TableContextV2,seekdb_plugin_table_execution_context_v2_t,reserved);
  FIELD(SqlApiV2,seekdb_plugin_sql_api_v2_t,v1);
  FIELD(SqlApiV2,seekdb_plugin_sql_api_v2_t,poll_query);
  FIELD(SqlApiV2,seekdb_plugin_sql_api_v2_t,reserved);
  TYPE(QueryStatus,seekdb_plugin_query_status_v1_t);
  FIELD(QueryStatus,seekdb_plugin_query_status_v1_t,struct_size);
  FIELD(QueryStatus,seekdb_plugin_query_status_v1_t,reserved_word);
  FIELD(QueryStatus,seekdb_plugin_query_status_v1_t,database_error);
  FIELD(QueryStatus,seekdb_plugin_query_status_v1_t,remaining_us);
  FIELD(QueryStatus,seekdb_plugin_query_status_v1_t,reserved);
  TYPE(TableColumn,seekdb_plugin_table_column_descriptor_v1_t);
#define COL(f) FIELD(TableColumn,seekdb_plugin_table_column_descriptor_v1_t,f)
  COL(struct_size); COL(sql_name); COL(type_id); COL(nullable); COL(reserved_bytes); COL(reserved);
  TYPE(TableFunction,seekdb_plugin_table_function_descriptor_v1_t);
#define TF(f) FIELD(TableFunction,seekdb_plugin_table_function_descriptor_v1_t,f)
  TF(struct_size); TF(object_id); TF(sql_name); TF(minimum_arity); TF(maximum_arity);
  TF(argument_type_ids); TF(argument_type_count); TF(signature_flags); TF(columns); TF(column_count);
  TF(reserved_word); TF(flags); TF(implementation); TF(reserved);
  TYPE(TableRow,seekdb_plugin_table_row_v1_t);
#define ROW(f) FIELD(TableRow,seekdb_plugin_table_row_v1_t,f)
  ROW(struct_size); ROW(columns); ROW(column_count); ROW(reserved_word); ROW(reserved);
  TYPE(TableContext,seekdb_plugin_table_execution_context_v1_t);
#define CT(f) FIELD(TableContext,seekdb_plugin_table_execution_context_v1_t,f)
  CT(struct_size); CT(host); CT(emit_row); CT(reserved);
  TYPE(TableFunctionService,seekdb_plugin_table_function_service_v1_t);
#define TS(f) FIELD(TableFunctionService,seekdb_plugin_table_function_service_v1_t,f)
  TS(struct_size); TS(spi_major); TS(spi_minor); TS(reserved_word); TS(open); TS(next); TS(rescan); TS(close); TS(reserved);
  TYPE(TablePlanningInfo,seekdb_plugin_table_planning_info_v1_t);
#define TPI(f) FIELD(TablePlanningInfo,seekdb_plugin_table_planning_info_v1_t,f)
  TPI(struct_size); TPI(argument_count); TPI(object_id); TPI(argument_type_ids); TPI(column_count); TPI(reserved_word); TPI(reserved);
  TYPE(TableEstimate,seekdb_plugin_table_estimate_v1_t);
#define TE(f) FIELD(TableEstimate,seekdb_plugin_table_estimate_v1_t,f)
  TE(struct_size); TE(reserved_word); TE(rows); TE(row_width); TE(total_cost); TE(reserved);
  TYPE(TableFunctionServiceV2,seekdb_plugin_table_function_service_v2_t);
#define TSV2(f) FIELD(TableFunctionServiceV2,seekdb_plugin_table_function_service_v2_t,f)
  TSV2(v1); TSV2(estimate); TSV2(reserved);
  printf("kind.table=%d\nstatus.end=%d\n", SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION, SEEKDB_PLUGIN_STATUS_END_OF_STREAM);
  return 0;
}
