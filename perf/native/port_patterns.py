"""Exercise rule fixtures and partition seams using independently salted event IDs."""
import argparse
import collections
import datetime
import json
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile

from ordinal_fixtures import enrich, tables
from ordinal_rules import populate, spelling

DAY = 86400000
START = 1803772800000  # 2027-02-28 UTC


def f1(t):
    return datetime.datetime.fromtimestamp(t // 1000, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def write_parts(folder, header, rows, rng):
    folder.mkdir()
    rng.shuffle(rows)
    for n in range(5):
        tmp = [header]
        for row in rows[n::5]:
            tmp.append(",".join('"' + str(x) + '"' if rng.randrange(4) == 0 else str(x) for x in row))
        (folder / f"part-{n}.csv").write_text("\n".join(tmp) + ("\n" if n % 2 else ""))


def seams(data, variant):
    rng = random.Random(910247)
    us, es = rng.randrange(1 << 40), rng.randrange(1 << 40)
    user = lambda n: spelling("u", n, us)
    event = lambda n: spelling("e", n, es)
    opaque = "\tshared alias"
    text = variant == "noncanonical catalog"
    p1, p2, p3 = ("p01", "opaque product", "p3") if text else ("p1", "p2", "p3")
    c1, c2, c3, c4 = ("c_01" if text else "c_1"), "c_2", "c_3", "c_4"
    (data / "manifest.json").write_text(json.dumps({"window_start": f1(START), "window_end": f1(START + 9*DAY)}))
    (data / "products.csv").write_text("product_id,category\n" + f"{p1}, A\n{p2},Å\n{p3},views\n")
    (data / "campaigns.csv").write_text("campaign_id,channel\n" + f"{c1},search\n{c2},email\n{c3},social\n{c4},referral\n")
    first, second = START+DAY-1000, START+DAY+3601001
    events, clicks, links = [], [], []
    use_alias = variant != "regular IDs"
    if use_alias:
        links += [[user(0), opaque], [opaque, user(1)], [opaque, user(0)]]
    for i in range(16720):
        t = first if i < 16384 else START+DAY+1000 if i < 16510 else START+DAY+1801000 if i < 16580 else second
        who = user(1 if use_alias and i % 3 == 0 else 0)
        kind, prod, price = ("purchase", p1, "100") if i == 5 else ("purchase", p2, "00100") if i == 16384 else ("purchase", p2, "99") if i == 16622 else ("view", p3, "")
        row = [event(i), who, str(t), f1(t), kind, prod, price]
        events.append(row)
        if i == 7:
            events.append([event(i), user(2*512), "invalid", f1(START), "purchase", p1, "10000", "extra"])
        if i == 8:
            events.append([event(i), user(2*512), "invalid", f1(START-1000), "purchase", p1, "10000"])
        if i == 12:
            events.append([event(i), user(3*512), str(t), f1(t+10000), "purchase", p2, "85000"])
    # The 16,384th event crosses the revision bucket boundary. The high-volume
    # user's events stay in one or two sessions across midnight.
    for g in range(1, 90):
        for j in range(57):
            t = START+DAY+(g*61+j)*1000
            events.append([event(16720+(g-1)*57+j), user(g*512), str(t), f1(t), "view", p3, ""])
    click_user = opaque if use_alias else user(0)
    clicks = [
        ["k10", user(0), c1, str(first)], ["k2", click_user, c2, str(first)],
        ["k10", user(0), c1, f1(first)], ["\tcredit", click_user, c3, str(second)],
        ["k0", user(0), c2, str(second)], ["week", user(0), c3, str(first-7*DAY)],
        ["too early", click_user, c4, str(first-7*DAY-1)], ["too late", click_user, c4, str(second+1)],
    ]
    write_parts(data/"events", "event_id,user_id,ts,ingest_ts,event_type,product_id,price_cents", events, rng)
    write_parts(data/"clicks", "click_id,user_id,campaign_id,ts", clicks, rng)
    write_parts(data/"links", "user_id_a,user_id_b", links, rng)
    return opaque if use_alias else user(0), c1, c2, c3, c4


def known_boundary(rows, dim):
    name, c1, c2, c3, c4 = dim
    d0 = datetime.datetime.fromtimestamp(START//1000, datetime.timezone.utc).date()
    d1 = d0+datetime.timedelta(days=1)
    users = sorted(k for k in rows[0] if k[0] == name)
    expected = [(name, d0, 1, 16579, 200, " A"), (name, d1, 1, 140, 99, "Å")]
    if users != expected:
        raise AssertionError(("boundary person", users, expected))
    actual = {(r[0], r[2]): r[3:] for r in rows[1] if r[0] in (c1,c2,c3,c4)}
    expected = {(c1,d0):(1,1,200,1), (c2,d0):(0,0,0,1), (c3,d0):(0,0,0,1),
                (c1,d1):(0,0,0,1), (c2,d1):(0,0,0,1), (c3,d1):(1,1,99,1)}
    if actual != expected:
        raise AssertionError(("campaign click window or lexical tie", actual, expected))


def validated(table_rows):
    for i, rows in enumerate(table_rows):
        idx_day = i + 1
        if len({(r[0],r[idx_day]) for r in rows}) != len(rows):
            raise AssertionError("duplicate day/entity key")
    return table_rows


def audit(stderr):
    fallbacks = sorted(set(re.findall(r"\[pipeline\] fallback: ([^\n]+)", stderr)))
    if "[sharded] completed" in stderr:
        return {"path":"sharded","reasons":fallbacks}
    if fallbacks:
        if "[v1nd]" not in stderr:
            raise AssertionError("fallback was logged without a completed generic run: " + stderr)
        return {"path": "generic", "reasons": fallbacks}
    count = re.search(r"\[pipeline\] events=(\d+) clicks=(\d+) links=(\d+) simple=(\d+) complex=(\d+) odd=(\d+) runs=(\d+)", stderr)
    if not count or "[pipeline] exit " not in stderr:
        raise AssertionError("missing release path diagnostics: " + stderr)
    return {"path": "direct", **dict(zip(("events","clicks","links","simple","complex","odd","runs"), map(int,count.groups())))}


def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument("candidate", type=Path)
    a.add_argument("oracle", type=Path)
    a.add_argument("fixtures", type=Path)
    a.add_argument("--log", type=Path, required=True)
    args = a.parse_args()
    candidate, oracle = args.candidate.resolve(), args.oracle.resolve()
    records = []
    env = dict(os.environ, POLARS_MAX_THREADS="2", ARROW_NUM_THREADS="2")
    with args.log.open("x") as log:
        def test(data, name, workers, known=None):
            with tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                c = subprocess.run([oracle,data,base/"expected"], check=True, env=env|{"NCPU":"3","SHOP_GENERAL_READER":"1"},
                                   capture_output=True, timeout=60)
                reference = validated(tables(base/"expected"))
                if known:
                    known_boundary(reference, known)
                for w in workers:
                    out = base/("output-"+str(w))
                    s = subprocess.run([candidate,data,out],capture_output=True,text=True,timeout=90,
                                       env=env|{"NCPU":str(w),"XL_TIMING":"1","V1ND_LOG":"1"})
                    if s.returncode:
                        raise RuntimeError(f"{name}/{w} exited {s.returncode}: {s.stderr[-3000:]}")
                    actual = validated(tables(out))
                    seen = audit(s.stderr)
                    rec = {"case":name,"threads":w,**seen,"rows":list(map(len,actual)),"exact":actual==reference}
                    print(json.dumps(rec,ensure_ascii=False),file=log,flush=True)
                    if not rec["exact"]:
                        raise AssertionError((name,w,seen,[(list(set(x)-set(y))[:4],list(set(y)-set(x))[:4])
                                                            for x,y in zip(actual,reference)]))
                    if known:
                        known_boundary(actual,known)
                    records.append(rec)
        fixtures = sorted(args.fixtures.glob("*/input"))
        assert len(fixtures) == 36
        for seed,fixture in enumerate(fixtures,19):
            with tempfile.TemporaryDirectory() as tmp:
                data = Path(tmp)
                enrich(fixture,data,seed)
                test(data,"fixture-"+fixture.parent.name,(4,))
        for seed in (7933,27011,401273):
            with tempfile.TemporaryDirectory() as tmp:
                data = Path(tmp)
                populate(data,seed)
                test(data,"shuffled historical revisions "+str(seed),(1,8))
                for f in (data/"events").glob("*.csv"):
                    content = f.read_bytes().replace(b"1903-01-01T00:00:00Z",f1(START-60000).encode())
                    f.write_bytes(content.replace(b"1901-01-01T00:00:00Z",f1(START-120000).encode()))
                test(data,"shuffled bounded revisions "+str(seed),(1,8))
        for variant in ("regular IDs", "linked opaque aliases", "noncanonical catalog"):
            with tempfile.TemporaryDirectory() as tmp:
                data = Path(tmp)
                dim = seams(data,variant)
                test(data,"bucket seam "+variant,(1,8),dim)
    paths = collections.Counter(r["path"] for r in records)
    if not any(r["path"] in ("direct","sharded") and r["case"].startswith("bucket seam ") for r in records):
        raise AssertionError("the direct decoder did not run the bucket-boundary assertions")
    print(f"{len(records)} exact comparisons. Direct {paths['direct']}, generic {paths['generic']}, sharded {paths['sharded']}.",flush=True)
    for group in ("fixture-", "shuffled historical", "shuffled bounded", "bucket seam"):
        c = collections.Counter(r["path"] + ": " + ", ".join(r.get("reasons", []))
                                for r in records if r["case"].startswith(group))
        print(group,dict(c),flush=True)


if __name__ == "__main__":
    main()
