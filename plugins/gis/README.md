# seekdb GIS plugin

**Experimental and not a completed semantic migration.** The first Cartesian
backend extraction reuses SeekDB's bundled Boost.Geometry, replacing bounding-box
relations, vertex-only distance and rectangle-only overlay. The four original
regressions now pass. Cartesian area, length, centroid, buffer, interior-point
selection, topological validity, polygon repair, box clipping and the Cartesian
MVT pipeline now also use shared algorithms. GeoHash reuses the original
bounding-box precision and encoding algorithms; BestSRID reuses the geographic
box and projection-selection pipeline. General SRS and other algorithms still need
migration; SQL package/binding tests passing does not make the complete plugin
equivalent to the existing SeekDB GIS or PostGIS.
Do not use this implementation for correctness-sensitive GIS processing.

This SQL extension binds database-owned routines to the GIS execution SPI and returns
host-owned geometry payloads. It provides `POINT`, `ST_MakeEnvelope` and
2D/3D `ST_MakePoint`, plus byte-oriented `ST_X`, `ST_Y`,
`ST_SRID`, `ST_AsWKB`, `ST_AsBinary`, `_ST_GeometryType`, `ST_IsValid`,
`ST_AsText`, `ST_AsWKT`, `ST_GeomFromWKB` and `ST_GeometryFromWKB`
accessors, plus `_ST_SetSRID` geometry metadata mutation, `ST_Area`, `ST_Length`,
`ST_Distance`, WKT/GeoJSON constructors, collection constructors,
relations, transforms, topology/buffer/MVT operations and GeoHash.
The two spatial-index scalar names are NULL placeholders, not an index backend.
The native module registers implementation-only
function descriptors and supporting type/cast objects; the SQL package owns the
public function names, aliases and overloads in each database.
At the algorithm boundary, `POINT` takes two non-null little-endian
IEEE-754 `double` values (`x`, `y`); `ST_MakeEnvelope` takes four
(`xmin`, `ymin`, `xmax`, `ymax`) with default SRID 0. `ST_MakePoint` accepts the
same 2D arguments or an additional `z`. The result type is
`org.seekdb.gis.geometry`; Point and PointZ payloads are 26 and 34 bytes, while
the envelope polygon payload is 98 bytes, all using seekdb's
SRID/version/byte-order/WKB wire format.

The package intentionally contains no seekdb private headers or libraries. With
core GIS disabled, ordinary GIS SQL names resolve through database routine
schemas and the generic native `ObExprUDF` bridge to `PluginFunctionExpr`.
The SQL declarations specify types, overloads and compatibility aliases
(`POINT`, `AREA`, `CENTROID`, collection constructors and underscore-prefixed
names); the module checks exact ABI types/arity and implements NULL propagation
and numeric/SRID conversion.
The generic host adapter evaluates arguments, materializes LOBs, binds the
catalog identity and returns the result in the appropriate SQL datum format.
There is no plugin-mode `ObExprSTArea` implementation. Other legacy numeric
expression entries remain for internal spatial plan construction; `spatial_cellid`
and `spatial_mbr` return NULL without evaluating an operand or calling a plugin,
as in the original core. Their SQL-package compatibility routines also return
NULL (ordinary native-routine argument evaluation still applies).
Unqualified `spatial_cellid`/`spatial_mbr` remain reserved built-in names for
generated index columns; qualify a package routine with its database name to
exercise its native binding. Ordinary GIS functions do not have this exception.
A missing/inactive plugin
does not fall back to a built-in implementation of the public GIS SQL names.

To add an ordinary GIS function, add its implementation service and typed
descriptor and SQL declaration to this package; do not add an expression class, name registration
or function-specific result sink to the kernel. Core-GIS-enabled builds keep
their original built-in implementations.

SQL decimal/decimal-integer arguments are transported as exact decimal text and
converted by the plugin, not read as floating-point bit patterns. WKT and WKB
results use the generic `core.type.text` and `core.type.blob` representations to
retain long-value capacity and text/binary SQL collations. The host wraps and
unwraps their LOB representation at the common call boundary.

This migration covers ordinary scalar SQL calls, not new spatial optimizer or
index integration. Public calls now follow the generic native-routine path's
generated-column restrictions; enabling them in generated columns requires
durable function-dependency tracking, not merely marking a callback immutable.

Spatial indexing is a separate, still pending migration. The original DAS writer
expands a geometry into multiple S2 covering cells and an index MBR; the range
generator also needs deduplicated ancestor cells, descendant ranges and query
covering options, and the scan iterator applies the MBR filter. A centroid/Morton
scalar cannot replace these operations. The approximate Morton path and generic
32-byte envelope have been removed. In particular, the original MBR uses two
doubles for a point and four doubles in latitude/longitude interval order for
other geometry, including wrapped geographic intervals; it is not an arbitrary
`xmin,ymin,xmax,ymax` tuple. Core-GIS-off spatial writes/range generation remain
explicitly unsupported until this complete S2 path is connected and tested.
The topology probe covers the placeholder C ABI (including error propagation);
the SQL fixture separately verifies the internal expressions and database-qualified
package calls, with per-service DSO call counts. These are not spatial-index
storage tests.

The first S2 backend is now available as the leased execution service
`org.seekdb.gis.index.cover`, using `spatial_index_spi.h`, **not** a SQL routine
or a complete index access method. Its byte-only request carries resolved SRS
metadata (geographic longitude/latitude degrees or authoritative projected
bounds); its bounded result carries covering cells, optional unique ancestors,
legacy vertex-cell "inner cover", and the original geometry MBR. There are no
S2/STL objects or borrowed output pointers in this protocol. SRS resolution,
ellipsoid-to-angle conversion and storage row codecs remain host responsibilities.

`s2_covering.hpp` extracts the original visitor's per-region union, 4/50-cell
write/query options, full-range fallback, ancestors, vertex cells, geographic
MBR expansion and face-0 ST/UV mapping. Both the core visitor and plugin use
these algorithms. The query-window flag explicitly selects 50 cells; ordinary
write, covered-by and geographic distance-buffer modes retain 4, matching the
original three adapter constructors. The plugin preserves the 1%-inset bounds/envelope retry and
UINT64_MAX outside sentinel; callers must never interpret that sentinel as an
S2 cell. Getter buffering now works on copies, and the core retry reconstructs
its bounder after destruction. Plugin geometry adaptation normalizes projected
directions to unit vectors and omits the duplicate closing WKB vertex from an
S2Loop; invalid S2 geometry is rejected explicitly, not passed to fatal CHECKs.
These admission/representation fixes still require broad legacy differential
coverage before spatial-index activation; they are not a persistence-compatibility
claim. Projected query buffering must be done before the cover request.

The module statically links the same prepared S2, Abseil and OpenSSL BIGNUM
archives as the original algorithm, through individually validated private
targets. It does not link server libraries or introduce GEOS/PROJ. Exact Unix
system-library leaves are allowed; raw vendor archives/flags, link directories,
generator expressions and targets impersonating system libraries are rejected.

Verification: 200 direct-original-coverer comparisons; leased actual-DSO tests
for point/line/polygon/collection, ancestor uniqueness, bounds retry, geographic
date-line/buffer behavior, XY indexing of Z values and ABI failures; six GIS/
dependency CTests; isolated C/C++ compilation of all 12 installed SDK headers;
production plugin build/binary audit; existing real SQL/LOB fixture. The core
visitor passes production-flag syntax checking with core GIS enabled. **DAS
write expansion, range generation, MBR scan filtering and live storage tests
are still pending; core-GIS-off index admission remains disabled.**

