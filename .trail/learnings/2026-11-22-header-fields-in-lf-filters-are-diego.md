---
title: "Header fields in -LF filters are opt-in per category, so filter files without them keep printing the whole header"
author: Diego
date: 2026-11-22
branch: feat/sql-output
cost: { minutes: 35, tokens: 463849 }
id: l-21ac3779
scope: repo
status: active
src: agent
touches:
  - ".github/workflows/tests.yml"
  - CMakeLists.txt
  - HISTORY
  - README.md
  - SQL.md
  - setup.py
  - src/asterix/AsterixData.cpp
  - src/asterix/AsterixDefinition.cpp
  - src/asterix/AsterixDefinition.h
  - src/asterix/Category.cpp
---

## Decided
- -L lists 8 record header fields per category, before its items: "CATnnn:HDR:<id|cat|length|crc|timestamp|srcip|fspec|hexdata>". -LF reads them.
- The user rejected strict filtering. A category with no HDR line in the filter prints its whole header. Once a category has an HDR line, only its listed header fields print. Filter files written before HDR lines existed give byte-identical output (checked against every filtered golden file before tests/data/filter_subset.txt got HDR lines).
- The rule applies to every format: text (record and block header lines; "Data Record" loses its number without id), XML (ver always; no id/fspec there; hexdata stays the whole record), JSON (-j/-jx/-jh/-je) and -Q (header columns; rec_id always).
- A record still prints only when one of its data items has a filtered field. HDR lines alone print nothing.

## Watch out
- If hexdata or fspec is filtered out of -jx, the write-back by pos is no longer possible for that consumer.
- run_tests.sh masks the -Q wall-clock timestamp as the only value with 6 decimals (field values have 7). Its position in the INSERT now depends on the filter.

In the code: RECORD_HEADER_FIELDS, Category::printsHeader/filterOutHeader (Category.h/.cpp), DataRecord::getText/appendHeaderValue/getSqlText, DataBlock::getText, SqlSchema::addCategoryTable(name, headerFields), the filter loop in src/main/asterix.cpp; tests: HDR lines in tests/data/filter_subset.txt, cli checks in tests/run_tests.sh, unit test in tests/unit/test_parser.cpp
