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

use std::{
    collections::{BTreeMap, BTreeSet, HashMap},
    env,
    error::Error,
    fs,
    ops::Range,
};

use wasm_encoder::{
    BlockType, CodeSection, ElementSection, Elements, Encode, ExportKind, ExportSection, Function,
    FunctionSection, Instruction, Module, RawSection, RefType, TableSection, TableType,
    TypeSection, ValType,
};
use wasmparser::{
    ConstExpr, Element, ElementItems, ElementKind, ExternalKind, FuncType, FunctionBody, Operator,
    Parser, Payload, TableInit, TypeRef, Validator, WasmFeatures,
};

type Result<T> = std::result::Result<T, Box<dyn Error>>;

const CALL: u8 = 0x10;
const RETURN_CALL: u8 = 0x12;
const SEARCH_LIMIT: usize = 8;
const COMPARE_LIMIT: usize = 3;

enum Section {
    Raw(u8, Range<usize>),
    Type,
    Function,
    Table,
    Export,
    Element,
    Code,
}

#[derive(Default)]
struct Input<'a> {
    sections: Vec<Section>,
    types: Vec<FuncType>,
    function_types: Vec<u32>,
    imported_functions: usize,
    tables: usize,
    slots: Vec<(u32, u32)>,
    exports: Vec<(&'a str, ExportKind, u32)>,
    bodies: Vec<FunctionBody<'a>>,
}

struct Dispatcher<'a> {
    type_index: u32,
    arity: u32,
    slots: &'a [(u32, u32)],
}

fn main() -> Result<()> {
    let arguments: Vec<String> = env::args().skip(1).collect();
    let [input, output] = arguments.as_slice() else {
        return Err("usage: dispatch_call_indirect <input.wasm> <output.wasm>".into());
    };
    fs::write(output, rewrite(&fs::read(input)?)?)?;
    Ok(())
}

fn rewrite(wasm: &[u8]) -> Result<Vec<u8>> {
    let input = parse(wasm)?;
    let mut type_indices = HashMap::new();
    let canonical: Vec<u32> = input
        .types
        .iter()
        .enumerate()
        .map(|(index, ty)| *type_indices.entry(ty.clone()).or_insert(index as u32))
        .collect();
    let (called, referenced) = scan(&input, &canonical)?;

    let mut slots: HashMap<u32, Vec<(u32, u32)>> = HashMap::new();
    for &(slot, function) in &input.slots {
        let ty = canonical[input.function_types[function as usize] as usize];
        slots.entry(ty).or_default().push((slot, function));
    }
    for list in slots.values_mut() {
        list.sort_unstable();
    }

    let mut added_types = Vec::new();
    let mut dispatchers = Vec::new();
    let mut dispatcher_functions = HashMap::new();
    for &ty in &called {
        let signature = &input.types[ty as usize];
        let dispatcher_type = FuncType::new(
            signature
                .params()
                .iter()
                .copied()
                .chain([wasmparser::ValType::I32]),
            signature.results().iter().copied(),
        );
        let next_type = (input.types.len() + added_types.len()) as u32;
        let type_index = *type_indices
            .entry(dispatcher_type.clone())
            .or_insert_with(|| {
                added_types.push(dispatcher_type);
                next_type
            });
        dispatcher_functions.insert(ty, (input.function_types.len() + dispatchers.len()) as u32);
        dispatchers.push(Dispatcher {
            type_index,
            arity: signature.params().len() as u32,
            slots: slots.get(&ty).map_or(&[], Vec::as_slice),
        });
    }

    let mut module = Module::new();
    for section in &input.sections {
        match section {
            Section::Raw(id, range) => {
                module.section(&RawSection {
                    id: *id,
                    data: &wasm[range.clone()],
                });
            }
            Section::Type => {
                let mut types = TypeSection::new();
                for ty in input.types.iter().chain(&added_types) {
                    encode_type(&mut types, ty)?;
                }
                module.section(&types);
            }
            Section::Function => {
                let mut functions = FunctionSection::new();
                for &ty in &input.function_types[input.imported_functions..] {
                    functions.function(ty);
                }
                for dispatcher in &dispatchers {
                    functions.function(dispatcher.type_index);
                }
                module.section(&functions);
            }
            Section::Table => {
                let mut tables = TableSection::new();
                tables.table(TableType {
                    element_type: RefType::FUNCREF,
                    table64: false,
                    minimum: 1,
                    maximum: Some(1),
                    shared: false,
                });
                module.section(&tables);
            }
            Section::Export => {
                let mut exports = ExportSection::new();
                for &(name, kind, index) in &input.exports {
                    exports.export(name, kind, index);
                }
                module.section(&exports);
            }
            Section::Element => {
                if !referenced.is_empty() {
                    let mut elements = ElementSection::new();
                    elements.declared(Elements::Functions(
                        referenced.iter().copied().collect::<Vec<_>>().into(),
                    ));
                    module.section(&elements);
                }
            }
            Section::Code => {
                let mut code = CodeSection::new();
                for body in &input.bodies {
                    code.raw(&rewrite_body(
                        wasm,
                        body,
                        &canonical,
                        &dispatcher_functions,
                    )?);
                }
                for dispatcher in &dispatchers {
                    code.function(&dispatch(dispatcher.arity, dispatcher.slots));
                }
                module.section(&code);
            }
        }
    }
    let output = module.finish();
    Validator::new_with_features(WasmFeatures::all()).validate_all(&output)?;
    Ok(output)
}

