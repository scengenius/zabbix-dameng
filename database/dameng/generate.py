#!/usr/bin/env python3
"""Generate DM8 initialization and verification SQL from Zabbix 8.0.0."""

import argparse
import collections
import hashlib
from pathlib import Path
import re


IDENTIFIER = re.compile(r"[a-z_][a-z0-9_]*\Z", re.ASCII)
DDL_TOKEN = re.compile(r"'(?:''|[^'])*'|\bvarchar\(\d+\)|\b(?:bytea|bigserial|object|model)\b", re.I)
FUNCTION = re.compile(
    r"create or replace function (\w+)\(\) returns trigger as \$\$\s*"
    r"begin\s*(.*?)\s*return (?:new|old);\s*end;\s*\$\$ language plpgsql;",
    re.S,
)
TRIGGER = re.compile(
    r"^create trigger (\w+)\s+(before|after)\s+(insert|update|delete)\s+"
    r"(?:of (\w+)\s+)?on\s+(\w+)\s+for each row\s+"
    r"execute (?:procedure|function) (\w+)\(\);",
    re.M,
)
EPOCH_PG = "cast(extract(epoch from now()) as int)"
EPOCH_DM = "DATEDIFF(SECOND,TIMESTAMP '1970-01-01 00:00:00',SYS_EXTRACT_UTC(SYSTIMESTAMP))"

INSERT = re.compile(r"INSERT INTO (\w+) \(([^)]+)\) VALUES\s*")
VALUE = re.compile(r"E'(?:''|\\.|[^'\\])*'|'(?:''|[^'])*'|NULL|-?[0-9]+(?:\.[0-9]+)?", re.S)
SEPARATOR = re.compile(r"\s*([(),;])\s*")
ESCAPES = {"\\": "\\", "'": "'", "n": "\n", "r": "\r", "t": "\t", "b": "\b", "f": "\f"}
CLIENT_SENSITIVE = re.compile(r"[\r\n\t\\]")
IMAGE = re.compile(
    r"INSERT INTO images \(imageid,imagetype,name,image\) VALUES "
    r"\((\d+),(\d+),'((?:''|[^'])*)',decode\('([0-9a-f]+)','hex'\)\);",
    re.I,
)
BATCH_BYTES = 16_000
SQL_HEADER = """whenever sqlerror exit 1 rollback
set define off
set char_code UTF8
set timing off
set echo off
"""
SQL_HASHES = {
    "schema.sql": "838111cf9287d8705058aafb43378817f5305a066cfa5d64d36a6f85d86def53",
    "data.sql": "100520b79f407a6e09dcea25938cbc78234e0199ea5697868be83e2a7b4b4708",
    "images.sql": "c1cb60c54775f74ec169910c65eddeec586c2f21a52e7df09cee6eff187ea9bc",
}


def identifier(name):
    if not IDENTIFIER.fullmatch(name):
        raise ValueError(f"Unexpected identifier {name!r}")
    if name in {"object", "model"}:
        return f'"{name.upper()}"'
    return name


def ddl_sql(sql, char_varchar=False):
    types = {"bytea": "blob", "bigserial": "bigint identity(1,1)"}

    def replace(match):
        token = match[0].lower()
        if token.startswith("'"):
            return match[0]
        if token.startswith("varchar"):
            return token[:-1] + " char)" if char_varchar else token
        return types.get(token, identifier(token))

    return DDL_TOKEN.sub(replace, sql)


def schema_statements(source, char_varchar):
    trigger_start = source.index("create or replace function ")
    fk_start = source.index("ALTER TABLE ONLY ")
    base = source[:trigger_start]
    triggers = source[trigger_start:fk_start]
    foreign_keys = source[fk_start:].replace("ALTER TABLE ONLY ", "ALTER TABLE ")
    tables = [ddl_sql(sql.strip(), char_varchar) for sql in base.split(";") if sql.strip()]
    fks = [ddl_sql(sql.strip()) for sql in foreign_keys.split(";") if sql.strip()]
    if sum(sql.startswith("CREATE TABLE ") for sql in tables) != 230 or len(fks) != 299:
        raise ValueError("Expected exactly 230 tables and 299 foreign keys from Zabbix 8.0.0")
    return tables, fks, trigger_statements(triggers)


def trigger_statements(source):
    bodies = dict(FUNCTION.findall(source))
    triggers = []
    for name, timing, operation, column, table, func in TRIGGER.findall(source):
        if func == "hosts_name_upper_upper":
            # Update the pending row. Updating hosts from its own row trigger
            # would re-enter the changelog trigger or hit the mutating-table rule.
            timing = "before"
            body = ":new.name_upper := upper(:new.name);"
        else:
            body = ddl_sql(bodies[func]).replace(EPOCH_PG, EPOCH_DM)
            body = re.sub(r"\b(old|new)\.", r":\1.", body)
        event = f"{operation} of {column}" if column else operation
        triggers.append(f"create trigger {name} {timing} {event} on {table}\n"
                        f"for each row\nbegin\n{body}\nend;")
    if len(triggers) != 91 or len(bodies) != 89 or "create trigger" in TRIGGER.sub("", source):
        raise ValueError("Expected exactly 91 triggers and 90 functions from Zabbix 8.0.0")
    return triggers


