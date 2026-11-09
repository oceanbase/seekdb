// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use crate::mutation::{Delta, DeltaError, Operation, PhysicalDiff};
use crate::{BinaryResult, DuplicatePolicy, ParseOptions, Path, Value};

#[cxx::bridge(namespace = "oceanbase::common::json")]
pub mod ffi {
    #[repr(u32)]
    enum Duplicate {
        Overwrite = 0,
        Preserve = 1,
        Reject = 2,
        KeepFirst = 3,
    }

    #[repr(i32)]
    #[derive(Debug)]
    enum Status {
        Ok = 0,
        Syntax = 1,
        DuplicateKey = 2,
        Depth = 3,
        UnsupportedSchemaRef = 4,
        InvalidBinary = 5,
        InvalidPath = 6,
        NumericOverflow = 7,
        InvalidSchema = 8,
        InvalidSchemaMap = 9,
        InvalidSchemaIndex = 10,
        PatternFailure = 11,
        SchemaObjectType = 12,
        SchemaCompositionFailure = 13,
        MissingSchema = 14,
        SchemaArgument = 15,
        UnsupportedComparison = 16,
    }

    struct ParseResult {
        status: Status,
        byte_offset: u64,
        message: String,
    }

    struct BuildResult {
        status: i32,
        binary: Vec<u8>,
        input_count: usize,
        output_count: usize,
    }

    struct BinaryResult {
        status: Status,
        byte_offset: u64,
        message: String,
        binary: Vec<u8>,
    }

    struct BinaryMetadataResult {
        status: Status,
        byte_offset: u64,
        value_type: u8,
        depth: u32,
        opaque_field_type: u16,
        length: u64,
    }

    struct WriterBeginResult {
        status: i32,
        handle: u64,
    }

    struct WriterFinishResult {
        status: i32,
        binary: Vec<u8>,
    }

    enum StoredValueClass {
        Null,
        Int,
        Uint,
        Double,
        Float,
        Boolean,
        Text,
        Opaque,
        Container,
        Decimal,
        Temporal,
    }

    #[derive(Default)]
    struct StoredMetadataResult {
        status: i32,
        source_status: i32,
        value_class: StoredValueClass,
        value_type: u8,
        encoded_size: u64,
        root_offset: u64,
        has_header: bool,
        extension: u64,
        count: u64,
        entry_type: u8,
        data_offset: u64,
        field_type: u16,
        int_value: i64,
        uint_value: u64,
        float_value: f32,
        double_value: f64,
    }

    #[derive(Default)]
    struct StoredLocationResult {
        status: i32,
        source_status: i32,
        found: bool,
        offset: u64,
        length: u64,
        raw_type: u8,
        entry_type: u8,
        index: u64,
    }

    struct StoredValueResult {
        status: i32,
        source_status: i32,
        size: u64,
        binary: Vec<u8>,
    }

    struct PathMetadataResult {
        status: Status,
        byte_offset: u64,
        too_many_nodes: bool,
        can_match_many: bool,
        last_node: u8,
    }

    struct PathQueryResult {
        status: Status,
        byte_offset: u64,
        match_count: u64,
        single_length: u64,
    }

    struct PathValueResult {
        status: Status,
        byte_offset: u64,
        match_count: u64,
        value_type: u8,
        use_wrapper: bool,
        wrapper_required: bool,
        binary: Vec<u8>,
    }

    struct BoolResult {
        status: Status,
        platform_error: i32,
        byte_offset: u64,
        value: bool,
        is_null: bool,
    }

    struct FormatResult {
        status: Status,
        platform_error: i32,
        text: Vec<u8>,
    }

    struct HashResult {
        status: Status,
        platform_error: i32,
        value: u64,
    }

    struct OrderResult {
        status: Status,
        platform_error: i32,
        order: i32,
    }

    struct MergeOutput {
        status: i32,
        is_null: bool,
        binary: Vec<u8>,
    }

    unsafe extern "C++" {
        include!("rust/json/include/read.h");
        fn format_database_scalar(input: &[u8], quoted: bool) -> FormatResult;
        fn compare_temporal(left: &[u8], right: &[u8]) -> OrderResult;
        type HashCapability;
        fn append_bytes(self: &HashCapability, seed: u64, input: &[u8]) -> u64;
        fn hash_database_scalar(self: &HashCapability, seed: u64, kind: u8, integer: i64,
                                unsigned_value: u64, real: f64, payload: &[u8]) -> HashResult;
    }

    struct DeltaRange {
        offset: i64,
        length: i64,
    }

    struct SchemaStatus {
        status: Status,
        platform_error: i32,
    }
    struct SchemaCompileResult {
        status: Status,
        platform_error: i32,
        binary: Vec<u8>,
    }
    struct SchemaValidationResult {
        status: Status,
        platform_error: i32,
        valid: bool,
        report: Vec<u8>,
    }

    unsafe extern "C++" {
        include!("rust/json/include/schema_pattern.h");
        type SchemaPattern;
        fn compile_schema_pattern(pattern: &[u8]) -> Result<UniquePtr<SchemaPattern>>;
        fn matches(self: &SchemaPattern, text: &[u8]) -> Result<bool>;
    }

    unsafe extern "C++" {
        include!("rust/json/include/binary_source.h");
        type BinarySource;
        fn binary_length(self: &BinarySource) -> u64;
        fn binary_view(self: &BinarySource) -> &[u8];
        fn binary_read(self: &BinarySource, offset: u64, output: &mut [u8]) -> i32;
    }

