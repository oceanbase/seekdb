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

//! UUID v1 generation with one process-wide node ID and clock sequence.
//! Every platform uses a random node without reading network-interface addresses.

mod bridge;

use bridge::ffi::Code;
use std::sync::OnceLock;
use std::time::{SystemTime, UNIX_EPOCH};
use uuid::{ContextV1, Timestamp, Uuid};

type Result<T> = std::result::Result<T, Code>;
static GENERATOR: OnceLock<Result<Generator>> = OnceLock::new();

fn generator() -> Result<&'static Generator> {
    GENERATOR
        .get_or_init(Generator::new)
        .as_ref()
        .map_err(|&code| code)
}

struct Generator {
    node: [u8; 6],
    context: ContextV1,
}

impl Generator {
    fn new() -> Result<Self> {
        let mut node = [0; 6];
        random_bytes(&mut node)?;
        // RFC 9562 section 6.10: distinguish random node IDs from real MACs.
        node[0] |= 1;
        // Seed the clock sequence independently of the node.
        let mut seed = [0; 2];
        random_bytes(&mut seed)?;
        Ok(Self {
            node,
            context: ContextV1::new(u16::from_ne_bytes(seed)),
        })
    }

    fn generate(&self) -> Result<Uuid> {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| Code::Clock)?;
        Ok(self.at(now.as_secs(), now.subsec_nanos()))
    }

    fn at(&self, seconds: u64, nanos: u32) -> Uuid {
        Uuid::new_v1(
            Timestamp::from_unix(&self.context, seconds, nanos),
            &self.node,
        )
    }
}

fn random_bytes(output: &mut [u8]) -> Result<()> {
    getrandom::fill(output).map_err(|_| Code::Entropy)
}
