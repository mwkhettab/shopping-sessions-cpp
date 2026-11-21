"""Cross-check the overlapped output scheduler against the sequential stages."""
import argparse
from collections import Counter
from datetime import date, timedelta
from pathlib import Path
import subprocess
import tempfile

import pyarrow.parquet as pq


def table(folder):
    files = sorted(folder.glob('*.parquet'))
    assert files, f'missing table {folder}'
    batches = [pq.read_table(f) for f in files]
    assert len({str(b.schema) for b in batches}) == 1
    rows = Counter()
    for b in batches:
        rows.update(tuple(row.values()) for row in b.to_pylist())
    return tuple(batches[0].column_names), rows


def main():
    p = argparse.ArgumentParser()
    p.add_argument('binary', type=Path)
    args = p.parse_args()
    day0 = date(1970, 1, 1) + timedelta(days=20000)
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        for shape in ('empty', 'filled'):
            result = {}
            for mode in ('serial', 'combined'):
                out = work / (shape+mode)
                subprocess.run([args.binary.resolve(), shape, mode, out], check=True)
                result[mode] = [table(out/'user_daily'), table(out/'campaign_daily')]
            assert result['serial'] == result['combined']
            (u_cols, users), (c_cols, campaigns) = result['combined']
            assert u_cols == ('user_id', 'day', 'sessions', 'events', 'revenue_cents', 'top_category')
            assert c_cols == ('campaign_id', 'channel', 'day', 'sessions', 'purchasing_sessions',
                              'revenue_cents', 'assisted_sessions')
            assert sum(users.values()) == (23860 if shape=='filled' else 0)
            assert sum(campaigns.values()) == (7 if shape=='filled' else 0)
            if shape=='filled':
                assert len(users) == sum(users.values())
                for i in range(11900):
                    for d, price, cat in ((0, 25 if not i%3 else 0, i%3), (1, 11 if i%2 else 0, (i+1)%3)):
                        want = ('seed-'+str(i), day0+timedelta(days=d), 1, 1, price,
                                'xyz'[cat] if price else None)
                        assert users[want] == 1
                expected=Counter()
                for row in campaigns:
                    cid,channel,day,sessions,purchases,revenue,assists=row
                    assert channel==('organic' if cid=='organic' else 'earned' if cid=='c_1' else 'paid')
                    expected.update(sessions=sessions,revenue=revenue,purchases=purchases,assists=assists)
                assert expected['sessions']==23860 and expected['purchases']==3967+5950+60
                assert expected['revenue']==3967*25+5950*11+60*10
                assert expected['assists']==23800
            print(shape, sum(users.values()), 'user rows,', sum(campaigns.values()), 'campaign rows matched')


if __name__ == '__main__':
    main()