    extern "Rust" {
        fn is_document_header(value: u8) -> bool;
        fn hash_binary(input: &[u8], seed: u64, capability: &HashCapability) -> HashResult;
        type ContainerBuilder;
        fn new_container_builder(object: bool) -> Box<ContainerBuilder>;
        fn append_binary(self: &mut ContainerBuilder, key: &[u8], value: &[u8]) -> i32;
        fn append_owned_binary(self: &mut ContainerBuilder, key: &[u8], value: Vec<u8>) -> i32;
        fn append_aggregate_binary(self: &mut ContainerBuilder, key: &[u8], value: &[u8]) -> i32;
        fn approximate_length(self: &ContainerBuilder) -> u64;
        fn extend_binary(self: &mut ContainerBuilder, input: &[u8]) -> i32;
        fn rename_key(self: &mut ContainerBuilder, old_key: &[u8], new_key: &[u8]) -> i32;
        fn build_container(self: &ContainerBuilder, duplicate: Duplicate, sort_keys: bool,
                           aggregate: bool, max_depth: u32) -> BuildResult;
        fn encode_scalar(kind: u8, integer: i64, unsigned_value: u64, real: f64,
                         payload: &[u8], field_type: u16) -> BuildResult;

        fn format_binary(input: &[u8], quoted: bool, pretty: bool, depth: usize) -> FormatResult;
        fn order_binary(left: &[u8], right: &[u8]) -> OrderResult;
        fn quote_string(input: &[u8], quoted: bool) -> Vec<u8>;


        type SchemaCache;
        fn new_schema_cache() -> Box<SchemaCache>;
        fn prepare(
            self: &mut SchemaCache,
            slot: usize,
            input: &[u8],
            binary: bool,
            normalized: bool,
        ) -> SchemaStatus;
        fn validate_schema(
            self: &SchemaCache,
            slot: usize,
            input: &[u8],
            report: bool,
        ) -> SchemaValidationResult;
        fn compile_schema(input: &[u8], binary: bool, max_depth: u32) -> SchemaCompileResult;

        #[namespace = "oceanbase::common"]
        #[cxx_name = "BinaryMerge"]
        type MergeState;
        fn new_merge_accumulator(mode: u8) -> Box<MergeState>;
        #[namespace = "oceanbase::common"]
        fn merge_status(self: &MergeState) -> i32;
        #[namespace = "oceanbase::common"]
        fn merge_needs_input(self: &MergeState) -> bool;
        #[namespace = "oceanbase::common"]
        fn merge_input_failed(self: &MergeState) -> bool;
        #[namespace = "oceanbase::common"]
        fn merge_push(
            self: &mut MergeState,
            input: &[u8],
            text: bool,
            sql_null: bool,
            max_depth: u32,
        ) -> i32;
        fn finish_merge(merger: Box<MergeState>) -> MergeOutput;
        type SqlEditor;
        type PreparedMutation;
        fn new_sql_editor_owned(input: &[u8], text: bool, max_depth: u32) -> Box<SqlEditor>;
        fn new_sql_editor_external(source: &BinarySource) -> Box<SqlEditor>;
        fn editor_status(self: &SqlEditor) -> i32;
        fn editor_source_status(self: &SqlEditor) -> i32;
        fn prepare_owned(self: &mut SqlEditor, path: &Path, operation: u8)
            -> Box<PreparedMutation>;
        fn prepare_external(
            self: &mut SqlEditor,
            source: &BinarySource,
            path: &Path,
            operation: u8,
        ) -> Box<PreparedMutation>;
        fn prepared_status(self: &PreparedMutation) -> i32;
        fn prepared_source_status(self: &PreparedMutation) -> i32;
        fn prepared_needs_value(self: &PreparedMutation) -> bool;
        fn apply_owned(self: &mut SqlEditor, prepared: Box<PreparedMutation>, value: &[u8]) -> i32;
        fn apply_external(
            self: &mut SqlEditor,
            source: &BinarySource,
            prepared: Box<PreparedMutation>,
            value: &[u8],
        ) -> i32;
        fn finish_owned(editor: Box<SqlEditor>) -> Box<NativeEdit>;
        fn finish_external(
            editor: Box<SqlEditor>,
            source: &BinarySource,
            prior_rebuilt: bool,
            prior_unchanged: bool,
        ) -> Box<NativeEdit>;
        #[namespace = "oceanbase::common"]
        #[cxx_name = "BinaryEdit"]
        type NativeEdit;
        type BinaryWriter;
        fn new_binary_writer() -> Box<BinaryWriter>;
        fn writer_begin(self: &mut BinaryWriter, object: bool, count: u64, width: u8) -> WriterBeginResult;
        fn writer_key(self: &mut BinaryWriter, key: &[u8]) -> i32;
        fn writer_string(self: &mut BinaryWriter, value: &[u8]) -> i32;
        fn writer_double(self: &mut BinaryWriter, value: f64) -> i32;
        fn writer_end(self: &mut BinaryWriter, handle: u64) -> i32;
        fn writer_finish(writer: Box<BinaryWriter>) -> WriterFinishResult;
        fn stored_position(source: &BinarySource, offset: u64) -> StoredLocationResult;
        fn stored_document(source: &BinarySource, offset: u64, raw_type: u8,
                           entry_type: u8) -> StoredValueResult;
        fn stored_metadata(source: &BinarySource, offset: u64, raw_type: u8,
                           entry_type: u8) -> StoredMetadataResult;
        fn stored_child(source: &BinarySource, offset: u64, raw_type: u8,
                        entry_type: u8, index: u64) -> StoredLocationResult;
        fn stored_key(source: &BinarySource, offset: u64, raw_type: u8,
                      entry_type: u8, index: u64) -> StoredLocationResult;
        fn stored_find_key(source: &BinarySource, offset: u64, raw_type: u8,
                           entry_type: u8, key: &[u8]) -> StoredLocationResult;
        fn stored_depth(source: &BinarySource, offset: u64, raw_type: u8,
                        entry_type: u8) -> StoredValueResult;
        fn stored_free_space(source: &BinarySource, offset: u64, raw_type: u8,
                             entry_type: u8) -> StoredValueResult;

        fn plan_binary_edit(
            source: &BinarySource,
            ancestors: &[u64],
            operation: u8,
            index: u64,
            key: &[u8],
            value: &[u8],
        ) -> Box<NativeEdit>;
        #[namespace = "oceanbase::common"]
        fn edit_status(self: &NativeEdit) -> i32;
        #[namespace = "oceanbase::common"]
        fn edit_source_status(self: &NativeEdit) -> i32;
        #[namespace = "oceanbase::common"]
        fn edit_is_partial(self: &NativeEdit) -> bool;
        #[namespace = "oceanbase::common"]
        fn edit_binary(self: &NativeEdit) -> &[u8];
        #[namespace = "oceanbase::common"]
        fn edit_write_count(self: &NativeEdit) -> usize;
        #[namespace = "oceanbase::common"]
        fn edit_write_offset(self: &NativeEdit, index: usize) -> u64;
        #[namespace = "oceanbase::common"]
        fn edit_write_bytes(self: &NativeEdit, index: usize) -> &[u8];
        fn merge_edit(self: &mut Delta, edit: &mut NativeEdit) -> i32;
        type Delta;
        fn new_delta() -> Box<Delta>;
        fn delta_record(
            self: &mut Delta,
            operation: u8,
            value_type: u8,
            flags: u16,
            path: &[u8],
            value: &[u8],
        ) -> i32;
        fn delta_record_physical(self: &mut Delta, offset: i64, length: i64) -> i32;
        fn delta_logical_count(self: &Delta) -> usize;
        fn delta_physical_count(self: &Delta) -> usize;
        fn delta_physical(self: &Delta, index: usize) -> DeltaRange;
        fn delta_is_rebuilt(self: &Delta) -> bool;
        fn delta_mark_rebuilt(self: &mut Delta);
        fn delta_should_pack(self: &Delta, total: u64, extension: u64, root_offset: u64) -> i32;
        fn delta_intersects(self: &Delta, offset: u64, length: u64) -> bool;
        fn delta_encoded_size(self: &Delta) -> usize;
        fn delta_encode(self: &Delta, output: &mut [u8]) -> i32;
        fn delta_restore(self: &mut Delta, input: &[u8], physical: &[DeltaRange]) -> i32;

        fn validate(
            input: &[u8],
            duplicate: Duplicate,
            max_depth: u32,
            schema: bool,
        ) -> ParseResult;
        fn parse_binary(
            input: &[u8],
            duplicate: Duplicate,
            max_depth: u32,
            schema: bool,
        ) -> BinaryResult;
        fn inspect_binary(input: &[u8], with_depth: bool) -> BinaryMetadataResult;
        fn inspect_text(input: &[u8], with_depth: bool, max_depth: u32) -> BinaryMetadataResult;
        type Path;
        type PathParseResult;
        fn parse_mysql_path(input: &[u8]) -> Box<PathParseResult>;
        fn inspect_path(input: &[u8], mysql: bool) -> PathMetadataResult;
        fn status(self: &PathParseResult) -> Status;
        fn byte_offset(self: &PathParseResult) -> u64;
        fn path(self: &PathParseResult) -> &Path;
        fn can_match_many(self: &Path) -> bool;
        fn query_binary(
            path: &Path,
            input: &[u8],
            auto_wrap: bool,
            first_only: bool,
        ) -> PathQueryResult;
        fn query_text(
            path: &Path,
            input: &[u8],
            auto_wrap: bool,
            first_only: bool,
            max_depth: u32,
        ) -> PathQueryResult;
        type Query;
        type Value;
        fn identity(self: &Value) -> u64;
        type SelectionResult;
        type ValueParseResult;
        fn parse_value(input: &[u8], text: bool, max_depth: u32) -> Box<ValueParseResult>;
        fn status(self: &ValueParseResult) -> Status;
        fn byte_offset(self: &ValueParseResult) -> u64;
        fn take_value(parsed: Box<ValueParseResult>) -> Box<Value>;
        fn select_values(value: &Value, path: &Path, first_only: bool) -> Box<SelectionResult>;
        fn status(self: &SelectionResult) -> Status;
        fn byte_offset(self: &SelectionResult) -> u64;
        fn len(self: &SelectionResult) -> u64;
        fn value(self: &SelectionResult, index: u64) -> &Value;
        fn selected_output(
            value: &Value,
            path: &Path,
            wrapper: u8,
            scalars: u8,
            wrap_single: bool,
            wrap_multiple: bool,
        ) -> PathValueResult;
        fn new_query(input: &[u8], text: bool, max_depth: u32) -> Box<Query>;
        fn status(self: &Query) -> Status;
        fn byte_offset(self: &Query) -> u64;
        fn add_path(self: &mut Query, path: &Path) -> Status;
        fn finish(self: &mut Query) -> BinaryResult;
        fn query(self: &Query, path: &Path, auto_wrap: bool, first_only: bool) -> PathQueryResult;
        fn query_value(self: &Query, path: &Path, wrap_result: bool) -> PathValueResult;
        fn first_value(self: &Query, path: &Path, auto_wrap: bool) -> BinaryResult;
        fn check_schema_references(input: &[u8]) -> BoolResult;
        fn query_value_with_options(
            self: &Query,
            path: &Path,
            wrapper: u8,
            scalars: u8,
            wrap_single: bool,
            wrap_multiple: bool,
        ) -> PathValueResult;
        fn root_value(self: &Query, wrap_result: bool) -> PathValueResult;
        fn empty_container(object: bool) -> Vec<u8>;
        fn object_keys(self: &Query) -> BinaryResult;
        fn object_keys_at_path(self: &Query, path: &Path) -> BinaryResult;
        fn contains(self: &Query, candidate: &Query) -> BoolResult;
        fn contains_at_path(self: &Query, candidate: &Query, path: &Path) -> BoolResult;
        fn overlaps(self: &Query, other: &Query) -> BoolResult;
        fn member_of(self: &Query, target: &Query) -> BoolResult;
        fn search(self: &Query, target: &[u8], escape: u32, first_only: bool) -> BinaryResult;
    }
}

pub struct Query {
    state: Result<crate::Query, (ffi::Status, u64)>,
    paths: Vec<Path>,
}

pub struct SelectionResult {
    state: BinaryResult<crate::Selection>,
}

pub struct ValueParseResult { state: Result<Value, (ffi::Status, u64)> }
impl ValueParseResult {
    fn status(&self) -> ffi::Status { self.state.as_ref().map_or_else(|(status, _)| *status, |_| ffi::Status::Ok) }
    fn byte_offset(&self) -> u64 { self.state.as_ref().err().map_or(0, |(_, offset)| *offset) }
}
fn parse_value(input: &[u8], text: bool, max_depth: u32) -> Box<ValueParseResult> {
    let state = if text {
        crate::Value::from_text(input, ParseOptions { max_depth, ..ParseOptions::default() })
            .map_err(|error| (parse_status(&error), error.byte_offset as u64))
    } else {
        crate::BinaryDocument::new(input.to_vec()).and_then(crate::Value::root)
            .map_err(|error| (ffi::Status::InvalidBinary, error.byte_offset as u64))
    };
    Box::new(ValueParseResult { state })
}
#[allow(clippy::boxed_local)]
fn take_value(parsed: Box<ValueParseResult>) -> Box<Value> {
    Box::new(parsed.state.expect("value parse status was checked"))
}

fn select_values(value: &Value, path: &Path, first_only: bool) -> Box<SelectionResult> {
    Box::new(SelectionResult {
        state: value.select(path, crate::QueryOptions { auto_wrap: true, first_only }),
    })
}

impl SelectionResult {
    fn status(&self) -> ffi::Status {
        if self.state.is_ok() { ffi::Status::Ok } else { ffi::Status::InvalidBinary }
    }
    fn byte_offset(&self) -> u64 {
        self.state.as_ref().err().map_or(0, |error| error.byte_offset as u64)
    }
    fn len(&self) -> u64 {
        self.state.as_ref().map_or(0, |selection| selection.len() as u64)
    }
    fn value(&self, index: u64) -> &Value {
        self.state.as_ref().expect("selection status was checked")
            .get(index as usize).expect("selection index is in range")
    }
}

