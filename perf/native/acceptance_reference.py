"""Fingerprint sorted logical Parquet rows independently of encodings and file boundaries."""
import argparse
import hashlib
import json
from pathlib import Path

import polars as pl

TABLES = {
    "user_daily": (("user_id", "day", "sessions", "events", "revenue_cents", "top_category"), {"user_id", "top_category"}, "user_id"),
    "campaign_daily": (("campaign_id", "channel", "day", "sessions", "purchasing_sessions", "revenue_cents", "assisted_sessions"),
                       {"campaign_id", "channel"}, "campaign_id"),
}


def fingerprint(folder):
    output = {}
    for table, (columns, text, entity) in TABLES.items():
        files = sorted((folder / table).glob("*.parquet"))
        if not files:
            raise ValueError(f"empty table directory {folder / table}")
        for file in files:
            schema = pl.read_parquet_schema(file)
            if set(schema) != set(columns) or schema["day"] != pl.Date:
                raise ValueError(f"{file}: invalid column names or date type")
            for c in columns:
                v = schema[c]
                if c in text and v.base_type() not in (pl.String, pl.Categorical, pl.Enum):
                    raise ValueError(f"{file}: {c} is {v}")
                if c not in text and c != "day" and not v.is_integer():
                    raise ValueError(f"{file}: {c} is {v}")
        scan = pl.scan_parquet(files)
        days = scan.select(pl.col("day").unique().sort()).collect()["day"].to_list()
        if any(d is None for d in days):
            raise ValueError(f"{table}: null day")
        digest = hashlib.sha256()
        rows = 0
        for day in days:
            chunk = scan.filter(pl.col("day") == day).select(columns).sort(entity).collect()
            if chunk[entity].null_count() or chunk[entity].n_unique() != chunk.height:
                raise ValueError(f"{table}: null or duplicate daily key on {day}")
            # String values start with V, null strings encode as N, and dates encode
            # as epoch days. All fields have CSV quoting to preserve arbitrary UTF-8.
            fields = []
            for c in columns:
                v = pl.col(c)
                if c in text:
                    v = pl.when(v.is_null()).then(pl.lit("N")).otherwise(pl.concat_str([pl.lit("V"), v.cast(pl.String)]))
                elif c == "day":
                    v = v.cast(pl.Int32)
                else:
                    v = v.cast(pl.Int128)
                fields.append(v.alias(c))
            canonical = chunk.select(fields).write_csv(include_header=False, quote_style="always", null_value="N", line_terminator="\n")
            digest.update(canonical.encode("utf-8"))
            rows += chunk.height
        output[table] = {"rows": rows, "sha256": digest.hexdigest()}
    return output


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("folder", type=Path)
    p.add_argument("--expected", type=Path)
    p.add_argument("--scale", choices=("20M", "60M"))
    args = p.parse_args()
    result = fingerprint(args.folder)
    print(json.dumps(result, sort_keys=True), flush=True)
    if args.expected and result != json.loads(args.expected.read_text())[args.scale]:
        raise SystemExit("the logical results differ from the independently validated tables")
