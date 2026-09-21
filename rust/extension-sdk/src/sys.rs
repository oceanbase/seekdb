// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Low-level subset of the public plugin headers. No host-private Rust bridge.
//! Layouts are checked against the C headers by tests/abi_layout.rs. All raw
//! pointer callbacks obey the ownership and lifetime rules in those headers.

use std::ffi::{c_char, c_void};

#[repr(C)]
pub struct OptimizerHookDescriptor {
    pub struct_size: u32,
    pub object_id: *const c_char,
    pub hook_point: *const c_char,
    pub priority: i32,
    pub reserved_word: u32,
    pub flags: u64,
    pub implementation: Implementation,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct OptimizerInfo {
    pub struct_size: u32,
    pub statement_kind: u32,
    pub database_id: u64,
    pub user_id: u64,
    pub reserved: [u64; 4],
}
pub type OptimizerNext = unsafe extern "C" fn(*mut c_void, *mut i32) -> Status;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct CandidateInfo {
    pub struct_size: u32,
    pub operator_type: u32,
    pub cost: f64,
    pub rows: f64,
    pub width: f64,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateContext {
    pub struct_size: u32,
    pub candidate_count: u32,
    pub host_context: *mut c_void,
    pub get: Option<unsafe extern "C" fn(*mut c_void, u32, *mut CandidateInfo) -> Status>,
    pub select: Option<unsafe extern "C" fn(*mut c_void, u32) -> Status>,
    pub continuation: *mut c_void,
    pub next: Option<OptimizerNext>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateService {
    pub struct_size: u32,
    pub spi_major: u32,
    pub spi_minor: u32,
    pub mode: u32,
    pub invoke: Option<unsafe extern "C" fn(*mut Handle, *const CandidateContext) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct PathRequest {
    pub struct_size: u32,
    pub kind: u32,
    pub input_index: u32,
    pub reserved_word: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomPathRequest {
    pub v1: PathRequest,
    pub service_id: *const c_char,
    pub service_major: u32,
    pub minimum_minor: u32,
    pub plan: *const u8,
    pub plan_size: u32,
    pub flags: u32,
    pub operator_cost: f64,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomPathRequestV2 {
    pub v1: CustomPathRequest,
    pub inputs: *const u32,
    pub input_count: u32,
    pub output_count: u32,
    pub outputs: *const u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomPathRequestV3 {
    pub v2: CustomPathRequestV2,
    pub input_plans: *const u32,
    pub plan_count: u32,
    pub execution: u32,
    pub input_offsets: *const u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct InputBinding {
    pub parameter: u32,
    pub source_input: u32,
    pub source_column: u32,
    pub target_input: u32,
}
#[repr(C)]
pub struct CustomPathRequestV4 {
    pub v3: CustomPathRequestV3,
    pub bindings: *const InputBinding,
    pub binding_count: u32,
    pub reserved_word: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateContextV2 {
    pub v1: CandidateContext,
    pub current_count: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    pub build: Option<unsafe extern "C" fn(*mut c_void, *const PathRequest, *mut u32) -> Status>,
    pub get_error: Option<unsafe extern "C" fn(*mut c_void) -> i32>,
    pub reserved: [u64; 4],
}
#[repr(C)]
#[derive(Default)]
pub struct PlanInfo {
    pub struct_size: u32,
    pub operator_type: u32,
    pub child_count: u32,
    pub join_type: u32,
    pub expression_counts: [u32; 5],
    pub reserved_word: u32,
    pub cost: f64,
    pub rows: f64,
    pub width: f64,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct ExprInfo {
    pub struct_size: u32,
    pub expression_type: u32,
    pub sql_type: u32,
    pub flags: u32,
    pub argument_count: u32,
    pub collation: i32,
    pub precision: i32,
    pub scale: i32,
    pub table_id: u64,
    pub column_id: u64,
    pub type_id: [c_char; 256],
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateContextV3 {
    pub v2: CandidateContextV2,
    pub root: Option<unsafe extern "C" fn(*mut c_void, u32, *mut u32) -> Status>,
    pub plan: Option<unsafe extern "C" fn(*mut c_void, u32, *mut PlanInfo) -> Status>,
    pub child: Option<unsafe extern "C" fn(*mut c_void, u32, u32, *mut u32) -> Status>,
    pub expression:
        Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, *mut u32, *mut u32) -> Status>,
    pub describe_expression:
        Option<unsafe extern "C" fn(*mut c_void, u32, *mut ExprInfo) -> Status>,
    pub argument: Option<unsafe extern "C" fn(*mut c_void, u32, u32, *mut u32) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
#[derive(Default)]
pub struct QueryInfo {
    pub struct_size: u32,
    pub statement_type: u32,
    pub flags: u32,
    pub target_count: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateContextV4 {
    pub v3: CandidateContextV3,
    pub query: Option<unsafe extern "C" fn(*mut c_void, *mut QueryInfo) -> Status>,
    pub target: Option<unsafe extern "C" fn(*mut c_void, u32, *mut u32) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
#[derive(Default)]
pub struct PlanSemantics {
    pub struct_size: u32,
    pub relation_kind: u32,
    pub flags: u32,
    pub reserved_word: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
#[derive(Default)]
pub struct ExprSemantics {
    pub struct_size: u32,
    pub comparison_kind: u32,
    pub value_kind: u32,
    pub flags: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateContextV5 {
    pub v4: CandidateContextV4,
    pub plan_semantics:
        Option<unsafe extern "C" fn(*mut c_void, u32, *mut PlanSemantics) -> Status>,
    pub expression_semantics:
        Option<unsafe extern "C" fn(*mut c_void, u32, *mut ExprSemantics) -> Status>,
    pub scope: Option<unsafe extern "C" fn(*mut c_void, u32, u32, *mut u32) -> Status>,
    pub column_count: Option<unsafe extern "C" fn(*mut c_void, *mut u32) -> Status>,
    pub column: Option<unsafe extern "C" fn(*mut c_void, u32, *mut u32) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateContextV6 {
    pub v5: CandidateContextV5,
    pub binding_count: Option<unsafe extern "C" fn(*mut c_void, u32, u32, *mut u32) -> Status>,
    pub binding:
        Option<unsafe extern "C" fn(*mut c_void, u32, u32, u32, *mut u32, *mut u32) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
#[derive(Default)]
pub struct SortInfo {
    pub struct_size: u32,
    pub flags: u32,
    pub key_count: u32,
    pub prefix_key_count: u32,
    pub partition_key_count: u32,
    pub topn_expression: u32,
    pub topk_limit_expression: u32,
    pub topk_offset_expression: u32,
    pub hash_expression: u32,
    pub reserved_word: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateContextV7 {
    pub v6: CandidateContextV6,
    pub sort_info: Option<unsafe extern "C" fn(*mut c_void, u32, *mut SortInfo) -> Status>,
    pub sort_key: Option<unsafe extern "C" fn(*mut c_void, u32, u32, *mut u32, *mut u32) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
#[derive(Default)]
pub struct ValueInfo {
    pub struct_size: u32,
    pub flags: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CandidateContextV8 {
    pub v7: CandidateContextV7,
    pub value_info: Option<unsafe extern "C" fn(*mut c_void, u32, u32, *mut ValueInfo) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomRow {
    pub struct_size: u32,
    pub column_count: u32,
    pub values: *const Value,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomContext {
    pub struct_size: u32,
    pub input_count: u32,
    pub output_column_count: u32,
    pub reserved_word: u32,
    pub host_context: *mut c_void,
    pub next_input:
        Option<unsafe extern "C" fn(*mut c_void, u32, *mut CustomRow, *mut i32) -> Status>,
    pub emit: Option<unsafe extern "C" fn(*mut c_void, *const Value, u32, *mut i32) -> Status>,
    pub check_interrupt: Option<unsafe extern "C" fn(*mut c_void, *mut i32) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomExecutor {
    pub struct_size: u32,
    pub spi_major: u32,
    pub spi_minor: u32,
    pub reserved_word: u32,
    pub open: Option<unsafe extern "C" fn(*mut Handle, *const u8, u32, *mut *mut c_void) -> Status>,
    pub next:
        Option<unsafe extern "C" fn(*mut Handle, *mut c_void, *const CustomContext) -> Status>,
    pub rescan: Option<unsafe extern "C" fn(*mut Handle, *mut c_void) -> Status>,
    pub close: Option<unsafe extern "C" fn(*mut Handle, *mut c_void) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct CustomColumn {
    pub struct_size: u32,
    pub flags: u32,
    pub encoding: u32,
    pub sql_type: u32,
    pub collation: i32,
    pub precision: i32,
    pub scale: i32,
    pub reserved_word: u32,
    pub type_id: [c_char; 256],
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomSchema {
    pub struct_size: u32,
    pub column_count: u32,
    pub columns: *const CustomColumn,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomContextV2 {
    pub v1: CustomContext,
    pub inputs: *const CustomSchema,
    pub output: *const CustomSchema,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomContextV3 {
    pub v2: CustomContextV2,
    pub rescan_input: Option<unsafe extern "C" fn(*mut c_void, u32, *mut i32) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CustomContextV4 {
    pub v3: CustomContextV3,
    pub bind_rescan_input: Option<unsafe extern "C" fn(*mut c_void, u32, *mut i32) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct OptimizerContext {
    pub struct_size: u32,
    pub info: *const OptimizerInfo,
    pub continuation: *mut c_void,
    pub next: Option<OptimizerNext>,
    pub reserved: [u64; 4],
}

#[repr(C)]
pub struct CatalogContext {
    pub struct_size: u32,
    pub reserved_word: u32,
    pub tenant_id: u64,
    pub database_id: u64,
    pub owner_id: u64,
    pub extension_name: *const c_char,
    pub extension_version: *const c_char,
    pub host_context: *mut c_void,
    pub emit_sql: Option<unsafe extern "C" fn(*mut c_void, *const c_char, u64) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CatalogService {
    pub struct_size: u32,
    pub spi_major: u32,
    pub spi_minor: u32,
    pub reserved_word: u32,
    pub prepare: Option<unsafe extern "C" fn(*mut Handle, *const CatalogContext) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CatalogBuildContext {
    pub struct_size: u32,
    pub reserved_word: u32,
    pub tenant_id: u64,
    pub database_id: u64,
    pub owner_id: u64,
    pub extension_name: *const c_char,
    pub extension_version: *const c_char,
    pub host_context: *mut c_void,
    pub create_routine:
        Option<unsafe extern "C" fn(*mut c_void, *const c_char, u64, *mut u64) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CatalogBuildContextV2 {
    pub v1: CatalogBuildContext,
    pub lookup_routine:
        Option<unsafe extern "C" fn(*mut c_void, u32, *const c_char, u64, *mut u64) -> Status>,
    pub reserved: [u64; 4],
}
pub const CATALOG_ROUTINE_FUNCTION: u32 = 1;
pub const CATALOG_ROUTINE_PROCEDURE: u32 = 2;
pub const CATALOG_MAX_ROUTINE_NAME_BYTES: usize = 2048;
#[repr(C)]
pub struct CatalogServiceV2 {
    pub v1: CatalogService,
    pub build: Option<unsafe extern "C" fn(*mut Handle, *const CatalogBuildContext) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct OptimizerService {
    pub struct_size: u32,
    pub spi_major: u32,
    pub spi_minor: u32,
    pub reserved_word: u32,
    pub invoke: Option<unsafe extern "C" fn(*mut Handle, *const OptimizerContext) -> Status>,
    pub reserved: [u64; 4],
}

pub type Status = i32;
pub type Handle = c_void;
pub const OK: Status = 0;
pub const INVALID: Status = 1;
pub const UNSUPPORTED_ABI: Status = 2;
pub const NO_MEMORY: Status = 5;
pub const FAILED_PRECONDITION: Status = 6;
pub const UNAVAILABLE: Status = 8;
pub const INTERNAL: Status = 9;
pub const TIMEOUT: Status = 13;
pub const THREAD_SAFE: u64 = 1;
pub const PERSISTENT_DATA: u64 = 1 << 3;
/// Manifest only; never a service requirement or implementation capability.
pub const SERVER_DEV: u64 = 1 << 6;
pub const FUNCTION: i32 = 2;
pub const TYPE: i32 = 1;
pub const CAST: i32 = 3;
pub const TABLE_FUNCTION: i32 = 8;
pub const END_OF_STREAM: Status = 16;
pub const DETERMINISTIC: u64 = 1;
pub const IMMUTABLE: u64 = 2;
pub const NULL_PROPAGATING: u64 = 4;
pub const PERSISTENT: u64 = 1 << 3;
pub const REQUIRES_CATALOG: u64 = 1 << 5;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Version {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
}
#[repr(C)]
pub struct VersionRange {
    pub struct_size: u32,
    pub minimum_inclusive: Version,
    pub maximum_exclusive: Version,
    pub reserved: [u64; 2],
}
#[repr(C)]
pub struct ServiceProvide {
    pub struct_size: u32,
    pub service_id: *const c_char,
    pub version: Version,
    pub service: *const c_void,
    pub capabilities: u64,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct ServiceRequire {
    pub struct_size: u32,
    pub service_id: *const c_char,
    pub version_range: VersionRange,
    pub service_slot: *mut *const c_void,
    pub optional: u8,
    pub reserved_bytes: [u8; 7],
    pub required_capabilities: u64,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct HostApiV1 {
    pub struct_size: u32,
    pub abi_major: u32,
    pub abi_minor: u32,
    pub host_handle: *mut Handle,
    pub alloc: Option<unsafe extern "C" fn(*mut Handle, u64, u32) -> *mut c_void>,
    pub free: Option<unsafe extern "C" fn(*mut Handle, *mut c_void, u64, u32)>,
    pub log: Option<unsafe extern "C" fn(*mut Handle, i32, *const c_char, *const c_char)>,
    pub acquire_service: Option<
        unsafe extern "C" fn(
            *mut Handle,
            *const c_char,
            *const VersionRange,
            u64,
            *mut *const c_void,
            *mut Version,
            *mut *mut Handle,
        ) -> Status,
    >,
    pub release_service: Option<unsafe extern "C" fn(*mut Handle, *mut Handle)>,
    pub begin_registration: Option<unsafe extern "C" fn(*mut Handle, *mut *mut Handle) -> Status>,
    pub register_service:
        Option<unsafe extern "C" fn(*mut Handle, *mut Handle, *const ServiceProvide) -> Status>,
    pub commit_registration: Option<unsafe extern "C" fn(*mut Handle, *mut Handle) -> Status>,
    pub abort_registration: Option<unsafe extern "C" fn(*mut Handle, *mut Handle)>,
    pub reserved: [u64; 8],
}
#[repr(C)]
pub struct HostApiV2 {
    pub host: HostApiV1,
    pub registration_spi_major: u32,
    pub registration_spi_minor: u32,
    pub register_extension:
        Option<unsafe extern "C" fn(*mut Handle, *mut Handle, i32, *const c_void, u32) -> Status>,
    pub registration_reserved: [u64; 4],
}
#[repr(C)]
#[derive(Default)]
pub struct OwnedBytesV1 {
    pub struct_size: u32,
    pub alignment: u32,
    pub size: u64,
    pub data: *mut u8,
    pub owner: *mut Handle,
    pub release: Option<unsafe extern "C" fn(*mut Handle)>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct HostApiV3 {
    pub v2: HostApiV2,
    pub memory_spi_major: u32,
    pub memory_spi_minor: u32,
    pub allocate_owned_bytes:
        Option<unsafe extern "C" fn(*mut Handle, u64, u32, *mut OwnedBytesV1) -> Status>,
    pub memory_reserved: [u64; 4],
}
#[repr(C)]
pub struct Manifest {
    pub struct_size: u32,
    pub abi_major: u32,
    pub abi_minor: u32,
    pub plugin_id: *const c_char,
    pub vendor: *const c_char,
    pub version: Version,
    pub build_id: *const c_char,
    pub catalog_version: u32,
    pub data_format_version: u32,
    pub capabilities: u64,
    pub provides: *const ServiceProvide,
    pub provides_count: u32,
    pub required_services: *const ServiceRequire,
    pub required_services_count: u32,
    pub init: Option<unsafe extern "C" fn(*const HostApiV1, *mut *mut Handle) -> Status>,
    pub start: Option<unsafe extern "C" fn(*mut Handle) -> Status>,
    pub stop: Option<unsafe extern "C" fn(*mut Handle) -> Status>,
    pub deinit: Option<unsafe extern "C" fn(*mut Handle)>,
    pub reserved: [u64; 8],
}
#[repr(C)]
pub struct ServerDevManifest {
    pub v1: Manifest,
    pub bridge_version: u32,
    pub host_build_id_size: u32,
    pub host_build_id: [u8; 64],
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct Implementation {
    pub struct_size: u32,
    pub service_id: *const c_char,
    pub version_range: VersionRange,
    pub required_capabilities: u64,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TypeDescriptor {
    pub struct_size: u32,
    pub object_id: *const c_char,
    pub sql_name: *const c_char,
    pub physical_format_id: *const c_char,
    pub physical_format_version: u32,
    pub reserved_word: u32,
    pub flags: u64,
    pub codec_service: Implementation,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct CastDescriptor {
    pub struct_size: u32,
    pub object_id: *const c_char,
    pub source_type_id: *const c_char,
    pub target_type_id: *const c_char,
    pub context: i32,
    pub cost: u32,
    pub flags: u64,
    pub implementation: Implementation,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct FunctionV1 {
    pub struct_size: u32,
    pub object_id: *const c_char,
    pub sql_name: *const c_char,
    pub minimum_arity: u32,
    pub maximum_arity: u32,
    pub static_result_type_id: *const c_char,
    pub flags: u64,
    pub implementation: Implementation,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct FunctionV2 {
    pub descriptor: FunctionV1,
    pub argument_type_ids: *const *const c_char,
    pub argument_type_count: u32,
    pub signature_flags: u32,
    pub signature_reserved: [u64; 4],
}
#[repr(C)]
pub struct Value {
    pub struct_size: u32,
    pub type_id: *const c_char,
    pub data: *const u8,
    pub data_size: u64,
    pub is_null: u8,
    pub reserved_bytes: [u8; 7],
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableColumn {
    pub struct_size: u32,
    pub sql_name: *const c_char,
    pub type_id: *const c_char,
    pub nullable: u8,
    pub reserved_bytes: [u8; 7],
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableFunction {
    pub struct_size: u32,
    pub object_id: *const c_char,
    pub sql_name: *const c_char,
    pub minimum_arity: u32,
    pub maximum_arity: u32,
    pub argument_type_ids: *const *const c_char,
    pub argument_type_count: u32,
    pub signature_flags: u32,
    pub columns: *const TableColumn,
    pub column_count: u32,
    pub reserved_word: u32,
    pub flags: u64,
    pub implementation: Implementation,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableRow {
    pub struct_size: u32,
    pub columns: *const Value,
    pub column_count: u32,
    pub reserved_word: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableContext {
    pub struct_size: u32,
    pub host: *mut Handle,
    pub emit_row: Option<unsafe extern "C" fn(*mut Handle, *const TableRow) -> Status>,
    pub reserved: [u64; 6],
}
#[repr(C)]
pub struct TableContextV2 {
    pub v1: TableContext,
    pub query_context: *mut Handle,
    pub poll_query: Option<unsafe extern "C" fn(*mut Handle, *mut QueryStatus) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableContextV3 {
    pub v2: TableContextV2,
    pub sql_api: *const SqlApi,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableContextV4 {
    pub v3: TableContextV3,
    pub column_count: u32,
    pub reserved_word: u32,
    pub requested_columns: *const u8,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TablePlanningInfo {
    pub struct_size: u32,
    pub argument_count: u32,
    pub object_id: *const c_char,
    pub argument_type_ids: *const *const c_char,
    pub column_count: u32,
    pub reserved_word: u32,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableEstimate {
    pub struct_size: u32,
    pub reserved_word: u32,
    pub rows: f64,
    pub row_width: f64,
    pub total_cost: f64,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableFunctionServiceV2 {
    pub v1: TableFunctionService,
    pub estimate: Option<
        unsafe extern "C" fn(*mut Handle, *const TablePlanningInfo, *mut TableEstimate) -> Status,
    >,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct TableFunctionService {
    pub struct_size: u32,
    pub spi_major: u32,
    pub spi_minor: u32,
    pub reserved_word: u32,
    pub open: Option<
        unsafe extern "C" fn(
            *mut Handle,
            *const TableContext,
            *const Value,
            u32,
            *mut *mut Handle,
        ) -> Status,
    >,
    pub next: Option<
        unsafe extern "C" fn(
            *mut Handle,
            *mut Handle,
            *const TableContext,
            u32,
            *mut u32,
        ) -> Status,
    >,
    pub rescan: Option<unsafe extern "C" fn(*mut Handle, *mut Handle, *const Value, u32) -> Status>,
    pub close: Option<unsafe extern "C" fn(*mut Handle, *mut Handle) -> Status>,
    pub reserved: [u64; 8],
}
pub type Emit = unsafe extern "C" fn(*mut Handle, *const Value) -> Status;
#[repr(C)]
pub struct ContextV1 {
    pub struct_size: u32,
    pub host: *mut Handle,
    pub emit_result: Option<Emit>,
    pub reserved: [u64; 6],
}
pub type Execute = unsafe extern "C" fn(*mut Handle, *const ContextV1, *const Value, u32) -> Status;
#[repr(C)]
pub struct FunctionService {
    pub struct_size: u32,
    pub spi_major: u32,
    pub spi_minor: u32,
    pub reserved_word: u32,
    pub execute: Option<Execute>,
    pub reserved: [u64; 8],
}
pub const RESULT_TYPE_MINOR: u32 = 2;
pub const MAX_IDENTIFIER_BYTES: usize = 255;
#[repr(C)]
pub struct ResolvedType {
    pub struct_size: u32,
    pub type_id: [c_char; MAX_IDENTIFIER_BYTES + 1],
    pub reserved: [u64; 4],
}
pub type ResolveResult =
    unsafe extern "C" fn(*mut Handle, *const *const c_char, u32, *mut ResolvedType) -> Status;
#[repr(C)]
pub struct FunctionServiceV2 {
    pub v1: FunctionService,
    pub resolve_result: Option<ResolveResult>,
    pub resolution_reserved: [u64; 4],
}
pub const BATCH_MINOR: u32 = 3;
pub const MAX_BATCH_ROWS: u32 = 1024;
pub const MAX_BATCH_BYTES: u64 = 64 * 1024 * 1024;
#[repr(C)]
pub struct BatchRow {
    pub struct_size: u32,
    pub argument_count: u32,
    pub arguments: *const Value,
    pub reserved: [u64; 4],
}
pub type EmitBatchResult = unsafe extern "C" fn(*mut Handle, u32, *const Value) -> Status;
#[repr(C)]
pub struct BatchContext {
    pub struct_size: u32,
    pub reserved_word: u32,
    pub query_context: *const ContextV1,
    pub host: *mut Handle,
    pub emit_result: Option<EmitBatchResult>,
    pub reserved: [u64; 4],
}
pub type ExecuteBatch =
    unsafe extern "C" fn(*mut Handle, *const BatchContext, *const BatchRow, u32) -> Status;
#[repr(C)]
pub struct FunctionServiceV3 {
    pub v2: FunctionServiceV2,
    pub execute_batch: Option<ExecuteBatch>,
    pub batch_reserved: [u64; 4],
}
#[repr(C)]
pub struct TypeCodecService {
    pub struct_size: u32,
    pub spi_major: u32,
    pub spi_minor: u32,
    pub reserved_word: u32,
    pub decode:
        Option<unsafe extern "C" fn(*mut Handle, *const ContextV1, *const u8, u64) -> Status>,
    pub encode: Option<unsafe extern "C" fn(*mut Handle, *const ContextV1, *const Value) -> Status>,
    pub reserved: [u64; 8],
}
#[repr(C)]
pub struct TypeComparison {
    pub struct_size: u32,
    pub ordering: i32,
    pub reserved: [u64; 4],
}
pub type TypeCompare =
    unsafe extern "C" fn(*mut Handle, *const Value, *const Value, *mut TypeComparison) -> Status;
#[repr(C)]
pub struct TypeCodecServiceV2 {
    pub v1: TypeCodecService,
    pub compare: Option<TypeCompare>,
    pub comparison_reserved: [u64; 4],
}
#[repr(C)]
pub struct SqlValue {
    pub struct_size: u32,
    pub kind: u32,
    pub data: *const c_void,
    pub data_size: u64,
    pub reserved: [u64; 2],
}
#[repr(C)]
pub struct SqlResult {
    pub struct_size: u32,
    pub reserved_word: u32,
    pub database_error: i64,
    pub affected_rows: i64,
    pub returned_rows: u64,
    pub reserved: [u64; 2],
}
pub type ConsumeRow = unsafe extern "C" fn(*mut c_void, *const SqlValue, u32) -> Status;
#[repr(C)]
pub struct SqlApi {
    pub struct_size: u32,
    pub spi_major: u32,
    pub spi_minor: u32,
    pub reserved_word: u32,
    pub execute: Option<
        unsafe extern "C" fn(
            *mut Handle,
            *const c_char,
            u64,
            *const SqlValue,
            u32,
            u64,
            Option<ConsumeRow>,
            *mut c_void,
            *mut SqlResult,
        ) -> Status,
    >,
    pub reserved: [u64; 6],
}
#[repr(C)]
pub struct QueryStatus {
    pub struct_size: u32,
    pub reserved_word: u32,
    pub database_error: i64,
    pub remaining_us: i64,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct SqlApiV2 {
    pub v1: SqlApi,
    pub poll_query: Option<unsafe extern "C" fn(*mut Handle, *mut QueryStatus) -> Status>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct RoutineLookupResult {
    pub struct_size: u32,
    pub reserved_word: u32,
    pub database_error: i64,
    pub object_id: u64,
    pub reserved: [u64; 4],
}
pub type LookupRoutine =
    unsafe extern "C" fn(*mut Handle, u32, *const c_char, u64, *mut RoutineLookupResult) -> Status;
#[repr(C)]
pub struct SqlApiV3 {
    pub v2: SqlApiV2,
    pub lookup_routine: Option<LookupRoutine>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct RoutineMutationResult {
    pub struct_size: u32,
    pub outcome: u32,
    pub database_error: i64,
    pub object_id: u64,
    pub close_error: i64,
    pub identity_error: i64,
    pub data_rollback_error: i64,
    pub view_rollback_error: i64,
    pub poison_error: i64,
    pub reserved: [u64; 4],
}
pub type MutateRoutine =
    unsafe extern "C" fn(*mut Handle, *const c_char, u64, *mut RoutineMutationResult) -> Status;
#[repr(C)]
pub struct SqlApiV4 {
    pub v3: SqlApiV3,
    pub mutate_routine: Option<MutateRoutine>,
    pub reserved: [u64; 4],
}
#[repr(C)]
pub struct ContextV2 {
    pub v1: ContextV1,
    pub sql_api: *const SqlApi,
    pub sql_context: *mut Handle,
    pub reserved: [u64; 4],
}
