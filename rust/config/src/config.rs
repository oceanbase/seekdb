// Copyright (c) 2025 OceanBase.
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

include!(concat!(env!("OUT_DIR"), "/generated_config.rs"));

use std::collections::BTreeMap;
use std::path::Path;
use std::sync::Mutex;

static CONFIGURED: OnceLock<Mutex<BTreeMap<String, String>>> = OnceLock::new();
static BOOTSTRAP_DIRTY: OnceLock<Mutex<BTreeMap<String, String>>> = OnceLock::new();
static EFFECTIVE_TEXT: OnceLock<RwLock<BTreeMap<String, String>>> = OnceLock::new();
static SERVER_CREATE_TIME: AtomicI64 = AtomicI64::new(0);
static GENERATION: AtomicI64 = AtomicI64::new(0);
static SERVER_ROLE_INFO: OnceLock<RwLock<String>> = OnceLock::new();

pub fn server_create_time() -> i64 {
    SERVER_CREATE_TIME.load(Ordering::Acquire)
}

pub fn generation() -> i64 {
    GENERATION.load(Ordering::Acquire)
}

pub fn server_role_info() -> String {
    SERVER_ROLE_INFO
        .get_or_init(|| RwLock::new(String::new()))
        .read()
        .expect("role state lock poisoned")
        .clone()
}

fn configured() -> &'static Mutex<BTreeMap<String, String>> {
    CONFIGURED.get_or_init(|| Mutex::new(BTreeMap::new()))
}

fn bootstrap_dirty() -> &'static Mutex<BTreeMap<String, String>> {
    BOOTSTRAP_DIRTY.get_or_init(|| Mutex::new(BTreeMap::new()))
}

fn effective_text() -> &'static RwLock<BTreeMap<String, String>> {
    EFFECTIVE_TEXT.get_or_init(|| RwLock::new(BTreeMap::new()))
}

fn invalid_value(name: &str) -> crate::Error {
    crate::Error::new(0, Some(name.to_owned()), "invalid parameter value")
}

fn parse_number_prefix<'a>(name: &str, input: &'a str) -> Result<(i64, &'a str), crate::Error> {
    let text = input.trim_start_matches(|ch: char| ch.is_ascii_whitespace());
    let (negative, unsigned) = if let Some(rest) = text.strip_prefix('-') {
        (true, rest)
    } else {
        (false, text.strip_prefix('+').unwrap_or(text))
    };
    let (digits_and_tail, radix) = if let Some(rest) = unsigned
        .strip_prefix("0x")
        .or_else(|| unsigned.strip_prefix("0X"))
    {
        (rest, 16)
    } else if unsigned.len() > 1 && unsigned.starts_with('0') {
        (unsigned, 8)
    } else {
        (unsigned, 10)
    };
    let digits_len = digits_and_tail
        .bytes()
        .take_while(|byte| char::from(*byte).is_digit(radix))
        .count();
    if digits_len == 0 {
        return Err(invalid_value(name));
    }
    let magnitude = i128::from_str_radix(&digits_and_tail[..digits_len], radix)
        .map_err(|_| invalid_value(name))?;
    let signed = if negative { -magnitude } else { magnitude };
    let value = i64::try_from(signed).map_err(|_| invalid_value(name))?;
    Ok((value, &digits_and_tail[digits_len..]))
}

pub(crate) fn parse_int(name: &str, input: &str) -> Result<i64, crate::Error> {
    let (value, suffix) = parse_number_prefix(name, input)?;
    if suffix.is_empty() {
        Ok(value)
    } else {
        Err(invalid_value(name))
    }
}

