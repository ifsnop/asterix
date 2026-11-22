/*
 *  Copyright (c) 2013 Croatia Control Ltd. (www.crocontrol.hr)
 *
 *  This file is part of Asterix.
 *
 *  Asterix is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Asterix is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Asterix.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "SqlSchema.h"
#include "DataItemFormat.h"
#include "DataItemBits.h"
#include "Utils.h"

// Record header columns of every category table, in the order of the -j record header
static const struct {
    const char *name;
    const char *type;
} HEADER_COLUMNS[] = {
        {"id",        "INT"},
        {"cat",       "TINYINT UNSIGNED"},
        {"length",    "INT UNSIGNED"},
        {"crc",       "CHAR(8)"},
        {"timestamp", "DOUBLE"},
        {"srcip",     "VARCHAR(15)"},
        {"fspec",     "VARCHAR(255)"},
        {"hexdata",   "MEDIUMTEXT"},
};

static const char TABLE_OPTIONS[] = ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;\n";

int SqlSchema::addCategoryTable(const std::string &name) {
    Table t;
    t.name = name;
    t.parent = -1;
    t.depth = 0;
    t.bds = false;
    t.names.insert("rec_id");
    for (size_t i = 0; i < sizeof(HEADER_COLUMNS) / sizeof(HEADER_COLUMNS[0]); i++)
        t.names.insert(HEADER_COLUMNS[i].name);
    m_sTableNames.insert(name);
    m_vTables.push_back(t);
    return (int) m_vTables.size() - 1;
}

int SqlSchema::addChildTable(int parent, const std::string &suffix, bool bds) {
    Table t;
    std::string base = m_vTables[parent].name + "_" + (suffix.empty() ? std::string("rep") : suffix);
    t.name = base;
    for (int n = 1; m_sTableNames.count(t.name); n++)
        t.name = base + std::to_string(n);
    t.parent = parent;
    t.depth = m_vTables[parent].depth + 1;
    t.bds = bds;
    std::vector<std::string> keys = keyColumns(t.depth);
    t.names.insert(keys.begin(), keys.end());
    if (bds)
        t.names.insert("reg_idx");
    m_sTableNames.insert(t.name);
    m_vTables.push_back(t);
    return (int) m_vTables.size() - 1;
}

const std::string &SqlSchema::addColumn(int table, const std::string &name, const std::string &type,
                                        const std::string &comment) {
    Table &t = m_vTables[table];
    std::string unique = name;
    for (int n = 1; t.names.count(unique); n++)
        unique = name + std::to_string(n);
    t.names.insert(unique);
    Column c;
    c.name = unique;
    c.type = type;
    c.comment = comment;
    t.columns.push_back(c);
    return t.columns.back().name;
}

std::vector<std::string> SqlSchema::keyColumns(int depth) {
    std::vector<std::string> keys;
    keys.push_back("rec_id");
    for (int up = depth - 1; up >= 1; up--)
        keys.push_back(up == 1 ? std::string("parent_rep_idx") : "parent" + std::to_string(up) + "_rep_idx");
    if (depth > 0)
        keys.push_back("rep_idx");
    return keys;
}

std::string SqlSchema::tableSuffix(const std::string &prefix) {
    std::string s = prefix;
    while (!s.empty() && s[s.length() - 1] == '_')
        s.erase(s.length() - 1);
    return s;
}

std::string SqlSchema::columnType(const DataItemBits &bits) {
    int nbits = bits.m_nTo >= bits.m_nFrom ? bits.m_nTo - bits.m_nFrom + 1 : bits.m_nFrom - bits.m_nTo + 1;
    switch (bits.m_eEncoding) {
        case DataItemBits::DATAITEM_ENCODING_UNSIGNED:
        case DataItemBits::DATAITEM_ENCODING_SIGNED: {
            if (bits.m_dScale != 0)
                return "DOUBLE";
            const char *type;
            if (nbits <= 8)
                type = "TINYINT";
            else if (nbits <= 16)
                type = "SMALLINT";
            else if (nbits <= 24)
                type = "MEDIUMINT";
            else if (nbits <= 32)
                type = "INT";
            else
                type = "BIGINT";
            std::string s = type;
            if (bits.m_eEncoding == DataItemBits::DATAITEM_ENCODING_UNSIGNED)
                s += " UNSIGNED";
            return s;
        }
        case DataItemBits::DATAITEM_ENCODING_SIX_BIT_CHAR:
            return "VARCHAR(" + std::to_string(nbits / 6 > 0 ? nbits / 6 : 1) + ")";
        case DataItemBits::DATAITEM_ENCODING_HEX_BIT_CHAR:
            return "VARCHAR(" + std::to_string((nbits + 7) / 8 * 2) + ")"; // two hex digits per byte
        case DataItemBits::DATAITEM_ENCODING_OCTAL:
            return "VARCHAR(" + std::to_string((nbits + 2) / 3) + ")";
        case DataItemBits::DATAITEM_ENCODING_ASCII:
            return "VARCHAR(" + std::to_string(nbits / 8 > 0 ? nbits / 8 : 1) + ")";
    }
    return "VARCHAR(255)";
}

void SqlSchema::appendIdentifier(std::string &s, const std::string &name) {
    s += '`';
    for (size_t i = 0; i < name.length(); i++) {
        if (name[i] == '`')
            s += '`';
        s += name[i];
    }
    s += '`';
}

void SqlSchema::appendString(std::string &s, const char *str, bool trim) {
    size_t len = strlen(str);
    size_t from = 0;
    if (trim) {
        while (from < len && str[from] == ' ')
            from++;
        while (len > from && str[len - 1] == ' ')
            len--;
    }
    s += '\'';
    for (size_t i = from; i < len; i++) {
        char c = str[i];
        if (c == '\'' || c == '\\')
            s += c == '\'' ? '\'' : '\\';
        s += c;
    }
    s += '\'';
}

static void appendKeyList(std::string &s, const std::vector<std::string> &keys, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (i)
            s += ',';
        SqlSchema::appendIdentifier(s, keys[i]);
    }
}

std::string SqlSchema::render() const {
    // a table is created if it has a field or a child table that is created (children come after parents)
    std::vector<bool> keep(m_vTables.size(), false);
    for (size_t i = m_vTables.size(); i-- > 0;) {
        if (!m_vTables[i].columns.empty())
            keep[i] = true;
        if (keep[i] && m_vTables[i].parent >= 0)
            keep[m_vTables[i].parent] = true;
    }

    std::string s = "-- MySQL schema and data of asterix -Q output: one table per category, child tables for "
                    "repetitive data items (see SQL.md)\n";
    for (size_t i = 0; i < m_vTables.size(); i++) {
        if (!keep[i])
            continue;
        const Table &t = m_vTables[i];
        s += "CREATE TABLE IF NOT EXISTS ";
        appendIdentifier(s, t.name);
        s += " (\n";
        std::vector<std::string> keys = keyColumns(t.depth);
        if (t.depth == 0) {
            s += "  `rec_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,\n";
            for (size_t h = 0; h < sizeof(HEADER_COLUMNS) / sizeof(HEADER_COLUMNS[0]); h++) {
                s += "  ";
                appendIdentifier(s, HEADER_COLUMNS[h].name);
                s += ' ';
                s += HEADER_COLUMNS[h].type;
                s += ",\n";
            }
        } else {
            s += "  `rec_id` BIGINT UNSIGNED NOT NULL,\n";
            for (size_t k = 1; k < keys.size(); k++) {
                s += "  ";
                appendIdentifier(s, keys[k]);
                s += " SMALLINT UNSIGNED NOT NULL,\n";
            }
            if (t.bds)
                s += "  `reg_idx` SMALLINT UNSIGNED NOT NULL,\n";
        }
        for (size_t c = 0; c < t.columns.size(); c++) {
            s += "  ";
            appendIdentifier(s, t.columns[c].name);
            s += ' ';
            s += t.columns[c].type;
            if (!t.columns[c].comment.empty()) {
                s += " COMMENT ";
                appendString(s, t.columns[c].comment.c_str(), false);
            }
            s += ",\n";
        }
        s += "  PRIMARY KEY (";
        appendKeyList(s, keys, keys.size());
        s += ")";
        if (t.parent >= 0) {
            std::vector<std::string> parentKeys = keyColumns(t.depth - 1);
            s += ",\n  FOREIGN KEY (";
            appendKeyList(s, keys, keys.size() - 1);
            s += ") REFERENCES ";
            appendIdentifier(s, m_vTables[t.parent].name);
            s += " (";
            appendKeyList(s, parentKeys, parentKeys.size());
            s += ") ON DELETE CASCADE";
        }
        s += '\n';
        s += TABLE_OPTIONS;
    }
    return s;
}

SqlRecord *SqlRecord::s_pCurrent = NULL;

SqlRecord::SqlRecord(const std::string *pTable) : m_nCurrent(0), m_nNextRepIdx(0) {
    Row r;
    r.pTable = pTable;
    r.parent = -1;
    r.depth = 0;
    r.bds = false;
    r.repIdx = 0;
    r.regIdx = 0;
    r.hasFields = false;
    m_vRows.push_back(r);
}

int SqlRecord::openRow(const std::string *pTable, int repIdx, bool bds) {
    Row r;
    r.pTable = pTable;
    r.parent = m_nCurrent;
    r.depth = m_vRows[m_nCurrent].depth + 1;
    r.bds = bds;
    r.repIdx = repIdx;
    r.regIdx = 0;
    r.hasFields = false;
    if (bds) { // occurrence number of this register among the rows of the same parent row
        for (size_t i = 0; i < m_vRows.size(); i++) {
            if (m_vRows[i].parent == m_nCurrent && m_vRows[i].pTable == pTable)
                r.regIdx++;
        }
    }
    int previous = m_nCurrent;
    m_vRows.push_back(r);
    m_nCurrent = (int) m_vRows.size() - 1;
    return previous;
}

std::string *SqlRecord::addField(const std::string &column) {
    Row &r = m_vRows[m_nCurrent];
    for (size_t i = 0; i < r.used.size(); i++) {
        if (r.used[i] == &column || *r.used[i] == column)
            return NULL; // e.g. extents of a Variable item past its last defined part reuse that part
    }
    r.used.push_back(&column);
    if (r.hasFields) {
        r.columns += ',';
        r.values += ',';
    }
    r.hasFields = true;
    SqlSchema::appendIdentifier(r.columns, column);
    return &r.values;
}

void SqlRecord::render(std::string &strResult) const {
    if (m_vRows.empty() || m_vRows[0].pTable == NULL || m_vRows[0].pTable->empty())
        return;
    // a child row is written if it has a field or a child row that is written (its key is needed)
    std::vector<bool> emit(m_vRows.size(), false);
    for (size_t i = m_vRows.size(); i-- > 1;) {
        if (m_vRows[i].hasFields && !m_vRows[i].pTable->empty())
            emit[i] = true;
        if (emit[i])
            emit[m_vRows[i].parent] = true;
    }
    emit[0] = true;

    const Row &root = m_vRows[0];
    strResult.append("INSERT INTO ", 12);
    SqlSchema::appendIdentifier(strResult, *root.pTable);
    strResult.append(" (", 2);
    strResult += root.columns;
    strResult.append(") VALUES (", 10);
    strResult += root.values;
    strResult.append(");\n", 3);

    bool first = true;
    for (size_t i = 1; i < m_vRows.size(); i++) {
        if (!emit[i])
            continue;
        if (first) {
            strResult += "SET @asterix_rec = LAST_INSERT_ID();\n";
            first = false;
        }
        const Row &r = m_vRows[i];
        std::vector<std::string> keys = SqlSchema::keyColumns(r.depth);
        // indexes of the parent rows, outermost first
        std::vector<int> parentIdx;
        for (int p = r.parent; p > 0; p = m_vRows[p].parent)
            parentIdx.insert(parentIdx.begin(), m_vRows[p].repIdx);

        strResult.append("INSERT INTO ", 12);
        SqlSchema::appendIdentifier(strResult, *r.pTable);
        strResult.append(" (", 2);
        appendKeyList(strResult, keys, keys.size());
        if (r.bds)
            strResult += ",`reg_idx`";
        if (r.hasFields) {
            strResult += ',';
            strResult += r.columns;
        }
        strResult.append(") VALUES (@asterix_rec", 22);
        for (size_t p = 0; p < parentIdx.size(); p++) {
            strResult += ',';
            appendLong(strResult, parentIdx[p]);
        }
        strResult += ',';
        appendLong(strResult, r.repIdx);
        if (r.bds) {
            strResult += ',';
            appendLong(strResult, r.regIdx);
        }
        if (r.hasFields) {
            strResult += ',';
            strResult += r.values;
        }
        strResult.append(");\n", 3);
    }
}
