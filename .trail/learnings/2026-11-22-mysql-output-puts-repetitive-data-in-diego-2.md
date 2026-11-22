---
title: "MySQL output puts repetitive data in child tables, one table per BDS register, with names fixed once on the definitions"
author: Diego
date: 2026-11-22
branch: feat/sql-output
cost: { minutes: 8, tokens: 21372 }
id: l-5ad0e38d
scope: repo
status: active
replaces: l-c02559e4
src: agent
---

## Decided
- -Q prints CREATE TABLE IF NOT EXISTS, then the INSERTs of every record, one transaction per packet. The user chose child tables over JSON columns:
  - catNNN has rec_id AUTO_INCREMENT, the -j header and one column per field, named by its -j path (I010_SAC).
  - Every -j array goes to a child table keyed (rec_id, [parent_rep_idx,] rep_idx) with ON DELETE CASCADE: Repetitive, single-part Variable (CAT001 I210), Explicit SP/RE.
- BDS: one table per register (_BDS0 for registers without a definition) plus reg_idx, so joins on reg_idx = 0 do not multiply rows.
- rep_idx is the index in the -j array. With -LF the arrays leave out elements that print nothing, so those are not counted.
- The JSON "id" is not unique (record number in its packet); text fields are trimmed (MySQL 8 collations are NO PAD).
- Names are resolved once when the schema is built and stored on the definition objects, so the INSERTs cannot drift from it.
- With -LF, -j used to leave a comma for a repetition that printed nothing ("{...},]", invalid JSON). Fixed in the same change: the repetition is dropped.

## Tried and ruled out
- JSON columns (need JSON_TABLE and pivots, no types); one sparse table per BDS item (registers share field names).

## Watch out
- MySQL: group_concat_max_len defaults to 1024, ONLY_FULL_GROUP_BY is enforced (MariaDB does neither), and -p prints a warning on stderr (CI uses MYSQL_PWD).
- -Q is about 1.7x slower than -j and about 25% bigger.

In the code: src/asterix/SqlSchema.{h,cpp}, addSqlColumns in each DataItemFormat*, the ESQL cases in getText, DataRecord::getSqlText; tests/tools/check_sql.py, tests/sql/load_mysql.sh (CI job mysql), tests/unit/test_sql.cpp; SQL.md