fn selected_output(value: &Value, path: &Path, wrapper: u8, scalars: u8,
                   wrap_single: bool, wrap_multiple: bool) -> ffi::PathValueResult {
    let wrapper = match wrapper {
        2 | 3 | 4 | 6 => crate::WrapperPolicy::With,
        5 | 7 => crate::WrapperPolicy::Conditional,
        _ => crate::WrapperPolicy::Without,
    };
    match value.query_output(path, crate::QueryOutputOptions {
        wrapper, allow_scalar: scalars != 1, wrap_single, wrap_multiple,
    }) {
        Ok(output) => ffi::PathValueResult {
            status: ffi::Status::Ok, byte_offset: 0,
            match_count: output.match_count as u64,
            value_type: output.value_type.map_or(0, |kind| kind as u8),
            use_wrapper: output.use_wrapper, wrapper_required: output.wrapper_required,
            binary: output.binary.unwrap_or_default(),
        },
        Err(error) => ffi::PathValueResult {
            status: ffi::Status::InvalidBinary, byte_offset: error.byte_offset as u64,
            match_count: 0, value_type: 0, use_wrapper: false, wrapper_required: false,
            binary: Vec::new(),
        },
    }
}

impl Query {
    fn status(&self) -> ffi::Status {
        match &self.state {
            Ok(_) => ffi::Status::Ok,
            Err((status, _)) => *status,
        }
    }

    fn byte_offset(&self) -> u64 {
        match &self.state {
            Ok(_) => 0,
            Err((_, offset)) => *offset,
        }
    }

    fn add_path(&mut self, path: &Path) -> ffi::Status {
        match &mut self.state {
            Ok(_) => {
                self.paths.push(path.clone());
                ffi::Status::Ok
            }
            Err((status, _)) => *status,
        }
    }

    fn finish(&mut self) -> ffi::BinaryResult {
        let state = std::mem::replace(&mut self.state, Err((ffi::Status::InvalidBinary, 0)));
        let paths = std::mem::take(&mut self.paths);
        match state {
            Ok(document) => optional_binary_result(document.extract(&paths)),
            Err((status, byte_offset)) => ffi::BinaryResult {
                status,
                byte_offset,
                message: String::new(),
                binary: Vec::new(),
            },
        }
    }

    fn query(&self, path: &Path, auto_wrap: bool, first_only: bool) -> ffi::PathQueryResult {
        match &self.state {
            Ok(document) => {
                let result = document
                    .query(
                        path,
                        crate::QueryOptions {
                            auto_wrap,
                            first_only,
                        },
                    )
                    .and_then(|values| {
                        let single_length = if values.len() == 1 {
                            values[0].len()?
                        } else {
                            0
                        };
                        Ok((values.len() as u64, single_length))
                    });
                query_result(result)
            }
            Err((status, byte_offset)) => ffi::PathQueryResult {
                status: *status,
                byte_offset: *byte_offset,
                match_count: 0,
                single_length: 0,
            },
        }
    }

    fn first_value(&self, path: &Path, auto_wrap: bool) -> ffi::BinaryResult {
        match &self.state {
            Ok(document) => optional_binary_result(document.first_value(path, auto_wrap)),
            Err((status, byte_offset)) => ffi::BinaryResult {
                status: *status, byte_offset: *byte_offset, message: String::new(), binary: Vec::new(),
            },
        }
    }

    fn query_value(&self, path: &Path, wrap_result: bool) -> ffi::PathValueResult {
        let result = match &self.state {
            Ok(document) => document.query_output(path, wrap_result),
            Err((status, byte_offset)) => {
                return ffi::PathValueResult {
                    status: *status,
                    byte_offset: *byte_offset,
                    match_count: 0,
                    value_type: 0,
                    use_wrapper: false,
                    wrapper_required: false,
                    binary: Vec::new(),
                }
            }
        };
        match result {
            Ok(output) => ffi::PathValueResult {
                status: ffi::Status::Ok,
                byte_offset: 0,
                match_count: output.match_count as u64,
                value_type: output.value_type.map_or(0, |value_type| value_type as u8),
                use_wrapper: output.use_wrapper,
                wrapper_required: output.wrapper_required,
                binary: output.binary.unwrap_or_default(),
            },
            Err(error) => ffi::PathValueResult {
                status: ffi::Status::InvalidBinary,
                byte_offset: error.byte_offset as u64,
                match_count: 0,
                value_type: 0,
                use_wrapper: false,
                wrapper_required: false,
                binary: Vec::new(),
            },
        }
    }

    fn query_value_with_options(
        &self,
        path: &Path,
        wrapper: u8,
        scalars: u8,
        wrap_single: bool,
        wrap_multiple: bool,
    ) -> ffi::PathValueResult {
        let wrapper = match wrapper {
            2 | 3 | 4 | 6 => crate::WrapperPolicy::With,
            5 | 7 => crate::WrapperPolicy::Conditional,
            _ => crate::WrapperPolicy::Without,
        };
        let result = match &self.state {
            Ok(document) => document.query_output_with_options(
                path,
                crate::QueryOutputOptions {
                    wrapper,
                    allow_scalar: scalars != 1,
                    wrap_single,
                    wrap_multiple,
                },
            ),
            Err((status, byte_offset)) => {
                return ffi::PathValueResult {
                    status: *status,
                    byte_offset: *byte_offset,
                    match_count: 0,
                    value_type: 0,
                    use_wrapper: false,
                    wrapper_required: false,
                    binary: Vec::new(),
                }
            }
        };
        match result {
            Ok(output) => ffi::PathValueResult {
                status: ffi::Status::Ok,
                byte_offset: 0,
                match_count: output.match_count as u64,
                value_type: output.value_type.map_or(0, |value_type| value_type as u8),
                use_wrapper: output.use_wrapper,
                wrapper_required: output.wrapper_required,
                binary: output.binary.unwrap_or_default(),
            },
            Err(error) => ffi::PathValueResult {
                status: ffi::Status::InvalidBinary,
                byte_offset: error.byte_offset as u64,
                match_count: 0,
                value_type: 0,
                use_wrapper: false,
                wrapper_required: false,
                binary: Vec::new(),
            },
        }
    }

    fn root_value(&self, wrap_result: bool) -> ffi::PathValueResult {
        let result = match &self.state {
            Ok(document) => document.root_output(wrap_result),
            Err((status, byte_offset)) => {
                return ffi::PathValueResult {
                    status: *status,
                    byte_offset: *byte_offset,
                    match_count: 0,
                    value_type: 0,
                    use_wrapper: false,
                    wrapper_required: false,
                    binary: Vec::new(),
                }
            }
        };
        match result {
            Ok(output) => ffi::PathValueResult {
                status: ffi::Status::Ok,
                byte_offset: 0,
                match_count: output.match_count as u64,
                value_type: output.value_type.map_or(0, |value_type| value_type as u8),
                use_wrapper: output.use_wrapper,
                wrapper_required: output.wrapper_required,
                binary: output.binary.unwrap_or_default(),
            },
            Err(error) => ffi::PathValueResult {
                status: ffi::Status::InvalidBinary,
                byte_offset: error.byte_offset as u64,
                match_count: 0,
                value_type: 0,
                use_wrapper: false,
                wrapper_required: false,
                binary: Vec::new(),
            },
        }
    }

    fn object_keys(&self) -> ffi::BinaryResult {
        match &self.state {
            Ok(document) => optional_binary_result(document.object_keys(None)),
            Err((status, byte_offset)) => failed_binary_result(*status, *byte_offset),
        }
    }

    fn object_keys_at_path(&self, path: &Path) -> ffi::BinaryResult {
        match &self.state {
            Ok(document) => optional_binary_result(document.object_keys(Some(path))),
            Err((status, byte_offset)) => failed_binary_result(*status, *byte_offset),
        }
    }

    fn contains(&self, candidate: &Query) -> ffi::BoolResult {
        bool_result(match (&self.state, &candidate.state) {
            (Ok(target), Ok(candidate)) => target
                .contains_with(candidate, None, &mut compare_database_temporal)
                .map_err(order_error),
            (Err(error), _) | (_, Err(error)) => Err((error.0, error.1, 0)),
        })
    }

    fn contains_at_path(&self, candidate: &Query, path: &Path) -> ffi::BoolResult {
        bool_result(match (&self.state, &candidate.state) {
            (Ok(target), Ok(candidate)) => target
                .contains_with(candidate, Some(path), &mut compare_database_temporal)
                .map_err(order_error),
            (Err(error), _) | (_, Err(error)) => Err((error.0, error.1, 0)),
        })
    }

    fn overlaps(&self, other: &Query) -> ffi::BoolResult {
        bool_result(match (&self.state, &other.state) {
            (Ok(left), Ok(right)) => left
                .overlaps_with(right, &mut compare_database_temporal)
                .map(Some)
                .map_err(order_error),
            (Err(error), _) | (_, Err(error)) => Err((error.0, error.1, 0)),
        })
    }

    fn member_of(&self, target: &Query) -> ffi::BoolResult {
        bool_result(match (&self.state, &target.state) {
            (Ok(candidate), Ok(target)) => candidate
                .member_of_with(target, &mut compare_database_temporal)
                .map(Some)
                .map_err(order_error),
            (Err(error), _) | (_, Err(error)) => Err((error.0, error.1, 0)),
        })
    }

    fn search(&self, target: &[u8], escape: u32, first_only: bool) -> ffi::BinaryResult {
        let document = match &self.state {
            Ok(document) => document,
            Err((status, byte_offset)) => return failed_binary_result(*status, *byte_offset),
        };
        let target = match std::str::from_utf8(target) {
            Ok(target) => target,
            Err(error) => {
                return failed_binary_result(ffi::Status::Syntax, error.valid_up_to() as u64)
            }
        };
        let Some(escape) = char::from_u32(escape) else {
            return failed_binary_result(ffi::Status::Syntax, 0);
        };
        optional_binary_result(document.search(
            &self.paths,
            crate::SearchOptions {
                pattern: target,
                escape,
                first_only,
            },
        ))
    }
}

fn binary_error(error: crate::BinaryError) -> (ffi::Status, u64) {
    (ffi::Status::InvalidBinary, error.byte_offset as u64)
}

fn compare_error(error: crate::CompareError) -> (ffi::Status, u64) {
    match error {
        crate::CompareError::Binary(error) => binary_error(error),
        crate::CompareError::NumericOverflow => (ffi::Status::NumericOverflow, 0),
        crate::CompareError::UnsupportedTypes => (ffi::Status::UnsupportedComparison, 0),
    }
}

