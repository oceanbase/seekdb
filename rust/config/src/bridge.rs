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

use crate::config::{
    self,
    bridge::{CheckCallback, Status},
};
use crate::Error;
use cxx::CxxString;

fn status(result: Result<(), Error>) -> Status {
    match result {
        Ok(()) => Status {
            ok: true,
            commit_uncertain: false,
            message: String::new(),
        },
        Err(error) => Status {
            ok: false,
            commit_uncertain: error.commit_uncertain,
            message: error.to_string(),
        },
    }
}

fn storage(store: &local::Store) -> Result<&kv::Store, Error> {
    store
        .kv()
        .map_err(|error| Error::new(None, format!("access local storage: {error}")))
}

fn text<'a>(input: &'a CxxString, label: &str) -> Result<&'a str, Error> {
    input
        .to_str()
        .map_err(|_| Error::new(None, format!("{label} is not UTF-8")))
}

pub(crate) fn load_active_status(store: &local::Store, startup: bool) -> Status {
    status(storage(store).and_then(|store| config::load_active(store, startup)))
}

pub(crate) fn save_bootstrap_status(store: &local::Store) -> Status {
    status(storage(store).and_then(config::save_bootstrap))
}

pub(crate) fn bootstrap_set_status(name: &CxxString, value: &CxxString) -> Status {
    status((|| {
        config::bootstrap_set(text(name, "name")?, text(value, "value")?)
    })())
}

pub(crate) fn update_internal_state_status(
    store: &local::Store,
    name: &CxxString,
    value: &CxxString,
) -> Status {
    status((|| {
        config::update_internal_state(storage(store)?, text(name, "name")?, text(value, "value")?)
    })())
}

pub(crate) fn update_checked_status(
    store: &local::Store,
    name: &CxxString,
    value: &CxxString,
    reset: bool,
    callback: &CheckCallback,
) -> Status {
    status((|| {
        let name = text(name, "name")?;
        let value = if reset {
            None
        } else {
            Some(text(value, "value")?)
        };
        config::update_parameter_checked(storage(store)?, name, value, || {
            let code = callback.check();
            if code == 0 {
                Ok(())
            } else {
                Err(Error::new(
                    Some(name.to_owned()),
                    format!("business checker rejected value ({code})"),
                ))
            }
        })
    })())
}
