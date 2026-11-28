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

use crate::bridge::ffi::{
    AccessOptions, AccountIdentity, AccountKind, AccountLimits, AccountNameForm, AuthenticationKind,
    PrivilegeObject, PrivilegeScope, PrivilegeTarget, RoleSelection, TlsMode, TlsField, TlsOption, TlsRequirement,
};
use crate::node::{Kind, Node};

fn child(node: &Node, kind: Kind) -> Option<&Node> {
    node.children.iter().find(|child| child.kind == kind)
}

fn list(node: &Node, kind: Kind) -> &[Node] {
    child(node, kind).map_or(&[], |list| list.children.as_slice())
}

fn role_selection(statement: &Node) -> Option<&Node> {
    statement.children.iter().find(|child| {
        matches!(
            child.kind,
            Kind::ListedRoles
                | Kind::AllRoles
                | Kind::AllExceptRoles
                | Kind::NoRoles
                | Kind::DefaultRoles
        )
    })
}

pub(crate) fn access_options(statement: &Node) -> AccessOptions {
    AccessOptions {
        if_exists: child(statement, Kind::IfExists).is_some(),
        if_not_exists: child(statement, Kind::IfNotExists).is_some(),
        grant_option: child(statement, Kind::WithGrantOption).is_some(),
        admin_option: child(statement, Kind::WithAdminOption).is_some(),
        ignore_unknown_user: child(statement, Kind::IgnoreUnknownUser).is_some(),
        locked: statement.kind == Kind::LockUser && statement.value != 0,
    }
}

pub(crate) fn account_limits(statement: &Node) -> AccountLimits<'_> {
    let mut limits = AccountLimits { connections_per_hour: &[], user_connections: &[] };
    for option in &statement.children {
        match option.kind {
            Kind::MaxConnectionsPerHour => {
                limits.connections_per_hour = &option.children;
            }
            Kind::MaxUserConnections => {
                limits.user_connections = &option.children;
            }
            _ => {}
        }
    }
    limits
}

pub(crate) fn account_tls(statement: &Node) -> TlsRequirement<'_> {
    let requirement = statement.children.iter().find(|node|
        matches!(node.kind, Kind::TlsNone | Kind::TlsSsl | Kind::TlsX509 | Kind::TlsOptions));
    TlsRequirement {
        mode: match requirement.map(|node| node.kind) {
            Some(Kind::TlsNone) => TlsMode::None,
            Some(Kind::TlsSsl) => TlsMode::Ssl,
            Some(Kind::TlsX509) => TlsMode::X509,
            Some(Kind::TlsOptions) => TlsMode::Specified,
            None => TlsMode::Absent,
            _ => unreachable!(),
        },
        options: requirement.into_iter().flat_map(|node| &node.children).map(|option| TlsOption {
            field: match option.kind {
                Kind::TlsCipher => TlsField::Cipher,
                Kind::TlsIssuer => TlsField::Issuer,
                Kind::TlsSubject => TlsField::Subject,
                _ => unreachable!(),
            },
            value: option.text.as_bytes(),
        }).collect(),
    }
}

pub(crate) fn access_accounts(statement: &Node) -> &[Node] {
    list(statement, Kind::Users)
}

pub(crate) fn account_renames(statement: &Node) -> &[Node] {
    &statement.children
}

pub(crate) fn account_rename_source(rename: &Node) -> &Node {
    &rename.children[0]
}

pub(crate) fn account_rename_target(rename: &Node) -> &Node {
    &rename.children[1]
}

pub(crate) fn access_roles(statement: &Node) -> &[Node] {
    list(role_selection(statement).unwrap_or(statement), Kind::Roles)
}

pub(crate) fn access_privileges(statement: &Node) -> &[Node] {
    list(statement, Kind::Privileges)
}

pub(crate) fn access_role_selection(statement: &Node) -> RoleSelection {
    match role_selection(statement).unwrap().kind {
        Kind::ListedRoles => RoleSelection::Listed,
        Kind::AllRoles => RoleSelection::All,
        Kind::AllExceptRoles => RoleSelection::AllExcept,
        Kind::NoRoles => RoleSelection::None,
        Kind::DefaultRoles => RoleSelection::Default,
        _ => unreachable!(),
    }
}

fn principal(spec: &Node) -> &Node {
    spec.children
        .iter()
        .find(|child| matches!(child.kind, Kind::UserHost | Kind::CurrentUser))
        .unwrap()
}

fn host(spec: &Node) -> Option<&Node> {
    let principal = principal(spec);
    if principal.kind == Kind::CurrentUser || principal.children[1].kind == Kind::Missing {
        None
    } else {
        Some(&principal.children[1])
    }
}

fn authentication(spec: &Node) -> Option<&Node> {
    spec.children.iter().find(|child| {
        matches!(
            child.kind,
            Kind::PlaintextAuthentication | Kind::EncodedAuthentication
        )
    })
}

fn plugin(spec: &Node) -> Option<&Node> {
    authentication(spec).and_then(|auth| child(auth, Kind::AuthenticationPlugin))
}