pub(crate) fn parse_capacity(name: &str, input: &str) -> Result<i64, crate::Error> {
    let (value, suffix) = parse_number_prefix(name, input)?;
    if value < 0 {
        return Err(invalid_value(name));
    }
    let multiplier = match suffix.to_ascii_lowercase().as_str() {
        "" if value == 0 => 1,
        "m" | "mb" => 1_i64 << 20,
        "b" | "byte" => 1,
        "k" | "kb" => 1_i64 << 10,
        "g" | "gb" => 1_i64 << 30,
        "t" | "tb" => 1_i64 << 40,
        "p" | "pb" => 1_i64 << 50,
        _ => return Err(invalid_value(name)),
    };
    value
        .checked_mul(multiplier)
        .ok_or_else(|| invalid_value(name))
}

pub(crate) fn parse_time(name: &str, input: &str) -> Result<i64, crate::Error> {
    let (value, suffix) = parse_number_prefix(name, input)?;
    if value < 0 {
        return Err(invalid_value(name));
    }
    let multiplier = match suffix.to_ascii_lowercase().as_str() {
        "us" => 1,
        "ms" => 1_000,
        "" | "s" => 1_000_000,
        "m" => 60_000_000,
        "h" => 3_600_000_000,
        "d" => 86_400_000_000,
        _ => return Err(invalid_value(name)),
    };
    value
        .checked_mul(multiplier)
        .ok_or_else(|| invalid_value(name))
}

pub(crate) fn parse_bool(name: &str, input: &str) -> Result<bool, crate::Error> {
    match input.to_ascii_lowercase().as_str() {
        "true" | "on" | "yes" | "t" | "1" => Ok(true),
        "false" | "off" | "no" | "f" | "0" => Ok(false),
        _ => Err(invalid_value(name)),
    }
}

pub(crate) fn parse_double(name: &str, input: &str) -> Result<f64, crate::Error> {
    let text = input.trim_start_matches(|ch: char| ch.is_ascii_whitespace());
    text.parse::<f64>().map_err(|_| invalid_value(name))
}

fn bounds<'a>(
    name: &str,
    range: &'a str,
) -> Result<Option<(bool, &'a str, &'a str, bool)>, crate::Error> {
    if range.is_empty() {
        return Ok(None);
    }
    let start = range.find(['[', '(']).ok_or_else(|| invalid_value(name))?;
    let opening = range.as_bytes()[start];
    let closing_index = range[start + 1..]
        .find([']', ')'])
        .map(|offset| start + 1 + offset)
        .ok_or_else(|| invalid_value(name))?;
    let closing = range.as_bytes()[closing_index];
    let (low, high) = range[start + 1..closing_index]
        .split_once(',')
        .ok_or_else(|| invalid_value(name))?;
    Ok(Some((
        opening == b'[',
        low.trim(),
        high.trim(),
        closing == b']',
    )))
}

fn validate_i64_range(
    name: &str,
    value: i64,
    range: &str,
    parser: fn(&str, &str) -> Result<i64, crate::Error>,
) -> Result<(), crate::Error> {
    if let Some((include_low, low, high, include_high)) = bounds(name, range)? {
        if !low.is_empty() {
            let minimum = parser(name, low)?;
            if value < minimum || (!include_low && value == minimum) {
                return Err(invalid_value(name));
            }
        }
        if !high.is_empty() {
            let maximum = parser(name, high)?;
            if value > maximum || (!include_high && value == maximum) {
                return Err(invalid_value(name));
            }
        }
    }
    Ok(())
}

fn validate_f64_range(name: &str, value: f64, range: &str) -> Result<(), crate::Error> {
    if !value.is_finite() {
        return Err(invalid_value(name));
    }
    if let Some((include_low, low, high, include_high)) = bounds(name, range)? {
        if !low.is_empty() {
            let minimum = parse_double(name, low)?;
            if value < minimum || (!include_low && value == minimum) {
                return Err(invalid_value(name));
            }
        }
        if !high.is_empty() {
            let maximum = parse_double(name, high)?;
            if value > maximum || (!include_high && value == maximum) {
                return Err(invalid_value(name));
            }
        }
    }
    Ok(())
}