fn bool_result(result: Result<Option<bool>, (ffi::Status, u64, i32)>) -> ffi::BoolResult {
    match result {
        Ok(value) => ffi::BoolResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            platform_error: 0,
            value: value.unwrap_or(false),
            is_null: value.is_none(),
        },
        Err((status, byte_offset, platform_error)) => ffi::BoolResult {
            status,
            byte_offset,
            platform_error,
            value: false,
            is_null: false,
        },
    }
}

fn new_query(input: &[u8], text: bool, max_depth: u32) -> Box<Query> {
    let state = if text {
        crate::Query::from_text(
            input,
            ParseOptions {
                max_depth,
                ..ParseOptions::default()
            },
        )
        .map_err(|error| (parse_status(&error), error.byte_offset as u64))
    } else {
        crate::Query::from_binary(input.to_vec())
            .map_err(|error| (ffi::Status::InvalidBinary, error.byte_offset as u64))
    };
    Box::new(Query {
        state,
        paths: Vec::new(),
    })
}

fn optional_binary_result(result: BinaryResult<Option<Vec<u8>>>) -> ffi::BinaryResult {
    match result {
        Ok(binary) => ffi::BinaryResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            message: String::new(),
            binary: binary.unwrap_or_default(),
        },
        Err(error) => ffi::BinaryResult {
            status: ffi::Status::InvalidBinary,
            byte_offset: error.byte_offset as u64,
            message: error.to_string(),
            binary: Vec::new(),
        },
    }
}

fn failed_binary_result(status: ffi::Status, byte_offset: u64) -> ffi::BinaryResult {
    ffi::BinaryResult {
        status,
        byte_offset,
        message: String::new(),
        binary: Vec::new(),
    }
}

fn policy(duplicate: ffi::Duplicate) -> DuplicatePolicy {
    match duplicate {
        ffi::Duplicate::Overwrite => DuplicatePolicy::Overwrite,
        ffi::Duplicate::Preserve => DuplicatePolicy::Preserve,
        ffi::Duplicate::Reject => DuplicatePolicy::Reject,
        ffi::Duplicate::KeepFirst => DuplicatePolicy::KeepFirst,
        _ => DuplicatePolicy::Overwrite,
    }
}

fn result(result: crate::Result<()>) -> ffi::ParseResult {
    match result {
        Ok(()) => ffi::ParseResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            message: String::new(),
        },
        Err(error) => ffi::ParseResult {
            status: match error.kind {
                crate::ErrorKind::Syntax => ffi::Status::Syntax,
                crate::ErrorKind::DuplicateKey => ffi::Status::DuplicateKey,
                crate::ErrorKind::Depth => ffi::Status::Depth,
                crate::ErrorKind::UnsupportedSchemaRef => ffi::Status::UnsupportedSchemaRef,
            },
            byte_offset: error.byte_offset as u64,
            message: error.message,
        },
    }
}

pub(crate) fn validate(
    input: &[u8],
    duplicate: ffi::Duplicate,
    max_depth: u32,
    schema: bool,
) -> ffi::ParseResult {
    let options = ParseOptions {
        duplicate_policy: policy(duplicate),
        max_depth,
        schema,
    };
    result(crate::validate(input, options))
}

pub(crate) fn parse_binary(
    input: &[u8],
    duplicate: ffi::Duplicate,
    max_depth: u32,
    schema: bool,
) -> ffi::BinaryResult {
    let options = ParseOptions {
        duplicate_policy: policy(duplicate),
        max_depth,
        schema,
    };
    match crate::parse_binary(input, options) {
        Ok(binary) => ffi::BinaryResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            message: String::new(),
            binary,
        },
        Err(error) => ffi::BinaryResult {
            status: match error.kind {
                crate::ErrorKind::Syntax => ffi::Status::Syntax,
                crate::ErrorKind::DuplicateKey => ffi::Status::DuplicateKey,
                crate::ErrorKind::Depth => ffi::Status::Depth,
                crate::ErrorKind::UnsupportedSchemaRef => ffi::Status::UnsupportedSchemaRef,
            },
            byte_offset: error.byte_offset as u64,
            message: error.message,
            binary: Vec::new(),
        },
    }
}

pub(crate) fn inspect_binary(input: &[u8], with_depth: bool) -> ffi::BinaryMetadataResult {
    match crate::BinaryDocument::new(input).and_then(|document| {
        let root = document.root()?;
        let length = root.len()?;
        let value_type = root.json_type()? as u8;
        let opaque_field_type = if value_type == crate::JsonType::Opaque as u8 {
            root.opaque_field_type()?
        } else {
            0
        };
        let depth = if with_depth { document.depth()? } else { 0 };
        Ok((value_type, depth, opaque_field_type, length))
    }) {
        Ok((value_type, depth, opaque_field_type, length)) => ffi::BinaryMetadataResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            value_type,
            depth,
            opaque_field_type,
            length,
        },
        Err(error) => ffi::BinaryMetadataResult {
            status: ffi::Status::InvalidBinary,
            byte_offset: error.byte_offset as u64,
            value_type: 0,
            depth: 0,
            opaque_field_type: 0,
            length: 0,
        },
    }
}

fn parse_status(error: &crate::ParseError) -> ffi::Status {
    match error.kind {
        crate::ErrorKind::Syntax => ffi::Status::Syntax,
        crate::ErrorKind::DuplicateKey => ffi::Status::DuplicateKey,
        crate::ErrorKind::Depth => ffi::Status::Depth,
        crate::ErrorKind::UnsupportedSchemaRef => ffi::Status::UnsupportedSchemaRef,
    }
}

pub(crate) fn inspect_text(
    input: &[u8],
    with_depth: bool,
    max_depth: u32,
) -> ffi::BinaryMetadataResult {
    let options = ParseOptions {
        max_depth,
        ..ParseOptions::default()
    };
    match crate::parse_binary(input, options) {
        Ok(binary) => inspect_binary(&binary, with_depth),
        Err(error) => ffi::BinaryMetadataResult {
            status: parse_status(&error),
            byte_offset: error.byte_offset as u64,
            value_type: 0,
            depth: 0,
            opaque_field_type: 0,
            length: 0,
        },
    }
}

pub struct PathParseResult {
    path: Option<crate::Path>,
    error: Option<crate::PathError>,
}

fn empty_container(object: bool) -> Vec<u8> {
    crate::binary::serialize(if object {
        crate::Document::Object(Vec::new())
    } else {
        crate::Document::Array(Vec::new())
    })
}

impl PathParseResult {
    fn status(&self) -> ffi::Status {
        if self.path.is_some() {
            ffi::Status::Ok
        } else {
            ffi::Status::InvalidPath
        }
    }

    fn byte_offset(&self) -> u64 {
        self.error
            .as_ref()
            .map_or(0, |error| error.byte_offset as u64)
    }

    fn path(&self) -> &crate::Path {
        self.path
            .as_ref()
            .expect("valid paths are checked before use")
    }
}

pub(crate) fn parse_mysql_path(input: &[u8]) -> Box<PathParseResult> {
    Box::new(match crate::Path::parse_mysql(input) {
        Ok(path) => PathParseResult {
            path: Some(path),
            error: None,
        },
        Err(error) => PathParseResult {
            path: None,
            error: Some(error),
        },
    })
}

fn query_result(result: crate::BinaryResult<(u64, u64)>) -> ffi::PathQueryResult {
    match result {
        Ok((match_count, single_length)) => ffi::PathQueryResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            match_count,
            single_length,
        },
        Err(error) => ffi::PathQueryResult {
            status: ffi::Status::InvalidBinary,
            byte_offset: error.byte_offset as u64,
            match_count: 0,
            single_length: 0,
        },
    }
}

pub(crate) fn query_binary(
    path: &crate::Path,
    input: &[u8],
    auto_wrap: bool,
    first_only: bool,
) -> ffi::PathQueryResult {
    let result = crate::BinaryDocument::new(input).and_then(|document| {
        let matches = path.query(
            document.root()?,
            crate::QueryOptions {
                auto_wrap,
                first_only,
            },
        )?;
        let single_length = if matches.len() == 1 {
            matches[0].len()?
        } else {
            0
        };
        Ok((matches.len() as u64, single_length))
    });
    query_result(result)
}

pub(crate) fn query_text(
    path: &crate::Path,
    input: &[u8],
    auto_wrap: bool,
    first_only: bool,
    max_depth: u32,
) -> ffi::PathQueryResult {
    let options = ParseOptions {
        max_depth,
        ..ParseOptions::default()
    };
    match crate::parse_binary(input, options) {
        Ok(binary) => query_binary(path, &binary, auto_wrap, first_only),
        Err(error) => ffi::PathQueryResult {
            status: parse_status(&error),
            byte_offset: error.byte_offset as u64,
            match_count: 0,
            single_length: 0,
        },
    }
}

fn new_delta() -> Box<Delta> {
    Box::new(Delta::default())
}

fn delta_status(result: Result<(), DeltaError>) -> i32 {
    match result {
        Ok(()) => 0,
        Err(DeltaError::Allocation) => 1,
        Err(DeltaError::UnsupportedVersion) => 2,
        Err(DeltaError::Truncated) => 3,
        Err(DeltaError::InvalidRecord | DeltaError::InvalidRange) => 4,
    }
}

