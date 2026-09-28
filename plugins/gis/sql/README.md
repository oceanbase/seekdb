# GIS SQL extension package

`gis.control` and `gis--1.0.sql` describe the complete current GIS scalar SQL
surface: 82 names and 112 native declarations. CMake's `plugins` installation
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
`ST_MakePoint`, one/two/three-argument ordinary WKT/WKB parsers and the optional radius of
`ST_Distance_Sphere`. Collection constructors have a final `VARIADIC GEOMETRY[]`
parameter. Buffer has a two-argument signature and a signature with a repeating
strategy tail. An omitted argument is not replaced with NULL: explicit NULL
must retain the implementation's existing NULL propagation.

Ordinary `ST_GeomFromText`/`ST_GeometryFromText` and WKB counterparts accept a
third `axis-order` option. `ST_AsText`/`ST_AsWKT`/`ST_AsWKB`/`ST_AsBinary` accept
it as their second argument. Values are `srid-defined` (also the omitted/blank
default), `long-lat` and `lat-long`. Nonzero SRIDs require the host SRS catalog.
For EPSG:4326 the default textual/WKB order is latitude, longitude; internal
geometry remains X/longitude, Y/latitude. Explicit axis options do not swap
Cartesian/projected coordinates. PG-style functions use separate conventions:
`_ST_GeomFromEWKT` accepts one text argument, with optional `SRID=...;` prefix;
`_ST_GeomFromEWKB` reads SRID from its binary header and accepts an optional
axis-order string (NULL means the default longitude-first order). `_ST_AsEWKB`
emits the SRID/Z flags and the optional root SRID field. The original EWKB
little-endian-only admission and M rejection are retained. Geography text
constructors take one argument, default to 4326 (also for `SRID=0`), require a
geographic SRS and reuse the original degree-based coordinate folding.
The formerly advertised extra numeric arguments to EWKT/geography constructors
were incorrect and have been removed. `_ST_AsEWKT` output/precision remains
unfinished; see the plugin README for test limits. Replacing a DSO does not
migrate already installed experimental routine signatures.

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
schema view with controlled IDs and Root's real slot assignment. All 112 signatures coexist across 82
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
