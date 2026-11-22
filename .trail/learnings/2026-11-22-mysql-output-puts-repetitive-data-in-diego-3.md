---
title: "MySQL output puts repetitive data in child tables, one table per BDS register, with no transactions in the output"
author: Diego
date: 2026-11-22
branch: feat/sql-output
cost: { minutes: 2, tokens: 13516 }
id: l-f40b4fab
scope: repo
status: active
replaces: l-5ad0e38d
src: agent
touches:
  - HISTORY
  - README.md
  - SQL.md
  - install/sample_output/filter.txt
  - src/asterix/AsterixDefinition.cpp
  - src/asterix/AsterixDefinition.h
  - src/asterix/Category.cpp
  - src/asterix/Category.h
  - src/asterix/DataBlock.cpp
  - src/asterix/DataRecord.cpp
---

## Decided
- -Q prints CREATE TABLE IF NOT EXISTS, then the INSERTs of every record. It prints no START TRANSACTION/COMMIT: the user removed them as not needed. With autocommit every row is stored at once; for bulk loads the user wraps the whole stream in one transaction.
- Child tables over JSON columns (the user's choice): catNNN has rec_id AUTO_INCREMENT, the -j header and one column per field named by its -j path. Every -j array goes to a child table keyed (rec_id, [parent_rep_idx,] rep_idx) with ON DELETE CASCADE.
- BDS: one table per register (_BDS0 when there is no definition) plus reg_idx, so joins on reg_idx = 0 do not multiply rows. rep_idx is the index in the -j array.
- The JSON "id" is not unique. Text fields are trimmed. Names are resolved once on the definition objects, so the INSERTs match the schema.

## Tried and ruled out
- JSON columns (need JSON_TABLE and pivots, no types); one sparse table per BDS item (registers share field names); a transaction per packet (removed at the user's request).

## Watch out
- MySQL: group_concat_max_len defaults to 1024, ONLY_FULL_GROUP_BY is enforced (MariaDB does neither), and -p prints a warning on stderr (CI uses MYSQL_PWD).
- tests/run_tests.sh takes the -Q data part of golden outputs from the first "INSERT INTO" line.

In the code: src/asterix/SqlSchema.{h,cpp}, addSqlColumns in each DataItemFormat*, the ESQL cases in getText, DataRecord::getSqlText; tests/tools/check_sql.py, tests/sql/load_mysql.sh (CI job mysql); SQL.md