impl Delta {
    fn delta_record(
        &mut self,
        operation: u8,
        value_type: u8,
        flags: u16,
        path: &[u8],
        value: &[u8],
    ) -> i32 {
        delta_status(
            Operation::try_from(u64::from(operation))
                .and_then(|operation| self.record(operation, value_type, flags, path, value)),
        )
    }
    fn delta_record_physical(&mut self, offset: i64, length: i64) -> i32 {
        delta_status(
            PhysicalDiff::new(offset as u64, length as u64)
                .and_then(|diff| self.record_physical(diff)),
        )
    }
    fn delta_logical_count(&self) -> usize {
        self.logical().len()
    }
    fn delta_physical_count(&self) -> usize {
        self.physical().len()
    }
    fn delta_physical(&self, index: usize) -> ffi::DeltaRange {
        match self.physical().get(index) {
            Some(diff) => ffi::DeltaRange {
                offset: diff.offset as i64,
                length: diff.length as i64,
            },
            None => ffi::DeltaRange {
                offset: -1,
                length: -1,
            },
        }
    }
    fn delta_is_rebuilt(&self) -> bool {
        self.is_rebuilt()
    }
    fn delta_mark_rebuilt(&mut self) {
        self.mark_rebuilt();
    }
    fn delta_should_pack(&self, total: u64, extension: u64, root_offset: u64) -> i32 {
        match self.should_pack(total, extension, root_offset) {
            Ok(value) => i32::from(value),
            Err(_) => -1,
        }
    }
    fn delta_intersects(&self, offset: u64, length: u64) -> bool {
        self.intersects(offset, length)
    }
    fn delta_encoded_size(&self) -> usize {
        self.encoded_logical_size()
    }
    fn delta_encode(&self, output: &mut [u8]) -> i32 {
        delta_status(self.write_logical(output).map(|_| ()))
    }
    fn delta_restore(&mut self, input: &[u8], physical: &[ffi::DeltaRange]) -> i32 {
        let mut ranges = Vec::new();
        if ranges.try_reserve_exact(physical.len()).is_err() {
            return 1;
        }
        for range in physical {
            match PhysicalDiff::new(range.offset as u64, range.length as u64) {
                Ok(range) => ranges.push(range),
                Err(error) => return delta_status(Err(error)),
            }
        }
        delta_status(self.restore(input, &ranges).map(|_| ()))
    }
}

struct NativeEdit {
    binary: Vec<u8>,
    partial: bool,
    result: Result<crate::mutation::editor::Update, crate::mutation::editor::EditError>,
}

struct CursorSource<'a> {
    source: &'a ffi::BinarySource,
    contiguous: &'a [u8],
    length: u64,
}

impl<'a> CursorSource<'a> {
    fn new(source: &'a ffi::BinarySource) -> Self {
        Self { source, contiguous: source.binary_view(), length: source.binary_length() }
    }
}

impl crate::mutation::editor::BinarySource for CursorSource<'_> {
    fn contiguous(&self) -> Option<&[u8]> {
        (!self.contiguous.is_empty()).then_some(self.contiguous)
    }
    fn len(&self) -> u64 {
        self.length
    }
    fn read_exact(&self, offset: u64, output: &mut [u8]) -> std::io::Result<()> {
        if !self.contiguous.is_empty() {
            let start = usize::try_from(offset).map_err(|_| std::io::ErrorKind::UnexpectedEof)?;
            let end = start.checked_add(output.len()).ok_or(std::io::ErrorKind::UnexpectedEof)?;
            let input = self.contiguous.get(start..end).ok_or(std::io::ErrorKind::UnexpectedEof)?;
            output.copy_from_slice(input);
            return Ok(());
        }
        let status = self.source.binary_read(offset, output);
        if status == 0 {
            Ok(())
        } else {
            Err(std::io::Error::from_raw_os_error(status))
        }
    }
}

fn plan_binary_edit(
    source: &ffi::BinarySource,
    ancestors: &[u64],
    operation: u8,
    index: u64,
    key: &[u8],
    value: &[u8],
) -> Box<NativeEdit> {
    use crate::mutation::editor::{EditError, Editor};
    let result = (|| {
        let source = CursorSource::new(source);
        let mut editor = Editor::open(&source)?;
        if operation == 3 {
            editor.remove(ancestors, index)?;
        } else {
            let value = crate::BinaryDocument::new(value)?;
            match operation {
                1 => editor.replace(ancestors, index, value.root()?)?,
                2 => editor.insert(ancestors, index, key, value.root()?)?,
                _ => return Err(EditError::InvalidIndex),
            }
        }
        Ok(editor.finish())
    })();
    Box::new(NativeEdit {
        result,
        binary: Vec::new(),
        partial: true,
    })
}

impl NativeEdit {
    fn edit_status(&self) -> i32 {
        use crate::mutation::editor::EditError;
        match &self.result {
            Ok(_) => 0,
            Err(EditError::Delta(DeltaError::Allocation)) => 1,
            Err(EditError::Source(_)) => 2,
            Err(EditError::Binary(_)) => 3,
            Err(
                EditError::InvalidIndex
                | EditError::InvalidPath
                | EditError::VacuousPath
                | EditError::StalePlan,
            ) => 4,
            Err(EditError::InvalidObjectKey) => 5,
            Err(EditError::EmptyObjectKey) => 9,
            Err(EditError::Delta(_)) => 6,
        }
    }
    fn edit_source_status(&self) -> i32 {
        match &self.result {
            Err(crate::mutation::editor::EditError::Source(error)) => {
                error.raw_os_error().unwrap_or(-1)
            }
            _ => 0,
        }
    }
    fn edit_is_partial(&self) -> bool {
        self.partial
    }
    fn edit_binary(&self) -> &[u8] {
        &self.binary
    }
    fn edit_write_count(&self) -> usize {
        self.result.as_ref().map_or(0, |update| update.writes.len())
    }
    fn edit_write_offset(&self, index: usize) -> u64 {
        self.result
            .as_ref()
            .ok()
            .and_then(|update| update.writes.get(index))
            .map_or(u64::MAX, |write| write.offset)
    }
    fn edit_write_bytes(&self, index: usize) -> &[u8] {
        self.result
            .as_ref()
            .ok()
            .and_then(|update| update.writes.get(index))
            .map_or(&[], |write| write.bytes.as_slice())
    }
}

impl Delta {
    fn merge_edit(&mut self, edit: &mut NativeEdit) -> i32 {
        match &mut edit.result {
            Ok(update) => delta_status(self.append(&mut update.delta)),
            Err(_) => 4,
        }
    }
}

struct PlatformPattern(cxx::UniquePtr<ffi::SchemaPattern>);
struct PlatformPatternCompiler;
impl crate::schema::Pattern for PlatformPattern {
    fn is_match(&self, text: &[u8]) -> Result<bool, crate::schema::Error> {
        self.0
            .matches(text)
            .map_err(|error| crate::schema::Error::Pattern(error.to_string()))
    }
}
impl crate::schema::PatternCompiler for PlatformPatternCompiler {
    fn compile(
        &self,
        text: &[u8],
    ) -> Result<Box<dyn crate::schema::Pattern>, crate::schema::PatternError> {
        let pattern = ffi::compile_schema_pattern(text)
            .map_err(|error| crate::schema::PatternError::Resource(error.to_string()))?;
        if pattern.is_null() {
            Err(crate::schema::PatternError::Invalid)
        } else {
            Ok(Box::new(PlatformPattern(pattern)))
        }
    }
}
pub struct SchemaCache(crate::schema::Cache);
fn new_schema_cache() -> Box<SchemaCache> {
    Box::new(SchemaCache(crate::schema::Cache::with_comparator(
        schema_compare_temporal,
    )))
}
impl SchemaCache {
    fn prepare(
        &mut self,
        slot: usize,
        input: &[u8],
        binary: bool,
        normalized: bool,
    ) -> ffi::SchemaStatus {
        use crate::schema::InputFormat;
        let format = if normalized {
            InputFormat::Normalized
        } else if binary {
            InputFormat::Binary
        } else {
            InputFormat::Text
        };
        match self
            .0
            .prepare(slot, input, format, &PlatformPatternCompiler)
        {
            Ok(_) => ffi::SchemaStatus {
                status: ffi::Status::Ok,
                platform_error: 0,
            },
            Err(error) => ffi::SchemaStatus {
                status: schema_error(&error),
                platform_error: schema_platform_error(&error),
            },
        }
    }
    fn validate_schema(
        &self,
        slot: usize,
        input: &[u8],
        report: bool,
    ) -> ffi::SchemaValidationResult {
        let result = (|| {
            let schema = self.0.get(slot)?;
            let document = crate::BinaryDocument::new(input)?;
            schema.validate(document.root()?)
        })();
        match result {
            Ok(validation) => ffi::SchemaValidationResult {
                status: ffi::Status::Ok,
                platform_error: 0,
                valid: validation.is_valid(),
                report: if report {
                    validation.report()
                } else {
                    Vec::new()
                },
            },
            Err(error) => ffi::SchemaValidationResult {
                status: schema_error(&error),
                platform_error: schema_platform_error(&error),
                valid: false,
                report: Vec::new(),
            },
        }
    }
}
fn schema_compare_temporal(
    left: crate::BinaryValue<'_>,
    right: crate::BinaryValue<'_>,
) -> crate::schema::Result<std::cmp::Ordering> {
    compare_database_temporal(left, right).map_err(|(status, offset, code)| {
        if code != 0 {
            crate::schema::Error::Platform(code)
        } else if status == ffi::Status::InvalidBinary {
            crate::schema::Error::Binary(crate::BinaryError::new(
                crate::BinaryErrorKind::InvalidType,
                offset as usize,
            ))
        } else {
            crate::schema::Error::UnsupportedComparison
        }
    })
}
fn schema_platform_error(error: &crate::schema::Error) -> i32 {
    if let crate::schema::Error::Platform(code) = error {
        *code
    } else {
        0
    }
}
fn compile_schema(input: &[u8], binary: bool, max_depth: u32) -> ffi::SchemaCompileResult {
    let format = if binary {
        crate::schema::InputFormat::Binary
    } else {
        crate::schema::InputFormat::Text
    };
    let result = crate::schema::Schema::from_input(
        input,
        format,
        max_depth,
        &PlatformPatternCompiler,
        schema_compare_temporal,
    )
    .and_then(|schema| schema.encode());
    match result {
        Ok(binary) => ffi::SchemaCompileResult {
            status: ffi::Status::Ok,
            platform_error: 0,
            binary,
        },
        Err(error) => ffi::SchemaCompileResult {
            status: schema_error(&error),
            platform_error: schema_platform_error(&error),
            binary: Vec::new(),
        },
    }
}
fn schema_error(error: &crate::schema::Error) -> ffi::Status {
    use crate::schema::Error;
    match error {
        Error::Binary(_) => ffi::Status::InvalidBinary,
        Error::Compare(crate::CompareError::Binary(_)) => ffi::Status::InvalidBinary,
        Error::Compare(crate::CompareError::NumericOverflow) => ffi::Status::NumericOverflow,
        Error::Compare(crate::CompareError::UnsupportedTypes) => ffi::Status::UnsupportedComparison,
        Error::Parse(error) => parse_status(error),
        Error::InvalidSchema => ffi::Status::InvalidSchema,
        Error::InvalidMap => ffi::Status::InvalidSchemaMap,
        Error::MissingSchema => ffi::Status::MissingSchema,
        Error::ObjectType => ffi::Status::SchemaArgument,
        Error::InvalidArgument => ffi::Status::SchemaArgument,
        Error::Composition(_) => ffi::Status::SchemaCompositionFailure,
        Error::InvalidIndex => ffi::Status::InvalidSchemaIndex,
        Error::Pattern(_) => ffi::Status::PatternFailure,
        Error::Platform(_) => ffi::Status::Ok,
        Error::UnsupportedComparison => ffi::Status::UnsupportedComparison,
    }
}

