// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;
use seekdb_extension::{
    catalog::{
        CatalogContext, Installer, RoutineKind, Service, TransactionContext, TransactionalInstaller,
    },
    schema::{scalar_wrapper, SqlAccess, SqlType},
};

struct TextCatalog;
impl Installer for TextCatalog {
    fn prepare(
        instance: *mut sys::Handle,
        context: &mut CatalogContext<'_>,
    ) -> seekdb_extension::Result<()> {
        validate_instance(instance)?;
        // The module may back multiple SQL packages. Opt into this one's object
        // set, without assuming every association wants the same SQL names.
        let name = match context.extension_name() {
            "rust_text_ops" => "rust_runtime_length",
            "rust_text_native" => "rust_native_length",
            _ => return Ok(()),
        };
        {
            let sql = scalar_wrapper(
                name,
                &count_function::DEFINITION,
                &[("input_text", SqlType::Text)],
                SqlType::BigInt,
                SqlAccess::NoSql,
            )
            .map_err(|_| sys::INVALID)?;
            context.declare_sql(&sql)?;
        }
        Ok(())
    }
}
impl TransactionalInstaller for TextCatalog {
    fn build(
        instance: *mut sys::Handle,
        context: &mut TransactionContext<'_>,
    ) -> seekdb_extension::Result<()> {
        validate_instance(instance)?;
        if context.extension_name() != "rust_text_built" {
            return Ok(());
        }
        // Inspect this installation view without adopting an existing object.
        if context
            .lookup_routine(RoutineKind::Function, "rust_built_length")?
            .is_some()
        {
            return Err(sys::INVALID);
        }
        let sql = scalar_wrapper(
            "rust_built_length",
            &count_function::DEFINITION,
            &[("input_text", SqlType::Text)],
            SqlType::BigInt,
            SqlAccess::NoSql,
        )
        .map_err(|_| sys::INVALID)?;
        let first = context.create_routine(&sql)?;
        if context.lookup_routine(RoutineKind::Function, "RUST_BUILT_LENGTH")? != Some(first) {
            return Err(sys::INTERNAL);
        }
        if context
            .lookup_routine(RoutineKind::Procedure, "rust_built_length")?
            .is_some()
        {
            return Err(sys::INTERNAL);
        }
        let second = context.create_routine(
            "CREATE FUNCTION rust_built_nonempty(v TEXT) RETURNS INT DETERMINISTIC NO SQL SQL SECURITY INVOKER RETURN rust_built_length(v) > 0;")?;
        if first.value() == second.value() {
            return Err(sys::INTERNAL);
        }
        Ok(())
    }
}
static SERVICE: sys::CatalogServiceV2 = Service::<TextCatalog>::V2;
pub(super) const fn provide() -> sys::ServiceProvide {
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-text.catalog.install".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 1,
            patch: 0,
        },
        service: (&SERVICE as *const sys::CatalogServiceV2).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    }
}