The next host bridge is implemented for MBR candidate filtering:
`org.seekdb.gis.index.filter` uses the original Cartesian containment/intersection
and S2 geographic rectangle predicates (including date-line wrapping and the
geographic point-pair `ApproxEquals` shortcut). Its one-byte result means
**reject the candidate**, not exact geometry truth. Core-GIS-off
`ObSpatialMBR::filter` calls this leased service; the core-GIS-on implementation
shares `s2_mbr.hpp`. Unsupported operations and invalid coordinate intervals
are rejected before calling S2, including the previously accidental point-pair
shortcut for unsupported operations.

`spatial_mbr.hpp` shares only the original native-endian storage byte codec:
16 bytes `[xmin,ymin]` for points, 32 bytes `[ymin,ymax,xmin,xmax]` otherwise.
It checks exact length and finite coordinates, handles unaligned bytes via
`memcpy`, and preserves the output on failed decode. The host bridge contains
no S2/Boost algorithm reference. Plugin failures, missing/duplicate emissions,
wrong result type/size/boolean values and nonzero reserved fields cannot publish
a partial filter decision. Default MBR flags are now initialized in both profiles.

Verified by 192 direct S2 predicate comparisons, real leased DSO ABI tests, and
the production-object SQL/LOB fixture exercising the host codec/filter bridge
and malformed-producer/error paths. The new host object was compiled, archived
and linked into seekdb; core-GIS-on MBR code passed production-flag syntax
checking. This is still **not a live storage scan test**. At that stage, DAS
scan entrypoints, range generation and writes remained gated; the later DAS
write slice below updates row-generation status. No instance was deployed
or restarted.

Core-GIS-off `ObS2Adapter` now obtains its covering through the leased service
as well. Cover service **1.1** adds an opt-in `ALL_VIEWS` request and a v2 result
containing cover cells, unique ancestors, vertex cells and the legacy interleaved
query-cell sequence. Existing v1 requests retain their original result layout;
the execution SPI itself is unchanged. One invocation produces an owned host
snapshot, so getters neither retain plugin buffers nor mix provider generations.
The original constructor choices remain 4 cells versus 50 for query windows,
with geographic angular buffering passed to the original S2 implementation.

`org.seekdb.gis.index.cells` provides bounded batches of original S2 descendant
ranges and ancestors. Invalid IDs fail before S2 level/range operations; the
outside-bounds sentinel maps only to its exact key and has no ancestors. The
host no longer fabricates zero ranges on failure, and the legacy range caller
checks the returned status. Failed initialization is retryable, successful
initialization cannot silently be reused, and malformed/duplicate callbacks or
errors after emission do not publish a partial adapter state or metadata output.

These bridges alone do **not** establish full spatial-index support: complete
DAS range/scan integration, persistent-index plugin lifecycle and live storage
acceptance remain pending. Cached covering snapshots own bytes, but are not
persistent-index dependency or provider-generation pinning.

Verification of this bridge: 186 direct S2 cell-level/range comparisons across
all six faces, v1/v2 view equivalence and batch metadata tests through the real
DSO, and production-object SQL/LOB tests covering snapshot ownership, retries,
query options, empty/outside/buffer cases and nine faulty-producer modes. Six
GIS/boundary CTests and the 12-header installed SDK check pass. The production
host object was rebuilt, archived and relinked; core-GIS-on adapter/range code
passed production-flag syntax checks. This is partial-build and offline fixture
evidence, not a clean full build or live spatial-index acceptance.

The subsequent DAS write slice removes the core-GIS-off rejection in
`ObDASDomainUtils::generate_spatial_index_rows`. It now uses the leased S2
adapter for covering and MBR, leaving only projection/row assembly in the host.
The host reads the routing SRID from the plugin geometry envelope v1 (byte 1,
not the legacy SWKB 0x41 marker); the plugin still validates the full geometry.
This does not add legacy payload conversion or a public ABI. Invalid header,
projection or SRS inputs fail before row publication. Allocation/reshape/append
failure removes only rows appended by this call, preserving earlier output.

The production-object SQL fixture calls the actual DAS row generator with a
converted write-plan fixture and an SRS snapshot built by the real catalog-row
parser. It verifies planar/geographic points, lines, empty collections,
outside-bounds sentinel, cell/MBR/composite-primary-key layout, missing SRS,
bad projections, missing provider and nine faulty plugin responses. A 17-column
row forces allocations beyond inline datum capacity; each allocation failure,
including failures after row publication starts, restores the original output.
Full SQL/LOB and nine GIS/boundary CTests pass. The DAS and routing-header objects
were rebuilt/archived and seekdb relinked; the core-GIS-on DAS Unity unit passes
production-flag syntax checking. This verifies row generation, **not storage
writes, index DDL/backfill, range generation or scan execution**. Those paths
and durable module dependencies remain unfinished. No server was deployed or
restarted.

The DAS scan slice enables the existing spatial scan/sort/lookup-tree factory
in core-GIS-off builds and routes its scalar and batch MBR filtering through
the leased plugin service. Batch reads no longer inherit the unfiltered base
scan implementation: retained rows are compacted across all outputs, including
transaction metadata, without fetching another batch while payloads remain
borrowed. A final nonempty batch retains `OB_ITER_END`; errors return zero rows.
Rebinding scan parameters clears the previous whole-range bypass flag. MBR
validation accepts binary VARCHAR storage (the `is_varchar()` predicate excludes
VARBINARY), while rejecting NULL, other types and malformed payloads.

`gis_das_scan_fixture.h` exercises the real production iterator with a controlled
storage row source and actual GIS DSO. It covers scalar/batch filtering, ORed
ranges, rebind transitions, optional transaction metadata, all-rejected batches,
compaction, capacity limits, final rows, missing/faulty providers and storage
errors. This does not execute a real tablet scan, sort/lookup tree or index DDL;
range generation, backfill, persistent-index module dependencies and live
storage acceptance remain unfinished.
The complete SQL/LOB fixture including this scan suite, nine GIS/boundary
CTests, source boundary and new-file license checks pass. The production DAS
Unity object was rebuilt/archived and seekdb relinked; the same Unity source
passes core-GIS-on production-flag syntax checking. No server was deployed or
restarted.

The plugin owns recursive WKB decoding/encoding and WKT/GeoJSON codecs.

The table-scan backfill slice removes the core-GIS-off gate from
`ObTableScanOp::inner_get_next_spatial_index_row`. The host's
`get_cellid_mbr_from_geom` bridge obtains the original S2 covering and MBR from
the leased plugin adapter, then publishes caller-owned storage bytes and cell
IDs. Failed calls preserve the caller's existing cell prefix and MBR buffer.
No geometry algorithm or new public ABI is added to the core.

Backfill skips SQL NULL and empty collections until a real covering is found;
zero-length non-NULL geometry is invalid. This avoids returning stale generated
columns from the preceding source row. SRS lookup failures propagate, partial
coverings are discarded on error, and generated-row position advances only
after projection succeeds. Cache initialization validates unique geometry/cell/
MBR outputs and publishes state only after all allocations succeed, preserving
allocation errors and allowing retry after initialization failure.