fn quote_string(input: &[u8], quoted: bool) -> Vec<u8> {
    let mut output = Vec::new();
    crate::format::append_string(input, quoted, &mut output);
    output
}

fn format_binary(input: &[u8], quoted: bool, pretty: bool, depth: usize) -> ffi::FormatResult {
    let mut output = ffi::FormatResult {
        status: ffi::Status::Ok,
        platform_error: 0,
        text: Vec::new(),
    };
    let result = (|| {
        let document = crate::BinaryDocument::new(input)?;
        crate::format::format(
            document.root()?,
            crate::format::FormatOptions {
                quoted,
                pretty,
                depth,
            },
            &mut |value, quoted| {
                let binary = crate::binary::serialize_value(value)
                    .map_err(|_| (ffi::Status::InvalidBinary, 0))?;
                let result = ffi::format_database_scalar(&binary, quoted);
                if result.status == ffi::Status::Ok && result.platform_error == 0 {
                    Ok(result.text)
                } else {
                    Err((result.status, result.platform_error))
                }
            },
        )
    })();
    match result {
        Ok(text) => output.text = text,
        Err(crate::format::FormatError::Binary(_)) => output.status = ffi::Status::InvalidBinary,
        Err(crate::format::FormatError::Scalar((status, error))) => {
            output.status = status;
            output.platform_error = error;
        }
    }
    output
}

pub(crate) fn compare_database_temporal(
    left: crate::BinaryValue<'_>,
    right: crate::BinaryValue<'_>,
) -> Result<std::cmp::Ordering, (ffi::Status, u64, i32)> {
    let binary_error =
        |error: crate::BinaryError| (ffi::Status::InvalidBinary, error.byte_offset as u64, 0);
    let left = crate::binary::serialize_value(left).map_err(binary_error)?;
    let right = crate::binary::serialize_value(right).map_err(binary_error)?;
    let result = ffi::compare_temporal(&left, &right);
    if result.status == ffi::Status::Ok && result.platform_error == 0 {
        Ok(result.order.cmp(&0))
    } else {
        Err((result.status, 0, result.platform_error))
    }
}

fn order_error(
    error: crate::compare::OrderError<(ffi::Status, u64, i32)>,
) -> (ffi::Status, u64, i32) {
    match error {
        crate::compare::OrderError::Value(error) => {
            let (status, offset) = compare_error(error);
            (status, offset, 0)
        }
        crate::compare::OrderError::UnsupportedTypes => (ffi::Status::UnsupportedComparison, 0, 0),
        crate::compare::OrderError::Scalar(error) => error,
    }
}

fn order_binary(left: &[u8], right: &[u8]) -> ffi::OrderResult {
    let mut output = ffi::OrderResult {
        status: ffi::Status::Ok,
        platform_error: 0,
        order: 0,
    };
    let result = (|| {
        let left = crate::BinaryDocument::new(left)?;
        let right = crate::BinaryDocument::new(right)?;
        crate::compare::order(left.root()?, right.root()?, &mut compare_database_temporal)
    })();
    match result {
        Ok(order) => output.order = order as i32,
        Err(error) => {
            let (status, _, platform_error) = order_error(error);
            output.status = status;
            output.platform_error = platform_error;
            if status == ffi::Status::UnsupportedComparison {
                output.order = 2;
            }
        }
    }
    output
}


type MutationError = crate::mutation::editor::EditError;

enum EditorBackend {
    Owned(crate::mutation::sql::OwnedEditor),
    External(crate::mutation::editor::Session),
}
struct SqlEditor {
    state: Option<Result<EditorBackend, MutationError>>,
}

struct PreparedMutation {
    edit: Result<crate::mutation::sql::PreparedEdit, MutationError>,
}

fn mutation_error_status(error: &MutationError) -> i32 {
    match error {
        MutationError::Delta(DeltaError::Allocation) => 1,
        MutationError::Source(_) => 2,
        MutationError::Binary(_) => 3,
        MutationError::InvalidIndex | MutationError::StalePlan => 4,
        MutationError::InvalidObjectKey => 5,
        MutationError::EmptyObjectKey => 9,
        MutationError::Delta(_) => 6,
        MutationError::InvalidPath => 7,
        MutationError::VacuousPath => 8,
    }
}

fn mutation_source_status(error: &MutationError) -> i32 {
    match error {
        MutationError::Source(error) => error.raw_os_error().unwrap_or(-1),
        _ => 0,
    }
}

fn sql_operation(operation: u8) -> Result<crate::mutation::sql::SqlOperation, MutationError> {
    use crate::mutation::sql::SqlOperation;
    match operation {
        0 => Ok(SqlOperation::Set),
        1 => Ok(SqlOperation::Insert),
        2 => Ok(SqlOperation::Replace),
        3 => Ok(SqlOperation::Remove),
        4 => Ok(SqlOperation::ArrayAppend),
        5 => Ok(SqlOperation::ArrayInsert),
        _ => Err(MutationError::InvalidIndex),
    }
}

fn new_sql_editor_owned(input: &[u8], text: bool, max_depth: u32) -> Box<SqlEditor> {
    use crate::mutation::sql::OwnedEditor;
    let state = (|| {
        let editor = if text {
            let document = crate::parse(input, ParseOptions { max_depth, ..ParseOptions::default() })
                .map_err(|_| MutationError::InvalidIndex)?;
            OwnedEditor::from_document(document)?
        } else {
            let document = crate::BinaryDocument::new(input)?;
            OwnedEditor::from_binary(document.root()?)?
        };
        Ok(EditorBackend::Owned(editor))
    })();
    Box::new(SqlEditor { state: Some(state) })
}

fn new_sql_editor_external(source: &ffi::BinarySource) -> Box<SqlEditor> {
    Box::new(SqlEditor { state: Some(crate::mutation::editor::Session::open(&CursorSource::new(source))
        .map(EditorBackend::External)) })
}

impl SqlEditor {
    fn editor_status(&self) -> i32 {
        self.state.as_ref().unwrap().as_ref().err().map_or(0, mutation_error_status)
    }
    fn editor_source_status(&self) -> i32 {
        self.state.as_ref().unwrap().as_ref().err().map_or(0, mutation_source_status)
    }
    fn prepare_owned(&mut self, path: &Path, operation: u8) -> Box<PreparedMutation> {
        let edit = (|| {
            let operation = sql_operation(operation)?;
            let Ok(EditorBackend::Owned(editor)) = self.state.as_ref().unwrap() else { return Err(MutationError::InvalidIndex); };
            editor.prepare(path, operation)
        })();
        Box::new(PreparedMutation { edit })
    }
    fn prepare_external(&mut self, source: &ffi::BinarySource, path: &Path, operation: u8) -> Box<PreparedMutation> {
        let edit = (|| {
            let operation = sql_operation(operation)?;
            let Ok(EditorBackend::External(session)) = self.state.as_mut().unwrap() else { return Err(MutationError::InvalidIndex); };
            session.prepare(&CursorSource::new(source), path, operation)
        })();
        Box::new(PreparedMutation { edit })
    }
    fn apply_owned(&mut self, prepared: Box<PreparedMutation>, value: &[u8]) -> i32 {
        let state = self.state.take().unwrap();
        self.state = Some((|| {
            let EditorBackend::Owned(editor) = state? else { return Err(MutationError::InvalidIndex); };
            let prepared = prepared.edit?;
            let document = if prepared.needs_value() { Some(crate::BinaryDocument::new(value)?) } else { None };
            let value = document.as_ref().map(|document| document.root()).transpose()?;
            editor.apply(prepared, value).map(EditorBackend::Owned)
        })());
        self.editor_status()
    }
    fn apply_external(&mut self, source: &ffi::BinarySource, prepared: Box<PreparedMutation>, value: &[u8]) -> i32 {
        let state = self.state.take().unwrap();
        self.state = Some((|| {
            let EditorBackend::External(mut session) = state? else { return Err(MutationError::InvalidIndex); };
            let prepared = prepared.edit?;
            let document = if prepared.needs_value() { Some(crate::BinaryDocument::new(value)?) } else { None };
            let value = document.as_ref().map(|document| document.root()).transpose()?;
            session.apply(&CursorSource::new(source), prepared, value)?;
            Ok(EditorBackend::External(session))
        })());
        self.editor_status()
    }
}

impl PreparedMutation {
    fn prepared_status(&self) -> i32 {
        self.edit.as_ref().err().map_or(0, mutation_error_status)
    }
    fn prepared_source_status(&self) -> i32 {
        self.edit.as_ref().err().map_or(0, mutation_source_status)
    }
    fn prepared_needs_value(&self) -> bool {
        self.edit.as_ref().is_ok_and(|edit| edit.needs_value())
    }
}

fn finish_owned(editor: Box<SqlEditor>) -> Box<NativeEdit> {
    let mut binary = Vec::new();
    let result = (|| {
        let EditorBackend::Owned(editor) = editor.state.unwrap()? else { return Err(MutationError::InvalidIndex); };
        binary = editor.finish()?;
        Ok(crate::mutation::editor::Update { writes: Vec::new(), delta: Delta::default() })
    })();
    Box::new(NativeEdit { result, binary, partial: false })
}

