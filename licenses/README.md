# Third-party license corpus

Each directory under `licenses/` represents one third-party component:

```text
licenses/<component>/
  METADATA.json
  LICENSE
  NOTICE
  ...
```

`METADATA.json` records the component name, exact version, upstream source,
packaging form, license, modification state, license files, redistribution
requirements, and any corresponding-source offer.  The license and notice files
in the same directory are upstream text and must remain verbatim.

`package/compliance.py` walks this directory, validates the metadata and files,
and generates the release bundle:

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
