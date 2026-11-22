/*
 * -Q output (SqlSchema, SqlRecord): MySQL type of each encoding, quoting, keys of child tables, and the
 * INSERT statements of real and synthetic records (BDS registers, repeated registers, Explicit, lists).
 */
#include <memory>
#include <string>
#include <vector>

#include "minitest.h"
#include "test_support.h"
#include "AsterixData.h"
#include "Category.h"
#include "SqlSchema.h"

namespace {

std::string typeOf(DataItemBits::_eEncoding encoding, int bits, double scale = 0.0) {
    std::unique_ptr<DataItemBits> b(makeBits("F", 1, bits, encoding, scale));
    return SqlSchema::columnType(*b);
}

std::string quoted(const char *s, bool trim) {
    std::string out;
    SqlSchema::appendString(out, s, trim);
    return out;
}

// the schema of every definition is built once (it gives the formats their table and column names)
const std::string &schema() {
    static std::string s = parser().printSqlSchema();
    return s;
}

std::string sql(const std::string &blockHex) {
    schema();
    std::vector<unsigned char> data = fromHex(blockHex);
    std::unique_ptr<AsterixData> d(parser().parsePacket(&data[0], (unsigned int) data.size(), 12.5));
    std::string out;
    d->getText(out, CAsterixFormat::ESQL);
    return out;
}

bool has(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE("sql: MySQL type of each encoding and width") {
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_UNSIGNED, 1), std::string("TINYINT UNSIGNED"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_UNSIGNED, 8), std::string("TINYINT UNSIGNED"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_UNSIGNED, 9), std::string("SMALLINT UNSIGNED"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_UNSIGNED, 24), std::string("MEDIUMINT UNSIGNED"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_UNSIGNED, 32), std::string("INT UNSIGNED"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_UNSIGNED, 33), std::string("BIGINT UNSIGNED"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_SIGNED, 16), std::string("SMALLINT"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_SIGNED, 16, 0.25), std::string("DOUBLE"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_UNSIGNED, 16, 1.0 / 128), std::string("DOUBLE"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_SIX_BIT_CHAR, 48), std::string("VARCHAR(8)"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_HEX_BIT_CHAR, 24), std::string("VARCHAR(6)"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_OCTAL, 12), std::string("VARCHAR(4)"));
    CHECK_EQ(typeOf(DataItemBits::DATAITEM_ENCODING_ASCII, 56), std::string("VARCHAR(7)"));
}

TEST_CASE("sql: strings are quoted with ' and \\ escaped, text fields trimmed") {
    CHECK_EQ(quoted("DLH65A  ", true), std::string("'DLH65A'"));
    CHECK_EQ(quoted("  A B ", true), std::string("'A B'"));
    CHECK_EQ(quoted("        ", true), std::string("''"));
    CHECK_EQ(quoted("two's", false), std::string("'two''s'"));
    CHECK_EQ(quoted("a\\b", false), std::string("'a\\\\b'"));
    std::string id;
    SqlSchema::appendIdentifier(id, "a`b");
    CHECK_EQ(id, std::string("`a``b`"));
}

TEST_CASE("sql: keys of category, child and nested child tables") {
    CHECK_EQ(SqlSchema::keyColumns(0).size(), (size_t) 1);
    std::vector<std::string> k1 = SqlSchema::keyColumns(1);
    REQUIRE(k1.size() == 2);
    CHECK_EQ(k1[1], std::string("rep_idx"));
    std::vector<std::string> k3 = SqlSchema::keyColumns(3);
    REQUIRE(k3.size() == 4);
    CHECK_EQ(k3[1], std::string("parent2_rep_idx"));
    CHECK_EQ(k3[2], std::string("parent_rep_idx"));
    CHECK_EQ(k3[3], std::string("rep_idx"));
}

TEST_CASE("sql: schema names, duplicates, child tables and foreign keys") {
    SqlSchema s;
    int cat = s.addCategoryTable("cat999", HDR_ALL);
    CHECK_EQ(s.addColumn(cat, "I010_FX", "TINYINT UNSIGNED", "x"), std::string("I010_FX"));
    CHECK_EQ(s.addColumn(cat, "I010_FX", "TINYINT UNSIGNED", "x"), std::string("I010_FX1"));
    CHECK_EQ(s.addColumn(cat, "id", "INT", ""), std::string("id1")); // a record header name is taken
    int child = s.addChildTable(cat, "I020", false);
    int nested = s.addChildTable(child, "SUB", false);
    int empty = s.addChildTable(cat, "I030", true);
    s.addColumn(nested, "V", "INT", "it's");
    CHECK_EQ(s.tableName(nested), std::string("cat999_I020_SUB"));
    std::string r = s.render();
    CHECK(has(r, "CREATE TABLE IF NOT EXISTS `cat999_I020` (")); // no field, kept for its nested table
    CHECK(!has(r, s.tableName(empty)));                            // no field: not created
    CHECK(has(r, "PRIMARY KEY (`rec_id`,`parent_rep_idx`,`rep_idx`),\n  FOREIGN KEY (`rec_id`,`parent_rep_idx`) "
                 "REFERENCES `cat999_I020` (`rec_id`,`rep_idx`) ON DELETE CASCADE"));
    CHECK(has(r, "`V` INT COMMENT 'it''s'"));
    CHECK(has(r, "  `hexdata` MEDIUMTEXT,\n"));

    // -LF with HDR lines: only those record header columns
    SqlSchema f;
    int t = f.addCategoryTable("cat998", HDR_TIMESTAMP | HDR_SRCIP);
    f.addColumn(t, "I010_SAC", "TINYINT UNSIGNED", "");
    CHECK(has(f.render(), "  `rec_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,\n  `timestamp` DOUBLE,\n"
                          "  `srcip` VARCHAR(15),\n  `I010_SAC` TINYINT UNSIGNED,\n"));
}

