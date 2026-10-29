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

//! Instance policy, immutable request overrides and the sole active context.
//! The C++ level-map class only owns an opaque Rust handle. Entering a request
//! retains the immutable map, so no borrowed session pointer can escape.
use std::cell::{Cell, RefCell};
use std::ffi::{c_char, CStr};
use std::sync::atomic::{AtomicI64, AtomicI8, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

pub const MODULES: usize = 64;
const WIDTH: usize = 65;
const DEFAULT_INDEX: usize = MODULES * WIDTH;
const INVALID_ARGUMENT: i32 = -4002;
const INVALID_MODULE: i32 = -4168;
const UNKNOWN_MODULE: i32 = -4169;
const INVALID_LEVEL: i32 = -4170;
const SYNTAX: i32 = -4171;
pub const NAMES: [&str; 7] = ["ERROR", "WARN", "INFO", "EDIAG", "WDIAG", "TRACE", "DEBUG"];
static LEVELS: [AtomicI8; DEFAULT_INDEX + 1] = [const { AtomicI8::new(0) }; DEFAULT_INDEX + 1];
static CONFIG: Mutex<()> = Mutex::new(());
static VERSION: AtomicI64 = AtomicI64::new(0);
static NEXT_MAP: AtomicU64 = AtomicU64::new(1);
static NAMES_MAP: Mutex<Vec<(i32, i32, String)>> = Mutex::new(Vec::new());

// Options are owned here even before files/the asynchronous backend exist.
pub const ASYNC: usize = 0;
pub const STOP_APPEND: usize = 1;
pub const PERF: usize = 2;
pub const IGNORE_REQUEST_LEVEL: usize = 3;
pub const FORCE_CHECK: usize = 4;
pub const INFO_AS_WDIAG: usize = 5;
pub const STOPPED: usize = 6;
pub const INITIALIZED: usize = 7;
pub const ALERT_LEVEL: usize = 8;
static OPTIONS: [AtomicI64; 9] = [
    AtomicI64::new(1),
    AtomicI64::new(0),
    AtomicI64::new(0),
    AtomicI64::new(0),
    AtomicI64::new(0),
    AtomicI64::new(1),
    AtomicI64::new(1),
    AtomicI64::new(0),
    AtomicI64::new(2),
];

#[derive(Clone)]
struct LevelMap {
    base: i8,
    modules: Option<Box<[i8; DEFAULT_INDEX]>>,
}
impl Default for LevelMap {
    fn default() -> Self {
        Self {
            base: 2,
            modules: None,
        }
    }
}
impl LevelMap {
    fn get(&self, parent: i32, sub: i32) -> i8 {
        match index(parent, sub) {
            Some(DEFAULT_INDEX) => self.base,
            Some(i) => self.modules.as_ref().map_or(self.base, |levels| levels[i]),
            None => -1,
        }
    }
    fn set(&mut self, parent: i32, sub: i32, level: i8) {
        if parent < 0 {
            self.base = level;
            self.modules = None;
        } else {
            let levels = self
                .modules
                .get_or_insert_with(|| Box::new([self.base; DEFAULT_INDEX]));
            let start = parent as usize * WIDTH;
            if sub < 0 {
                levels[start..start + WIDTH].fill(level);
            } else {
                levels[start + sub as usize + 1] = level;
            }
        }
    }
}
pub struct MapHandle {
    id: u64,
    map: Arc<LevelMap>,
}

pub struct Context {
    map: Option<Arc<LevelMap>>,
    map_id: u64,
    pub level: i8,
    pub disabled: bool,
    pub guard: bool,
    pub trace_mode: bool,
    pub last_cost: u64,
    pub trace_buffer: Vec<u8>,
    dba_trace: [[u64; 4]; 2],
    dba_count: [u64; 2],
}
impl Default for Context {
    fn default() -> Self {
        Self {
            map: None,
            map_id: 0,
            level: 7,
            disabled: false,
            guard: false,
            trace_mode: false,
            last_cost: 0,
            trace_buffer: Vec::new(),
            dba_trace: [[0; 4]; 2],
            dba_count: [0; 2],
        }
    }
}
thread_local! {
    pub static CONTEXT: RefCell<Context> = RefCell::new(Context::default());
    // Separate fixed-size storage allows a stable read-only C ABI view without
    // keeping a RefCell borrow alive across C++ calls. Mutation is only by FFI.
    pub static TRACE: Cell<[u64; 4]> = const { Cell::new([0; 4]) };
}

// Foreign thread-local destructors can run after the Rust context destructor.
// Suppress their records instead of panicking across the C ABI.
pub fn with_context<R: Default>(f: impl FnOnce(&RefCell<Context>) -> R) -> R {
    CONTEXT.try_with(f).unwrap_or_default()
}
pub fn context_available() -> bool {
    CONTEXT.try_with(|_| ()).is_ok()
}

fn index(parent: i32, sub: i32) -> Option<usize> {
    if parent == -1 && sub == -1 {
        Some(DEFAULT_INDEX)
    } else if (0..64).contains(&parent) && (-1..64).contains(&sub) {
        Some(parent as usize * WIDTH + (sub + 1) as usize)
    } else {
        None
    }
}
pub fn option(key: usize) -> i64 {
    OPTIONS
        .get(key)
        .map_or(0, |value| value.load(Ordering::Relaxed))
}
pub fn set_option(key: usize, value: i64) -> i32 {
    let Some(target) = OPTIONS.get(key) else {
        return INVALID_ARGUMENT;
    };
    if key == ALERT_LEVEL && !(0..=2).contains(&value) {
        return INVALID_LEVEL;
    }
    target.store(value, Ordering::Relaxed);
    0
}

pub fn current_level(parent: i32, sub: i32) -> i8 {
    // PERF is a frequent rejection path: no mutex, allocation or TLS access.
    if option(PERF) != 0 {
        return 0;
    }
    if option(IGNORE_REQUEST_LEVEL) == 0 {
        let override_level = with_context(|ctx| {
            let ctx = ctx.borrow();
            if let Some(map) = &ctx.map {
                Some(map.get(parent, sub))
            } else if ctx.level != 7 {
                Some(ctx.level)
            } else {
                None
            }
        });
        if let Some(level) = override_level {
            return level;
        }
    }
    index(parent, sub).map_or(-1, |i| LEVELS[i].load(Ordering::Relaxed))
}

// Rust's explicit targets use the existing C++ module names (SQL, SQL.ENG).
// Unregistered targets, including third-party module paths such as mio::poll,
// follow the request/instance default. No new module table or level state.
pub(crate) fn target_level(target: &str) -> i8 {
    if option(PERF) != 0 {
        return 0;
    }
    if target.contains("::") {
        return current_level(-1, -1);
    }
    let (parent, sub) = target
        .split_once('.')
        .map_or((target, None), |(p, s)| (p, Some(s)));
    let ids = {
        let names = NAMES_MAP.lock().unwrap();
        names
            .iter()
            .find(|(_, s, n)| *s == -1 && n.eq_ignore_ascii_case(parent))
            .and_then(|(p, _, _)| match sub {
                None => Some((*p, -1)),
                Some(sub) => names
                    .iter()
                    .find(|(id, s, n)| id == p && *s >= 0 && n.eq_ignore_ascii_case(sub))
                    .map(|(_, s, _)| (*p, *s)),
            })
    };
    let (parent, sub) = ids.unwrap_or((-1, -1));
    current_level(parent, sub)
}

pub fn get_level(parent: i32, sub: i32, request: i32) -> i32 {
    if request != 0 {
        current_level(parent, sub) as i32
    } else {
        index(parent, sub).map_or(-1, |i| LEVELS[i].load(Ordering::Relaxed) as i32)
    }
}
pub fn option_value(key: usize, value: i64, write: i32) -> i64 {
    if write != 0 {
        set_option(key, value) as i64
    } else {
        option(key)
    }
}

pub unsafe fn map_clone(handle: *const MapHandle) -> *mut MapHandle {
    if handle.is_null() {
        std::ptr::null_mut()
    } else {
        Box::into_raw(Box::new(MapHandle {
            id: NEXT_MAP.fetch_add(1, Ordering::Relaxed),
            map: (*handle).map.clone(),
        }))
    }
}
pub unsafe fn map_free(handle: *mut MapHandle) {
    if !handle.is_null() {
        drop(Box::from_raw(handle));
    }
}
pub unsafe fn map_level(handle: *const MapHandle, parent: i32, sub: i32) -> i32 {
    if handle.is_null() {
        LevelMap::default().get(parent, sub) as i32
    } else {
        (*handle).map.get(parent, sub) as i32
    }
}
pub unsafe fn map_set(handle: *mut *mut MapHandle, parent: i32, sub: i32, level: i32) -> i32 {
    if handle.is_null() || index(parent, sub).is_none() {
        return INVALID_MODULE;
    }
    if !(-1..=7).contains(&level) {
        return INVALID_LEVEL;
    }
    if (*handle).is_null() {
        *handle = Box::into_raw(Box::new(MapHandle {
            id: NEXT_MAP.fetch_add(1, Ordering::Relaxed),
            map: Arc::new(LevelMap::default()),
        }));
    }
    let handle = &mut **handle;
    Arc::make_mut(&mut handle.map).set(parent, sub, level as i8);
    with_context(|ctx| {
        let mut ctx = ctx.borrow_mut();
        if ctx.map_id == handle.id {
            ctx.map = Some(handle.map.clone());
        }
    });
    0
}

pub unsafe fn context_map(handle: *const MapHandle) {
    with_context(|ctx| {
        let mut ctx = ctx.borrow_mut();
        if handle.is_null() {
            ctx.map = None;
            ctx.map_id = 0;
            ctx.level = 7;
        } else {
            ctx.map = Some((*handle).map.clone());
            ctx.map_id = (*handle).id;
            let handle = &*handle;
            ctx.level = handle.map.base;
        }
    });
}

// Keys: scalar override, recursion suppression, macro guard, trace mode,
// previous logging cost.
pub fn context_value(key: usize, value: i64, write: i32) -> i64 {
    if !context_available() {
        return if key == 1 || key == 2 { 1 } else { 0 };
    }
    with_context(|ctx| {
        let mut ctx = ctx.borrow_mut();
        let old = match key {
            0 => ctx.level as i64,
            1 => ctx.disabled as i64,
            2 => ctx.guard as i64,
            3 => ctx.trace_mode as i64,
            4 => ctx.last_cost as i64,
            _ => return INVALID_ARGUMENT as i64,
        };
        if write != 0 {
            match key {
                0 if (-1..=7).contains(&value) => ctx.level = value as i8,
                1 => ctx.disabled = value != 0,
                2 => ctx.guard = value != 0,
                3 => {
                    ctx.trace_mode = value != 0;
                    if !ctx.trace_mode {
                        ctx.trace_buffer = Vec::new();
                    }
                }
                4 => ctx.last_cost = value.max(0) as u64,
                _ => return INVALID_ARGUMENT as i64,
            }
        }
        old
    })
}
pub fn trace_id() -> *const u64 {
    TRACE.with(|trace| trace.as_ptr().cast())
}
pub unsafe fn set_trace_id(words: *const u64) {
    let value = if words.is_null() {
        [0; 4]
    } else {
        std::ptr::read(words.cast::<[u64; 4]>())
    };
    TRACE.with(|trace| trace.set(value));
}

pub fn dba_enabled(level: i32, force: i32) -> i32 {
    if !(0..=2).contains(&level) || level as i64 > option(ALERT_LEVEL) {
        return 0;
    }
    if level == 2 || force != 0 {
        return 1;
    }
    let trace = TRACE.with(Cell::get);
    if trace == [0; 4] {
        return 1;
    }
    with_context(|ctx| {
        let mut ctx = ctx.borrow_mut();
        let i = level as usize;
        if ctx.dba_trace[i] != trace {
            ctx.dba_trace[i] = trace;
            ctx.dba_count[i] = 1;
            1
        } else {
            ctx.dba_count[i] += 1;
            // Negative multiples preserve the existing diagnostic callback
            // performed actively by the C++ caller, outside Rust state borrows.
            if ctx.dba_count[i].is_multiple_of(20) {
                -(ctx.dba_count[i] as i32)
            } else {
                0
            }
        }
    })
}

pub unsafe fn register_module(parent: i32, sub: i32, name: *const c_char) -> i32 {
    if !(0..64).contains(&parent) || !(-1..64).contains(&sub) || name.is_null() {
        return INVALID_ARGUMENT;
    }
    let value = CStr::from_ptr(name).to_string_lossy().into_owned();
    let mut names = NAMES_MAP.lock().unwrap();
    if let Some(entry) = names.iter_mut().find(|(p, s, _)| *p == parent && *s == sub) {
        entry.2 = value;
    } else {
        names.push((parent, sub, value));
    }
    0
}
pub unsafe fn find_module(
    parent_name: *const c_char,
    sub_name: *const c_char,
    parent: *mut u64,
    sub: *mut u64,
) -> i32 {
    if parent_name.is_null() || parent.is_null() {
        return INVALID_ARGUMENT;
    }
    let names = NAMES_MAP.lock().unwrap();
    let p = CStr::from_ptr(parent_name).to_bytes();
    let Some((id, _, _)) = names
        .iter()
        .find(|(_, s, n)| *s == -1 && n.as_bytes().eq_ignore_ascii_case(p))
    else {
        return UNKNOWN_MODULE;
    };
    parent.write(*id as u64);
    if !sub_name.is_null() {
        if sub.is_null() {
            return INVALID_ARGUMENT;
        }
        let s = CStr::from_ptr(sub_name).to_bytes();
        let Some((_, id, _)) = names
            .iter()
            .find(|(p, sub, n)| *p == *id && *sub >= 0 && n.as_bytes().eq_ignore_ascii_case(s))
        else {
            return UNKNOWN_MODULE;
        };
        sub.write(*id as u64);
    }
    0
}

fn parse_level_name(name: &[u8], alert: bool) -> Result<i8, i32> {
    let Some(level) = NAMES
        .iter()
        .position(|n| n.as_bytes().eq_ignore_ascii_case(name))
    else {
        return Err(INVALID_LEVEL);
    };
    Ok(if level == 2 && !alert && option(INFO_AS_WDIAG) != 0 {
        4
    } else {
        level as i8
    })
}
pub unsafe fn parse_level(name: *const c_char, out: *mut i8, alert: i32) -> i32 {
    if name.is_null() || out.is_null() {
        return INVALID_ARGUMENT;
    }
    match parse_level_name(CStr::from_ptr(name).to_bytes(), alert != 0) {
        Ok(level) => {
            out.write(level);
            0
        }
        Err(error) => error,
    }
}

fn parse(text: &str, valid: &mut i32) -> Result<Vec<(i32, i32, i8)>, i32> {
    let text = text.trim();
    if !text.contains(':') {
        let level = parse_level_name(text.as_bytes(), false)?;
        *valid = text.len() as i32;
        return Ok(vec![(-1, -1, level)]);
    }
    let names = NAMES_MAP.lock().unwrap();
    let mut result = Vec::new();
    let mut offset = 0;
    for setting in text.split_inclusive([',', ';']) {
        let item = setting.trim_end_matches([',', ';']).trim();
        let Some((modules, level)) = item.split_once(':') else {
            return Err(SYNTAX);
        };
        let Some((parent, sub)) = modules.split_once('.') else {
            return Err(SYNTAX);
        };
        let parent = parent.trim();
        let sub = sub.trim();
        let level = level.trim();
        if parent.len() >= 20 || sub.len() >= 20 || level.len() >= 10 {
            return Err(SYNTAX);
        }
        let level = parse_level_name(level.as_bytes(), false)?;
        let (p, s) = if parent.eq_ignore_ascii_case("ALL") && sub == "*" {
            (-1, -1)
        } else {
            let Some((p, _, _)) = names
                .iter()
                .find(|(_, s, n)| *s == -1 && n.eq_ignore_ascii_case(parent))
            else {
                return Err(UNKNOWN_MODULE);
            };
            let s = if sub == "*" {
                -1
            } else {
                names
                    .iter()
                    .find(|(id, s, n)| id == p && *s >= 0 && n.eq_ignore_ascii_case(sub))
                    .map(|(_, s, _)| *s)
                    .ok_or(UNKNOWN_MODULE)?
            };
            (*p, s)
        };
        result.push((p, s, level));
        offset += setting.len();
        *valid = offset as i32;
    }
    Ok(result)
}

// map == NULL selects instance policy. check != 0 only validates. Map handles
// are replaced after complete parsing; an invalid suffix applies no prefix.
pub unsafe fn parse_settings(
    text: *const c_char,
    len: i32,
    valid: *mut i32,
    map: *mut *mut MapHandle,
    version: i64,
    check: i32,
) -> i32 {
    if text.is_null() || len < 0 {
        return INVALID_ARGUMENT;
    }
    let mut scratch_valid = 0;
    let valid = if valid.is_null() {
        &mut scratch_valid as *mut i32
    } else {
        valid
    };
    valid.write(0);
    let bytes = std::slice::from_raw_parts(text.cast::<u8>(), len as usize);
    let Ok(text) = std::str::from_utf8(bytes) else {
        return SYNTAX;
    };
    let _config = CONFIG.lock().unwrap();
    if map.is_null() && version > 0 && version <= VERSION.load(Ordering::Relaxed) && check == 0 {
        return 0;
    }
    if map.is_null() && text.eq_ignore_ascii_case("PERF") {
        valid.write(len);
        if check == 0 {
            set_option(PERF, 1);
            if version > 0 {
                VERSION.store(version, Ordering::Relaxed);
            }
        }
        return 0;
    }
    let changes = match parse(text, &mut *valid) {
        Ok(changes) => changes,
        Err(error) => return error,
    };
    if check != 0 {
        return 0;
    }
    if map.is_null() {
        for (p, s, level) in changes {
            if p < 0 {
                for slot in &LEVELS {
                    slot.store(level, Ordering::Relaxed);
                }
            } else if s < 0 {
                for slot in &LEVELS[p as usize * WIDTH..(p as usize + 1) * WIDTH] {
                    slot.store(level, Ordering::Relaxed);
                }
            } else {
                LEVELS[index(p, s).unwrap()].store(level, Ordering::Relaxed);
            }
        }
        set_option(PERF, 0);
        if version > 0 {
            VERSION.store(version, Ordering::Relaxed);
        }
    } else {
        for (p, s, level) in changes {
            let result = map_set(map, p, s, level as i32);
            if result != 0 {
                return result;
            }
        }
    }
    0
}

pub fn set_instance_level(level: i32, version: i64) -> i32 {
    if !(-1..=7).contains(&level) {
        return INVALID_LEVEL;
    }
    let _config = CONFIG.lock().unwrap();
    if version <= 0 || version > VERSION.load(Ordering::Relaxed) {
        for slot in &LEVELS {
            slot.store(level as i8, Ordering::Relaxed);
        }
        if version > 0 {
            VERSION.store(version, Ordering::Relaxed);
        }
    }
    0
}

pub fn level_name(level: i32) -> *const c_char {
    match level {
        0 => c"ERROR".as_ptr(),
        1 => c"WARN".as_ptr(),
        2 => c"INFO".as_ptr(),
        3 => c"EDIAG".as_ptr(),
        4 => c"WDIAG".as_ptr(),
        5 => c"TRACE".as_ptr(),
        6 => c"DEBUG".as_ptr(),
        _ => c"?".as_ptr(),
    }
}
static TRACE_SEQUENCE: AtomicU64 = AtomicU64::new(0);
thread_local! { static SEQUENCE: Cell<u64> = const { Cell::new(0) }; }
pub fn trace_sequence() -> u64 {
    SEQUENCE.with(|sequence| {
        let mut value = sequence.get();
        if value.is_multiple_of(1 << 20) {
            value = TRACE_SEQUENCE.fetch_add(1 << 20, Ordering::Relaxed);
        }
        value += 1;
        sequence.set(value);
        value
    })
}
pub fn trace_field(key: usize, value: u64) {
    TRACE.with(|trace| {
        let mut words = trace.get();
        let (shift, mask) = match key {
            0 => (48, 1),
            1 => (52, 15),
            2 => (56, 255),
            _ => return,
        };
        words[0] = (words[0] & !(mask << shift)) | ((value & mask) << shift);
        trace.set(words);
    });
}

pub fn trace_seed(seed: u64) {
    TRACE_SEQUENCE.store(seed, Ordering::Relaxed);
}
