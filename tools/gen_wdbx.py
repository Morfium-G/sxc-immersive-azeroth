"""Writes the WDBX Editor definitions of the mod's tables from src/Tables.cpp.

    python tools/gen_wdbx.py

Output: docs/wdbx/ImmersiveAzeroth.xml (all tables, loadable as a definition file) and one
docs/wdbx/<Table>.xml per table (the <Table> element alone, to paste into another definition file).
Run it after changing a schema: the files on disk are .cdbc files with the plain WDBC layout, one
column per number (a vector is X, Y, Z; a colour in float form R, G, B), strings in the string block.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TABLES = os.path.join(ROOT, 'src', 'Tables.cpp')
OUT = os.path.join(ROOT, 'docs', 'wdbx')

# Schema builder calls -> WDBX column types (WXL's definitions used int for unsigned ids too).
TYPES = {
    'Int': 'int', 'Uint': 'int', 'Enum': 'int', 'Flags': 'int', 'ForeignKey': 'int', 'Bool': 'int',
    'Float': 'float', 'String': 'string', 'Text': 'string', 'Path': 'string',
}

SCHEMA = re.compile(r'Schema\s+s\("(\w+)"')
CALL = re.compile(r'\bs\.(\w+)\(("(\w+)")?([^;]*)')


def parse(text):
    tables = []
    for block in re.split(r'\n\s*Schema\s+\w+\(\)\s*\{', text)[1:]:
        body = block.split('return s;')[0]
        name = SCHEMA.search(body)
        if not name:
            continue
        fields = []
        for line in body.splitlines():
            line = line.strip()
            if line.startswith('Scope(s)'):
                fields += [('ScopeType', 'int'), ('ScopeID', 'int')]
                continue
            if line.startswith('Comment(s)'):
                fields.append(('Comment', 'string'))
                continue
            call = CALL.search(line)
            if not call:
                continue
            kind, column, rest = call.group(1), call.group(3), call.group(4) or ''
            if kind == 'Id':
                fields.append(('ID', 'id'))
            elif kind == 'Vector':
                fields += [(column + axis, 'float') for axis in 'XYZ']
            elif kind == 'Color':
                if 'ColorFormat::Float3' in rest:
                    fields += [(column + c, 'float') for c in 'RGB']
                else:
                    fields.append((column, 'uint'))
            elif kind in TYPES and column:
                fields.append((column, TYPES[kind]))
            elif column:
                sys.exit('unknown field kind %s (%s.%s): add it to TYPES' % (kind, name.group(1), column))
        tables.append((name.group(1), fields))
    return tables


def table_xml(name, fields, indent='  '):
    lines = ['%s<Table Name="%s" Build="12340">' % (indent, name)]
    for column, kind in fields:
        if kind == 'id':
            lines.append('%s  <Field Name="%s" Type="int" IsIndex="true" />' % (indent, column))
        else:
            lines.append('%s  <Field Name="%s" Type="%s" />' % (indent, column, kind))
    lines.append('%s</Table>' % indent)
    return '\n'.join(lines)


def main():
    with open(TABLES, 'r', encoding='utf-8') as f:
        tables = parse(f.read())
    if not tables:
        sys.exit('no schemas found in ' + TABLES)
    os.makedirs(OUT, exist_ok=True)
    tables.sort()
    for name, fields in tables:
        with open(os.path.join(OUT, name + '.xml'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(table_xml(name, fields) + '\n')
    with open(os.path.join(OUT, 'ImmersiveAzeroth.xml'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n')
        f.write('<Definition xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">\n')
        f.write('\n'.join(table_xml(name, fields) for name, fields in tables) + '\n')
        f.write('</Definition>\n')
    print('%d tables -> %s' % (len(tables), OUT))


if __name__ == '__main__':
    main()