The backfill fixture uses the real table-scan operator, a controlled DAS source
and actual GIS DSO. It does not perform tablet IO, DDL checksum reporting or a
committed index build. The existing 32-cell per-source-row backfill bound remains;
larger coverings are rejected rather than truncated. Optimizer range generation,
persistent-index plugin dependency and live storage/index acceptance still need
completion.
The complete production-object SQL/LOB suite passes, including NULL/empty
skipping, multi-cell/PK association, planar/geographic and outside-bounds rows,
raw/in-row LOB input, missing SRS/provider, nine producer faults, malformed
geometry, storage error and all three cache allocation failures followed by
retry. Nine GIS/boundary CTests, source boundary and new-file license checks
pass. The table Unity and SPI bridge objects were rebuilt/archived and seekdb
relinked; the table Unity passes core-GIS-on production-flag syntax checking.
No instance was deployed or restarted.

The range-generation slice enables the five core-GIS-off spatial range
entrypoints in `ObRangeGenerator`. Geometry covering, ancestor and child-cell
metadata still come from the leased plugin's original S2 implementation; the
host assembles index keys and MBR filters. Planar distance queries use the
service-only `org.seekdb.gis.index.planar_buffer`, reusing the existing buffer
algorithm after authoritative SRS lookup. Its label adapter permits projected
SRIDs without transforming coordinates; it does not admit unknown/geographic
SRS or extend the algorithm's dimensionality. Geographic distance queries keep
the original S2 angular-buffer path. The SQL package remains 82 names and 112
declarations; this internal service adds no SQL routine.

SRS lookup errors retain their identity, non-finite distances are rejected, and
child-cell lookup failures propagate. Empty/NULL geometry materializes the
conservative whole-range fallback without publishing an empty/NaN MBR. Failed
generation removes newly appended ranges/MBRs and restores the single-value
flag. This is tested with initially empty output arrays; preservation of an
arbitrary preexisting range list across an in-place merge is not asserted.

`gis_range_fixture.h` constructs pre-range graphs through public APIs and runs
the actual generator and DSO. It is **not** evidence that database-owned native
SQL routines select a spatial index: at that stage `is_spatial_expr()` and
domain-op mapping recognized only builtin expression kinds. The next slice
below adds native predicate extraction. Durable index/module dependencies, tablet IO and live
index-build/query acceptance also remain open.

The complete production-object SQL/LOB regression passes with the new range
fixture: point interval/ancestor comparison, intersect/covers/covered-by,
planar and geographic distance, projected SRID 32631, unconstrained column SRID,
outside-bounds sentinel, empty/NULL fallback, malformed headers and OR branches,
missing SRS/service, timeout and nine producer faults. Buffer error paths retain
the caller's output; service type/reserved-field validation is exercised.
Nine GIS/boundary CTests, the source boundary check and new-file license check
pass. SQL range/common and share bridge objects were rebuilt, archived and
seekdb relinked; the range Unity also passes core-GIS-on syntax checking.
This is incremental-build/offline evidence, not a clean build or live acceptance.

Native spatial planner support is now declared by the implementation descriptor,
using one of `SPATIAL_INTERSECTS`, `SPATIAL_COVERS`, `SPATIAL_WITHIN`, or
`SPATIAL_DWITHIN`. The registry admits these only on typed, fixed-arity,
implementation-only, deterministic, immutable, NULL-propagating scalar
functions. They describe conservative candidates for the existing spatial
index format, not arbitrary index implementations or exact predicate truth.
SQL names are irrelevant: aliases acquire metadata through owner/object-ID
binding. Raw UDF copies/hashes retain the flags; code generation checks them
again against the live binding, and runtime still validates catalog/ACL/leases.
The original native predicate must remain as a residual filter.

An immutable native geometry constructor such as `POINT(...)` can be retained
as a range input expression instead of being executed by generic PL constant
folding. All nested UDFs must have verified immutable native metadata. This
does not remove the planning-time prohibition on executing ordinary PL UDFs.
The actual SQL extraction fixture and complete SQL/LOB regression pass: parsed
table predicates produce domain nodes and then real ranges/MBRs through native
`POINT` evaluation and the DSO. Covers/within argument orientation, distance,
package aliases and a custom SQL name bound to intersects are covered. A routine
named `st_intersects` bound to distance receives no spatial strategy. Graph
extraction executes no native constructor; removing its immutable proof falls
back without executing it. Copying preserves support metadata, and changing
the bound strategy before codegen is rejected. Ten GIS/registry/boundary CTests
and Rust/C ABI checks pass; three changed SQL Unity units also pass core-GIS-on
syntax checking. This is incremental-build/offline evidence, not live index IO.

The simple-filter-column collector now also recognizes verified native spatial
strategies, including three-argument distance predicates. The real SQL fixture
checks both argument orientations, custom names, package aliases, same-name
non-spatial implementations, same-table column/column comparisons, unrelated
table IDs, constant-only predicates and AND/OR deduplication. Collection does
not execute plugin callbacks. With a controlled spatial-index schema, the
collected geometry column passes the actual schema-backed index-candidate
matcher. Ordinary UDFs remain excluded and ordinary comparison collection is
unchanged. The complete SQL/LOB regression, ten related CTests, source boundary
check and the changed rewrite Unity's core-GIS-on syntax check pass. This still
does not execute live index IO.

The fixture now also invokes the real `ObOptimizer::optimize` entrypoint with
controlled table/index schemas, local address/tablet identities and default
statistics (an internal session avoids requiring a statistics SQL backend).
Seven unhinted queries covering intersects, both covers orientations, within,
distance, a package alias and a custom SQL name select the spatial index. A
same-name routine bound to the distance implementation selects the base table.
The selected spatial scans retain an index-back plan, a non-whole range graph
and the original exact predicate as a filter. The fixture then invokes the real
physical code generator and checks the resulting MBR access expression,
spatial scan flag, row-key sort and table-lookup attachment for the index path;
the base-table path is checked to have no lookup attachment. These assertions
and the complete SQL/LOB regression pass, along with ten related CTests. No
production planner change was needed for this step. The fixture still does not
execute tablet IO or establish production statistics quality or performance.

For diagnosing this offline regression, `gis_sql.py --keep-artifacts` prints and
retains its isolated temporary directory (binary, DSO, package copies and logs).
Without that option it continues to clean up automatically. Retained artifacts
can be large and must be removed by the developer when no longer needed.

The SRS migration now has a shared **WKT1 grammar** in
`include/seekdb/geo/srs_wkt_grammar.hpp`, extracted from the original
Boost.Spirit parser. Both the legacy core adapter and `srs_parser.cpp` use it;
the latter owns strings, optional authority/axis defaults, datum seven-parameter
transforms and ordered projection parameters independently of core containers.
Grammar productions are retained, including mandatory geographic axes, optional
projected axes, bracket/parenthesis forms and case-insensitive keywords.
Empty/whitespace input is checked before indexing the final character; core
string/container allocation failures now propagate instead of being ignored.
The obsolete per-string arena wrapper has been removed.

`plugin_gis_srs_parser` parses all **5,151** built-in catalog definitions (483
geographic, 4,668 projected, 39 distinct projection authorities). To additionally
compare every parsed field against the real core adapter, including allocation
failure and whitespace regressions, run:

```sh
python3 rust/plugin-runtime/tests/gis_srs_parser.py --build-dir build_release
```

This is raw parse-record parity using the shared grammar, not an independent
projection-algorithm differential test. The parser remains a private component;
the metadata service described below exposes only numeric C ABI records.
The core-GIS-off host bridge is described below; catalog lifecycle integration
and general projection transforms still need migration.
It does not lift the current SQL SRID restrictions or spatial-index admission
gates. No SRS/STL object is added to the public C ABI.
The production plugin build/binary audit, actual-core adapter parity run, seven
GIS/boundary CTests and existing SQL/LOB fixture pass. Both core parser and SRS
factory sources pass core-GIS-on production-flag syntax checks. These are
component tests, not a clean full server build or live SRS/storage acceptance.