fn parse(wasm: &[u8]) -> Result<Input<'_>> {
    let mut input = Input::default();
    for payload in Parser::new(0).parse_all(wasm) {
        let payload = payload?;
        let raw = payload
            .as_section()
            .map(|(id, range)| Section::Raw(id, range.start as usize..range.end as usize));
        match payload {
            Payload::TypeSection(reader) => {
                for ty in reader.into_iter_err_on_gc_types() {
                    input.types.push(ty?);
                }
                input.sections.push(Section::Type);
            }
            Payload::ImportSection(reader) => {
                for import in reader.into_imports() {
                    match import?.ty {
                        TypeRef::Func(ty) => {
                            input.function_types.push(ty);
                            input.imported_functions += 1;
                        }
                        TypeRef::FuncExact(_) | TypeRef::Table(_) => {
                            return Err("unsupported import".into());
                        }
                        _ => {}
                    }
                }
                input.sections.extend(raw);
            }
            Payload::FunctionSection(reader) => {
                for ty in reader {
                    input.function_types.push(ty?);
                }
                input.sections.push(Section::Function);
            }
            Payload::TableSection(reader) => {
                for table in reader {
                    let table = table?;
                    if table.ty.element_type != wasmparser::RefType::FUNCREF
                        || table.ty.table64
                        || table.ty.shared
                        || !matches!(table.init, TableInit::RefNull)
                    {
                        return Err("unsupported table".into());
                    }
                    input.tables += 1;
                }
                input.sections.push(Section::Table);
            }
            Payload::GlobalSection(reader) => {
                for global in reader {
                    if references_function(&global?.init_expr)? {
                        return Err("ref.func in a global initializer is not supported".into());
                    }
                }
                input.sections.extend(raw);
            }
            Payload::ExportSection(reader) => {
                for export in reader {
                    let export = export?;
                    input
                        .exports
                        .push((export.name, export_kind(export.kind)?, export.index));
                }
                input.sections.push(Section::Export);
            }
            Payload::ElementSection(reader) => {
                for element in reader {
                    read_slots(element?, &mut input.slots)?;
                }
                input.sections.push(Section::Element);
            }
            Payload::CodeSectionStart { .. } => input.sections.push(Section::Code),
            Payload::CodeSectionEntry(body) => input.bodies.push(body),
            _ => input.sections.extend(raw),
        }
    }
    if input.tables != 1 {
        return Err("expected exactly one table".into());
    }
    Ok(input)
}

