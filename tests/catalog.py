"""Check distinct catalog spellings, sparse identifiers and long CSV records."""
import csv
import subprocess
import sys
import tempfile
from datetime import date
from pathlib import Path

import polars as pl


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
            write_csv(root / "events/b.csv", columns,
                      [("original", "unlinked", second, late, "purchase", product, "1"),
                       ("drop", "outsider", first, first, "view", product, "")])
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


if __name__ == "__main__":
    executable = Path(sys.argv[1]).resolve()
    check(executable)
    check_timeline_layouts(executable)
