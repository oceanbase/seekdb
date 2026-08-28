# seekdb Documentation

seekdb documentation is maintained in two places:

- The [seekdb documentation site](https://docs.seekdb.ai/) contains product overviews, deployment and configuration guides, API references, tutorials, and integration documentation. Its source is maintained in the [seekdb-doc repository](https://github.com/oceanbase/seekdb-doc).
- The developer guide in this repository documents how to build, test, debug, and contribute to the seekdb source code. Start with the [English](developer-guide/en/README.md) or [Chinese](developer-guide/zh/README.md) guide.

## Build the developer guide

Install the documentation dependencies and run MkDocs from this directory:

```bash
python3 -m pip install -r requirements.txt
mkdocs build --strict
```

The generated site is written to `site/` and is published separately from the product documentation site.

## Feedback and contributions

Open an issue for incorrect or unclear documentation, or submit a focused pull request. Follow the repository's canonical [contribution guide](../CONTRIBUTING.md).