fn validate_moment(name: &str, value: &str) -> Result<(), crate::Error> {
    if value.eq_ignore_ascii_case("disable") {
        return Ok(());
    }
    let (hour, minute) = value.split_once(':').ok_or_else(|| invalid_value(name))?;
    if hour.is_empty()
        || minute.len() != 2
        || !hour.bytes().all(|c| c.is_ascii_digit())
        || !minute.bytes().all(|c| c.is_ascii_digit())
    {
        return Err(invalid_value(name));
    }
    let hour = hour.parse::<u8>().map_err(|_| invalid_value(name))?;
    let minute = minute.parse::<u8>().map_err(|_| invalid_value(name))?;
    if hour >= 24 || minute >= 60 {
        return Err(invalid_value(name));
    }
    Ok(())
}

fn validate_storable(name: &str, value: &str) -> Result<&'static ParameterMeta, crate::Error> {
    let parameter = find(name)
        .ok_or_else(|| crate::Error::new(0, Some(name.to_owned()), "unknown parameter"))?;
    if value.len() >= 64 * 1024 || value.contains(['\n', '\r', '\0']) {
        return Err(invalid_value(name));
    }
    match parameter.kind {
        "INT" | "INT_WITH_CHECKER" => {
            parse_int(name, value)?;
        }
        "CAP" | "CAP_WITH_CHECKER" => {
            parse_capacity(name, value)?;
        }
        "TIME" | "TIME_WITH_CHECKER" => {
            parse_time(name, value)?;
        }
        "DBL" => validate_f64_range(name, parse_double(name, value)?, "")?,
        "BOOL" => {
            parse_bool(name, value)?;
        }
        "MOMENT" => validate_moment(name, value)?,
        "MODE" | "LOG_LEVEL" | "WORK_AREA_POLICY" | "STR" | "STR_WITH_CHECKER" => {}
        _ => unreachable!("build script checks every parameter kind"),
    }
    Ok(parameter)
}

pub fn validate(name: &str, value: &str) -> Result<&'static ParameterMeta, crate::Error> {
    let parameter = validate_storable(name, value)?;
    match parameter.kind {
        "INT" | "INT_WITH_CHECKER" => {
            validate_i64_range(name, parse_int(name, value)?, parameter.range, parse_int)?
        }
        "CAP" | "CAP_WITH_CHECKER" => validate_i64_range(
            name,
            parse_capacity(name, value)?,
            parameter.range,
            parse_capacity,
        )?,
        "TIME" | "TIME_WITH_CHECKER" => {
            validate_i64_range(name, parse_time(name, value)?, parameter.range, parse_time)?
        }
        "DBL" => validate_f64_range(name, parse_double(name, value)?, parameter.range)?,
        _ => {}
    }
    Ok(parameter)
}

pub(crate) fn validate_file_entry(entry: &crate::Entry) -> Result<(), crate::Error> {
    if entry.name.len() >= 128 || entry.value.len() >= 64 * 1024 {
        return Err(crate::Error::new(
            entry.line,
            Some(entry.name.clone()),
            "configuration entry is too long",
        ));
    }
    let result = if find(&entry.name).is_some() {
        validate_storable(&entry.name, &entry.value).map(|_| ())
    } else if INTERNAL_STATE.iter().any(|state| state.name == entry.name) {
        match entry.name.as_str() {
            "server_create_time" => parse_int(&entry.name, &entry.value).and_then(|value| {
                if value > 0 {
                    Ok(())
                } else {
                    Err(invalid_value(&entry.name))
                }
            }),
            "server_role_info" => validate_role_state(&entry.name, &entry.value),
            _ => unreachable!("the internal-state catalog must have an explicit validator"),
        }
    } else {
        Err(crate::Error::new(
            0,
            Some(entry.name.clone()),
            "unknown configuration key",
        ))
    };
    result.map_err(|mut error| {
        error.line = entry.line;
        error
    })
}

