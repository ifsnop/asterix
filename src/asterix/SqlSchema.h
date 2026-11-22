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

#ifndef SQLSCHEMA_H_
#define SQLSCHEMA_H_

#include <string>
#include <vector>
#include <set>

class DataItemBits;

/**
 * MySQL schema of the -Q output (see SQL.md).
 *
 * One table per category (catNNN) with a rec_id key, the record header columns and one column per field;
 * repetitive data (Repetitive, Variable items with a single repeating part, Explicit, BDS registers) goes
 * to child tables keyed (rec_id, [parent_rep_idx,] rep_idx). The schema is built once from the definitions
 * (addSqlColumns of every format): each field keeps its resolved column name (DataItemBits::m_strSqlColumn)
 * and each format that opens a child table keeps the table name (DataItemFormat::m_strSqlTable), so the
 * INSERT statements written while decoding always match the CREATE TABLE statements.
 */
class SqlSchema {
public:
    struct Column {
        std::string name;
        std::string type;
        std::string comment;
    };

    struct Table {
        std::string name;
        int parent;                   //!< index of the parent table, -1 for a category table
        int depth;                    //!< 0 category table, 1 its child tables, 2 a child of a child table...
        bool bds;                     //!< BDS register table: has reg_idx
        unsigned headerFields;        //!< category table: record header columns it has (HDR_* of Category.h)
        std::vector<Column> columns;  //!< field columns (keys and record header are not included)
        std::set<std::string> names;  //!< every column name in use, keys included
    };

    SqlSchema() {}

    int addCategoryTable(const std::string &name, unsigned headerFields); // header columns HDR_* of Category.h
    int addChildTable(int parent, const std::string &suffix, bool bds);
    const std::string &addColumn(int table, const std::string &name, const std::string &type,
                                 const std::string &comment); // returns the unique name given to the column
    const std::string &tableName(int table) const { return m_vTables[table].name; }
    const std::vector<Table> &tables() const { return m_vTables; }

    std::string render() const; // CREATE TABLE statements of the tables that have a field (or a child that has)

    static std::string tableSuffix(const std::string &prefix); // "I250_" -> "I250"
    static std::string columnType(const DataItemBits &bits);
    static void appendIdentifier(std::string &s, const std::string &name); // `name`
    static void appendString(std::string &s, const char *str, bool trim); // 'str', quotes and \ escaped

    /** Key columns of a table at the given depth: rec_id for a category table, then for child tables
     *  [parentN_rep_idx..., parent2_rep_idx,] [parent_rep_idx,] rep_idx (the indexes of the parent rows) */
    static std::vector<std::string> keyColumns(int depth);

private:
    std::vector<Table> m_vTables;
    std::set<std::string> m_sTableNames;
};

/**
 * Rows of one decoded record in -Q output. DataRecord sets the current record (Scope) while its items are
 * written; formats that make child rows open and close them, and fields add their value to the open row.
 */
class SqlRecord {
public:
    struct Row {
        const std::string *pTable;
        int parent;      //!< index of the parent row, -1 for the category row
        int depth;
        bool bds;
        int repIdx;
        int regIdx;
        bool hasFields;
        std::string columns; //!< `a`,`b`,...
        std::string values;  //!< 1,'x',...
        std::vector<const std::string *> used; //!< column names already in this row
    };

    explicit SqlRecord(const std::string *pTable);

    /** Opens a child row in pTable and makes it the current row; returns the row that was current. */
    int openRow(const std::string *pTable, int repIdx, bool bds);

    void closeRow(int previous) { m_nCurrent = previous; }

    /** Adds a column to the current row; returns the string to append its value to, or NULL if it is already set. */
    std::string *addField(const std::string &column);

    /** Index the next BDS row gets (set by Repetitive for each repetition); 0 when not set. */
    void setNextRepIdx(int repIdx) { m_nNextRepIdx = repIdx; }

    int takeNextRepIdx() {
        int r = m_nNextRepIdx;
        m_nNextRepIdx = 0;
        return r;
    }

    void render(std::string &strResult) const; // INSERT statements

    static SqlRecord *s_pCurrent;

    /** While an object of this class exists, s_pCurrent is the given record. Output is single threaded. */
    class Scope {
    public:
        explicit Scope(SqlRecord *pRecord) : m_pPrevious(s_pCurrent) { s_pCurrent = pRecord; }

        ~Scope() { s_pCurrent = m_pPrevious; }

    private:
        SqlRecord *m_pPrevious;
    };

private:
    std::vector<Row> m_vRows;
    int m_nCurrent;
    int m_nNextRepIdx;
};

#endif /* SQLSCHEMA_H_ */
