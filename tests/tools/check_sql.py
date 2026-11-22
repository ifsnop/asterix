#!/usr/bin/env python3
"""
Checks that the -Q (MySQL) output says the same as -j, for every case in tests/cases.txt, without and with
the -LF filter of the golden tests. This is the contract a database loader relies on (see SQL.md):

  - the schema (CREATE TABLE) has every table and column an INSERT uses, and the keys of every child table
  - each -j record is one INSERT into its category table (same header values), followed by the rows of its
    child tables, in the same order as the arrays of -j:
      * an object is flattened into columns named by its path (I010_SAC, I380_ADR_ADR); a name repeated in
        the same row is numbered (FX, FX1, ...)
      * an array (Repetitive, Variable list of parts, Explicit with several bodies) is one row per element in
        table <parent>_<path>, keyed (rec_id, [parent_rep_idx,] rep_idx); an Explicit item with one body is
        an object in -j and one row in SQL
      * a BDS register goes to <parent>_<path>_BDS<id> (BDS0 for registers without a definition of their
        own) and also has reg_idx, its occurrence number among the rows of that table in the parent row
      * a row is written if it has a field or a child row that is written
  - values have the same text, text fields trimmed

usage: check_sql.py <asterix binary>     (run from anywhere)
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
DEFS = 'install/config/asterix.ini'
FILTER = 'tests/data/filter_subset.txt'
HEADER = ('id', 'cat', 'length', 'crc', 'timestamp', 'srcip', 'fspec', 'hexdata')
errors = []


def cases():
    for line in open(os.path.join(ROOT, 'tests', 'cases.txt')):
        line = line.strip()
        if line and not line.startswith('#'):
            name, args, mask = line.split('|')
            yield name, args.split(), mask == 'mask'


def run(binary, args):
    r = subprocess.run([binary, '-d', DEFS] + args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return r.stdout.decode('latin-1')


CREATE = re.compile(r'^CREATE TABLE IF NOT EXISTS `([^`]+)` \($')
COLUMN = re.compile(r'^  `([^`]+)` ')
INSERT = re.compile(r'^INSERT INTO `([^`]+)` \(([^)]*)\) VALUES \((.*)\);$')
TOKEN = re.compile(r"'((?:[^']|'')*)'|([^,']+)")


def sql_values(text):
    values = []
    pos = 0
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise ValueError('bad VALUES at %d: %s' % (pos, text[pos:pos + 40]))
        values.append(('s', m.group(1).replace("''", "'").replace('\\\\', '\\')) if m.group(1) is not None
                      else ('n', m.group(2)))
        pos = m.end()
        if pos < len(text):
            if text[pos] != ',':
                raise ValueError('bad VALUES at %d: %s' % (pos, text[pos:pos + 40]))
            pos += 1
    return values


def parse_sql(text):
    """schema {table: [columns]} and records [[(table, keys, {column: (kind, value)})...]...]"""
    schema, records, table = {}, [], None
    for line in text.splitlines():
        m = CREATE.match(line)
        if m:
            table = m.group(1)
            schema[table] = []
            continue
        if table is not None:
            m = COLUMN.match(line)
            if m:
                schema[table].append(m.group(1))
                continue
            if line.startswith(')'):
                table = None
            continue
        m = INSERT.match(line)
        if not m:
            continue
        tname = m.group(1)
        cols = [c.strip('`') for c in m.group(2).split(',')]
        vals = sql_values(m.group(3))
        if len(cols) != len(vals):
            raise ValueError('%d columns and %d values: %s' % (len(cols), len(vals), line[:120]))
        row = dict(zip(cols, vals))
        keys = tuple(row.pop(k)[1] for k in [c for c in cols if c == 'rec_id' or c.endswith('rep_idx')
                                                 or c == 'reg_idx'])
        if re.match(r'^cat\d{3}$', tname):
            records.append([])
        records[-1].append((tname, keys, row))
    return schema, records


def strip_key(prefix):
    return prefix.rstrip('_')


class Expected:
    def __init__(self, schema):
        self.schema = schema
        self.rows = []

    def new_row(self, table, parent, rep_idx, bds):
        row = {'table': table, 'parent': parent, 'rep': rep_idx, 'bds': bds, 'reg': 0, 'cols': {},
               'emit': False}
        if bds:
            row['reg'] = sum(1 for r in self.rows if r['parent'] is parent and r['table'] == table)
        self.rows.append(row)
        return row

    def add(self, row, name, value):
        col, n = name, 1
        while col in row['cols']:
            col = name + str(n)
            n += 1
        row['cols'][col] = value

    def child_table(self, row, prefix, key):
        return row['table'] + '_' + (strip_key(prefix + key) or 'rep')

    def elements(self, row, prefix, key, value):
        """child rows of an array (or of an Explicit object with one body)"""
        base = self.child_table(row, prefix, key)
        for idx, elem in enumerate(value):
            elem_pairs = elem if isinstance(elem, list) else []
            bds_value = [v for k, v in elem_pairs if k == 'BDS'] if base not in self.schema else []
            if bds_value:
                reg = 'BDS%x' % int(bds_value[0], 16)
                table = base + '_' + reg
                if table not in self.schema:
                    table = base + '_BDS0'
                child = self.new_row(table, row, idx, True)
            else:
                child = self.new_row(base, row, idx, False)
            self.flatten(child, '', elem_pairs)

    def flatten(self, row, prefix, obj):
        for key, value in obj:
            if isinstance(value, JsonArray):
                self.elements(row, prefix, key, list(value))
            elif isinstance(value, list):  # an object, as (key, value) pairs
                if value and self.child_table(row, prefix, key) in self.schema:
                    self.elements(row, prefix, key, [value])  # Explicit item with one body
                else:
                    self.flatten(row, prefix + key + '_', value)
            else:
                self.add(row, prefix + key, value)

    def finish(self):
        for r in reversed(self.rows):
            if r['cols']:
                r['emit'] = True
            if r['emit'] and r['parent'] is not None:
                r['parent']['emit'] = True
        out = []
        for r in self.rows:
            if r['parent'] is not None and not r['emit']:
                continue
            idx = []
            p = r
            while p['parent'] is not None:
                idx.insert(0, str(p['rep']))
                p = p['parent']
            keys = (('@asterix_rec',) + tuple(idx) + ((str(r['reg']),) if r['bds'] else ())) \
                if r['parent'] is not None else ()
            out.append((r['table'], keys, r['cols']))
        return out


class JsonArray(list):
    pass


def decode_json(line):
    """-j record with objects as lists of (key, value) pairs, arrays as JsonArray, numbers as their text"""
    dec = json.JSONDecoder(object_pairs_hook=lambda p: [tuple(x) for x in p], parse_float=str, parse_int=str)
    # json gives arrays as list: mark them by decoding with a custom scanner is not possible, so arrays are
    # told apart from objects by their elements: objects are lists of 2-tuples
    obj = dec.decode(line)

    def mark(v):
        if isinstance(v, list):
            if v and all(isinstance(e, tuple) for e in v):
                return [(k, mark(x)) for k, x in v]
            return JsonArray(mark(e) for e in v)
        return v
    return mark(obj)


def expected_rows(schema, record):
    header = dict((k, v) for k, v in record if not k.startswith('CAT'))
    cat_items = [v for k, v in record if k.startswith('CAT')][0]
    table = 'cat%03d' % int(header['cat'])
    e = Expected(schema)
    root = e.new_row(table, None, 0, False)
    for k in HEADER:
        root['cols'][k] = header[k]
    e.flatten(root, '', cat_items)
    return e.finish()


def same_value(json_value, sql_value):
    kind, text = sql_value
    if kind == 's':
        return isinstance(json_value, str) and json_value.strip(' ') == text
    return json_value == text


def check_case(binary, name, args, mask):
    sql = run(binary, args + ['-Q'])
    js = [l for l in run(binary, args + ['-j']).splitlines() if l.strip()]
    try:
        schema, records = parse_sql(sql)
    except ValueError as ex:
        errors.append('%s: %s' % (name, ex))
        return 0
    if len(records) != len(js):
        errors.append('%s: %d records in -Q, %d in -j' % (name, len(records), len(js)))
        return 0
    nrows = 0
    for n, (line, got) in enumerate(zip(js, records)):
        exp = expected_rows(schema, decode_json(line))
        where = '%s record %d' % (name, n + 1)
        if len(exp) != len(got):
            errors.append('%s: rows %s in -Q, %s expected from -j' % (
                where, [(t, k) for t, k, _ in got], [(t, k) for t, k, _ in exp]))
            continue
        for (et, ek, ecols), (gt, gk, gcols) in zip(exp, got):
            nrows += 1
            if et != gt or ek != tuple(v for v in gk):
                errors.append('%s: row %s %s, expected %s %s' % (where, gt, gk, et, ek))
                continue
            if gt not in schema:
                errors.append('%s: table %s is not in the schema' % (where, gt))
                continue
            for c in gcols:
                if c not in schema[gt] and c not in HEADER:
                    errors.append('%s: column %s.%s is not in the schema' % (where, gt, c))
            if set(ecols) != set(gcols):
                errors.append('%s: %s columns %s, expected %s' % (where, gt, sorted(gcols), sorted(ecols)))
                continue
            for c, v in ecols.items():
                if c == 'timestamp' and mask:
                    continue  # wall-clock time of raw and ORADIS file input: differs between the two runs
                if not same_value(v, gcols[c]):
                    errors.append('%s: %s.%s = %s, -j has %r' % (where, gt, c, gcols[c][1], v))
    return nrows


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    binary = os.path.abspath(sys.argv[1])
    total = 0
    for name, args, mask in cases():
        for extra, label in (([], name), (['-LF', FILTER], name + ' -LF')):
            total += check_case(binary, label, args + extra, mask)
    for e in errors[:50]:
        print(e)
    if errors:
        print('%d errors' % len(errors))
        sys.exit(1)
    print('-Q matches -j: %d rows checked' % total)


if __name__ == '__main__':
    main()
