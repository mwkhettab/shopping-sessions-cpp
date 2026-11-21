"""Check that the suffix of a long user ID never becomes a separate identity link."""
from collections import Counter
from datetime import date
from pathlib import Path
from random import Random
import subprocess
import sys
import tempfile

import polars as pl

from check_keys import spelling


def check(binary):
    rng = Random(71)
    user_key, event_key = (rng.randrange(1 << 40) for _ in range(2))
    instant = "2028-02-29T00:00:00Z"
    day = date(2028, 2, 29)
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        root.chmod(0o755)
        for folder in ("events", "clicks", "links"):
            (root / folder).mkdir()
        (root / "manifest.json").write_text(
            '{"window_start":"2028-02-29T00:00:00Z","window_end":"2028-03-01T00:00:00Z"}'
        )
        (root / "products.csv").write_text("product_id,category\np1,A\n")
        (root / "campaigns.csv").write_text("campaign_id,channel\nc_0,test\n")
        (root / "clicks/part.csv").write_text("click_id,user_id,campaign_id,ts\n")
        (root / "links/part.csv").write_text("user_id_a,user_id_b\n" + "Q" * 33 + "Truncated,Merged\n")

        # Repeated and distinct owners exercise compact key recovery before the two purchases.
        anchors = Counter()
        with (root / "events/part.csv").open("w") as events:
            events.write("event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents\n")
            for i in range(72):
                owner = max(0, i - 31)
                user = "u" + spelling(owner * 512, user_key)
                anchors[user] += 1
                events.write(f"e{spelling(i, event_key)},{user},{instant},{instant},view,p1,\n")
            for i, (user, price) in enumerate((("Truncated", 11), ("Merged", 23)), 72):
                events.write(f"e{spelling(i, event_key)},{user},{instant},{instant},purchase,p1,{price}\n")

        output = root / "out"
        subprocess.run([binary, root, output], check=True)
        users = pl.read_parquet(output / "user_daily/*.parquet").select(
            "user_id", "day", "sessions", "events", "revenue_cents", "top_category"
        ).rows()
        expected = [(user, day, 1, count, 0, None) for user, count in anchors.items()]
        expected += [("Truncated", day, 1, 1, 11, "A"), ("Merged", day, 1, 1, 23, "A")]
        assert sorted(users) == sorted(expected), (set(expected) - set(users), set(users) - set(expected))
        campaigns = pl.read_parquet(output / "campaign_daily/*.parquet").select(
            "campaign_id", "channel", "day", "sessions", "purchasing_sessions",
            "revenue_cents", "assisted_sessions"
        ).rows()
        assert campaigns == [("organic", "organic", day, len(expected), 2, 34, 0)], campaigns


if __name__ == "__main__":
    check(Path(sys.argv[1]).resolve())
