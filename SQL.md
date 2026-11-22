# MySQL output (`-Q`)

`asterix -Q` prints the decoded data as MySQL statements, ready to be loaded into MySQL or MariaDB:

1. first a `CREATE TABLE IF NOT EXISTS` statement for every table, built from the loaded definitions
   (only the fields in the `-LF` filter when one is given);
2. then, for every decoded record, the `INSERT` statements of its rows, one transaction per input packet.

```sh
mysql -e 'CREATE DATABASE asterix'
asterix -Q -P -f capture.pcap | mysql asterix                 # schema + data
asterix -Q -G -f capture.gps -LF myfilter.txt | mysql asterix  # only the filtered fields
asterix -Q -L > schema.sql                                    # only the schema (no input needed)
asterix -Q -L -LF myfilter.txt > schema.sql                   # schema of the filtered fields
```

Because the schema uses `IF NOT EXISTS`, several captures can be loaded one after the other into the same
database. Do not mix the schema of one filter with the data of another: a filtered schema only has the
filtered columns. The data is the same as in the `-j` output (same values, same records), so whatever
reads `-j` today can read the database instead. `tests/tools/check_sql.py` checks this on every test case.

## Tables

### One table per category

Each category has a table named `catNNN` (`cat048`, `cat062`...) with one row per record:

| Column | Type | Content (same as the `-j` record header) |
|---|---|---|
| `rec_id` | `BIGINT UNSIGNED AUTO_INCREMENT`, primary key | Number of the row in the database, given by MySQL. The child tables refer to it. |
| `id` | `INT` | Number of the record in its packet. It is **not** unique: do not use it as a key. |
| `cat` | `TINYINT UNSIGNED` | Category. |
| `length` | `INT UNSIGNED` | Record length in bytes. |
| `crc` | `CHAR(8)` | CRC-32 of the record, in hex. |
| `timestamp` | `DOUBLE` | Timestamp of the input (PCAP/GPS time, or wall-clock time for raw files and network input). |
| `srcip` | `VARCHAR(15)` | Source IP address (multicast input, GPS files), `0.0.0.0` when unknown. |
| `fspec` | `VARCHAR(255)` | FSPEC bytes, in hex. |
| `hexdata` | `MEDIUMTEXT` | Data items after the FSPEC, in hex (the record is `fspec` + `hexdata`). |

Then there is one column per field, named after its path in `-j`, joined with `_`:

| `-j` | Column |
|---|---|
| `"I010":{"SAC":25,"SIC":201}` | `I010_SAC`, `I010_SIC` |
| `"I130":{"SRLP":{"SRL":3.77}}` (compound item) | `I130_SRLP_SRL` |
| `"I380":{"ADR":{"ADR":"4CA7A8"}}` | `I380_ADR_ADR` |

- **Repeated names**: some items repeat a name, like the `FX` bit of each extent of CAT048 I170, or several `spare`
  fields. The second occurrence gets `1`, the third `2`, and so on, in the order of the definition:
  `I170_FX`, `I170_FX1`. `-j` prints these keys twice.
- **Comments**: every column has the field description and its unit as a comment (`SHOW FULL COLUMNS FROM cat048`).
- **Absent fields**: fields not present in a record are `NULL`.

### Child tables for repetitive data

Data that can appear a variable number of times in a record cannot be a column. It goes to a child table, with
one row per element. These are the arrays of `-j`:

| ASTERIX structure | Example | Child table |
|---|---|---|
| Repetitive item | CAT034 I070 (plot counters) | `cat034_I070` |
| Variable item with a single repeating part | CAT001 I210 (track quality) | `cat001_I210` |
| Repetitive subfield of a compound item | CAT062 I390 TOD (flight plan times) | `cat062_I390_TOD` |
| Explicit item (SP, RE), one row per body | CAT048 SP | `cat048_ISP` |
| BDS register (Mode S MB data), one table per register | CAT048 I250 | `cat048_I250_BDS40`, `cat048_I250_BDS60`... |

A child table has no `AUTO_INCREMENT`. Its key is:

| Column | Content |
|---|---|
| `rec_id` | `rec_id` of the record (its row in the category table). |
| `rep_idx` | Position of the element in the `-j` array (0, 1, 2...). With `-LF`, elements with no filtered field (e.g. a BDS register not in the filter) are not in the array and are not counted. |
| `reg_idx` | BDS tables only: occurrence number of this register in the record (0 for the first BDS 4,0 of the plot, 1 for a second BDS 4,0...). |
| `parent_rep_idx` | Only in a child table of a child table (e.g. CAT020 RE subfield DA, list MDB: `cat020_IRE_DA_MDB`): `rep_idx` of the parent row. |

The primary key is `(rec_id, [parent_rep_idx,] rep_idx)`. A `FOREIGN KEY ... ON DELETE CASCADE` points to the
parent table, so deleting a record also deletes its child rows.

**BDS registers**: each element of a BDS item can be a different register, with different fields, so each
register defined in `asterix_bds.xml` has its own table (`_BDS40`, `_BDS44`, `_BDS45`, `_BDS50`, `_BDS60`).
Registers with no definition of their own go to `_BDS0` (raw `VAL`). Every table keeps the register number in
column `BDS`. Use `rep_idx` for the order the registers came in, and `reg_idx = 0` to join one register per
plot without duplicating rows (see Q1).

### What `-LF` changes

With a filter, a column exists only for the fields of the filter. A table is created only if it has a filtered
field, or a child table that has one. Categories with no filtered field have no table. An `INSERT` lists only
the filtered fields.

## Data mapping between ASTERIX and MySQL

Each field has the MySQL type that holds every value `-j` can print for it:

| ASTERIX encoding (XML `encode`) | Scaled (`BitsUnit scale`)? | MySQL type | Example |
|---|---|---|---|
| unsigned, 1 to 8 bits | no | `TINYINT UNSIGNED` | CAT048 I010 SAC (8 bits), I020 SIM (1 bit) |
| unsigned, 9 to 16 bits | no | `SMALLINT UNSIGNED` | CAT048 I161 Tn, track number (12 bits) |
| unsigned, 17 to 24 bits | no | `MEDIUMINT UNSIGNED` | |
| unsigned, 25 to 32 bits | no | `INT UNSIGNED` | |
| unsigned, more than 32 bits | no | `BIGINT UNSIGNED` | |
| signed (two's complement), same widths | no | `TINYINT` ... `BIGINT` | CAT001 I131 Power (8 bits): `TINYINT` |
| unsigned or signed | yes | `DOUBLE` (`-j` prints value × scale with 7 decimals) | CAT048 I090 FL (signed 14 bits, LSB 1/4 FL), I140 ToD (unsigned 24 bits, LSB 1/128 s), CAT062 I105 Lat |
| six-bit characters (`6bitschar`) | | `VARCHAR(bits/6)` | CAT048 I240 TId, callsign (48 bits): `VARCHAR(8)` |
| ASCII (`ascii`) | | `VARCHAR(bits/8)` | CAT062 I390 CSN CS, callsign (56 bits): `VARCHAR(7)` |
| hexadecimal (`hex`) | | `VARCHAR`, 2 digits per byte | CAT048 I220 ACAddr, ICAO address (24 bits): `VARCHAR(6)` |
| octal (`octal`) | | `VARCHAR(bits/3)` | CAT048 I070 Mode3A (12 bits): `VARCHAR(4)` |

- **Text fields are inserted trimmed.** Six-bit and ASCII fields are padded with spaces in ASTERIX
  (`"DLH65A  "` in `-j`). `-Q` inserts them without leading or trailing spaces (`'DLH65A'`), so
  `WHERE I240_TId = 'DLH65A'` works with any collation. Text fields only have `A-Z`, `0-9`, space, `.` and `@`
  (`@` stands for any other byte, as in every output).
- **Values that mean something** (e.g. CAT048 I020 TYP 5 = "Single ModeS Roll-Call") are stored as numbers.
  The meanings are in the XML definitions and in the `-je` output.
- **Status bits.** Fields keep their raw meaning: BDS registers carry a status bit for each value
  (`MCP_ALT_STATUS`, `BP_STATUS`...). A value whose status is 0 is not valid, even though it is stored.
- **Character set.** Tables are `InnoDB` with `utf8mb4`, the MySQL 8 default.

How each ASTERIX item structure maps:

| Structure | Mapping |
|---|---|
| Fixed | Columns `Ixxx_FIELD` |
| Variable (several parts, FX extents) | Columns `Ixxx_FIELD` of every part (the `FX` of each extent numbered `FX`, `FX1`...) |
| Variable with a single repeating part | Child table `catNNN_Ixxx`, one row per part |
| Compound | Columns `Ixxx_SUBFIELD_FIELD` (the primary subfield, the presence bits, is not stored) |
| Repetitive | Child table `catNNN_Ixxx`, one row per repetition |
| Repetitive of BDS registers | Child tables `catNNN_Ixxx_BDSrr`, one row per register |
| Explicit (SP, RE) | Child table `catNNN_ISP`, one row per body |

## The statements

For the sample record of AEE2BR (CAT048, BDS 4,0 and 6,0) `-Q` prints (columns shortened):

```sql
START TRANSACTION;
INSERT INTO `cat048` (`id`,`cat`,`length`,`crc`,`timestamp`,`srcip`,`fspec`,`hexdata`,`I010_SAC`,`I010_SIC`,
  `I140_ToD`,...,`I090_FL`,...,`I220_ACAddr`,`I240_TId`,`I161_Tn`,...)
  VALUES (2,48,60,'F31EC111',27356536.000000,'0.0.0.0','FFFF02','190D356E06A0...',25,13,
  27356.0468750,...,360.0000000,...,'4692D1','AEE2BR',761,...);
SET @asterix_rec = LAST_INSERT_ID();
INSERT INTO `cat048_I250_BDS40` (`rec_id`,`rep_idx`,`reg_idx`,`MCP_ALT_STATUS`,`MCP_ALT`,...,`BDS`)
  VALUES (@asterix_rec,0,0,1,36000.0000000,...,'40');
INSERT INTO `cat048_I250_BDS60` (`rec_id`,`rep_idx`,`reg_idx`,`HDG_STATUS`,`HDG`,`IAS_STAT`,`IAS`,...,`BDS`)
  VALUES (@asterix_rec,1,0,1,-45.0000000,1,251.0000000,...,'60');
COMMIT;
```

`LAST_INSERT_ID()` belongs to the connection, so several loaders can write to the same database at the
same time. Each packet is one transaction: with live network input the rows show up packet by packet.

The child table of that register:

```sql
CREATE TABLE IF NOT EXISTS `cat048_I250_BDS60` (
  `rec_id` BIGINT UNSIGNED NOT NULL,
  `rep_idx` SMALLINT UNSIGNED NOT NULL,
  `reg_idx` SMALLINT UNSIGNED NOT NULL,
  `HDG_STATUS` TINYINT UNSIGNED COMMENT 'Heading Status',
  `HDG` DOUBLE COMMENT 'Magnetic Heading [deg]',
  `IAS_STAT` TINYINT UNSIGNED COMMENT 'Indicated Airspeed Status',
  `IAS` DOUBLE COMMENT 'Indicated Airspeed [kt]',
  `MACH_STATUS` TINYINT UNSIGNED COMMENT 'Mach Speed Status',
  `MACH` DOUBLE COMMENT 'Mach Speed [MACH]',
  `BAR_STATUS` TINYINT UNSIGNED COMMENT 'Barometric Altitude Rate Status',
  `BAR` DOUBLE COMMENT 'Barometric Altitude Rate [ft/min]',
  `IVV_STATUS` TINYINT UNSIGNED COMMENT 'Inertial Vertical Velocity Status',
  `IVV` DOUBLE COMMENT 'Inertial Vertical Velocity [ft/min]',
  `BDS` VARCHAR(2) COMMENT 'BDS register',
  PRIMARY KEY (`rec_id`,`rep_idx`),
  FOREIGN KEY (`rec_id`) REFERENCES `cat048` (`rec_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

## Reading the data back: examples

These queries are in [tests/sql/doc_queries.sql](tests/sql/doc_queries.sql) and CI runs them on MySQL 8.
The results below are from a database loaded with the sample captures:

```sh
for f in "-P -f install/sample_data/cat_034_048.pcap" "-R -f install/sample_data/cat_001_002.pcap" \
         "-P -f install/sample_data/asterix.pcap" "-P -f install/sample_data/cat_062_065.pcap"; do
    asterix -Q $f
done | mysql asterix
```

(`cat_034_048.pcap` has every packet twice, so every plot appears twice.)

### Q1: every message of a category, with all its tables joined

Join each child table with `LEFT JOIN`, so messages without that data are kept (with `NULL`s). Join on
`reg_idx = 0` for BDS registers and `rep_idx = 0` for other lists, so a message with two elements gives
one row, not two (use `GROUP_CONCAT` to see all of them, as in Q4 and Q8).

```sql
SELECT c.rec_id, c.I010_SAC AS sac, c.I010_SIC AS sic, c.I161_Tn AS track, c.I220_ACAddr AS icao,
       c.I240_TId AS callsign, c.I070_Mode3A AS squawk, c.I090_FL AS fl,
       b40.MCP_ALT AS sel_alt, b44.WIND_SPEED AS wind, b50.TAS AS tas, b60.IAS AS ias, b60.MACH AS mach,
       b0.BDS AS other_bds, rds.DOP AS doppler, sp.SP AS sp, re.VAL AS re
FROM cat048 c
LEFT JOIN cat048_I250_BDS40 b40 ON b40.rec_id = c.rec_id AND b40.reg_idx = 0
LEFT JOIN cat048_I250_BDS44 b44 ON b44.rec_id = c.rec_id AND b44.reg_idx = 0
LEFT JOIN cat048_I250_BDS45 b45 ON b45.rec_id = c.rec_id AND b45.reg_idx = 0
LEFT JOIN cat048_I250_BDS50 b50 ON b50.rec_id = c.rec_id AND b50.reg_idx = 0
LEFT JOIN cat048_I250_BDS60 b60 ON b60.rec_id = c.rec_id AND b60.reg_idx = 0
LEFT JOIN cat048_I250_BDS0  b0  ON b0.rec_id  = c.rec_id AND b0.reg_idx  = 0
LEFT JOIN cat048_I120_RDS   rds ON rds.rec_id = c.rec_id AND rds.rep_idx = 0
LEFT JOIN cat048_ISP        sp  ON sp.rec_id  = c.rec_id AND sp.rep_idx  = 0
LEFT JOIN cat048_IRE        re  ON re.rec_id  = c.rec_id AND re.rep_idx  = 0
ORDER BY c.rec_id
LIMIT 8;
```
```
+--------+------+------+-------+--------+----------+--------+------+---------+------+------+------+-------+-----------+---------+------+------+
| rec_id | sac  | sic  | track | icao   | callsign | squawk | fl   | sel_alt | wind | tas  | ias  | mach  | other_bds | doppler | sp   | re   |
+--------+------+------+-------+--------+----------+--------+------+---------+------+------+------+-------+-----------+---------+------+------+
|      1 |   25 |  201 |  3563 | 3C660C | DLH65A   | 1000   |  330 |   33008 | NULL | NULL | NULL |  NULL | NULL      |    NULL | NULL | NULL |
|      2 |   25 |  201 |  3563 | 3C660C | DLH65A   | 1000   |  330 |   33008 | NULL | NULL | NULL |  NULL | NULL      |    NULL | NULL | NULL |
|      3 |   25 |   13 |   482 | 4BAACD | THY9TX   | 2303   |  360 |   36000 | NULL | NULL | NULL |  NULL | NULL      |    NULL | NULL | NULL |
|      4 |   25 |   13 |   482 | 4BAACD | THY9TX   | 2303   |  360 |   36000 | NULL | NULL | NULL |  NULL | NULL      |    NULL | NULL | NULL |
|      5 |   25 |   13 |   730 | 44D074 | NULL     | NULL   | NULL |    NULL | NULL | NULL | NULL |  NULL | NULL      |    NULL | NULL | NULL |
|      6 |   25 |   13 |   761 | 4692D1 | AEE2BR   | 2030   |  360 |   36000 | NULL | NULL |  251 |  0.76 | NULL      |    NULL | NULL | NULL |
|      7 |   25 |   13 |  1435 | 3C648A | DLH695   | 7252   |  360 |   36000 | NULL | NULL | NULL |  NULL | NULL      |    NULL | NULL | NULL |
|      8 |   25 |   13 |  1424 | A022A3 | N108CE   | 4745   |  400 |    NULL | NULL | NULL |  244 | 0.804 | NULL      |    NULL | NULL | NULL |
+--------+------+------+-------+--------+----------+--------+------+---------+------+------+------+-------+-----------+---------+------+------+
```

### Q2: every message of every category, in time order

```sql
SELECT * FROM (
          SELECT 'CAT001' AS category, rec_id, timestamp, I010_SAC AS sac, I010_SIC AS sic FROM cat001
UNION ALL SELECT 'CAT002', rec_id, timestamp, I010_SAC, I010_SIC FROM cat002
UNION ALL SELECT 'CAT034', rec_id, timestamp, I010_SAC, I010_SIC FROM cat034
UNION ALL SELECT 'CAT048', rec_id, timestamp, I010_SAC, I010_SIC FROM cat048
UNION ALL SELECT 'CAT062', rec_id, timestamp, I010_SAC, I010_SIC FROM cat062
) m
ORDER BY timestamp, category, rec_id
LIMIT 8;
```
```
+----------+--------+-----------+------+------+
| category | rec_id | timestamp | sac  | sic  |
+----------+--------+-----------+------+------+
| CAT048   |      1 |  27356508 |   25 |  201 |
| CAT048   |      2 |  27356508 |   25 |  201 |
| CAT034   |      1 |  27356523 |   25 |   13 |
| CAT034   |      2 |  27356523 |   25 |   13 |
| CAT048   |      3 |  27356523 |   25 |   13 |
| CAT048   |      4 |  27356523 |   25 |   13 |
| CAT034   |      3 |  27356536 |   25 |   13 |
| CAT034   |      4 |  27356536 |   25 |   13 |
+----------+--------+-----------+------+------+
```

### Q3: a view with every BDS register of a plot

A view hides the table per register when only the register list matters:

```sql
CREATE OR REPLACE VIEW cat048_bds AS
          SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS40
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS44
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS45
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS50
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS60
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS0;
```

### Q4: number of BDS registers in each message, per aircraft, in the order received

```sql
SELECT c.I220_ACAddr AS icao, c.I240_TId AS callsign, c.rec_id, c.I140_ToD AS tod,
       COUNT(b.BDS) AS bds_count, GROUP_CONCAT(b.BDS ORDER BY b.rep_idx) AS registers
FROM cat048 c
LEFT JOIN cat048_bds b ON b.rec_id = c.rec_id
WHERE c.I220_ACAddr IN ('400C4A', '3D0CDE', '3C660C')
GROUP BY c.rec_id, c.I220_ACAddr, c.I240_TId, c.I140_ToD
ORDER BY icao, tod, c.rec_id;
```
```
+--------+----------+--------+---------------+-----------+-----------+
| icao   | callsign | rec_id | tod           | bds_count | registers |
+--------+----------+--------+---------------+-----------+-----------+
| 3C660C | DLH65A   |      1 | 27354.6015625 |         1 | 40        |
| 3C660C | DLH65A   |      2 | 27354.6015625 |         1 | 40        |
| 3D0CDE | DEDCW    |     73 |   27354.90625 |         2 | 40,02     |
| 3D0CDE | DEDCW    |     74 |   27354.90625 |         2 | 40,02     |
| 400C4A | EZY54JD  |     59 |   27356.21875 |         3 | 40,50,60  |
| 400C4A | EZY54JD  |     60 |   27356.21875 |         3 | 40,50,60  |
+--------+----------+--------+---------------+-----------+-----------+
```

Register `02` has no definition of its own, so it is in `cat048_I250_BDS0`.

### Q5: BDS registers per aircraft

```sql
SELECT m.icao, MAX(m.callsign) AS callsign, COUNT(*) AS messages, SUM(m.bds_count) AS bds_total,
       MAX(m.bds_count) AS max_per_message, ROUND(AVG(m.bds_count), 2) AS avg_per_message,
       GROUP_CONCAT(DISTINCT m.registers ORDER BY m.registers SEPARATOR ' | ') AS register_sets
FROM (SELECT c.rec_id, c.I220_ACAddr AS icao, c.I240_TId AS callsign, COUNT(b.BDS) AS bds_count,
             GROUP_CONCAT(b.BDS ORDER BY b.rep_idx) AS registers
      FROM cat048 c
      LEFT JOIN cat048_bds b ON b.rec_id = c.rec_id
      GROUP BY c.rec_id, c.I220_ACAddr, c.I240_TId) m
GROUP BY m.icao
ORDER BY bds_total DESC, m.icao
LIMIT 10;
```
```
+--------+----------+----------+-----------+-----------------+-----------------+---------------+
| icao   | callsign | messages | bds_total | max_per_message | avg_per_message | register_sets |
+--------+----------+----------+-----------+-----------------+-----------------+---------------+
| 400C4A | EZY54JD  |        2 |         6 |               3 |            3.00 | 40,50,60      |
| 4BAAC1 | THY5HS   |        2 |         6 |               3 |            3.00 | 40,50,60      |
| 3003AD | DLH1970  |        2 |         4 |               2 |            2.00 | 60,40         |
| 3C5EF7 | GWI7753  |        2 |         4 |               2 |            2.00 | 60,40         |
| 3D0CDE | DEDCW    |        2 |         4 |               2 |            2.00 | 40,02         |
| 4007FA | BAW162   |        2 |         4 |               2 |            2.00 | 60,40         |
| 424050 | AFL2424  |        2 |         4 |               2 |            2.00 | 40,60         |
| 4691C4 | AEE634   |        2 |         4 |               2 |            2.00 | 60,40         |
| 4692D1 | AEE2BR   |        2 |         4 |               2 |            2.00 | 40,60         |
| 47878B | NAX19X   |        2 |         4 |               2 |            2.00 | 60,40         |
+--------+----------+----------+-----------+-----------------+-----------------+---------------+
```

### Q6: Mode S data of one aircraft, plot by plot

The status bits tell which values are valid:

```sql
SELECT c.I140_ToD AS tod, c.I240_TId AS callsign, c.I090_FL AS fl,
       CASE WHEN b40.MCP_ALT_STATUS = 1 THEN b40.MCP_ALT END AS sel_alt,
       CASE WHEN b40.BP_STATUS = 1 THEN b40.BP END AS baro,
       b60.HDG AS mag_hdg, b60.IAS AS ias, b60.MACH AS mach, b60.IVV AS ivv
FROM cat048 c
LEFT JOIN cat048_I250_BDS40 b40 ON b40.rec_id = c.rec_id AND b40.reg_idx = 0
LEFT JOIN cat048_I250_BDS60 b60 ON b60.rec_id = c.rec_id AND b60.reg_idx = 0
WHERE c.I240_TId = 'AEE2BR'
ORDER BY tod;
```
```
+--------------+----------+------+---------+------+---------+------+------+------+
| tod          | callsign | fl   | sel_alt | baro | mag_hdg | ias  | mach | ivv  |
+--------------+----------+------+---------+------+---------+------+------+------+
| 27356.046875 | AEE2BR   |  360 |   36000 |  211 |     -45 |  251 | 0.76 |   64 |
| 27356.046875 | AEE2BR   |  360 |   36000 |  211 |     -45 |  251 | 0.76 |   64 |
+--------------+----------+------+---------+------+---------+------+------+------+
```

### Q7: plots with the same BDS register more than once

```sql
SELECT c.rec_id, c.I240_TId AS callsign, b.BDS, b.rep_idx, b.reg_idx
FROM cat048 c
JOIN cat048_bds b ON b.rec_id = c.rec_id
WHERE b.reg_idx > 0;
```

There are none in the samples (empty result). Such a plot would give one row per extra copy, for example
`BDS 40, rep_idx 2, reg_idx 1` for a second BDS 4,0 in third position.

### Q8: CAT001 track quality (a Variable item with a repeating part)

```sql
SELECT c.I161_tpn AS track, c.I070_Mode3A AS squawk, c.I090_ModeC AS fl,
       GROUP_CONCAT(q.Qty ORDER BY q.rep_idx) AS quality
FROM cat001 c
LEFT JOIN cat001_I210 q ON q.rec_id = c.rec_id
GROUP BY c.rec_id, c.I161_tpn, c.I070_Mode3A, c.I090_ModeC
ORDER BY c.rec_id;
```
```
+-------+--------+-------+---------+
| track | squawk | fl    | quality |
+-------+--------+-------+---------+
|  3762 | 1464   |   370 | 7       |
|  3957 | 7122   |   340 | 7       |
|  3530 | 7060   |   390 | 7       |
|  3432 | 0112   |   310 | 7       |
|  3297 | 5304   |   360 | 7       |
|  3088 | 2636   | 150.5 | 7       |
|  3853 | 2645   |   360 | 7       |
+-------+--------+-------+---------+
```

### Q9: CAT062 tracks (compound subfields are columns)

```sql
SELECT I040_TrkN AS track, I070_ToT AS tot, I380_ID_ACID AS callsign, I380_ADR_ADR AS icao,
       I105_Lat AS lat, I105_Lon AS lon, I136_MFL AS fl, I380_SAL_Alt AS sel_alt, I380_COM_COM AS com
FROM cat062
WHERE I380_ADR_ADR IS NOT NULL
ORDER BY tot, track;
```
```
+-------+---------------+----------+--------+------------+------------+-------+---------+------+
| track | tot           | callsign | icao   | lat        | lon        | fl    | sel_alt | com  |
+-------+---------------+----------+--------+------------+------------+-------+---------+------+
|  4713 | 45827.3984375 | RYR174C  | 4CA7A8 | 41.1671233 | 15.7088667 | 39000 |    NULL |    1 |
|  6831 | 45827.3984375 | ISS2007  | 4CAC7F | 41.4169389 | 19.3891364 | 38000 |    NULL |    1 |
+-------+---------------+----------+--------+------------+------------+-------+---------+------+
```

### Q10: messages per radar and category

```sql
SELECT category, sac, sic, COUNT(*) AS messages FROM (
          SELECT 'CAT001' AS category, I010_SAC AS sac, I010_SIC AS sic FROM cat001
UNION ALL SELECT 'CAT034', I010_SAC, I010_SIC FROM cat034
UNION ALL SELECT 'CAT048', I010_SAC, I010_SIC FROM cat048
UNION ALL SELECT 'CAT062', I010_SAC, I010_SIC FROM cat062
) m
GROUP BY category, sac, sic
ORDER BY category, sac, sic;
```
```
+----------+------+------+----------+
| category | sac  | sic  | messages |
+----------+------+------+----------+
| CAT001   |   25 |  201 |        7 |
| CAT034   |   25 |   11 |        4 |
| CAT034   |   25 |   12 |       10 |
| CAT034   |   25 |   13 |        8 |
| CAT034   |   25 |   14 |        4 |
| CAT034   |   25 |  201 |        2 |
| CAT034   |   25 |  204 |        2 |
| CAT034   |   25 |  205 |        4 |
| CAT048   |   25 |   11 |        8 |
| CAT048   |   25 |   12 |       38 |
| CAT048   |   25 |   13 |       16 |
| CAT048   |   25 |   14 |        4 |
| CAT048   |   25 |  201 |       28 |
| CAT048   |   25 |  204 |       28 |
| CAT048   |   25 |  205 |        6 |
| CAT062   | NULL | NULL |       36 |
| CAT062   |   25 |  100 |       50 |
+----------+------+------+----------+
```

`NULL`: those CAT062 records have no I010.

### Q11: deleting old data

The foreign keys delete the child rows of the deleted records:

```sql
START TRANSACTION;
SELECT (SELECT COUNT(*) FROM cat048) AS plots, (SELECT COUNT(*) FROM cat048_bds) AS bds_rows;
DELETE FROM cat048 WHERE I140_ToD < 27355;
SELECT (SELECT COUNT(*) FROM cat048) AS plots, (SELECT COUNT(*) FROM cat048_bds) AS bds_rows;
ROLLBACK;
```
```
+-------+----------+
| plots | bds_rows |
+-------+----------+
|   128 |      124 |
+-------+----------+
+-------+----------+
| plots | bds_rows |
+-------+----------+
|    70 |       56 |
+-------+----------+
```

## Notes for large captures

- **Loading speed.** Each `INSERT` is a single row. For bulk loads run `mysql` with
  `--init-command="SET unique_checks=0, foreign_key_checks=0"`, or load from a local socket.
- **Indexes.** Add indexes for the columns you search on, e.g.
  `CREATE INDEX icao ON cat048 (I220_ACAddr)` or `CREATE INDEX tod ON cat048 (I140_ToD)`. Child tables are
  already indexed by `rec_id` through their primary key.
- **Many columns.** CAT062 has about 300 columns without a filter. Use `-LF` to keep only what you need:
  `asterix -L > filter.txt` lists every field, one per line; keep the lines of the wanted fields (or comment
  the others out with `#`) and pass `-LF filter.txt`.
