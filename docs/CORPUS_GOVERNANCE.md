# Corpus Governance

## Corpus classes

### Historical corpus
The original corpus used before the 2026-08-17 conversation boundary.

Known series:
- EDGE — 64 cases
- horse — 64 cases
- naturel — 48 cases
- RASTER — 64 cases

The exact files, filenames, dimensions, licenses/source provenance, preprocessing commands and ordering are not currently recovered.
They must remain marked UNKNOWN until the original assets or logs are found.

### Current permanent corpus
A new corpus created after this master project snapshot.

It must be independent from the historical corpus and must never inherit historical scores.

## Required metadata for every future corpus asset

- corpus_id
- asset_id
- series
- original_filename
- source_type
- source_reference / license note
- width
- height
- pixel_format
- color_space
- alpha_mode
- preprocessing
- sha256_raw
- sha256_processed
- added_date
- notes

## Immutability

Raw assets are immutable once a corpus is published.
Any crop, resize, color conversion, gamma operation or generated reference becomes a new derived asset with its own SHA-256.

## Recovery of historical files

If historical assets are later found:
1. Do not overwrite the placeholder legacy manifest rows.
2. Add a recovery record with a stable recovered asset ID.
3. Hash every recovered file.
4. Record why the file is believed to belong to the historical corpus.
5. Re-run the historical benchmark as a new execution linked to the recovered corpus.
6. Never silently equate newly reconstructed files with the original historical files.