fn validate_role_state(name: &str, value: &str) -> Result<(), crate::Error> {
    let fields: Vec<_> = value.split(':').collect();
    if fields.len() != 2 && fields.len() != 4 {
        return Err(invalid_value(name));
    }
    let role = |value: &str| {
        ["INVALID", "PRIMARY", "STANDBY", "RESTORE"]
            .iter()
            .any(|candidate| value.eq_ignore_ascii_case(candidate))
    };
    if !role(fields[0]) {
        return Err(invalid_value(name));
    }
    if fields.len() == 2 {
        if !fields[1].eq_ignore_ascii_case("NORMAL") {
            return Err(invalid_value(name));
        }
    } else if !role(fields[1])
        || !["NORMAL", "PREPARING"]
            .iter()
            .any(|candidate| fields[2].eq_ignore_ascii_case(candidate))
        || fields[3].parse::<u64>().is_err()
    {
        return Err(invalid_value(name));
    }
    Ok(())
}

fn display_value(parameter: &ParameterMeta, value: &str) -> String {
    if parameter.kind == "BOOL" {
        if parse_bool(parameter.name, value).expect("validated boolean") {
            "True"
        } else {
            "False"
        }
        .to_owned()
    } else {
        value.to_owned()
    }
}

fn publish_effective(name: &str, value: &str) {
    apply(name, value).expect("validated parameter value must remain valid at publication");
    let parameter = find(name).expect("published parameter must be declared");
    effective_text()
        .write()
        .expect("effective config lock poisoned")
        .insert(name.to_owned(), display_value(parameter, value));
}

pub fn effective_value(name: &str) -> Option<String> {
    let parameter = find(name)?;
    Some(
        effective_text()
            .read()
            .expect("effective config lock poisoned")
            .get(parameter.name)
            .cloned()
            .unwrap_or_else(|| display_value(parameter, parameter.default)),
    )
}

pub fn configured_value(name: &str) -> Option<String> {
    let parameter = find(name)?;
    Some(
        configured()
            .lock()
            .expect("configured config lock poisoned")
            .get(parameter.name)
            .cloned()
            .unwrap_or_else(|| parameter.default.to_owned()),
    )
}

pub fn update_parameter(path: &Path, name: &str, value: Option<&str>) -> Result<(), crate::Error> {
    update_parameter_checked(path, name, value, || Ok(()))
}

pub fn update_parameter_checked(
    path: &Path,
    name: &str,
    value: Option<&str>,
    check: impl FnOnce() -> Result<(), crate::Error>,
) -> Result<(), crate::Error> {
    let parameter = find(name)
        .ok_or_else(|| crate::Error::new(0, Some(name.to_owned()), "unknown parameter"))?;
    let candidate = value.unwrap_or(parameter.default);
    validate(parameter.name, candidate)?;
    crate::update_with_commit(path, parameter.name, value, check, || {
        let mut configured = configured()
            .lock()
            .expect("configured config lock poisoned");
        if let Some(value) = value {
            configured.insert(parameter.name.to_owned(), value.to_owned());
        } else {
            configured.remove(parameter.name);
        }
        if parameter.edit_level != "STATIC_EFFECTIVE" {
            publish_effective(parameter.name, candidate);
        }
        GENERATION.fetch_add(1, Ordering::AcqRel);
    })
}

pub fn bootstrap_set(name: &str, value: &str) -> Result<(), crate::Error> {
    let _guard = crate::lock_writers()?;
    let parameter = validate(name, value)?;
    configured()
        .lock()
        .expect("configured config lock poisoned")
        .insert(parameter.name.to_owned(), value.to_owned());
    publish_effective(parameter.name, value);
    GENERATION.fetch_add(1, Ordering::AcqRel);
    bootstrap_dirty()
        .lock()
        .expect("bootstrap config lock poisoned")
        .insert(parameter.name.to_owned(), value.to_owned());
    Ok(())
}