fn read_slots(element: Element, slots: &mut Vec<(u32, u32)>) -> Result<()> {
    let ElementKind::Active {
        table_index: None | Some(0),
        offset_expr,
    } = element.kind
    else {
        return Err("unsupported element segment".into());
    };
    let Operator::I32Const { value } = offset_expr.get_operators_reader().read()? else {
        return Err("element segment offset is not a constant".into());
    };
    let base = value as u32;
    match element.items {
        ElementItems::Functions(functions) => {
            for (position, function) in functions.into_iter().enumerate() {
                slots.push((base + position as u32, function?));
            }
        }
        ElementItems::Expressions(_, expressions) => {
            for (position, expression) in expressions.into_iter().enumerate() {
                match expression?.get_operators_reader().read()? {
                    Operator::RefFunc { function_index } => {
                        slots.push((base + position as u32, function_index));
                    }
                    Operator::RefNull { .. } => {}
                    _ => return Err("unsupported element expression".into()),
                }
            }
        }
    }
    Ok(())
}

fn references_function(expression: &ConstExpr) -> Result<bool> {
    let mut operators = expression.get_operators_reader();
    while !operators.eof() {
        if let Operator::RefFunc { .. } = operators.read()? {
            return Ok(true);
        }
    }
    Ok(false)
}

fn export_kind(kind: ExternalKind) -> Result<ExportKind> {
    Ok(match kind {
        ExternalKind::Func => ExportKind::Func,
        ExternalKind::Table => ExportKind::Table,
        ExternalKind::Memory => ExportKind::Memory,
        ExternalKind::Global => ExportKind::Global,
        ExternalKind::Tag => ExportKind::Tag,
        ExternalKind::FuncExact => return Err("unsupported export".into()),
    })
}

fn encode_type(types: &mut TypeSection, ty: &FuncType) -> Result<()> {
    let params = ty
        .params()
        .iter()
        .map(|&ty| value_type(ty))
        .collect::<Result<Vec<_>>>()?;
    let results = ty
        .results()
        .iter()
        .map(|&ty| value_type(ty))
        .collect::<Result<Vec<_>>>()?;
    types.ty().function(params, results);
    Ok(())
}

fn value_type(ty: wasmparser::ValType) -> Result<ValType> {
    Ok(match ty {
        wasmparser::ValType::I32 => ValType::I32,
        wasmparser::ValType::I64 => ValType::I64,
        wasmparser::ValType::F32 => ValType::F32,
        wasmparser::ValType::F64 => ValType::F64,
        wasmparser::ValType::V128 => ValType::V128,
        wasmparser::ValType::Ref(reference) if reference == wasmparser::RefType::FUNCREF => {
            ValType::Ref(RefType::FUNCREF)
        }
        wasmparser::ValType::Ref(reference) if reference == wasmparser::RefType::EXTERNREF => {
            ValType::Ref(RefType::EXTERNREF)
        }
        other => return Err(format!("unsupported value type {other}").into()),
    })
}

fn scan(input: &Input, canonical: &[u32]) -> Result<(BTreeSet<u32>, BTreeSet<u32>)> {
    let mut called = BTreeSet::new();
    let mut referenced = BTreeSet::new();
    for body in &input.bodies {
        let mut operators = body.get_operators_reader()?;
        while !operators.eof() {
            match operators.read()? {
                Operator::CallIndirect { type_index, .. }
                | Operator::ReturnCallIndirect { type_index, .. } => {
                    called.insert(canonical[type_index as usize]);
                }
                Operator::RefFunc { function_index } => {
                    referenced.insert(function_index);
                }
                Operator::TableGet { .. }
                | Operator::TableSet { .. }
                | Operator::TableGrow { .. }
                | Operator::TableSize { .. }
                | Operator::TableFill { .. }
                | Operator::TableCopy { .. }
                | Operator::TableInit { .. }
                | Operator::ElemDrop { .. } => {
                    return Err("instructions that use the table directly are not supported".into());
                }
                _ => {}
            }
        }
    }
    Ok((called, referenced))
}

fn rewrite_body(
    wasm: &[u8],
    body: &FunctionBody,
    canonical: &[u32],
    dispatcher_functions: &HashMap<u32, u32>,
) -> Result<Vec<u8>> {
    let range = body.range();
    let mut copied = range.start as usize;
    let mut output = Vec::with_capacity((range.end - range.start) as usize);
    let mut operators = body.get_operators_reader()?;
    while !operators.eof() {
        let start = operators.original_position() as usize;
        let (opcode, type_index) = match operators.read()? {
            Operator::CallIndirect { type_index, .. } => (CALL, type_index),
            Operator::ReturnCallIndirect { type_index, .. } => (RETURN_CALL, type_index),
            _ => continue,
        };
        output.extend_from_slice(&wasm[copied..start]);
        output.push(opcode);
        dispatcher_functions[&canonical[type_index as usize]].encode(&mut output);
        copied = operators.original_position() as usize;
    }
    output.extend_from_slice(&wasm[copied..range.end as usize]);
    Ok(output)
}