fn finish_external(
    editor: Box<SqlEditor>,
    source: &ffi::BinarySource,
    prior_rebuilt: bool,
    prior_unchanged: bool,
) -> Box<NativeEdit> {
    let mut binary = Vec::new();
    let mut partial = false;
    let result = (|| {
        let EditorBackend::External(mut session) = editor.state.unwrap()? else { return Err(MutationError::InvalidIndex); };
        partial = session.should_pack_with(crate::mutation::UpdateState::from_flags(
            prior_rebuilt,
            prior_unchanged,
        ))?;
        if !partial {
            binary = session.binary(&CursorSource::new(source))?;
        }
        Ok(session.finish())
    })();
    Box::new(NativeEdit {
        result,
        binary,
        partial,
    })
}

fn check_schema_references(input: &[u8]) -> ffi::BoolResult {
    bool_result(crate::BinaryDocument::new(input)
        .and_then(|document| crate::schema::references_are_local(document.root()?))
        .map(Some).map_err(|error| (ffi::Status::InvalidBinary, error.byte_offset as u64, 0)))
}

fn inspect_path(input: &[u8], mysql: bool) -> ffi::PathMetadataResult {
    use crate::sql_path::ItemMethod;
    let result = if mysql {
        crate::Path::parse_mysql(input).map(|path| {
            let node = match path.steps().last() {
                None => 1,
                Some(crate::PathStep::Member(_)) => 3,
                Some(crate::PathStep::MemberWildcard) => 4,
                Some(crate::PathStep::ArrayCell(_)) => 5,
                Some(crate::PathStep::ArrayRange(_, _)) => 6,
                Some(crate::PathStep::ArrayWildcard) => 7,
                Some(crate::PathStep::RecursiveDescent) => 8,
            };
            (path.can_match_many(), node)
        })
    } else {
        crate::SqlPath::parse(input).map(|path| {
            let node = match path.last_method() {
                Some(ItemMethod::Abs) => 13,
                Some(ItemMethod::Boolean) => 14,
                Some(ItemMethod::BooleanOnly) => 15,
                Some(ItemMethod::Ceiling) => 16,
                Some(ItemMethod::Date) => 17,
                Some(ItemMethod::Double) => 18,
                Some(ItemMethod::Floor) => 19,
                Some(ItemMethod::Length) => 20,
                Some(ItemMethod::Lower) => 21,
                Some(ItemMethod::Number) => 22,
                Some(ItemMethod::NumberOnly) => 23,
                Some(ItemMethod::Size) => 24,
                Some(ItemMethod::String) => 25,
                Some(ItemMethod::StringOnly) => 26,
                Some(ItemMethod::Timestamp) => 27,
                Some(ItemMethod::Type) => 28,
                Some(ItemMethod::Upper) => 29,
                None => 1,
            };
            (path.can_match_many(), node)
        })
    };
    match result {
        Ok((can_match_many, last_node)) => ffi::PathMetadataResult {
            status: ffi::Status::Ok, byte_offset: 0, too_many_nodes: false, can_match_many, last_node,
        },
        Err(error) => ffi::PathMetadataResult {
            status: ffi::Status::InvalidPath, byte_offset: error.byte_offset as u64,
            too_many_nodes: error.kind == crate::PathErrorKind::TooManyNodes,
            can_match_many: false, last_node: 0,
        },
    }
}

fn stored_value_result(result: Result<(u64, Vec<u8>), MutationError>) -> ffi::StoredValueResult {
    match result {
        Ok((size, binary)) => ffi::StoredValueResult { status: 0, source_status: 0, size, binary },
        Err(error) => ffi::StoredValueResult {
            status: mutation_error_status(&error), source_status: mutation_source_status(&error),
            size: 0, binary: Vec::new(),
        },
    }
}

fn stored_free_space(source: &ffi::BinarySource, offset: u64, raw_type: u8,
                     entry_type: u8) -> ffi::StoredValueResult {
    stored_value_result((|| {
        let source = CursorSource::new(source);
        let value = crate::mutation::editor::StoredValue::open(&source, offset, raw_type, entry_type)?;
        Ok((value.free_space()?, Vec::new()))
    })())
}

struct ContainerBuilder(crate::construction::ContainerBuilder, bool);

fn new_container_builder(object: bool) -> Box<ContainerBuilder> {
    Box::new(ContainerBuilder(if object { crate::construction::ContainerBuilder::object() }
                             else { crate::construction::ContainerBuilder::array() }, object))
}

fn build_error_status(error: crate::construction::BuildError) -> i32 {
    use crate::construction::BuildError;
    match error {
        BuildError::Allocation => 1,
        BuildError::Binary(_) | BuildError::InvalidKind => 2,
        BuildError::DuplicateKey => 3,
        BuildError::Depth => 4,
        BuildError::MissingKey => 5,
        BuildError::UnsupportedType => 6,
    }
}

impl ContainerBuilder {
    fn append_aggregate_binary(&mut self, key: &[u8], value: &[u8]) -> i32 {
        self.0.append_aggregate(key, value).err().map_or(0, build_error_status)
    }
    fn append_binary(&mut self, key: &[u8], value: &[u8]) -> i32 {
        let result = if self.1 { self.0.insert(key, value) } else { self.0.append(value) };
        result.err().map_or(0, build_error_status)
    }
    fn append_owned_binary(&mut self, key: &[u8], value: Vec<u8>) -> i32 {
        let result = if self.1 { self.0.insert_owned(key, value) } else { self.0.append_owned(value) };
        result.err().map_or(0, build_error_status)
    }
    fn approximate_length(&self) -> u64 { self.0.approximate_length() }
    fn rename_key(&mut self, old_key: &[u8], new_key: &[u8]) -> i32 {
        self.0.rename_key(old_key, new_key).err().map_or(0, build_error_status)
    }
    fn extend_binary(&mut self, input: &[u8]) -> i32 {
        self.0.extend(input).err().map_or(0, build_error_status)
    }
    fn build_container(&self, duplicate: ffi::Duplicate, sort_keys: bool, aggregate: bool, max_depth: u32) -> ffi::BuildResult {
        let duplicate = match duplicate {
            ffi::Duplicate::Overwrite => DuplicatePolicy::Overwrite,
            ffi::Duplicate::KeepFirst => DuplicatePolicy::KeepFirst,
            ffi::Duplicate::Reject => DuplicatePolicy::Reject,
            _ => DuplicatePolicy::Preserve,
        };
        match self.0.finish(duplicate, sort_keys, aggregate, if max_depth == 0 { None } else { Some(max_depth) }) {
            Ok(output) => ffi::BuildResult { status: 0, binary: output.binary, input_count: output.input_count, output_count: output.output_count },
            Err(error) => ffi::BuildResult { status: build_error_status(error), binary: Vec::new(), input_count: 0, output_count: 0 },
        }
    }
}

fn encode_scalar(kind: u8, integer: i64, unsigned_value: u64, real: f64, payload: &[u8], field_type: u16) -> ffi::BuildResult {
    use crate::construction::Scalar;
    let value = match kind {
        0 => Scalar::Null,
        2 => Scalar::Signed(integer),
        3 => Scalar::Unsigned(unsigned_value),
        4 => Scalar::Float(real),
        5 => Scalar::String(payload),
        8 => Scalar::Boolean(integer != 0),
        13 => Scalar::Opaque { field_type, bytes: payload },
        _ => Scalar::Database { kind, payload },
    };
    match value.encode() {
        Ok(binary) => ffi::BuildResult { status: 0, binary, input_count: 0, output_count: 0 },
        Err(error) => ffi::BuildResult { status: build_error_status(error), binary: Vec::new(), input_count: 0, output_count: 0 },
    }
}

struct MergeState {
    input_failed: bool,
    state: Option<
        Result<crate::mutation::merge::MergeAccumulator, crate::mutation::merge::MergeError>,
    >,
}

fn merge_error_status(error: &crate::mutation::merge::MergeError) -> i32 {
    match error {
        crate::mutation::merge::MergeError::Allocation => 1,
        crate::mutation::merge::MergeError::Binary(_) => 3,
        crate::mutation::merge::MergeError::InvalidPatch => 9,
    }
}

fn new_merge_accumulator(mode: u8) -> Box<MergeState> {
    use crate::mutation::merge::{MergeAccumulator, MergeError, MergeMode};
    let state = match mode {
        0 => Ok(MergeAccumulator::new(MergeMode::Preserve)),
        1 => Ok(MergeAccumulator::new(MergeMode::Patch)),
        2 => Ok(MergeAccumulator::new(MergeMode::OraclePatch)),
        _ => Err(MergeError::InvalidPatch),
    };
    Box::new(MergeState {
        state: Some(state),
        input_failed: false,
    })
}

impl MergeState {
    fn merge_status(&self) -> i32 {
        self.state
            .as_ref()
            .and_then(|state| state.as_ref().err())
            .map_or(0, merge_error_status)
    }
    fn merge_needs_input(&self) -> bool {
        self.state
            .as_ref()
            .is_some_and(|state| state.as_ref().is_ok_and(|state| state.needs_input()))
    }
    fn merge_input_failed(&self) -> bool {
        self.input_failed
    }
    fn merge_push(&mut self, input: &[u8], text: bool, sql_null: bool, max_depth: u32) -> i32 {
        use crate::mutation::merge::{MergeArgument, MergeError, MergeInput};
        self.input_failed = true;
        let argument = (|| {
            if sql_null {
                MergeArgument::prepare(MergeInput::SqlNull)
            } else {
                if text {
                    let parsed = crate::parse(input, ParseOptions { max_depth, ..ParseOptions::default() })
                        .map_err(|error| MergeError::Binary(crate::BinaryError::new(
                            crate::BinaryErrorKind::InvalidHeader, error.byte_offset)))?;
                    MergeArgument::from_document(parsed)
                } else {
                    let document = crate::BinaryDocument::new(input)?;
                    MergeArgument::prepare(MergeInput::Value(document.root()?))
                }
            }
        })();
        let previous = self.state.take().unwrap();
        let result = argument.and_then(|argument| {
            self.input_failed = false;
            previous.and_then(|merger| merger.push_prepared(argument))
        });
        if matches!(&result, Err(MergeError::InvalidPatch)) {
            self.input_failed = true;
        }
        self.state = Some(result);
        self.merge_status()
    }
}