pub fn save_bootstrap(path: &Path) -> Result<(), crate::Error> {
    let pending = bootstrap_dirty()
        .lock()
        .expect("bootstrap config lock poisoned")
        .clone();
    for (name, value) in pending {
        update_parameter(path, &name, Some(&value))?;
        bootstrap_dirty()
            .lock()
            .expect("bootstrap config lock poisoned")
            .remove(&name);
    }
    Ok(())
}

pub fn update_internal_state(path: &Path, name: &str, value: &str) -> Result<(), crate::Error> {
    let state = INTERNAL_STATE
        .iter()
        .find(|state| state.name.eq_ignore_ascii_case(name))
        .ok_or_else(|| crate::Error::new(0, Some(name.to_owned()), "unknown internal state"))?;
    let entry = crate::Entry {
        name: state.name.to_owned(),
        value: value.to_owned(),
        line: 0,
    };
    validate_file_entry(&entry)?;
    crate::update_with_commit(
        path,
        state.name,
        Some(value),
        || Ok(()),
        || match state.name {
            "server_create_time" => SERVER_CREATE_TIME.store(
                parse_int(state.name, value).expect("validated creation time"),
                Ordering::Release,
            ),
            "server_role_info" => {
                *SERVER_ROLE_INFO
                    .get_or_init(|| RwLock::new(String::new()))
                    .write()
                    .expect("role state lock poisoned") = value.to_owned();
            }
            _ => unreachable!("declared internal state"),
        },
    )
}

pub fn load_active(path: &Path, startup: bool) -> Result<(), crate::Error> {
    let _guard = crate::lock_writers()?;
    let entries = crate::load(path)?;
    for entry in &entries {
        validate_file_entry(entry)?;
    }
    let create_time = entries
        .iter()
        .find(|entry| entry.name == "server_create_time")
        .map(|entry| {
            parse_int("server_create_time", &entry.value).expect("validated creation time")
        })
        .unwrap_or(0);
    let role_info = entries
        .iter()
        .find(|entry| entry.name == "server_role_info")
        .map(|entry| entry.value.clone())
        .unwrap_or_default();
    let overrides: BTreeMap<_, _> = entries
        .into_iter()
        .filter(|entry| find(&entry.name).is_some())
        .map(|entry| (entry.name, entry.value))
        .collect();
    for parameter in CATALOG {
        if startup || parameter.edit_level != "STATIC_EFFECTIVE" {
            let value = overrides
                .get(parameter.name)
                .map(String::as_str)
                .unwrap_or(parameter.default);
            publish_effective(parameter.name, value);
        }
    }
    *configured()
        .lock()
        .expect("configured config lock poisoned") = overrides;
    SERVER_CREATE_TIME.store(create_time, Ordering::Release);
    *SERVER_ROLE_INFO
        .get_or_init(|| RwLock::new(String::new()))
        .write()
        .expect("role state lock poisoned") = role_info;
    if startup {
        bootstrap_dirty()
            .lock()
            .expect("bootstrap config lock poisoned")
            .clear();
    }
    GENERATION.fetch_add(1, Ordering::AcqRel);
    Ok(())
}

pub fn snapshot(names: &[&str]) -> Result<Vec<String>, crate::Error> {
    let _guard = crate::lock_writers()?;
    names
        .iter()
        .map(|name| {
            effective_value(name)
                .ok_or_else(|| crate::Error::new(0, Some((*name).to_owned()), "unknown parameter"))
        })
        .collect()
}

fn parse_moment_parts(value: &str) -> bridge::MomentTime {
    if value.eq_ignore_ascii_case("disable") {
        bridge::MomentTime {
            disabled: true,
            hour: 0,
            minute: 0,
        }
    } else {
        let (hour, minute) = value.split_once(':').expect("validated moment");
        bridge::MomentTime {
            disabled: false,
            hour: hour.parse().expect("validated hour"),
            minute: minute.parse().expect("validated minute"),
        }
    }
}
