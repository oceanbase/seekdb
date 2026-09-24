# GIS SQL extension package

`gis.control` and `gis--1.0.sql` describe the complete current GIS scalar SQL
surface: 82 names and 106 native declarations. CMake's `plugins` installation
component delivers both files to `share/seekdb/extension`. The DSO registers
implementation-only functions: its presence alone does not publish SQL names.
This remains experimental; live installation and recovery are not yet verified.
Do not replace the DSO on an existing instance without installing the database
SQL objects and validating its dependent callers.

Each declaration binds a canonical implementation in `org.seekdb.gis` through
the stable C ABI. SQL names and aliases belong to distinct catalog routines;
no `RETURN` wrapper or private dispatch-function name is used. The control
file's `MODULE_PATHNAME` replacement is the controlled module ID, not a caller-
selected filesystem path. Algorithm code remains in the C/C++ GIS module.

Optional arguments are represented by separate signatures, e.g. 2D/3D
`ST_MakePoint`, one/two-argument geometry parsers and the optional radius of
`ST_Distance_Sphere`. Collection constructors have a final `VARIADIC GEOMETRY[]`
parameter. Buffer has a two-argument signature and a signature with a repeating
strategy tail. An omitted argument is not replaced with NULL: explicit NULL
must retain the implementation's existing NULL propagation.

SQL types are the current SeekDB ABI representations, not PostgreSQL OIDs:
integer/boolean controls use BIGINT, geometry uses GEOMETRY, and arbitrary byte
inputs use LONGBLOB. Text and binary results retain distinct SQL collations.
The descriptor's arity limits are still enforced during exact binding.

Validation:

```sh
python3 rust/plugin-runtime/tests/test_gis_declarations.py
python3 rust/plugin-runtime/tests/gis_sql.py --build-dir build_plugin_overlay_verify
```

The first command checks every declared module SQL name and accepted arity,
element types, canonical implementation mapping and ambiguous/missing overloads.
The second reads the actual control/SQL files through the package loader and
admits each declaration using the real SQL parser, CREATE resolver and loaded
GIS DSO; it also checks routine serialization. These tests do **not** install
same-name overloads into a database or prove transactional extension installation.
The declarations are additionally staged together in an owned, transaction-local
schema view with controlled IDs and Root's real slot assignment. All 106 signatures coexist across 82
name families. The same owned schemas are placed in a controlled schema-manager
snapshot and local guard cache; the name-family index and guard return the same
candidates for all 82 names. This verifies in-memory candidate lookup, not Root
ID allocation, loading a persistent catalog or signature-aware execution privileges.

The SQL regression also executes ordinary GIS calls against these native
database routines, rather than the global module registry, and checks empty-
database isolation, qualified calls, LOB transport and more than 1024 batch rows.
Real-server install/update/drop, two-database isolation, failure rollback and
restart recovery remain required gates. Full PostgreSQL array calls and empty
array semantics are separate unfinished capabilities; expanded collection calls
currently require at least one element.
