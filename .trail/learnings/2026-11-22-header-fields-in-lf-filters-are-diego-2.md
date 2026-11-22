---
title: "Header fields in -LF filters are opt-in per category; CATnnn:HDR:none prints none of them"
author: Diego
date: 2026-11-22
branch: feat/sql-output
cost: { minutes: 13, tokens: 29339 }
id: l-760c98b7
scope: repo
status: active
replaces: l-21ac3779
src: agent
touches:
  - HISTORY
  - README.md
  - SQL.md
  - src/asterix/AsterixData.cpp
  - src/main/asterix.cpp
  - tests/golden/ast_021/sql.filtered.txt
  - tests/golden/ast_021/sql.txt
  - tests/golden/final_001_002/sql.filtered.txt
  - tests/golden/final_001_002/sql.txt
  - tests/golden/gps_034_048/sql.filtered.txt
---

## Decided
- -L lists 8 record header fields per category, before its items: "CATnnn:HDR:<id|cat|length|crc|timestamp|srcip|fspec|hexdata>". -LF reads them.
- The user rejected strict filtering. A category with no HDR line prints its whole header, so old filter files give byte-identical output. A category with HDR lines prints only those fields.
- "CATnnn:HDR:none" turns the header filter on with no field (bit HDR_NONE, outside HDR_ALL). Listed fields are still printed. -L does not list it: in a full listing it would have no effect.
- It applies in every format: text (record and block header; "Data Record" without its number), XML (ver always; no id/fspec there; hexdata is the whole record), JSON (-j/-jx/-jh/-je) and -Q (header columns; rec_id always).
- A record still prints only when one of its data items has a filtered field.

## Watch out
- If hexdata or fspec is filtered out of -jx, the write-back by pos is no longer possible.
- run_tests.sh masks the -Q wall-clock timestamp as the only value with 6 decimals, wherever the filter puts it.
- Text output numbers data blocks with a process-wide static counter, so unit tests must not expect "Data Block 1".

In the code: RECORD_HEADER_FIELDS, HDR_NONE, Category::printsHeader/filterOutHeader (Category.h/.cpp), DataRecord::getText/appendHeaderValue/getSqlText, DataBlock::getText, the filter loop in src/main/asterix.cpp; tests: HDR lines in tests/data/filter_subset.txt, cli checks in tests/run_tests.sh, unit tests in tests/unit/test_parser.cpp