def literal_text(token):
    """Return the text used by PostgreSQL for a string literal."""
    if token.startswith("'"):
        return token[1:-1].replace("''", "'")

    def decode(match):
        return "'" if match[0] == "''" else ESCAPES[match[0][1]]

    return re.sub(r"''|\\.", decode, token[2:-1], flags=re.S)


def seed_rows(source):
    """Read the fixed release data syntax without normalizing literal bytes."""
    prefix = re.compile(r"START TRANSACTION;\s+")
    suffix = "DELETE FROM changelog;\nCOMMIT;"
    source = source.strip() + "\n"
    prologue = prefix.match(source)
    if prologue is None:
        raise ValueError("Unexpected seed transaction prologue")
    position = prologue.end()
    while not source.startswith(suffix, position):
        match = INSERT.match(source, position)
        if match is None:
            raise ValueError(f"Unexpected seed statement at character {position}")
        table = identifier(match[1])
        columns = tuple(identifier(column) for column in match[2].split(","))
        position = match.end()
        next_row = True
        while next_row:
            opening = SEPARATOR.match(source, position)
            if opening is None or opening[1] != "(":
                raise ValueError(f"Expected a row for {table} at character {position}")
            position = opening.end()
            tokens = []
            for index in range(len(columns)):
                value = VALUE.match(source, position)
                if value is None:
                    raise ValueError(f"Unexpected value for {table} at character {position}")
                tokens.append(value[0])
                delimiter = SEPARATOR.match(source, value.end())
                expected = ")" if index + 1 == len(columns) else ","
                if delimiter is None or delimiter[1] != expected:
                    raise ValueError(f"Unexpected delimiter for {table} at character {value.end()}")
                position = delimiter.end()
            yield table, columns, tokens

            separator = SEPARATOR.match(source, position)
            if separator is None or separator[1] not in {",", ";"}:
                raise ValueError(f"Unexpected row separator for {table} at character {position}")
            next_row = separator[1] == ","
            position = separator.end()

    if source[position:].strip() != suffix:
        raise ValueError("Unexpected seed transaction epilogue")


def image_statements(source):
    for number, line in enumerate(source.splitlines(), 1):
        match = IMAGE.fullmatch(line)
        if not match:
            raise ValueError(f"Unexpected image row {number}")
        yield int(match[1]), int(match[2]), match[3].replace("''", "'"), bytes.fromhex(match[4])


def checked_source(directory, filename):
    content = (directory / filename).read_bytes()
    if hashlib.sha256(content).hexdigest() != SQL_HASHES[filename]:
        raise ValueError(f"{filename} does not match the official Zabbix 8.0.0 release")
    return content.decode("utf-8")


def write_sql(path, statements, delimiter=";\n"):
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(SQL_HEADER)
        output.write(delimiter.join(statements) + delimiter)
        output.write("commit;\nexit;\n")


def write_seed(path, source, images, primary_keys):
    counts = collections.Counter()
    text_checks = []
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(SQL_HEADER + "set autocommit off\n")
        for imageid, imagetype, name, content in images:
            quoted_name = "'" + name.replace("'", "''") + "'"
            output.write("INSERT INTO images (imageid,imagetype,name,image) VALUES "
                         f"({imageid},{imagetype},{quoted_name},0x{content.hex()});\n")

        insert = ""
        batch = []
        batch_bytes = 0
        for table, columns, tokens in seed_rows(source):
            values = tokens.copy()
            for index, token in enumerate(tokens):
                if not token.startswith("E'") and not CLIENT_SENSITIVE.search(token):
                    continue
                text = literal_text(token).encode("utf-8")
                values[index] = f"utl_raw.cast_to_varchar2(0x{text.hex()})"
                keys = primary_keys[table]
                predicate = " AND ".join(
                    f"{key}={tokens[columns.index(key)]}" for key in keys
                )
                digest = hashlib.sha256(text).hexdigest().upper()
                text_checks.append((
                    f"SELECT COUNT(*) FROM {table} WHERE {predicate} AND "
                    f"RAWTOHEX(DBMS_CRYPTO.HASH(CAST({columns[index]} AS CLOB),4))='{digest}'",
                    f"{table}.{columns[index]} row {counts[table] + 1}",
                    1,
                ))
            prefix = f"INSERT INTO {table} ({','.join(columns)}) VALUES "
            row = "(" + ",".join(values) + ")"
            row_bytes = len(row.encode("utf-8")) + 1
            if batch and (prefix != insert or batch_bytes + row_bytes > BATCH_BYTES):
                output.write(insert + ",".join(batch) + ";\n")
                batch.clear()
                batch_bytes = 0
            if not batch:
                insert = prefix
                batch_bytes = len(insert)
            batch.append(row)
            batch_bytes += row_bytes
            counts[table] += 1
        output.write(insert + ",".join(batch) + ";\n")
        # This is the only persistent effect of the upstream trigger set during seed.
        # The release deletes its transient changelog before COMMIT.
        output.write("UPDATE hosts SET name_upper=UPPER(name);\ncommit;\nexit;\n")
    return counts, text_checks