TEST_CASE("sql: rows of nested child tables carry the index of each parent row") {
    std::string table("t"), child("t_c"), nested("t_c_n"), reg("t_r");
    std::string a("a"), b("b");
    SqlRecord rec(&table);
    *rec.addField(a) += "1";
    CHECK(rec.addField(a) == NULL); // a column is set once per row
    int top = rec.openRow(&child, 3, false);
    int mid = rec.openRow(&nested, 5, false);
    *rec.addField(b) += "2";
    rec.closeRow(mid);
    rec.closeRow(top);
    rec.openRow(&child, 4, false); // nothing in it: not written
    rec.closeRow(top);
    for (int i = 0; i < 2; i++) {
        int p = rec.openRow(&reg, i, true);
        *rec.addField(b) += "9";
        rec.closeRow(p);
    }
    std::string out;
    rec.render(out);
    CHECK_EQ(out, std::string("INSERT INTO `t` (`a`) VALUES (1);\n"
                              "SET @asterix_rec = LAST_INSERT_ID();\n"
                              "INSERT INTO `t_c` (`rec_id`,`rep_idx`) VALUES (@asterix_rec,3);\n"
                              "INSERT INTO `t_c_n` (`rec_id`,`parent_rep_idx`,`rep_idx`,`b`) VALUES (@asterix_rec,3,5,2);\n"
                              "INSERT INTO `t_r` (`rec_id`,`rep_idx`,`reg_idx`,`b`) VALUES (@asterix_rec,0,0,9);\n"
                              "INSERT INTO `t_r` (`rec_id`,`rep_idx`,`reg_idx`,`b`) VALUES (@asterix_rec,1,1,9);\n"));
}

TEST_CASE("sql: a real CAT001 record, with its I210 list of parts in a child table") {
    std::string out = sql("01001AF7C619C9A00EB2767F189408AA42D8033405C8800D400E");
    CHECK(has(out, "INSERT INTO `cat001` (`id`,`cat`,`length`,`crc`,`timestamp`,`srcip`,`fspec`,"
                   "`hexdata`,`I010_SAC`,`I010_SIC`,"));
    CHECK(has(out, ",'F7C6','19C9A00EB2767F189408AA42D8033405C8800D400E',25,201,"));
    CHECK(has(out, "SET @asterix_rec = LAST_INSERT_ID();\n"
                   "INSERT INTO `cat001_I210` (`rec_id`,`rep_idx`,`Qty`,`FX`) VALUES (@asterix_rec,0,7,0);\n"));
    CHECK(out.compare(0, 12, "INSERT INTO ") == 0); // no transaction around the statements
    CHECK(has(schema(), "CREATE TABLE IF NOT EXISTS `cat001_I210` ("));
}

TEST_CASE("sql: CAT048 BDS registers (one repeated, one without a definition) and Explicit SP bodies") {
    // I010, I250 with BDS 4,0 + 6,0 + 4,0 again, SP with two bodies
    std::string out = sql("3000258121010419C903C0780031BC0000400000000000000060C0780031BC00004003AABB");
    CHECK(has(out, "INSERT INTO `cat048_I250_BDS40` (`rec_id`,`rep_idx`,`reg_idx`,`MCP_ALT_STATUS`,`MCP_ALT`,"));
    CHECK(has(out, "VALUES (@asterix_rec,0,0,1,33008.0000000,"));
    CHECK(has(out, "INSERT INTO `cat048_I250_BDS60` (`rec_id`,`rep_idx`,`reg_idx`,"));
    CHECK(has(out, "VALUES (@asterix_rec,1,0,0,0.0000000,"));
    CHECK(has(out, "VALUES (@asterix_rec,2,1,1,33008.0000000,")); // second BDS 4,0: reg_idx 1
    CHECK(has(out, "INSERT INTO `cat048_ISP` (`rec_id`,`rep_idx`,`SP`) VALUES (@asterix_rec,0,170);\n"
                   "INSERT INTO `cat048_ISP` (`rec_id`,`rep_idx`,`SP`) VALUES (@asterix_rec,1,187);\n"));
    // register 2,0 has no definition of its own: table BDS0, with the register number in BDS; SP with one body
    out = sql("3000148121010419C901112233445566772002CC");
    CHECK(has(out, "INSERT INTO `cat048_I250_BDS0` (`rec_id`,`rep_idx`,`reg_idx`,`VAL`,`BDS`) "
                   "VALUES (@asterix_rec,0,0,'11223344556677','20');\n"));
    CHECK(has(out, "INSERT INTO `cat048_ISP` (`rec_id`,`rep_idx`,`SP`) VALUES (@asterix_rec,0,204);\n"));
}

TEST_CASE("sql: a packet with nothing to print writes nothing") {
    schema();
    std::vector<unsigned char> data = fromHex("010003");
    std::unique_ptr<AsterixData> d(parser().parsePacket(&data[0], (unsigned int) data.size(), 0));
    std::string out;
    d->getText(out, CAsterixFormat::ESQL);
    CHECK_EQ(out, std::string(""));
}