The next SRS slice adds the leased execution service
`org.seekdb.gis.srs.describe` (1.0). It accepts bounded WKT1 and an explicit SRID,
then emits copied POD metadata: geographic base ellipsoid, angular/linear units,
prime meridian, outer/base axes, WGS84/TOWGS84 state and ordered required
projection parameters. The host must still resolve authoritative catalog data,
bounds and proj4 text; the service does not guess definitions from SRIDs or
perform coordinate transforms. Unknown factory methods retain the original
zero-method fallback, which is not a claim of transform support.

`srs_projection_parameters.hpp` shares all 39 original factory parameter lists;
`srs_semantics.hpp` shares WGS84 classification, unit/axis/prime-meridian
normalization and semi-minor-axis calculation. The core factory now propagates
parameter-registration allocation errors. Cassini-Soldner's former erroneous
Mercator return type is corrected to EPSG 9806, without changing its original
parameter list. Numeric authority parsing retains the original prefix/sign and
integer-narrowing behavior; this migration does not silently redefine it.

`gis_srs_parser.py` additionally compares actual core-factory metadata and unit
conversions over all 5,151 definitions. The DSO fixture covers the new service,
WGS84/seven-parameter cases, Cassini/transverse-Mercator requirements, duplicate
parameter precedence, unknown-method fallback, malformed requests and callback
errors. No C++/STL/SRS object crosses the service boundary. **Host SRS-cache
integration, general projection transforms and live spatial-index acceptance
remain incomplete**, and SQL SRID/index admission gates have not been opened.
Verification for this slice: production DSO build/export audit and leased loader
tests, actual-core factory/normalization parity, seven GIS/boundary CTests,
13 installed SDK headers compiled separately as C and C++, and the existing
SQL/LOB fixture all pass. No server instance was deployed or restarted.

The core-GIS-off `ObSrsWktParser::parse_srs_wkt` now invokes the leased describe
service and validates/copies its response into a host-arena-owned `PluginSrs`.
Its vtable belongs to the host; it retains no plugin pointers, STL allocations
or lease, so bulk arena destruction and reads after module shutdown are safe.
Malformed/duplicate results, service errors and allocation failures leave the
caller's output untouched. Catalog bounds and proj4 text remain host-owned.
Common axis/unit accessors and the original 244 PG-reserved SRID proj4 strings
are available in both build profiles without linking Boost/S2 into the host.
The shared geographic formatter also fixes spheres to emit `+b=semi_major`
instead of the former invalid `+b=0`.

`gis_sql.py` exercises this bridge through the real DSO, producer/allocation
faults, input/output ownership and an actual `ObSrsCacheSnapShot` reserved entry
read after DSO shutdown. This does **not** validate catalog SQL refresh or
snapshot generation provenance: one describe lease does not pin a whole cache
refresh against plugin replacement. Generation binding/invalidation, general
projection transforms and live index/storage acceptance remain open; the SQL
SRID and index gates stay closed.
This slice passes production-object compilation/server relinking (not a clean
full rebuild), core-GIS-on syntax checks, the complete SQL/LOB fixture, all
5,151 core-factory parity cases, seven GIS/boundary CTests, real production-DSO
loader checks and independent C/C++ compilation of 13 installed SDK headers.

`include/seekdb/geo/cartesian_algorithms.hpp` contains source-level algorithm
entry points shared with the legacy `src/share/geo` adapters; it is not part of
the installed plugin C ABI. `cartesian_adapter.ipp` converts plugin-owned values
into owning Boost models. Only the project's prepared Boost headers are used,
not system Boost, core libraries or runtime calls back into legacy expressions.
No GEOS, PROJ or Rust algorithm dependency is added.

The Cartesian backend supports 2D Point/LineString/Polygon and their
homogeneous Multi variants for relations and distance, and Polygon/MultiPolygon
union, difference and symmetric difference. Ring winding is normalized without
changing shell/hole roles. Accepted SRIDs are 0 and 3857, with matching SRIDs
required. Until SRS and collection dispatch are extracted, unknown/geographic
SRIDs, Z, GeometryCollection relations/distance, mixed-dimensional overlays and invalid geometries
return an error rather than falling back to approximations. Empty distance
also returns an error rather than a fabricated zero. These admission limits
are temporary and **not** claims of full legacy empty/SRS/type compatibility.
C++ exceptions in these entry points are converted to C ABI statuses; failed
validation emits no result, and a failed result callback is not retried.
`ST_Distance_Sphere` remains a separate point-only implementation; it no longer
falls back to a Cartesian distance for unsupported geometry types.

Area, length and centroid reuse `ObGeoFuncArea/Length/Centroid`'s Cartesian Boost
entry points. Unary collection processing retains shell/hole roles and flattens
nested collections by dimension. Centroid uses the highest nonempty dimension
(polygon, then line, then point), with Boost's area/length weighting; an empty
centroid returns SQL NULL, not `(0,0)`.