def write_checks(path, checks, summary):
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(SQL_HEADER)
        # Bound anonymous block sizes so DIsql handles each one consistently.
        for start in range(0, len(checks), 40):
            output.write("declare\n  n bigint;\nbegin\n")
            for sql, label, expected in checks[start:start + 40]:
                query = sql.replace(" FROM ", " INTO n FROM ", 1)
                output.write(f"  {query};\n"
                             f"  if n is null or n<>{expected} then\n"
                             f"    raise_application_error(-20001,'{label}: expected {expected}, got '||n);\n"
                             "  end if;\n")
            output.write("end;\n/\n")
        output.write(f"prompt {summary}\nexit;\n")


def generate(pg_dir, output, char_varchar):
    schema = checked_source(pg_dir, "schema.sql")
    data = checked_source(pg_dir, "data.sql")
    images = list(image_statements(checked_source(pg_dir, "images.sql")))
    if len(images) != 187:
        raise ValueError("Expected exactly 187 images from Zabbix 8.0.0")
    tables, fks, triggers = schema_statements(schema, char_varchar)
    primary_keys = {
        match[1]: tuple(identifier(column) for column in match[2].split(","))
        for match in re.finditer(
            r"^CREATE TABLE (\w+) \(\n(?:(?!^\);).)*?\bPRIMARY KEY \(([^)]+)\)", schema, re.M | re.S
        )
    }

    output.mkdir(parents=True, exist_ok=True)
    write_checks(
        output / "preflight.sql",
        [
            ("SELECT COUNT(*) FROM user_tables", "Fresh schema table count", 0),
            ("SELECT SF_GET_UNICODE_FLAG() FROM dual", "Database UTF-8 flag", 1),
            ("SELECT LENGTHB('甲a') FROM dual", "DIsql client UTF-8 bytes", 4),
        ],
        "Verified empty schema and UTF-8 client encoding.",
    )
    write_sql(output / "ddl.sql", tables)
    counts, text_checks = write_seed(output / "seed.sql", data, images, primary_keys)
    if sum(counts.values()) != 255064:
        raise ValueError("Unexpected number of seed rows")
    write_sql(output / "fks.sql", fks)
    write_sql(output / "triggers.sql", triggers, delimiter="\n/\n")
    checks = [
        ("SELECT COUNT(*) FROM user_tables", "Tables", 230),
        ("SELECT COUNT(*) FROM user_constraints WHERE constraint_type='R'", "Foreign keys", 299),
        ("SELECT COUNT(*) FROM user_indexes WHERE index_name NOT LIKE 'INDEX%'", "Release indexes", 338),
        ("SELECT COUNT(*) FROM user_triggers WHERE status='Y'", "Enabled triggers", 91),
        ("SELECT COUNT(*) FROM user_objects WHERE object_type='TRIGGER' AND status='VALID'", "Valid triggers", 91),
        ("SELECT COUNT(*) FROM dbversion WHERE mandatory=8000000 AND optional=8000000", "DB version", 1),
        ("SELECT COUNT(*) FROM changelog", "Seed changelog", 0),
        ("SELECT COUNT(*) FROM hosts WHERE name_upper<>UPPER(name)", "Host uppercase search", 0),
        ("SELECT COUNT(*) FROM hosts WHERE status=3", "Templates", 390),
        ("SELECT COUNT(*) FROM images", "Images", 187),
    ]
    checks.extend((f"SELECT COUNT(*) FROM {table}", f"Seed {table}", count) for table, count in counts.items())
    for imageid, _, _, content in images:
        digest = hashlib.sha256(content).hexdigest().upper()
        checks.append((
            f"SELECT COUNT(*) FROM images WHERE imageid={imageid} AND "
            f"RAWTOHEX(DBMS_CRYPTO.HASH(image,4))='{digest}'",
            f"Image {imageid} SHA-256",
            1,
        ))
    checks.extend(text_checks)
    summary = (f"Verified 230 tables, 338 release indexes, 299 foreign keys, 91 enabled and valid triggers, "
               f"{sum(counts.values())} seed rows in {len(counts)} tables, "
               f"all {len(images)} image SHA-256 hashes and {len(text_checks)} seed text SHA-256 hashes.")
    write_checks(output / "verify.sql", checks, summary)
    print(f"Generated DM8 SQL in {output}")
    print(f"VARCHAR limits use {'characters' if char_varchar else 'bytes'}")
    print(f"Seed plan: {sum(counts.values())} rows in {len(counts)} tables, "
          f"{len(images)} images, {len(text_checks)} text byte checks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zabbix_source", type=Path, help="Zabbix 8.0.0 source directory with generated PostgreSQL SQL")
    parser.add_argument("output", type=Path, help="directory for generated SQL")
    parser.add_argument(
        "--byte-varchar", action="store_true",
        help="reproduce the byte-based lengths used by the first integration schema",
    )
    args = parser.parse_args()
    generate(args.zabbix_source / "database" / "postgresql", args.output, not args.byte_varchar)


if __name__ == "__main__":
    main()
