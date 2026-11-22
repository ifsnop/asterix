---
title: "MySQL output puts repetitive data in child tables, one table per BDS register, with names fixed once on the definitions"
author: Diego
date: 2026-11-22
branch: feat/sql-output
cost: { minutes: 51, tokens: 723277 }
id: l-c02559e4
scope: repo
status: active
src: agent
touches:
  - src/asterix/SqlSchema.h
  - src/asterix/SqlSchema.cpp
  - tests/tools/check_sql.py
  - tests/unit/test_sql.cpp
  - tests/sql/doc_queries.sql
  - SQL.md
---

## Decided
- -Q prints CREATE TABLE IF NOT EXISTS for every category, then INSERTs per record, one transaction per packet. The user chose this over JSON columns: one table per category (catNNN: rec_id AUTO_INCREMENT, the -j record header, one column per field named by its -j path, I010_SAC, I380_ADR_ADR), and child tables keyed (rec_id, [parent_rep_idx,] rep_idx) with FOREIGN KEY ... ON DELETE CASCADE for everything -j prints as an array: Repetitive, Variable items with a single repeating part (CAT001 I210), Explicit SP/RE (one row per body, even when -j prints a single body as an object), and BDS registers.
- BDS: one table per register (_BDS40, _BDS60...; registers without a definition go to _BDS0 with the real number in column BDS), plus reg_idx (occurrence of that register in the plot) so joins use reg_idx = 0 and do not multiply rows. rep_idx is the position in the whole -j array.
- The JSON "id" is not unique (it is the record number in its packet), hence rec_id.
- Text fields (six-bit, ASCII) are inserted trimmed, so WHERE I240_TId = 'DLH65A' works with MySQL 8's NO PAD collations.
- Table and column names are resolved once, when the schema is built (AsterixDefinition::printSqlSchema), and stored on the definition objects. The INSERT code only reads them, so it cannot drift from the schema. Repeated names in one table get numbers in definition order (FX, FX1).

## Tried and ruled out
- A JSON column per repetitive item: needs JSON_TABLE and CASE pivots to query, with no types.
- A single sparse table per BDS item with register-prefixed columns: registers share field names and most columns would be NULL.

## Watch out
- -j with -LF already writes invalid JSON when a BDS repetition prints nothing ("{...},]"); it is in the filtered golden files. check_sql.py reads empty slots as null so rep_idx keeps the real position. Not fixed here: it changes -j output.
- MySQL's group_concat_max_len is 1024 by default (MariaDB's is 1M). In CI the password goes in MYSQL_PWD, because -p makes the client print a warning on stderr. MySQL 8 enforces ONLY_FULL_GROUP_BY; MariaDB does not, so local runs set it.
- -Q output is about 25% bigger than -j and takes about 1.7x as long.

In the code: src/asterix/SqlSchema.{h,cpp} (schema, type mapping, SqlRecord rows), addSqlColumns in every DataItemFormat*, ESQL cases in DataItemBits/Repetitive/Variable/Explicit/BDS getText, DataRecord::getSqlText, AsterixData::getText (transaction); tests: tests/tools/check_sql.py, tests/sql/load_mysql.sh + doc_queries.sql/.expected (CI job mysql), tests/unit/test_sql.cpp, golden tests/golden/*/sql*.txt and tests/golden/sql_schema; docs SQL.md
