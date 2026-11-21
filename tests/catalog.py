"""Check distinct catalog spellings, sparse identifiers and long CSV records."""
import csv
import os
from collections import defaultdict
import subprocess
import sys
import tempfile
from datetime import date, datetime, timezone
from pathlib import Path

import polars as pl
import pyarrow.parquet as pq


def write_csv(path, columns, rows):
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(columns)
        writer.writerows(rows)


def check(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        root.chmod(0o755)
        for folder in ("events", "clicks", "links"):
            (root / folder).mkdir()
        (root / "manifest.json").write_text(
            '{"window_start":"1970-01-01T00:00:00Z","window_end":"1970-01-02T00:00:00Z"}'
        )
        keys = ["1", "01", "0", "00000000001", "987654321", "x", "1 ", "ünicode"]
        products = [("p" + key, "category" + key) for key in keys]
        campaigns = [("c_" + key, "channel" + key) for key in keys]
        write_csv(root / "products.csv", ["product_id", "category"], products)
        write_csv(root / "campaigns.csv", ["campaign_id", "channel"], campaigns)
        write_csv(root / "links/empty.csv", ["user_id_a", "user_id_b"], [])
        events, clicks, expected_users, expected_campaigns = [], [], [], []
        when = "1970-01-01T01:00:00Z"
        for i, ((product, category), (campaign, channel)) in enumerate(zip(products, campaigns)):
            user = f"u{i}"
            # This last record crosses multiple 512 KiB chunk boundaries.
            event_id = "e" + ("L" * 1_100_000 if i + 1 == len(keys) else str(i))
            events.append((event_id, user, when, when, "purchase", product, str(i + 1)))
            clicks.append((f"k{i}", user, campaign, when))
            expected_users.append((user, date(1970, 1, 1), 1, 1, i + 1, category))
            expected_campaigns.append((campaign, channel, date(1970, 1, 1), 1, 1, i + 1, 1))
        write_csv(root / "events/part.csv",
                  ["event_id", "user_id", "ts", "ingest_ts", "event_type", "product_id", "price_cents"], events)
        write_csv(root / "clicks/part.csv", ["click_id", "user_id", "campaign_id", "ts"], clicks)
        output = root / "output"
        subprocess.run([binary, root, output], check=True)
        for name, columns, expected in [
            ("user_daily", ["user_id", "day", "sessions", "events", "revenue_cents", "top_category"], expected_users),
            ("campaign_daily", ["campaign_id", "channel", "day", "sessions", "purchasing_sessions",
                                "revenue_cents", "assisted_sessions"], expected_campaigns),
        ]:
            actual = pl.read_parquet(output / name / "*.parquet", columns=columns).rows()
            if sorted(expected) != sorted(actual):
                raise AssertionError((name, expected, actual))


def check_timeline_layouts(binary):
    # Exercise both time widths and both category widths with identical facts.
    # The only purchased known product is the last entry in each catalog.
    for category_count, end in [(1, "1970-01-10"), (260, "1970-01-10"), (1, "1972-03-01")]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            root.chmod(0o755)
            for folder in ("events", "clicks", "links"):
                (root / folder).mkdir()
            (root / "manifest.json").write_text(
                '{"window_start":"1969-12-31T00:00:00Z","window_end":"' + end + 'T00:00:00Z"}'
            )
            products = [(f"p{i}", f"cat{i}") for i in range(category_count)]
            product, category = products[-1]
            write_csv(root / "products.csv", ["product_id", "category"], products)
            write_csv(root / "campaigns.csv", ["campaign_id", "channel"],
                      [("a", "alpha"), ("b", "beta"), ("c", "gamma")])
            write_csv(root / "links/left.csv", ["user_id_a", "user_id_b"],
                      [("uA", "ü"), ("uA", "uA"), ("ü", "uA")])
            write_csv(root / "links/right.csv", ["user_id_a", "user_id_b"], [(" ghost", "ü")])
            first = "1969-12-31T23:50:00Z"
            second = "1970-01-01T00:10:00Z"
            third = "1970-01-01T00:40:00Z"
            early, late = "1850-01-01T00:00:00Z", "2200-01-01T00:00:00Z"
            columns = ["event_id", "user_id", "ts", "ingest_ts", "event_type", "product_id", "price_cents"]
            events = [(f"v{i}", "uA", first, first, "view", product, "") for i in range(40)]
            events += [("original", "ü", second, early, "purchase", product, "10000000"),
                       ("unknown", "uA", third, third, "purchase", "absent", "10000000"),
                       ("drop", "ü", first, early, "bad", product, ""),
                       ("epoch", "uA", "4200001", third, "cart", product, "")]
            write_csv(root / "events/a.csv", columns, events)
            revisions = [("original", "unlinked", second, late, "purchase", product, "1")]
            revisions.extend([("drop", "outsider", first, first, "view", product, "")] * 23)
            write_csv(root / "events/b.csv", columns, revisions)
            long_click = "!" + "é" * 24
            clicks = [(long_click, "ü", "a", first), (long_click, "ü", "a", first),
                      ("k12345678", "uA", "b", first), ("é", "ü", "c", "1969-12-24T23:50:00Z")]
            write_csv(root / "clicks/part.csv", ["click_id", "user_id", "campaign_id", "ts"], clicks)
            output = root / "out"
            subprocess.run([binary, root, output], check=True)
            previous, following = date(1969, 12, 31), date(1970, 1, 1)
            user_columns = ["user_id", "day", "sessions", "events", "revenue_cents", "top_category"]
            users = sorted(pl.read_parquet(output / "user_daily/*.parquet", columns=user_columns).rows())
            assert users == [(" ghost", previous, 1, 42, 20000000, category),
                             (" ghost", following, 1, 1, 0, None)], users
            campaign_columns = ["campaign_id", "channel", "day", "sessions", "purchasing_sessions",
                                "revenue_cents", "assisted_sessions"]
            campaigns = sorted(pl.read_parquet(output / "campaign_daily/*.parquet", columns=campaign_columns).rows())
            assert campaigns == [("a", "alpha", previous, 1, 1, 20000000, 1),
                                 ("a", "alpha", following, 1, 0, 0, 1),
                                 ("b", "beta", previous, 0, 0, 0, 1),
                                 ("b", "beta", following, 0, 0, 0, 1),
                                 ("c", "gamma", previous, 0, 0, 0, 1)], campaigns


def check_link_chains(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        root.chmod(0o755)
        for folder in ("events", "clicks", "links"):
            (root / folder).mkdir()
        (root / "manifest.json").write_text(
            '{"window_start":"2001-01-01T00:00:00Z","window_end":"2001-01-02T00:00:00Z"}'
        )
        write_csv(root / "products.csv", ["product_id", "category"], [("p", "x")])
        write_csv(root / "campaigns.csv", ["campaign_id", "channel"], [])
        edges, events = [[] for _ in range(16)], []
        for group in range(4):
            aliases = [f"u{group}-{i:05d}" for i in range(1601)] + [f"!ghost{group}"]
            for i, pair in enumerate(zip(aliases, aliases[1:])):
                edges[(i * 7 + group) % len(edges)].append(pair if i % 2 else pair[::-1])
            edges[group].extend([(aliases[0], aliases[0]), (aliases[1300], aliases[23])])
            for i, when in [(0, "2001-01-01T00:00:00Z"), (1400, "2001-01-01T00:30:00Z")]:
                events.append((f"{group}-{i}", aliases[i], when, when, "purchase", "p", "1"))
        for i, part in enumerate(edges):
            write_csv(root / f"links/{i}.csv", ["user_id_a", "user_id_b"], part)
        write_csv(root / "events/part.csv",
                  ["event_id", "user_id", "ts", "ingest_ts", "event_type", "product_id", "price_cents"], events)
        for workers in ("1", "8"):
            output = root / ("out" + workers)
            subprocess.run([binary, root, output], env=dict(os.environ, NCPU=workers, CPUS=workers), check=True)
            columns = ["user_id", "day", "sessions", "events", "revenue_cents", "top_category"]
            actual = sorted(pl.read_parquet(output / "user_daily/*.parquet", columns=columns).rows())
            assert actual == [(f"!ghost{i}", date(2001, 1, 1), 1, 2, 2, "x") for i in range(4)], actual


def check_numeric_pages(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        root.chmod(0o755)
        for folder in ("events", "clicks", "links"):
            (root / folder).mkdir()
        (root / "manifest.json").write_text(
            '{"window_start":"2000-02-29T00:00:00Z","window_end":"2000-03-01T00:00:00Z"}'
        )
        write_csv(root / "products.csv", ["product_id", "category"], [("p", "food")])
        write_csv(root / "campaigns.csv", ["campaign_id", "channel"], [])
        rows, expected = [], {}
        when = "2000-02-29T12:00:00Z"
        for i in range(263):
            user = f"user{i}"
            count = (0, 1, 217)[i % 3]
            expected[user] = (max(1, count), count * 10000000, "food" if count else None)
            for j in range(max(1, count)):
                rows.append((f"e{i}-{j}", user, when, when, "purchase" if count else "cart", "p", "10000000"))
        write_csv(root / "events/data.csv",
                  ["event_id", "user_id", "ts", "ingest_ts", "event_type", "product_id", "price_cents"], rows)
        output = root / "out"
        # One file includes multiple integer miniblocks, null definitions, and
        # signed deltas whose magnitude exceeds INT32_MAX.
        subprocess.run([binary, root, output], env=dict(os.environ, NCPU="1", CPUS="1"), check=True)
        users = [r for f in (output / "user_daily").glob("*.parquet") for r in pq.read_table(f).to_pylist()]
        assert len(users) == len(expected)
        assert {r["user_id"]: (r["events"], r["revenue_cents"], r["top_category"]) for r in users} == expected
        assert all(r["day"] == date(2000, 2, 29) and r["sessions"] == 1 for r in users)
        campaigns = [r for f in (output / "campaign_daily").glob("*.parquet") for r in pq.read_table(f).to_pylist()]
        assert campaigns == [{"campaign_id": "organic", "channel": "organic", "day": date(2000, 2, 29),
                              "sessions": len(expected), "purchasing_sessions": sum(v[1] > 0 for v in expected.values()),
                              "revenue_cents": sum(v[1] for v in expected.values()), "assisted_sessions": 0}]


def check_dense_attribution(binary):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        root.chmod(0o755)
        for folder in ("events", "clicks", "links"):
            (root / folder).mkdir()
        (root / "manifest.json").write_text(
            '{"window_start":"2023-03-01T00:00:00Z","window_end":"2023-03-20T00:00:00Z"}'
        )
        write_csv(root / "products.csv", ["product_id", "category"], [("p", "x")])
        write_csv(root / "campaigns.csv", ["campaign_id", "channel"], [(c, c) for c in "abcd"])
        write_csv(root / "links/two.csv", ["user_id_a", "user_id_b"], [("u1", "ü")])
        hour = 3600000
        day = 24 * hour
        start = int(datetime(2023, 3, 1, 12, tzinfo=timezone.utc).timestamp()) * 1000
        sessions = [start, start + hour, start + 7*day, start + 8*day, start + 16*day]
        events = [(f"e{i}", "u1", ts, datetime.fromtimestamp(ts / 1000, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                   "view", "p", "") for i, ts in enumerate(sessions)]
        write_csv(root / "events/a.csv",
                  ["event_id", "user_id", "ts", "ingest_ts", "event_type", "product_id", "price_cents"], events)
        clicks = [(f"k{i:08}", "ü", "a", start) for i in range(192)]
        clicks += [("!tie", "u1", "b", start), ("é", "ü", "c", start - 7*day),
                   ("after", "u1", "d", start + hour + 1), ("last", "ü", "c", start + 8*day)]
        write_csv(root / "clicks/a.csv", ["click_id", "user_id", "campaign_id", "ts"], reversed(clicks))
        expected = defaultdict(lambda: [0, 0])
        for ts in sessions:
            session_day = datetime.fromtimestamp(ts / 1000, timezone.utc).date()
            eligible = [c for c in clicks if ts - 7*day <= c[3] <= ts]
            campaign = min(eligible, key=lambda c: (-c[3], c[0].encode()))[2] if eligible else "organic"
            expected[campaign, session_day][0] += 1
            for assisting in {c[2] for c in eligible}:
                expected[assisting, session_day][1] += 1
        output = root / "out"
        subprocess.run([binary, root, output], check=True)
        columns = ["campaign_id", "day", "sessions", "assisted_sessions"]
        actual = sorted(pl.read_parquet(output / "campaign_daily/*.parquet", columns=columns).rows())
        assert actual == sorted((*key, *values) for key, values in expected.items()), actual


if __name__ == "__main__":
    executable = Path(sys.argv[1]).resolve()
    check(executable)
    check_timeline_layouts(executable)
    check_numeric_pages(executable)
    check_link_chains(executable)
    check_dense_attribution(executable)
