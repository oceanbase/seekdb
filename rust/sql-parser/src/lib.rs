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

mod access;
mod batch;
mod budget;
mod parameterize;
mod prefix;
mod bridge;
mod command;
mod load_data;
mod select_into;
mod ddl;
mod defaults;
mod diagnostic;
mod embedded_sql;
pub mod expression;
mod fts;
// PEG adds framework arguments to rules with explicit parsing state.
#[allow(clippy::too_many_arguments)]
mod grammar;
mod inspect;
mod lex;
mod node;
pub mod numeric;
mod parser;
pub mod query;
mod routine;
mod scan;
mod statistics;

pub use bridge::ffi::{
    DefaultExpression, DefaultForm, SqlMode, ScanOptions, LiteralKind, Literal, ValuesPosition,
    ScanResult, NodeKind, Span, ParseDiagnostic, TransactionAccess, TransactionOptions,
    ColumnReference, ExpressionInfo, StatementSplits, StackBounds, ParseMode, ParseErrorKind, SearchOccurrence,
    AccountKind, AccountNameForm, AccountIdentity,
    AuthenticationKind, RoleSelection, PrivilegeScope, PrivilegeObject, AccessOptions,
    PrivilegeTarget, ExplainFormat, ExplainDisplay, SetOperator, AliasForm,
    SortDirection, IntoPosition, InsertSource, LockKind, LockWait,
    JoinKind, JoinCondition, SampleMethod, SampleScope, IndexHintKind,
    BinaryNotation, ExpressionLiteral, UnaryOperator, BinaryOperator, MembershipSource,
    ComparisonQuantifier, AggregateQuantifier, VariableScope, TypeName, GeometryType,
    TypeSyntax, NullTreatment, NthOrigin, FrameUnit, FrameBound,
    VectorMetric, FulltextMode, JsonResponse, JsonScalars, JsonWrapper,
    JsonColumnKind, JsonOptions, ParallelPolicy, ReadConsistency, PlanCachePolicy,
    MergeDirection, RoutineDataAccess, ViewCheck, ViewDefinition, TransactionIsolation,
    TransactionSetting, CacheKind, CacheFilter, SqlReferenceKind, SqlReference,
    SqlSymbol, SqlSymbolBinding, EmbeddedSql, ParameterMode, ParameterOptions,
    ParameterAction, ParameterError, ParameterOrigin, ParameterLoweringKind, ParameterScalePolicy,
    ParameterRepresentation, ParameterTypeCheck, NumericSignMode, NumericSignResult, ParameterToken,
    ParameterValue, ParameterTokenPolicy, ParameterBindingRecord, ParameterBinding, NumericValue, ParameterLowering, ParameterProjection, ProjectionNames,
    Parameterization, ParameterSql, ParameterClassification, StatementClass, TransactionCommand,
    PlPrefix, StatementInfo, RestoreParameter, RestoredParameter, RestoredSql,
    BatchCompression, TableSelectMode, ColumnGeneration, ColumnPosition, IndexKind,
    IndexMethod, PartitionKind, PartitionValues, ReferenceEvent, ReferenceAction,
    DropBehavior, TableOrganization, TableRowFormat, TableDefinition, ColumnDefinition,
    IndexDefinition, PartitionDefinition, PartitionElement, ForeignKeyDefinition, ReferentialAction,
    LoadDataDefinition, LoadDuplicate, LoadLocation, LoadFormatKind, LoadFormatOption,
    IntoKind, FileOutputDefinition, FileOptionKind, FileOption, ExternalFormatKind, ExternalFormatOption,
    ShowFilter, LiteralReference, StatisticsScope, StatisticsSizeKind, StatisticsColumnKind,
};
pub use node::Node;
pub use parser::{parse, Parsed};
pub use diagnostic::ParseFailure;
pub use inspect::syntax_literals;
pub use batch::{compress_insert, compress_values, restore_parameter_sql};
pub use prefix::{classify_statement, explain_body_offset, normalized_sql_length, is_single_statement, split_sql, fast_parameterization_allowed, transaction_command, insert_outline_hint};
pub use parameterize::{projection_names, resolve_unary, UnaryOperand, UnaryResolution, ParameterEffects, parameterize, classify_parameter_tokens, rewrite_parameter_sql, fold_parameter_in_lists, unchecked_parameter_types};
pub use scan::{begin_scan, finish_scan, Scanner};
pub use fts::{parse_search_query, SearchNode, SearchTree};
pub use embedded_sql::{embedded_sql_symbols, rewrite_embedded_sql};
pub use lex::{decode_binary_literal, identifier_requires_quotes, normalize_numeric_sign, integer_literal_is_nonnegative};

pub use defaults::{default_expression, default_literal, default_arguments, DefaultArguments};