`_ST_PointOnSurface` shares the original `ObGeoInteriorPointVisitor` scanline
and vertex-selection algorithms through `include/seekdb/geo/interior_point.hpp`.
It selects the widest interior polygon interval, excludes holes, prefers
interior line vertices, and selects an actual member for multipoints. Mixed
collections use the highest nonempty dimension. Collapsed polygons retain the
original first-vertex fallback; empty input returns an empty collection (unlike
Centroid's SQL NULL). This path does not use the stricter overlay validity gate.
It has the same temporary 2D, SRID 0/3857 admission limits as the Cartesian
backend. Non-finite centroid/distance calculations are rejected, not emitted
as a fabricated empty result.

Buffer uses the same Boost strategy combinations and defaults as
`ObGeoFuncBuffer`: 32 points per circle, round joins/ends, miter limit 5.
Negative polygon distances erode the shape; complete erosion returns an empty
collection. Negative point/line buffering is rejected, except the original
near-zero identity case. Collection buffers are combined by union.
`ST_Buffer_Strategy` now emits the original 12-byte `[uint32 type][double value]`
format (previously a 16-byte placeholder), consumed and validated by Buffer;
duplicate strategy categories and inappropriate geometry/strategy combinations
are rejected. PG-style text/quad-segment arguments still need migration.
Spatial cell selection, codecs and
general SRS operations remain on the migration checklist; no full equivalence
or release-readiness claim follows from these unary regressions.

`ST_IsValid` shares the original `ObGeoFuncIsValid` Cartesian predicate and
`ObGeoFuncCorrect` winding normalization. Like the original default geometry
build path, it closes unclosed rings before testing, without changing the input
payload. Self-intersections, exterior/overlapping holes, collapsed lines/polygons
and overlapping MultiPolygon members return false. GeometryCollection checks
members independently (member overlap alone is not invalid). Malformed geometry
and unsupported SRS/dimensions are errors, not false; the current admission
limits remain 2D and SRID 0/3857. Exceptions do not cross the C boundary.

`_ST_MakeValid` now shares `ObGeoFuncDissolvePolygon`'s single-polygon dissolution
and `ObGeoExprUtils::make_valid_polygon_inner`'s hole-classification/overlay
orchestration via `include/seekdb/geo/polygon_repair.hpp`. Self-crossing shells
use the original reversed-ring symmetric difference. Holes intersecting the
shell are unioned and combined with it by symmetric difference; exterior holes
become additional shells. Invalid MultiPolygons repair each member then union
the results. Correction preserves input ownership and SRID. All calculations
remain plugin-local; the shared helper has no SQL or server allocator dependency.

This preserves SeekDB's existing repair semantics, not a new PostGIS contract:
already-valid collections are retained, but a collection containing invalid
polygons does not recursively invoke polygon dissolution. A fully collapsed
polygon whose dissolution is empty retains its corrected input, so MakeValid
does **not** guarantee IsValid becomes true. Unsupported non-polygon repairs
return an error. These are explicit regression cases, not accidental omissions
masked by the old closure-only implementation. General SRS/Z handling and broad
legacy differential acceptance remain outstanding.

`_ST_ClipByBox2D` reuses `ObGeoBoxClipVisitor` through the source-level
`include/seekdb/geo/box_clip.hpp`, with separate core-arena and plugin-owning
container factories. It clips segments at their actual intersections, reconnects
polygon fragments clockwise along the box, and retains or assigns holes to the
resulting shells. It does not replace the shape with a bounding rectangle.
Point, line, polygon, Multi and mixed-collection output follow the original
visitor's type simplification. Empty clipping bounds yield SQL NULL; disjoint
input yields an empty collection. The original contained-input fast path keeps
boundary geometries, while the partial-overlap visitor excludes edge-only
points/segments. The second geometry supplies coordinate bounds, not a CRS
transform; output retains the first geometry's SRID. Current admission is still
2D, SRID 0/3857.

`_ST_AsMVTGeom` now follows the original Cartesian pipeline: select the highest
geometry dimension, apply the tile affine transform (Y inversion), snap to grid
with `rint`, remove adjacent duplicate and collinear points, clip at real box
intersections, repair polygon topology, then snap again (`floor` for polygons).
Affine/grid/zero-tolerance simplification reuse `tile_grid.hpp` in the original
core visitors and plugin; clipping/repair reuse the extracted algorithms above.
Subpixel lines and empty/collapsed results yield SQL NULL. Input SRID is retained.
The module descriptor is non-strict for this function: NULL input geometry
propagates, NULL bounds fail, and NULL optional controls use defaults 4096/256/true.
Extent and buffer must be integral and fit the original signed 32-bit limits;
clip must fit signed 8-bit and zero means false. The SQL package still declares
these controls as DOUBLE, so exact legacy resolver coercion/type-error parity
remains to be audited. General SRS, original-core differential and real-server
acceptance are still outstanding; this is not a full MVT compatibility claim.

`_ST_GeoHash` now shares `ObExprPrivSTGeoHash`'s automatic precision and encoding
through `include/seekdb/geo/geohash.hpp`. It encodes the bounding-box center,
not a centroid or weighted representative point. The original PG-expression Box
mode excludes polygon holes. Missing, NULL, zero and negative precision select
automatic precision: points use 20 characters; a shape crossing an initial cell
split can produce the non-NULL empty string. Empty geometry returns SQL NULL.
Explicit positive precision is no longer limited to 32; out-of-range coordinates
and signed-32-bit precision overflow are errors, not clamped/truncated output.
The plugin retains a 16 MiB output allocation budget (exhaustion returns NO_MEMORY).
The descriptor uses `core.type.int64` precision and per-argument NULL handling;
recreate experimental SQL bindings after replacing the DSO so cached unsigned
argument carriers do not survive the descriptor change.

As in the original expression's NULL-SRS geometry build, GeoHash reads raw X/Y,
ignores Z, and does not swap axes or project coordinates. Known SRIDs currently
are 0/4326/3857; general SRS existence lookup is not implemented yet. A nonempty
collection containing empty members is explicitly rejected where legacy Box
dispatch has undefined/uninitialized behavior. Full resolver coercion/error-code
parity and original-server differential acceptance remain outstanding.

`_ST_BestSRID` now uses `include/seekdb/geo/geographic_box.hpp`, shared with
`ObGeoBoxUtil` and `ObGeoExprUtils::get_box_bestsrid`. It bounds points and
great-circle segment extrema on the unit sphere, combines polygon rings and
pole coverage, and selects the original polar Lambert / UTM / LAEA / world
Mercator branch. It no longer echoes the input SRID or returns 3857 on mismatch.
Results belong to SeekDB's original private PG projection-ID family (999xxx),
not ordinary EPSG numbers. The catalog-backed transform can now resolve these
entries; the SQL regression includes `_ST_BestSRID` → 999031 → `ST_Transform`
using the host's original reserved-UTM WKT/proj4 builder. Full reserved-family
differential acceptance remains pending.

Extraction fixes defects in both core and plugin paths: the dot product's Z
term, inverted identical-point detection, an overwritten X/uninitialized Y
coordinate in arc-extrema calculation, and swallowed antipodal-arc errors.
Angular calculations clamp round-off to the trigonometric domain and handle
zero XY corners deterministically. Undefined legacy behavior is not preserved.
Both empty arguments select 999000; one empty argument contributes no box,
regardless of position. This also fixes the core's last-argument-empty shortcut.
Current nonempty admission is SRID 4326 with raw X/Y degrees, optionally Z
(ignored as in the original 3D-to-2D path). Projected, unknown SRS, out-of-range
coordinates, antipodal segments, and undefined nested-empty Box cases return
errors without publishing partial output. General SRS/axis/unit handling,
SQL coercion/error-code parity and original-server differential tests remain
release gates.

`ST_Transform` and `_ST_Transform` now opt into the SQL context and resolve both
nonzero SRIDs using one host `lookup_srs` batch. There is no hardcoded EPSG-pair
fallback. The plugin copies the raw catalog records, parses them under its
current implementation lease and runs the original Boost.Geometry projection
backend. Missing catalog support/records, bad definitions and failed transforms
are errors, not permission to relabel coordinates. SRID 0 only admits 0→0.

Stored WKB is longitude/X followed by latitude/Y, regardless of the WKT's axis
declaration. Geographic input/output use the shared original SRS direction,
angular-unit and prime-meridian conversions. Geographic proj4 text is generated
from the parsed ellipsoid/WGS84/seven-parameter metadata, as in the original
formatter; the prime meridian is applied there once, not duplicated in +pm.
Projected definitions use catalog proj4 text and its linear/vertical units.
Geographic systems missing WGS84/TOWGS84 and nonstandard projected axis
directions remain unsupported. SQL also rejects proj4 definitions without a
known datum, an explicit TOWGS84 shift or the identity-grid convention: Boost
otherwise silently skips datum conversion. Raw explicit-proj4 service callers
retain Boost's local/unknown-datum policy. Points, rings and collection members are visited;
query cancellation is polled before, every 1024 vertices and after computation.
All vertices must succeed before one result is emitted. System math remains a
private plugin dependency, not a kernel GIS callback.

The direct-DSO regression includes independent forward and inverse numerical
controls, hemispheres, high latitudes, PointZ, overflow and rejection cases.
For (2°, 49°), the old approximation produced northing about 5279943.671 m
instead of 6274861.394 m. This failure was reproduced before replacing the
approximation. Passing these controls is not proof of arbitrary CRS support.

The leased `org.seekdb.gis.srs.transform` service adds the general **explicit
proj4-definition** path. `srs_spi.h` documents its four byte-oriented arguments:
geometry, source/target definitions and output SRID. Definitions, not SRID
numbers, select the transformation, even when IDs match. The caller must supply
longitude/latitude radians relative to the definition's prime meridian (not raw
SQL geographic WKB); Boost applies its +pm exactly once. Projected coordinates
use the definition's linear units. All vertices, rings and nested
collections are transformed before one result is emitted. For 3D inputs, height
participates in Boost datum conversion rather than being blindly copied, using
proj4 vertical units (defaulting to the definition's linear units in Boost).

`include/seekdb/geo/projection.hpp` selects the same original Boost backend for
the core-enabled path and the plugin-private `Projection` owner. The original
core now checks Boost's failure return for both points and ranges. The plugin
constructs a transformer per call and retains no catalog/plugin-global cache.
Grid-backed definitions (including named NAD27), init-file expansion, geocentric
coordinates and non-ENU proj4 axes are rejected; no grid file is read or silently
ignored. The explicit identity-grid token `+nadgrids=@null`, used by the original
3857 catalog row, is accepted without a file. The SQL wrapper now supplies
catalog resolution and geographic normalization; full legacy SQL error-code
parity, constructor axis-option semantics and index gates remain outstanding.

`plugin_gis_projection` compares 1,000 points across ten definitions with the
original direct Boost range call and inverse controls. This is adapter parity,
not an independent projection algorithm. For Cassini, the original inverse has
up to 4.94359e-8 radians roundtrip error on this sample set; the adapter must match
that original inverse to 1e-12 radians, without claiming tighter roundtrip
accuracy. The other nine definitions also enforce a 1e-8-radian roundtrip bound.
The real-DSO loader fixture covers UTM
false origins, kilometres, prime meridians, 3D datum shifts, equal-ID/different-
definition calls, malformed ABI/definitions, empty input, tail failures and
callback errors. The existing Mercator SQL/LOB regression remains separate.
Verification for this slice: production plugin build/export audit, real-DSO
loader tests, all eight GIS/boundary CTests, complete SQL/LOB fixture, core-GIS-on
transform syntax check and independent C/C++ compilation of 13 installed SDK
headers pass. No clean full-core build, deployment or live catalog/storage
acceptance is claimed.

The host SQL API now has an append-only v5 suffix, `lookup_srs`, for a batch of
up to 16 nonzero SRIDs. It pins one SRS cache snapshot for the entire batch and
calls the consumer once only after every definition is available. The returned
POD records borrow raw catalog WKT, proj4 text and bounds for that callback;
plugins must copy anything they retain. This is a catalog snapshot, not a data
transaction snapshot or a persistent catalog epoch. Query deadlines,
cancellation, owning-thread and non-reentrancy rules apply, and failures remain
sticky on the query context.

`ObSrsCacheSnapShot` now owns deep copies of the raw definitions, including the
original PG-reserved entries. This gives a plugin the source bytes to parse
under its own implementation lease rather than consuming another generation's
parsed SRS objects. It does not solve generation binding for all existing
parsed-cache consumers. Catalog field/bounds extraction now preserves getter
errors instead of treating every numeric failure as a NULL bound.

The production-object `gis_sql.py` fixture covers actual catalog-row extraction,
snapshot ownership and the SQL API, including a provider switching snapshots
between batch acquisition and consumption, missing entries, invalid requests,
callback failure, reentrancy, thread checks and deadlines. SQL transport remains
controlled: this is not a live catalog refresh/concurrent-DDL acceptance test.
The next slice connects ordinary `ST_Transform` to this interface as described
above. Other GIS algorithms still have their own temporary SRS admission gates;
this is not a blanket claim of arbitrary-SRS support across the GIS package.
This slice passes affected production-object compilation/server relinking,
core-GIS-on syntax checks, the SQL/LOB fixture, eight GIS/boundary CTests, actual
DSO loading, SDK header installation checks and Rust SDK tests/Clippy, including
C/Rust layout checks for the new records and API. The full `kernel_script.py`
production-object regression also passes with the actual Rust text DSO, including
the existing SQL/query-catalog APIs through their older prefixes. No server was deployed or
restarted; full cache-refresh lifecycle and live storage acceptance remain open.

Catalog-transform regression uses the exact 4326, 3857 and 32631 records from
the repository's default SRS catalog, checking Mercator and UTM in both the real
DSO loader and real SQL/LOB expression path. Additional DSO cases exercise grad
units, west/south axes, a nonzero prime meridian, projected kilometres, 3D datum
shifts, missing/unknown datum definitions, missing
catalog support, unknown identity SRIDs, malformed/duplicate callbacks and
cancellation before/during/after a 2048-vertex transformation. The SQL fixture
uses the actual host snapshot and SPI with controlled catalog transport; it does
not prove live refresh, concurrent DDL, complete error-code parity or storage
acceptance. Cache refresh lifecycle and constructor/serializer SRS semantics
still need wider verification before release.
This slice passes the production DSO build/export audit and loader, the complete
SQL/LOB expression fixture (including the reserved-UTM composition), all eight
GIS/boundary CTests and the source dependency-boundary checker. No server was
started, deployed or restarted, and no live catalog/storage acceptance is claimed.

The SRS provider refresh lifecycle now transfers each old snapshot to exactly
one owner. Publication reads its reference count once, queues referenced
snapshots, and removes retired entries before disposing them. If queuing fails,
the unpublished candidate is discarded and the current snapshot is retained.
Refresh claims the existing invalidation flag before loading; a concurrent
`mark_stale()` remains pending, and failed refreshes restore the flag for retry.
Read/parse errors retain their original status rather than becoming an empty
catalog error. Both catalog queries explicitly use the `oceanbase` database.
Quiescent shutdown uses allocator reset instead of an explicit member destructor,
and repeated destroy/reinit is supported. Callers and guards must still drain
before provider destruction; this does not allow guards to outlive the service.

`gis_sql.py` now exercises the actual provider with a controlled SQL proxy,
including refresh failures, pinned old definitions, generated reserved SRS,
invalidation from another thread during a load, partial-row failure and
destroy/reinit. The proxy reports the import-ready count but supplies one
representative catalog row; this is not a full live catalog import. A separate
deterministic test of the production publication helper forces the final reader
release during retirement enqueue and injects queue allocation failure, checking
exactly-once disposal. The affected production object/server relink, core-GIS-on
syntax check, complete GIS SQL/LOB regression, eight GIS/boundary CTests and
source-boundary check pass. After rebuilding both affected observer Unity
objects and relinking, the GIS regression and the full `kernel_script.py`
regression with the actual Rust text DSO also pass. Full parsed-cache plugin-generation binding, live
refresh/storage and shutdown concurrency acceptance remain open.

Ordinary WKT/WKB I/O now resolves nonzero SRIDs through SQL API v5 and parses
the copied definition under the current plugin call. `ST_GeomFromText` /
`ST_GeometryFromText` and their WKB counterparts accept an optional third
`axis-order` argument; `ST_AsText`, `ST_AsWKT`, `ST_AsWKB` and `ST_AsBinary`
accept it as a second argument. The package now has 114 declarations over the
same 82 names. Blank/omitted options mean `srid-defined`; `long-lat` and
`lat-long` override geographical ordering, with case/ASCII whitespace handled
like the original option parser. Projected and SRID-0 coordinates are not
swapped. Internal WKB stays longitude/X, latitude/Y, so the EPSG:4326 default
input `POINT(49 2)` stores X=2, Y=49. Existing projection controls now explicitly
request `axis-order=long-lat` where their source text is longitude-first.

This I/O path checks ranges in SRS angular units but does not change datum,
prime meridian, axis signs or Z. It does not require `towgs84`, which is needed
only by the separate transformation path. Collections/rings recurse through the
same axis handling, and nonzero empty collections still require a catalog entry.
The original `GEOMETRYCOLLECTION EMPTY` and `GEOMETRYCOLLECTION()` inputs are
supported. Ordinary WKB rejects EWKB flags, including nested flags, rather than
letting an embedded SRID override the explicit argument. WKT parsing owns a
terminated input copy for numeric conversion and bounds collection recursion.
Nonzero-SRID I/O polls before/after traversal and every 1024 vertices; no partial
result is emitted on error/cancellation. PG-style EWKT/EWKB and geography I/O
remain separate, unfinished migrations, not aliases for these MySQL options.

The I/O regression includes independent coordinate controls, both WKB endian
orders, Z, aliases, default/explicit/blank options, local datums, grad units,
unknown IDs, malformed callbacks, cancellation and SQL NULL propagation. The
production-object runner extracts the original option-parser body under a
private test name to compare grammar behavior without enabling core GIS.
Exact original SQL error codes, complete WKT/3D/M/empty-type compatibility and
real-server acceptance remain open; this is not full GIS semantic equivalence.
The final production DSO build/export audit, actual loader (including failure
and cancellation controls), complete SQL/LOB fixture with all 114 declarations,
eight GIS/boundary CTests and source dependency check pass. The disposable-server
runner now includes these axis-order controls, but it was not run on a server;
no deployment, restart or live catalog/storage acceptance is claimed.

The next PG I/O slice replaces ordinary-parser aliases for `_ST_GeomFromEWKT`,
`_ST_GeomFromEWKB`, `_ST_AsEWKB`, `_ST_GeogFromText` and `_ST_GeographyFromText`.
EWKT input reads an optional `SRID=...;` prefix and keeps longitude-first order.
EWKB reads its SRID/Z flags and uses an optional **string** axis argument, not a
numeric SRID override, retaining the original little-endian-only/M-rejection
policy. NULL optional axis leaves the default order; explicit latitude-first
swaps even Cartesian input, matching the original entry. Output inserts a root
SRID only when nonzero and maps the root Z flag, retaining canonical child WKB.
Decoding validates child SRIDs and homogeneous multi-geometry type/dimensions.

Geography text defaults to 4326 when the prefix is absent or zero and rejects
projected SRS. `pg_coordinate_io.hpp` extracts the original coordinate folding
functions, used by both the legacy visitor and plugin. The 1e-10 edge tolerance,
degree-based policy and distinction between conditional 2D and unconditional
3D folding remain intact. Empty nonzero-SRID input still requires a catalog
entry, and results retain the I/O cancellation rules. Removing three unsupported
numeric-argument declarations brings the current package to **111 declarations
for 82 names**, superseding the intermediate 114-declaration stage above.
`_ST_AsEWKT` output/precision is not part of this slice and remains pending, as
do exact SQL errors, complete grammar/dimension parity and live acceptance.
This slice passes the final production DSO build/export audit and actual loader,
core-GIS-on syntax check of the shared normalization bridge, the complete
SQL/LOB fixture admitting all 111 declarations, eight GIS/boundary CTests and
source dependency check. Independent EWKB hex controls, SRID/Z, NULL options,
empty collections and original geography edge cases are included. The server
runner gained PG I/O controls but was only tested offline; no instance was
deployed, restarted or used for catalog/storage acceptance.

The EWKT output slice adds the `_ST_AsEWKT(geometry, maxdecimaldigits)` overload;
the package now has **112 declarations for 82 names**. It emits the stored SRID
prefix without a catalog lookup or axis reversal (including unknown SRIDs and
the legacy `SRID=NULL` sentinel). The default precision is 15; zero, negative
and >=25 disable rounding. The original 3D path ignores supplied precision and
uses a 25-byte coordinate-format buffer. Multi-geometries retain Z coordinates,
collections retain child type names, and explicit NULL precision returns NULL.

The original oblib dtoa implementation is now shared as private source, not a
host callback or SDK ABI. The plugin owns its numerical formatting and replaces
ObNumber mantissa formatting with decimal-digit rounding, checked against the
original formatter extracted from source and real kernel ObNumber/dtoa objects.
The differential runner passes 373,705 cases covering thresholds, carries,
negative zero, subnormals, extreme precisions and output limits. Plugin fallback
allocations are scoped so failure unwinds without crossing the C entry boundary.
Golden/allocator controls pass ASan/UBSan with leak detection disabled because
LeakSanitizer is unavailable in the test sandbox; no leak-sanitizer pass is claimed.
The actual DSO/export audit and loader, SQL/LOB fixture, nine GIS/boundary CTests
and source boundary checks pass. This is not full original geometry-visitor
differential coverage: empty/mixed-dimensional geometry, exact error codes and
buffer-capacity-sensitive unscaled 2D formatting still need broader compatibility
checks. No live SQL-package/storage/index acceptance or deployment is claimed.

The following serialization slice unifies ordinary `ST_AsText`/`ST_AsWKT` and
EWKT output on a plugin-owned WKT writer using the shared original dtoa engine.
Ordinary WKT now follows the original two-dimensional spacing and shortest
number formatting, emits 3D type markers on all geometry types, and retains Z
coordinates in multi-geometries. A two-dimensional collection containing a 3D
member (or the reverse) is rejected, matching the original visitors. Failed
serialization does not emit a partial result.

Unscaled 2D formatting tracks the original 512-byte initial buffer, doubling,
reserve estimates and remaining-coordinate width. This removes the prior
fixed-256-byte approximation. The logical formatting capacity is independent of
physical string allocation: the old quadratic inner-ring estimate does not
cause a correspondingly large allocation. Completed output is moved, not copied.
The differential runner compiles the original 2D/3D visitors and binary geometry
classes beside the real kernel and calls the actual GIS DSO. Its 1,290 cases
cover seven geometry kinds, nested/empty collections, stored SRIDs, precisions,
mixed-dimensional rejection and buffer-growth boundaries. Broader empty-type,
M, malformed-input/error-code and live-server acceptance remain open; this is
not a claim that all GIS grammar and serialization is now equivalent.
The final production DSO build/export audit and loader, complete SQL/LOB fixture,
nine GIS/boundary CTests, source dependency and new-file license checks pass.
No server was deployed or restarted.

The WKT input slice ports the original parser's global dimension inference,
joined/separated Z markers, consistent MultiPoint parentheses, line cardinality
and bitwise XY ring closure rules into plugin-owned geometry. Only collections
accept EMPTY/empty parentheses, as in the original parser. Numeric input uses
the shared original bounded strtod implementation; failure leaves outputs
unchanged. The original-parser differential adds 218 acceptance/raw-WKB cases
alongside the 1,290 output cases. Both suites, the SQL/LOB fixture and nine GIS/
boundary CTests pass. The numeric span probe passes ASan/UBSan (leak detection
disabled). This does not establish complete binary grammar, SQL error-code,
catalog/storage or live-server equivalence; no instance was deployed/restarted.

The EWKB admission slice now applies the original root-only envelope rewrite:
the root Z flag selects dimensions, its SRID is removed into the geometry
envelope, and child headers are ordinary WKB (no embedded EWKB flags/SRIDs).
The legacy root low-type modulo rule is retained; ISO type offsets alone do
not establish EWKB Z. This rule is specific to EWKB, not ordinary WKB. The 2D
path enforces uniform little-endian byte order, nonempty polygon/multi types
and bitwise XY ring closure. The original 3D empty/closure rules remain distinct.

The I/O differential runner extracts the original EWKB header/rewrite methods
and compiles the original 2D/3D validators. All 1,619 cases pass against the
actual GIS DSO: root flags/offsets, seven types, truncations/trailing bytes,
nested flags, empty types, closure, mixed dimensions and 2D mixed byte order.
The 218 WKT parser and 1,290 WKT output cases also pass, as do full SQL/LOB,
actual production-DSO loading, nine GIS/boundary CTests and source boundaries.
This is not exhaustive binary equivalence: nonfinite coordinates, all malformed
3D subtype/byte-order combinations, exact SQL errors and live storage/index
acceptance remain outstanding. No host callback implements this conversion;
the plugin owns its canonical buffer. No server was deployed or restarted.

Run the original-I/O differential and original-number checks against a prepared production build:

```bash
python3 rust/plugin-runtime/tests/gis_ewkt_number.py --build-dir build_release
python3 rust/plugin-runtime/tests/gis_wkt.py --build-dir build_release
```

Build with:

```bash
cmake -S . -B build_release -DSEEKDB_ENABLE_EXPERIMENTAL_PLUGINS=ON
cmake --build build_release --target seekdb_gis_plugin -j8
```

Run the Cartesian regression independently of the server:

```bash
cmake -S rust/plugin-runtime/tests -B build_release/plugin-runtime-tests
cmake --build build_release/plugin-runtime-tests --target plugin_gis_topology -j2
ctest --test-dir build_release/plugin-runtime-tests -R '^plugin_gis_topology$' --output-on-failure
```

This compiles the actual plugin adapter and shared algorithms, including the
four original regressions, holes, boundary predicates, nonrectangular overlays,
Multi geometries and C callback admission/error behavior, plus weighted
centroids, buffer strategies and interior-point selection for holes, concave
polygons, lines, multipoints, mixed collections and collapsed polygons.
`gis_sql.py` separately tests these paths (including empty-result semantics)
through the production DSO and real SQL expression/LOB bridge. Neither test proves
live-server installation, transactions or storage. Remaining algorithms and
broader compatibility still require migration and differential testing.

To run the lightweight profile, keep core GIS disabled and place the package
under the server base directory. `plugin.toml` is the package manifest used by
the explicit `INSTALL PLUGIN` command; startup only restores plugins that were
already persisted by that command. Loading the DSO no longer publishes public
GIS SQL function names. Also deliver the manifest, shared object and the
control/SQL package:

```bash
BASE=/data/seekdb
mkdir -p "$BASE/plugins/gis"
mkdir -p "$BASE/share/seekdb/extension"
cp build_release/plugins/gis/seekdb_gis.so "$BASE/plugins/gis/"
cp plugins/gis/plugin.toml "$BASE/plugins/gis/"
cp plugins/gis/sql/gis.control plugins/gis/sql/gis--1.0.sql "$BASE/share/seekdb/extension/"
build_release/src/observer/seekdb --base-dir="$BASE" --extension-dir="$BASE/share/seekdb/extension"
```

Experimental server validation sequence (not yet a verified live-server result;
use a disposable instance and an administrative account):

```sql
CREATE DATABASE gis_demo;
USE gis_demo;
INSTALL PLUGIN `org.seekdb.gis` SONAME 'gis/seekdb_gis.so';
CREATE EXTENSION gis;
SELECT ST_X(POINT(1, 2)), ST_Y(POINT(1, 2));
```

Plugin discovery and loading happen during startup, so restart seekdb after
replacing a package.  Phase 1 uses local identity pinning only; signatures
and content-hash trust are intentionally deferred.

## SQL/LOB regression

After completing a plugin-enabled, core-GIS-disabled production build, run:

```bash
python3 rust/plugin-runtime/tests/gis_sql.py --build-dir build_plugin_overlay_verify
```

This builds/audits the GIS DSO and links a private test executable using the
production kernel objects. It exercises real SQL parsing, type inference,
codegen, LOB materialization and plugin callbacks for distance, area, length,
WKT/WKB round trips, SRID, predicates, collections and NULLs. It also checks
catalog-only name resolution, generic bound calls (no per-GIS-function dispatch),
integer/decimal/string coercions, aliases, invalid arity and invalid SRIDs. Separate payload
checks cover raw/in-row geometry and text, embedded NUL and copied ownership.
Geometry collections retain child type names when serialized to WKT.
The batch case evaluates `ST_Area` on native geometry column datums with LOB
headers, NULLs, skipped rows, cached evaluation and more than 1024 input rows.

The host must materialize SQL LOB datums before passing bytes to the plugin;
an internal LOB header/locator is not part of the GIS payload ABI. Temporary
input buffers and scalar controls must survive through result emission.

The test does not start or change a server. Activation catalog metadata and
LOB storage services are controlled fixtures; it does not verify out-of-row
storage, spatial indexes, recovery or all GIS semantics. After deploying both
the rebuilt host and GIS DSO, rerun SQL smoke tests on the real server.

### Opt-in disposable-server acceptance runner

With core GIS disabled, the implementation-only GIS module loaded, the SQL
package delivered under `--extension-dir`, and PyMySQL installed:

```bash
python3 rust/plugin-runtime/tests/gis_extension_server.py \
  --port 2881 --user root --confirm-disposable-server
```

Use only a disposable instance. This command creates two uniquely named
databases and one user; it does not deploy modules, restart the server or change
global settings. An administrative password may be supplied through
`SEEKDB_TEST_PASSWORD`, not a command-line argument. Alternatively specify an
absolute `--unix-socket` instead of `--port`; no TCP fallback is performed.

The runner checks all 112 routine bindings, fresh overload slots and membership,
module dependencies, cross-session/two-database visibility, qualified calls,
GIS values/long LOBs, per-overload EXECUTE and prepared-call revocation. It also
checks DROP EXTENSION, fresh identities after reinstall, removal of live object
ACL entries, independence of the other database, and database-drop cleanup.
It does not invent a GIS 1.1 update: version updates, recovery, concurrent DDL
and injected commit failures remain separate gates.

Success deletes only this run's fixture databases/user (not recoverable).
Failure stops without automatic retry or cleanup and prints both confirmed
fixtures and attempted creations whose outcomes may be unknown. Inspect those
names before cleanup; rerunning does not clean up previous runs. Internal or
connection errors are never accepted as expected privilege/name errors.

`test_gis_extension_server.py` / CTest `plugin_gis_extension_runner` checks the
runner's validation, sequencing and safeguards offline. **Passing that test is
not evidence that this acceptance command passed against a server.**

## SQL Extension migration status

The [`sql/`](sql/README.md) package covers 82 SQL names with 112 native declarations.
CMake installs `gis.control` and `gis--1.0.sql` into `share/seekdb/extension`
without pulling the optional DSO into the core build. The actual GIS module's
function descriptors are now implementation-only; no test-only name-hiding
switch is needed. Type/cast registration remains module-scoped for ABI support.

Keyword constructors, aliases and ordinary functions use database routine
identities. The GIS regression stages the actual package declarations with
controlled IDs/storage/grants, then uses real SELECT resolution and execution.
It includes an empty database, explicit cross-database qualification, nested
native calls, EXECUTE revocation and independent alias deletion. This does not
prove durable `CREATE EXTENSION`, restart recovery or failure rollback on a
server; those remain release gates. Existing installations using module-global
names require a database SQL installation, not merely replacement of the DSO.
