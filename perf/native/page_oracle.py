"""Check complete multi-page outputs, including long UTF-8 values and nulls."""
import os
import subprocess
import sys
import tempfile
from datetime import date, timedelta
from pathlib import Path

import pyarrow.parquet as parquet

def check(binary):
    days = 17011
    start = date(1970, 1, 1)
    end = start + timedelta(days=days)
    long_category = '雪' * 600
    users = (' visitor α', 'visitor β')
    with tempfile.TemporaryDirectory() as temp:
        data = Path(temp)
        for name in ('events', 'clicks', 'links'):
            (data / name).mkdir()
        (data / 'manifest.json').write_text(
            '{"window_start":"1970-01-01T00:00:00Z","window_end":"' + end.isoformat() + 'T00:00:00Z"}')
        (data / 'products.csv').write_text('product_id,category\np,' + long_category + '\n')
        (data / 'campaigns.csv').write_text('campaign_id,channel\n')
        (data / 'links/a.csv').write_text('user_id_a,user_id_b\n')
        (data / 'clicks/a.csv').write_text('click_id,user_id,campaign_id,ts\n')
        with (data / 'events/a.csv').open('w') as events:
            print('event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents', file=events)
            for index in range(days):
                stamp = (start + timedelta(days=index)).isoformat() + 'T12:00:00Z'
                time = index * 86400000 + 43200000
                for owner, user in enumerate(users):
                    fields = 'purchase,p,7' if owner == 0 and index % 2 == 0 else 'view,p,'
                    print(f'e{index}x{owner},"{user}",{time},{stamp},{fields}', file=events)
        output = data / 'out'
        subprocess.run([binary, data, output], env=os.environ | {'NCPU': '1'}, check=True)
        seen = [bytearray(days), bytearray(days)]
        userrows = 0
        for file in (output / 'user_daily').glob('*.parquet'):
            for batch in parquet.ParquetFile(file).iter_batches(batch_size=1024):
                for row in batch.to_pylist():
                    owner = users.index(row['user_id'])
                    i = (row['day'] - start).days
                    assert 0 <= i < days and not seen[owner][i]
                    seen[owner][i] = 1
                    buy = owner == 0 and i % 2 == 0
                    assert (row['sessions'], row['events'], row['revenue_cents'], row['top_category']) == (
                        1, 1, 7 if buy else 0, long_category if buy else None)
                    userrows += 1
        assert userrows == 2 * days
        dates = bytearray(days)
        for file in (output / 'campaign_daily').glob('*.parquet'):
            for batch in parquet.ParquetFile(file).iter_batches(batch_size=1024):
                for row in batch.to_pylist():
                    i = (row['day'] - start).days
                    assert 0 <= i < days and not dates[i]
                    dates[i] = 1
                    buy = int(i % 2 == 0)
                    assert (row['campaign_id'], row['channel'], row['sessions'], row['purchasing_sessions'],
                            row['revenue_cents'], row['assisted_sessions']) == ('organic', 'organic', 2, buy, buy * 7, 0)
        assert sum(dates) == days
    print(f'Matched {2 * days:,} user-days and {days:,} campaign-days across Parquet pages.', flush=True)

if __name__ == '__main__':
    for item in sys.argv[1:]:
        check(str(Path(item).resolve()))
