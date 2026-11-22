#!/bin/bash
#
# Loads -Q output into MySQL or MariaDB and checks it (run by CI on MySQL 8, see tests/README.md):
#   - every case of tests/cases.txt, without and with the -LF filter, loads into a fresh database with no
#     error, and the category tables have as many rows as -j has records;
#   - the example queries of SQL.md (tests/sql/doc_queries.sql) give tests/sql/doc_queries.expected on the
#     samples loaded as SQL.md says.
#
# usage: tests/sql/load_mysql.sh <asterix binary> <mysql client command and options...>
#   e.g. tests/sql/load_mysql.sh obj/main/release/asterix mysql -h127.0.0.1 -uroot -proot
# environment: UPDATE=1 rewrites doc_queries.expected; DB=name of the scratch database (default asterix_test)
# Exit status: number of failed checks.

cd "$(dirname "$0")/../.." || exit 100
BIN=$1
shift
MYSQL=("$@")
DEFS="install/config/asterix.ini"
FILTER="tests/data/filter_subset.txt"
DB=${DB:-asterix_test}
# the strict mode of MySQL 8 (MariaDB does not check GROUP BY by default)
SQL_MODE="SET SESSION sql_mode='ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION';"
failed=0
passed=0
ok() { passed=$((passed + 1)); [ -n "$VERBOSE" ] && echo "OK      $1"; }
fail() { failed=$((failed + 1)); echo "FAILED  $1"; [ -n "$2" ] && echo "$2" | head -40 | sed 's/^/        /'; }

fresh_db() { "${MYSQL[@]}" -e "DROP DATABASE IF EXISTS \`$DB\`; CREATE DATABASE \`$DB\`"; }

# rows of all category tables (catNNN)
category_rows() {
    local q
    q=$("${MYSQL[@]}" -N "$DB" -e "SET SESSION group_concat_max_len = 1000000; SELECT GROUP_CONCAT(CONCAT('SELECT COUNT(*) AS n FROM \`', table_name, '\`') SEPARATOR ' UNION ALL ')
        FROM information_schema.tables WHERE table_schema = DATABASE() AND table_name REGEXP '^cat[0-9]{3}\$'")
    [ "$q" = "NULL" ] && { echo 0; return; }
    "${MYSQL[@]}" -N "$DB" -e "SELECT SUM(n) FROM ($q) t"
}

echo "== -Q output of every case loaded into $("${MYSQL[@]}" -N -e 'SELECT VERSION()')"
while IFS='|' read -r name input mask; do
    for lf in "" "-LF $FILTER"; do
        fresh_db
        # shellcheck disable=SC2086
        err=$( { echo "$SQL_MODE"; "$BIN" -d $DEFS $input -Q $lf 2>/dev/null; } | "${MYSQL[@]}" "$DB" 2>&1)
        if [ -n "$err" ]; then fail "load $name $lf" "$err"; continue; fi
        # shellcheck disable=SC2086
        expected=$("$BIN" -d $DEFS $input -j $lf 2>/dev/null | grep -c .)
        rows=$(category_rows)
        if [ "$rows" = "$expected" ]; then ok "load $name $lf: $rows records"
        else fail "load $name $lf: $rows rows in the category tables, $expected records in -j"; fi
    done
done < <(grep -v '^#' tests/cases.txt | grep -v '^[[:space:]]*$')

echo "== example queries of SQL.md"
fresh_db
for input in "-P -f install/sample_data/cat_034_048.pcap" "-R -f install/sample_data/cat_001_002.pcap" \
             "-P -f install/sample_data/asterix.pcap" "-P -f install/sample_data/cat_062_065.pcap"; do
    # shellcheck disable=SC2086
    "$BIN" -d $DEFS -Q $input 2>/dev/null
done | "${MYSQL[@]}" "$DB" || fail "load the samples of SQL.md"
out=$( { echo "$SQL_MODE"; cat tests/sql/doc_queries.sql; } | "${MYSQL[@]}" "$DB" 2>&1)
if [ -n "$UPDATE" ]; then
    echo "$out" > tests/sql/doc_queries.expected
    echo "tests/sql/doc_queries.expected updated: review it and the results shown in SQL.md"
elif [ "$out" = "$(cat tests/sql/doc_queries.expected)" ]; then ok "doc queries"
else fail "doc queries differ from tests/sql/doc_queries.expected (update SQL.md too)" "$(diff <(echo "$out") tests/sql/doc_queries.expected)"; fi
"${MYSQL[@]}" -e "DROP DATABASE IF EXISTS \`$DB\`"

echo
echo "passed: $passed  failed: $failed"
exit $failed