fn dispatch(arity: u32, slots: &[(u32, u32)]) -> Function {
    let mut function = Function::new([]);
    if slots.len() > SEARCH_LIMIT {
        jump_by_bucket(&mut function, arity, slots);
    } else if !slots.is_empty() {
        search(&mut function, arity, slots);
    }
    function.instruction(&Instruction::Unreachable);
    function.instruction(&Instruction::End);
    function
}

fn search(function: &mut Function, arity: u32, slots: &[(u32, u32)]) {
    if let [(slot, target)] = slots {
        call_if_equal(function, arity, *slot, *target);
        return;
    }
    let (lower, upper) = slots.split_at(slots.len() / 2);
    function.instruction(&Instruction::LocalGet(arity));
    function.instruction(&Instruction::I32Const(upper[0].0 as i32));
    function.instruction(&Instruction::I32LtU);
    function.instruction(&Instruction::If(BlockType::Empty));
    search(function, arity, lower);
    function.instruction(&Instruction::Unreachable);
    function.instruction(&Instruction::End);
    search(function, arity, upper);
}

fn jump_by_bucket(function: &mut Function, arity: u32, slots: &[(u32, u32)]) {
    let mut buckets: BTreeMap<u32, Vec<(u32, u32)>> = BTreeMap::new();
    for &(slot, target) in slots {
        buckets.entry(slot >> 8).or_default().push((slot, target));
    }
    let keys: Vec<u32> = buckets.keys().copied().collect();
    let groups: Vec<Vec<(u32, u32)>> = buckets.into_values().collect();
    let bucket = [
        Instruction::LocalGet(arity),
        Instruction::I32Const(8),
        Instruction::I32ShrU,
    ];
    jump_table(function, &bucket, &keys, |function, position| {
        let group = &groups[position];
        if group.len() <= COMPARE_LIMIT {
            for &(slot, target) in group {
                call_if_equal(function, arity, slot, target);
            }
        } else {
            let offsets: Vec<u32> = group.iter().map(|&(slot, _)| slot & 0xff).collect();
            let offset = [
                Instruction::LocalGet(arity),
                Instruction::I32Const(0xff),
                Instruction::I32And,
            ];
            jump_table(function, &offset, &offsets, |function, position| {
                call_and_return(function, arity, group[position].1);
            });
        }
        function.instruction(&Instruction::Unreachable);
    });
}

fn jump_table(
    function: &mut Function,
    key: &[Instruction],
    cases: &[u32],
    mut emit: impl FnMut(&mut Function, usize),
) {
    for _ in 0..=cases.len() {
        function.instruction(&Instruction::Block(BlockType::Empty));
    }
    for instruction in key {
        function.instruction(instruction);
    }
    let default = cases.len() as u32;
    let mut targets = vec![default; cases[cases.len() - 1] as usize + 1];
    for (depth, &case) in cases.iter().enumerate() {
        targets[case as usize] = depth as u32;
    }
    function.instruction(&Instruction::BrTable(targets.into(), default));
    for position in 0..cases.len() {
        function.instruction(&Instruction::End);
        emit(function, position);
    }
    function.instruction(&Instruction::End);
}

fn call_if_equal(function: &mut Function, arity: u32, slot: u32, target: u32) {
    function.instruction(&Instruction::LocalGet(arity));
    function.instruction(&Instruction::I32Const(slot as i32));
    function.instruction(&Instruction::I32Eq);
    function.instruction(&Instruction::If(BlockType::Empty));
    call_and_return(function, arity, target);
    function.instruction(&Instruction::End);
}

fn call_and_return(function: &mut Function, arity: u32, target: u32) {
    for parameter in 0..arity {
        function.instruction(&Instruction::LocalGet(parameter));
    }
    function.instruction(&Instruction::Call(target));
    function.instruction(&Instruction::Return);
}
