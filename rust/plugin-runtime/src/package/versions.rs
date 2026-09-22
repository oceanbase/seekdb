// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Version labels are opaque, not semver. A script is a directed unit-cost edge.
//! Ordered seeds/neighbors make equal-length choices independent of readdir order.
use super::{component, Error, INVALID, NOT_FOUND};
use std::collections::{BTreeMap, BTreeSet, VecDeque};

pub(super) const VERSION_LIMIT: usize = 1024;

#[derive(Default)]
struct Version {
    install: bool,
    next: BTreeSet<String>,
}

#[derive(Default)]
pub(super) struct Versions(BTreeMap<String, Version>);

impl Versions {
    pub(super) fn add(&mut self, package: &str, filename: &str) -> Result<(), Error> {
        let Some(versions) = filename
            .strip_prefix(package)
            .and_then(|tail| tail.strip_prefix("--"))
            .and_then(|tail| tail.strip_suffix(".sql"))
        else {
            return Ok(());
        };
        let parts: Vec<_> = versions.split("--").collect();
        if !(1..=2).contains(&parts.len()) || parts.iter().any(|part| !component(part)) {
            return Err((INVALID, "invalid versioned SQL filename"));
        }
        for version in &parts {
            self.0.entry((*version).to_owned()).or_default();
        }
        if self.0.len() > VERSION_LIMIT {
            return Err((INVALID, "too many package versions"));
        }
        if parts.len() == 1 {
            self.0.get_mut(parts[0]).unwrap().install = true;
        } else if parts[0] == parts[1] {
            return Err((INVALID, "update script cannot target its source version"));
        } else {
            self.0
                .get_mut(parts[0])
                .unwrap()
                .next
                .insert(parts[1].to_owned());
        }
        Ok(())
    }

    // None means a fresh installation: seed every available base script. Some
    // means an update from an installed version, never a reinstall shortcut.
    pub(super) fn path(&self, from: Option<&str>, target: &str) -> Result<Vec<String>, Error> {
        let mut queue = VecDeque::new();
        let mut previous = BTreeMap::<String, Option<String>>::new();
        for (name, version) in &self.0 {
            if from.map_or(version.install, |from| from == name) {
                queue.push_back(name.clone());
                previous.insert(name.clone(), None);
            }
        }
        while let Some(current) = queue.pop_front() {
            if current == target {
                let mut path = vec![current.clone()];
                let mut cursor = &current;
                while let Some(Some(parent)) = previous.get(cursor) {
                    path.push(parent.clone());
                    cursor = parent;
                }
                path.reverse();
                return Ok(path);
            }
            for next in &self.0[&current].next {
                if !previous.contains_key(next) {
                    previous.insert(next.clone(), Some(current.clone()));
                    queue.push_back(next.clone());
                }
            }
        }
        Err((NOT_FOUND, "no package script path to requested version"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn graph(scripts: &[&str]) -> Versions {
        let mut graph = Versions::default();
        for script in scripts {
            graph.add("demo", &format!("demo--{script}.sql")).unwrap();
        }
        graph
    }

    #[test]
    fn fresh_install_uses_shortest_chain_or_direct_script() {
        let mut versions = graph(&["base", "base--a", "a--tip", "other", "other--tip"]);
        assert_eq!(versions.path(None, "tip").unwrap(), ["other", "tip"]);
        versions.add("demo", "demo--tip.sql").unwrap();
        assert_eq!(versions.path(None, "tip").unwrap(), ["tip"]);
        assert_eq!(
            versions.path(Some("base"), "tip").unwrap(),
            ["base", "a", "tip"]
        );
    }

    #[test]
    fn cycles_downgrades_and_unreachable_versions() {
        let versions = graph(&["v9", "v9--v2", "v2--v9", "v2--beta", "orphan--tip"]);
        assert_eq!(versions.path(None, "beta").unwrap(), ["v9", "v2", "beta"]);
        assert_eq!(versions.path(Some("v2"), "v2").unwrap(), ["v2"]);
        assert_eq!(versions.path(None, "tip").unwrap_err().0, NOT_FOUND);
        assert_eq!(
            versions.path(Some("missing"), "v9").unwrap_err().0,
            NOT_FOUND
        );
    }

    #[test]
    fn equal_length_ties_do_not_depend_on_file_order() {
        let scripts = [
            "z", "a", "a--y", "a--b", "b--tip", "y--tip", "z--c", "c--tip",
        ];
        let expected = ["a", "b", "tip"];
        assert_eq!(graph(&scripts).path(None, "tip").unwrap(), expected);
        let reversed: Vec<_> = scripts.into_iter().rev().collect();
        assert_eq!(graph(&reversed).path(None, "tip").unwrap(), expected);
    }

    #[test]
    fn update_only_packages_need_no_base_and_cannot_jump_to_an_installation_seed() {
        let versions = graph(&["a--b", "b--tip", "a--z", "z--tip", "tip", "other"]);
        assert_eq!(versions.path(Some("a"), "tip").unwrap(), ["a", "b", "tip"]);
        assert_eq!(
            versions.path(Some("other"), "tip").unwrap_err().0,
            NOT_FOUND
        );
        assert_eq!(
            graph(&["new--old"]).path(Some("new"), "old").unwrap(),
            ["new", "old"]
        );
    }

    #[test]
    fn filenames_and_graph_size_are_bounded() {
        let mut versions = Versions::default();
        for filename in [
            "README.md",
            "demo.control",
            "other--1.sql",
            "demo--1.sql.bak",
        ] {
            versions.add("demo", filename).unwrap();
        }
        assert!(versions.0.is_empty());
        for filename in [
            "demo--.sql",
            "demo--a--a.sql",
            "demo--a--b--c.sql",
            "demo--a..b.sql",
        ] {
            assert!(
                Versions::default().add("demo", filename).is_err(),
                "{filename}"
            );
        }
        for i in 0..VERSION_LIMIT {
            versions.add("demo", &format!("demo--v{i}.sql")).unwrap();
        }
        assert!(versions.add("demo", "demo--overflow.sql").is_err());
    }

    #[test]
    fn longest_supported_chain_is_iterative() {
        let mut versions = graph(&["v0"]);
        for i in 1..VERSION_LIMIT {
            versions
                .add("demo", &format!("demo--v{}--v{i}.sql", i - 1))
                .unwrap();
        }
        let path = versions
            .path(None, &format!("v{}", VERSION_LIMIT - 1))
            .unwrap();
        assert_eq!(path.len(), VERSION_LIMIT);
        for (i, version) in path.iter().enumerate() {
            assert_eq!(version, &format!("v{i}"));
        }
    }
}
