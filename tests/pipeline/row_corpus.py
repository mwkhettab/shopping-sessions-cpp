"""Exercise parsed values, file-relative byte offsets and all 64-row flush boundaries."""
import argparse
from pathlib import Path
import random


def build(fixtures, output):
    rng = random.Random(3194811)
    times = [
        "1970-01-01T00:00:00Z", "0000-02-29T01:02:03Z", "2000-02-29T23:59:59-23:59",
        "1900-02-29T00:00:00Z", "9999-12-31T23:59:59+00:01", "2026-11-01T00:00:00-00:00",
        "2026-11-01T00:00:00+24:00", "2026-11-01T00:00:60Z", "", "-4", "٢٥٠", "0",
        "9223372036854775807", "9223372036854775808", "9"*51, "0"*65+"314", "9"*52+"x",
    ]
    ids = ["", "0", "1", "00", "abc123", "zzzzzzzz", "Z", "abc٩", " " , "012345", "x"*260]
    for name, count in (("events", 7), ("clicks", 4)):
        all_input = bytearray()
        for path in sorted(fixtures.glob("*/input/"+name+"/*.csv")):
            raw = path.read_bytes().partition(b"\n")[2]
            all_input.extend(raw)
            all_input.extend(b"\n")
        for j in range(5000):
            first = ("e" if name=="events" else "k")+rng.choice(ids)
            who, when = "u"+rng.choice(ids), rng.choice(times)
            if count==7:
                row = [first,who,when,rng.choice(times),rng.choice(["view","cart","purchase","Purchase",""]),
                       rng.choice(["p1","p0002","unknown","p99999999",""]),rng.choice(["1","000250","10000000","10000001","٠","800\r",""])]
            else:
                row = [first,who,rng.choice(["c_0","c_002","c_99999999","c_other",""]),when]
            if j%19==0:row.pop()
            if j%29==0:row.append("extra")
            if j%23==0:row[0]+='"q'
            if j%31==0:row[1]=chr(0)+row[1]
            line = ",".join('"'+x+'"' if rng.randrange(2) else x for x in row)
            all_input.extend(line.encode())
            all_input.extend(b"\n")
            if j%53==0:all_input.extend(b"\n")
        (output/(name+".csv")).write_bytes(all_input[:-1])


if __name__ == "__main__":
    args=argparse.ArgumentParser()
    args.add_argument("fixtures",type=Path)
    args.add_argument("output",type=Path)
    opts=args.parse_args()
    opts.output.mkdir(parents=True,exist_ok=True)
    build(opts.fixtures,opts.output)