#[allow(clippy::boxed_local)]
fn finish_merge(merger: Box<MergeState>) -> ffi::MergeOutput {
    let result = merger.state.unwrap().and_then(|state| state.finish());
    match result {
        Ok(binary) => ffi::MergeOutput {
            status: 0,
            is_null: binary.is_none(),
            binary: binary.unwrap_or_default(),
        },
        Err(error) => ffi::MergeOutput {
            status: merge_error_status(&error),
            is_null: false,
            binary: Vec::new(),
        },
    }
}


fn stored_metadata(source: &ffi::BinarySource, offset: u64, raw_type: u8,
                   entry_type: u8) -> ffi::StoredMetadataResult {
    use crate::mutation::editor::{StoredKind, StoredValue};
    let mut result = ffi::StoredMetadataResult { count: 1, entry_type, ..Default::default() };
    let read = (|| {
        let source = CursorSource::new(source);
        let value = StoredValue::open(&source, offset, raw_type, entry_type)?;
        let document = value.document_info();
        result.root_offset = document.root.offset;
        result.has_header = document.extension != 0;
        result.extension = document.extension;
        let metadata = value.metadata()?;
        result.encoded_size = metadata.encoded_size;
        result.value_type = metadata.value_type as u8;
        match metadata.kind {
            StoredKind::Null => {}
            StoredKind::Int(value) => { result.value_class = ffi::StoredValueClass::Int; result.int_value = value; }
            StoredKind::Uint(value) => { result.value_class = ffi::StoredValueClass::Uint; result.uint_value = value; }
            StoredKind::Double(value) => { result.value_class = ffi::StoredValueClass::Double; result.double_value = value; }
            StoredKind::Float(value) => { result.value_class = ffi::StoredValueClass::Float; result.float_value = value; }
            StoredKind::Boolean(value) => { result.value_class = ffi::StoredValueClass::Boolean; result.uint_value = u64::from(value); }
            StoredKind::Text(bytes) => {
                result.value_class = ffi::StoredValueClass::Text; result.count = bytes.end - bytes.start;
                result.data_offset = bytes.start - offset;
            }
            StoredKind::Opaque { field_type, bytes } => {
                result.value_class = ffi::StoredValueClass::Opaque; result.field_type = field_type;
                result.count = bytes.end - bytes.start; result.data_offset = bytes.start - offset;
            }
            StoredKind::Container(container) => {
                result.value_class = ffi::StoredValueClass::Container; result.count = container.count;
                result.entry_type = container.entry_type;
            }
            StoredKind::Decimal(_) => { result.value_class = ffi::StoredValueClass::Decimal; }
            StoredKind::Temporal(value) => { result.value_class = ffi::StoredValueClass::Temporal; result.int_value = value; }
        }
        Ok::<_, MutationError>(())
    })();
    if let Err(error) = read {
        result.status = mutation_error_status(&error); result.source_status = mutation_source_status(&error);
    }
    result
}

fn stored_location_result(result: Result<ffi::StoredLocationResult, MutationError>) -> ffi::StoredLocationResult {
    result.unwrap_or_else(|error| ffi::StoredLocationResult {
        status: mutation_error_status(&error), source_status: mutation_source_status(&error), ..Default::default()
    })
}

fn stored_child(source: &ffi::BinarySource, offset: u64, raw_type: u8,
                entry_type: u8, index: u64) -> ffi::StoredLocationResult {
    stored_location_result((|| {
        let source = CursorSource::new(source);
        let value = crate::mutation::editor::StoredValue::open(&source, offset, raw_type, entry_type)?;
        let child = value.child_position(index)?;
        Ok(ffi::StoredLocationResult { found: true, offset: child.offset,
            raw_type: child.raw_type, entry_type: child.entry_type, ..Default::default() })
    })())
}

fn stored_key(source: &ffi::BinarySource, offset: u64, raw_type: u8,
              entry_type: u8, index: u64) -> ffi::StoredLocationResult {
    stored_location_result((|| {
        let source = CursorSource::new(source);
        let value = crate::mutation::editor::StoredValue::open(&source, offset, raw_type, entry_type)?;
        let bytes = value.key_range(index)?;
        Ok(ffi::StoredLocationResult { found: true, offset: bytes.start,
            length: bytes.end - bytes.start, ..Default::default() })
    })())
}

fn stored_find_key(source: &ffi::BinarySource, offset: u64, raw_type: u8,
                   entry_type: u8, key: &[u8]) -> ffi::StoredLocationResult {
    stored_location_result((|| {
        let source = CursorSource::new(source);
        let value = crate::mutation::editor::StoredValue::open(&source, offset, raw_type, entry_type)?;
        let index = value.find_key(key)?;
        Ok(ffi::StoredLocationResult { found: index.is_some(), index: index.unwrap_or(0), ..Default::default() })
    })())
}

impl Default for ffi::StoredValueClass {
    fn default() -> Self { Self::Null }
}

fn stored_depth(source: &ffi::BinarySource, offset: u64, raw_type: u8,
                entry_type: u8) -> ffi::StoredValueResult {
    stored_value_result((|| {
        let source = CursorSource::new(source);
        let value = crate::mutation::editor::StoredValue::open(&source, offset, raw_type, entry_type)?;
        Ok((u64::from(value.depth()?), Vec::new()))
    })())
}

type BinaryWriter = crate::writer::BinaryWriter;
fn new_binary_writer() -> Box<BinaryWriter> { Box::default() }
fn writer_error(error: crate::writer::WriteError) -> i32 {
    match error { crate::writer::WriteError::Allocation => 1, crate::writer::WriteError::State => 2,
                  crate::writer::WriteError::Width => 3 }
}
fn writer_status(result: crate::writer::Result<()>) -> i32 {
    result.map_or_else(writer_error, |_| 0)
}
impl BinaryWriter {
    fn writer_begin(&mut self, object: bool, count: u64, width: u8) -> ffi::WriterBeginResult {
        match self.begin(object, count, width) {
            Ok(handle) => ffi::WriterBeginResult { status: 0, handle: handle.0 },
            Err(error) => ffi::WriterBeginResult { status: writer_error(error), handle: 0 },
        }
    }
    fn writer_key(&mut self, key: &[u8]) -> i32 { writer_status(self.key(key)) }
    fn writer_string(&mut self, value: &[u8]) -> i32 { writer_status(self.string(value)) }
    fn writer_double(&mut self, value: f64) -> i32 { writer_status(self.double(value)) }
    fn writer_end(&mut self, handle: u64) -> i32 { writer_status(self.end(crate::writer::ContainerId(handle))) }
}
#[allow(clippy::boxed_local)]
fn writer_finish(writer: Box<BinaryWriter>) -> ffi::WriterFinishResult {
    match writer.finish(false) {
        Ok(binary) => ffi::WriterFinishResult { status: 0, binary },
        Err(error) => ffi::WriterFinishResult { status: writer_error(error), binary: Vec::new() },
    }
}

fn stored_position(source: &ffi::BinarySource, offset: u64) -> ffi::StoredLocationResult {
    stored_location_result((|| {
        let source = CursorSource::new(source);
        let position = crate::mutation::editor::StoredValue::encoded_position(&source, offset)?;
        Ok(ffi::StoredLocationResult { found: true, offset: position.offset,
            raw_type: position.raw_type, entry_type: position.entry_type, ..Default::default() })
    })())
}

fn stored_document(source: &ffi::BinarySource, offset: u64, raw_type: u8,
                   entry_type: u8) -> ffi::StoredValueResult {
    stored_value_result((|| {
        let source = CursorSource::new(source);
        let value = crate::mutation::editor::StoredValue::open(&source, offset, raw_type, entry_type)?;
        let binary = value.to_document()?;
        Ok((binary.len() as u64, binary))
    })())
}

fn is_document_header(value: u8) -> bool { crate::binary::is_document_header(value) }


fn hash_binary(input: &[u8], seed: u64, capability: &ffi::HashCapability) -> ffi::HashResult {
    struct Adapter<'a>(&'a ffi::HashCapability);
    impl crate::hash::HashCapability for Adapter<'_> {
        type Error = (ffi::Status, i32);
        fn append_bytes(&mut self, seed: u64, input: &[u8]) -> u64 { self.0.append_bytes(seed, input) }
        fn hash_database_scalar(&mut self, seed: u64, value: crate::BinaryValue<'_>) -> Result<u64, Self::Error> {
            use crate::{JsonType, Number};
            let (mut integer, mut unsigned_value, mut real, mut payload) = (0, 0, 0.0, &[][..]);
            let kind = value.json_type().map_err(|_| (ffi::Status::InvalidBinary, 0))?;
            let decoded = match kind {
                JsonType::Int | JsonType::OInt | JsonType::Uint | JsonType::OLong
                | JsonType::Double | JsonType::ODouble | JsonType::OFloat => value.number().map(|number| match number {
                    Number::I64(value) => integer = value,
                    Number::U64(value) => unsigned_value = value,
                    Number::F64(value) => real = value,
                }),
                JsonType::Decimal | JsonType::ODecimal => value.decimal_bytes().map(|bytes| payload = bytes),
                JsonType::Opaque => value.opaque_bytes().map(|bytes| payload = bytes),
                _ => value.temporal().map(|value| integer = value),
            };
            decoded.map_err(|_| (ffi::Status::InvalidBinary, 0))?;
            let result = self.0.hash_database_scalar(seed, kind as u8, integer, unsigned_value, real, payload);
            if result.status == ffi::Status::Ok && result.platform_error == 0 { Ok(result.value) }
            else { Err((result.status, result.platform_error)) }
        }
    }
    let result = (|| {
        let document = crate::BinaryDocument::new(input)?;
        crate::hash::hash(document.root()?, seed, &mut Adapter(capability))
    })();
    match result {
        Ok(value) => ffi::HashResult { status: ffi::Status::Ok, platform_error: 0, value },
        Err(crate::hash::HashError::Binary(_)) => ffi::HashResult { status: ffi::Status::InvalidBinary, platform_error: 0, value: seed },
        Err(crate::hash::HashError::Scalar((status, platform_error))) => ffi::HashResult { status, platform_error, value: seed },
    }
}
