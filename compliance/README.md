# Third-party compliance data

`components.json` is the source of truth for the engine's redistribution
inventory.  Each component records its exact version when known, upstream
source, packaging form, license, modification state, license texts, and
redistribution requirement.

Original license and notice texts are kept under `compliance/licenses/`.  Do not edit upstream text; update the component record and replace the
file only when the dependency version changes.

Generate and validate the bundle with:

```bash
python3 package/compliance.py check
python3 package/compliance.py stage --target build/compliance --version 1.4.1.0
```

The package build stages the generated bundle into:

- `usr/share/seekdb/licenses` on Linux;
- `opt/seekdb/share/seekdb/licenses` on macOS;
- `share/licenses` on Windows;
- the top-level `licenses/` directory of the portable binary archive.

The portable archive copy is consumed by downstream bindings.  The generated
`THIRD_PARTY_COMPONENTS.json` is the machine-readable inventory; the Markdown
files are for reviewers and downstream redistributors.

Entries marked `unspecified` still require provenance work.  They are kept in
the inventory so the gap is visible instead of silently omitted.