pub(crate) fn account_identity(principal: &Node) -> AccountIdentity<'_> {
    let name = &principal.children[0];
    let host = &principal.children[1];
    AccountIdentity {
        name: name.text.as_bytes(),
        host: host.text.as_bytes(),
        name_form: if name.kind == Kind::Ident {
            AccountNameForm::Identifier
        } else {
            AccountNameForm::String
        },
        has_host: host.kind != Kind::Missing,
    }
}

pub(crate) fn account_kind(spec: &Node) -> AccountKind {
    if principal(spec).kind == Kind::CurrentUser {
        AccountKind::Current
    } else {
        AccountKind::Named
    }
}

pub(crate) fn account_name_form(spec: &Node) -> AccountNameForm {
    let principal = principal(spec);
    if principal.kind == Kind::CurrentUser || principal.children[0].kind == Kind::Ident {
        AccountNameForm::Identifier
    } else {
        AccountNameForm::String
    }
}

pub(crate) fn account_name(spec: &Node) -> &[u8] {
    let principal = principal(spec);
    if principal.kind == Kind::CurrentUser {
        &[]
    } else {
        principal.children[0].text.as_bytes()
    }
}

pub(crate) fn account_has_host(spec: &Node) -> bool {
    host(spec).is_some()
}

pub(crate) fn account_host(spec: &Node) -> &[u8] {
    host(spec).map_or(&[][..], |host| host.text.as_bytes())
}

pub(crate) fn account_authentication(spec: &Node) -> AuthenticationKind {
    match authentication(spec).map(|auth| auth.kind) {
        None => AuthenticationKind::Absent,
        Some(Kind::PlaintextAuthentication) => AuthenticationKind::Plaintext,
        Some(Kind::EncodedAuthentication) => AuthenticationKind::Encoded,
        _ => unreachable!(),
    }
}

pub(crate) fn account_password(spec: &Node) -> &[u8] {
    authentication(spec).map_or(&[][..], |auth| child(auth, Kind::Password).unwrap().text.as_bytes())
}

pub(crate) fn account_has_plugin(spec: &Node) -> bool {
    plugin(spec).is_some()
}

pub(crate) fn account_plugin(spec: &Node) -> &[u8] {
    plugin(spec).map_or(&[][..], |plugin| plugin.children[0].text.as_bytes())
}

pub(crate) fn privilege_name(privilege: &Node) -> &[u8] {
    privilege.text.as_bytes()
}

pub(crate) fn privilege_columns(privilege: &Node) -> &[Node] {
    list(privilege, Kind::ColumnList)
}

pub(crate) fn privilege_target(object: Option<Node>, first: Node, second: Option<Node>) -> Node {
    let (kind, mut children) = if let Some(second) = second {
        let kind = match (first.kind == Kind::Star, second.kind == Kind::Star) {
            (true, true) => Kind::GlobalPrivilegeTarget,
            (false, true) => Kind::DatabasePrivilegeTarget,
            (false, false) => Kind::ObjectPrivilegeTarget,
            (true, false) => Kind::WildcardQualifiedObjectTarget,
        };
        (
            kind,
            vec![
                Node::new(Kind::TargetDatabase, vec![first]),
                Node::new(Kind::TargetObject, vec![second]),
            ],
        )
    } else {
        let kind = if first.kind == Kind::Star {
            Kind::DatabasePrivilegeTarget
        } else {
            Kind::ObjectPrivilegeTarget
        };
        (kind, vec![Node::new(Kind::TargetObject, vec![first])])
    };
    children.extend(object);
    Node::new(kind, children)
}

fn target(statement: &Node) -> &Node {
    statement
        .children
        .iter()
        .find(|child| {
            matches!(
                child.kind,
                Kind::GlobalPrivilegeTarget
                    | Kind::DatabasePrivilegeTarget
                    | Kind::ObjectPrivilegeTarget
                    | Kind::WildcardQualifiedObjectTarget
            )
        })
        .unwrap()
}

pub(crate) fn access_target(statement: &Node) -> PrivilegeTarget {
    let target = target(statement);
    let scope = match target.kind {
        Kind::GlobalPrivilegeTarget => PrivilegeScope::Global,
        Kind::DatabasePrivilegeTarget => PrivilegeScope::Database,
        Kind::ObjectPrivilegeTarget => PrivilegeScope::Object,
        Kind::WildcardQualifiedObjectTarget => PrivilegeScope::WildcardQualifiedObject,
        _ => unreachable!(),
    };
    let object = target
        .children
        .iter()
        .find_map(|child| match child.kind {
            Kind::TablePrivilegeObject => Some(PrivilegeObject::Table),
            Kind::ProcedurePrivilegeObject => Some(PrivilegeObject::Procedure),
            Kind::FunctionPrivilegeObject => Some(PrivilegeObject::Function),
            _ => None,
        })
        .unwrap_or(PrivilegeObject::Unspecified);
    PrivilegeTarget {
        scope,
        object,
        explicit_database: child(target, Kind::TargetDatabase).is_some(),
    }
}

pub(crate) fn access_target_database(statement: &Node) -> &[u8] {
    child(target(statement), Kind::TargetDatabase).map_or(&[][..], |database| database.children[0].text.as_bytes())
}

pub(crate) fn access_target_object(statement: &Node) -> &[u8] {
    child(target(statement), Kind::TargetObject)
        .unwrap()
        .children[0]
        .text.as_bytes()
}
