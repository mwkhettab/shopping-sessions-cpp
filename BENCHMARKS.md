# Reproducing the Shopping Sessions measurements

## Reader experiments after the 0.88 s official submission

The next native comparison includes the exact 0.87 s and 0.88 s release sources, independent GCC partial-training builds, eight-row clock validation, minimum-ISA marked offsets and single-pass CSV tiles. The single-pass reader can finish a tile partway through a physical row and restart at the last proven complete event. Alternate kernels verify newline positions before or after packing separators. Guard pages and the independent scalar reader check both the AVX2 kernel and the normally selected kernel.

On a separate Xeon 6975P-C, transposing the field offsets for eight events at once reduced 20M event-reader CPU from 1.757 to 1.450 core-seconds. It won five of five Docker practice pairs, three of three at 60M and three of three on the held-out 60M. The respective candidate medians were 0.954, 2.898 and 2.891 s against 1.026, 3.109 and 3.147 s at the 0.87 s source. The local branch now compares that same eight-row algorithm with AVX2-only, AVX-512F/BW-only and both-path compiler training. [Architecture comparison](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44915109608/job/136878946721)

Each surviving build must pass the supplied fixtures and both published reference tables. Fresh-process measurements rotate among candidates. The best two in each design family and explicit lower-ISA variants also get two complete 60M comparisons. The first 60M seed participates in image selection. The second is held out. The selected Docker image runs against the 0.87 s release under identical CPU affinity, cgroup and cache conditions. The experiment also tests the effect of disabling the optional wide event reader in the same trained executable.

## Official score history

| Git revision | Hidden 20M Intel Linux time | Production changes |
| --- | ---: | --- |
| `61e9ba65eafa2dc9a002c03aa2bbf4135931d448` | **1.80 s** | Original scored release |
| `1bcbf0cd1a2754fb8812fd91e2ef5f4237dc928e` | **1.82 s** | Only `.gitignore` changed |
| `8b2f568600d0fb5cc9bc066dc13ce609ed5d0e7b` | **1.83 s** | First 512-shard release |
| `6129109af768205fc66e05bd2d89711fa5d03b46` | **1.68 s** | SIMD CSV decoding, dense linked-person routing and delta Parquet |
| `8430241e49a4147b9b42dffce4cf21a4088eb5ff` | **1.13 s** | Concurrent input, compact revision and timeline storage, fused alias graph, partition slabs and direct Parquet |
| `33fb788bf962900617e8c0455d366f7f0a5d7eeb` | **0.99 s** | Reversible ID hashes and synthetic, build-time profile-guided compilation |
| `4db76beed31640ddf4b414ed19deba5ebc660b81` | **0.87 s** | Variable-width direct event decoding, grouped revision tags and reuse of revision pages for person timelines |
| `25a940f0d0a8f4a38ab8c5c96a47bb8927b3e4e3` | **0.88 s** | Verified tile-wide CSV structure and marked field boundaries on supporting x86-64 CPUs |

The entrant reported **0.88 s** for `BB-1F2F9464` at `25a940f0d0a8`. The entered best remains **0.87 s** at `4db76beed316`. The two Xeons that won every paired release-image comparison both support VBMI2. The hidden result does not report the selected CPU path. A 0.01 s difference between two official runs cannot distinguish ISA, training, input composition or run variation.

The scorer entered `BB-3FCA75FD` in the final at **0.87 s**. Queue wait was 220 ms. This improved the prior official 0.99 s time by 12.1%. The entrant supplied these scores. The 0.99 s run reduced the previous best by 12.4%. The scorer marked the earlier `BB-BE7C95B8` as entered in the final with 3 ms queue wait. Its 1.13 s runtime was 32.7% lower than the previous 1.68 s best. Entries `BB-065C223B` and `BB-E8D93ABE` listed 41 ms and 105 ms of queue wait separately. Relative to `61e9ba6`, the 512-shard release decodes each field as its delimiter is encountered, raises the event and person shard count from 128 to 512, reserves identity storage from input size, and compares inline canonical IDs bytewise without a separate display-name vector. It also reserves Arrow builder capacity and uses unchecked numeric appends within that reservation. Its compiler flags, Dockerfile, global deduplication rules, session boundaries and attribution algorithm match the official baseline. Its official run was 0.03 s slower. Two submissions of the identical baseline pipeline differed by 0.02 s.

## Eight-row event decoding on native Intel

The prototype transposes the verified CSV field boundaries of eight events. It identifies views and carts with nonempty products in one pass and then validates the original IDs and clocks. Purchases, other record shapes and irregular CSV retain their general parsers. Four pinned vCPUs, preloaded CSVs, fresh output directories and a fresh Docker process were used for each run.

| Dataset | 6975P-C, official 0.87 s source → prototype | Platinum 8573C, official 0.87 s source → prototype |
| --- | ---: | ---: |
| 20M seed 23, five pairs | 1.026 → 0.954 s | 1.386 → 1.275 s |
| 60M seed 47, three pairs | 3.109 → 2.898 s | 4.212 → 3.891 s |
| Held-out 60M seed 733, three pairs | 3.147 → 2.891 s | 4.199 → 3.817 s |

All 22 paired Docker runs favored the prototype. Both images passed all 36 fixtures, both published tables, five independent differential suites, full comparisons of both large output tables at every scale and the single-alias 8,388,673-event regression. The eight-worker ThreadSanitizer checks passed. The maximum observed process RSS was 8,437.0 MiB. Event input on the 6975P-C used 1.450 CPU-seconds versus 1.757 for the official source. Both hosts support AVX-512VBMI2. Earlier 25a940f results show that this CPU feature is an important limit on generalizing from these runners. [6975P-C results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44915109608/job/136878946721) · [8573C results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44915109608/job/136878947128)

On the 6975P-C the 20M CSVs occupied 1,917,888,312 bytes. Summing every byte from resident input took 0.0664 s median across three runs (28.9 GB/s). A narrower parse-only probe checked accepted event field spans in 0.2263 s on the official 0.87 s source and 0.1508 s on the eight-row prototype. Adding the key and timestamp calculations produced 0.3207 and 0.2485 s. The official source's full input stage took 0.524 s at 99.5% worker CPU utilization. An independent five-run probe measured 0.4284 s with full validation and no event partition writes, 0.4900 s writing only user events, 0.4801 s writing only revisions, and 0.5319 s writing both. Clicks and links ran normally in all four. At the held-out 60M scale the same control measured 1.537 s for input, 0.309 s for deduplication, 0.544 s for grouping and singleton sessions, and 0.500 s for linked-person sessions. The gap between the first and last worker finishing input was 0.2 ms at both scales. These resident-input measurements identify CSV decoding and partition traffic as larger opportunities than scheduling changes on this runner. [Input projections, write controls and worker timings](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44915109608/job/136878946721)

Linking each alias's raw records in place removed a scatter pass but increased the held-out 60M runtime on the 6975P-C from 3.100 to 4.560 s. Packing eight records per link took 3.339 s. The contiguous alias timelines remained faster on both Intel platforms. [Grouping and session experiments](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44915109608/job/136878946721)

A separate scatter experiment used the existing 64-bit raw-row key to store an alias index and the row's ordinal within that alias. During the second pass, each event's destination could then be computed from an immutable group offset. Shards too large for either field kept the size_t cursor. The forward, reverse and compact-offset versions passed all fixtures and both published tables. The reverse, compact-offset and reduced-capacity fallback builds matched all 20M and 60M values, including 6,684,549 user-day rows at 60M. In five warm eight-worker Mac runs the reverse version measured 0.495 s against 0.497 s for the 0.87 s source. The forward and compact-offset versions took 0.513 and 0.506 s. Sessionizing linked event spans without a merge buffer measured 0.509 versus 0.512 s in a separate rotation. These native arm64 differences provide little basis for selecting a scorer build.

### Training across Intel instruction sets

On a four-vCPU 6975P-C, compiling the eight-row prototype after training both the wider and AVX2 readers improved results across all three datasets. The training uses generated layouts at image build time. The same executable can be capped at AVX2 to exercise the route available without VBMI2. The following are medians of fresh processes. Each cell compares builds in the same rotation.

| Input | Automatic, official 0.87 s source | Automatic, mixed training | Forced AVX2, official source | Forced AVX2, automatic-only training | Forced AVX2, mixed training |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20M seed 23 | 0.9945 s | 0.9049 s | 0.9895 s | 0.9995 s | — |
| 60M seed 47 | 3.1169 s | 2.8256 s | 3.0850 s | 3.1296 s | 3.0597 s |
| Held-out 60M seed 733 | 3.0424 s | 2.7748 s | 3.0673 s | 3.1085 s | 3.0030 s |

The public annotation cuts off before the last practice cell. A separately trained AVX2-only build took 0.9810, 3.0759 and 3.0159 s in the automatic rotation. A separate decoder restricted to AVX-512F/BW with scalar delimiter extraction took 0.9595, 3.0467 and 2.9306 s. Both tests ran on the same physical CPU. The mixed-training Docker image won 11/11 pairs against the entered source, with medians of 0.909 versus 0.997 s, 2.804 versus 3.068 s and 2.815 versus 3.076 s. The exact outputs matched at every scale. The eight-worker executable on the held-out 60M dataset peaked at 8,893,444 KiB RSS (8.48 GiB). The image passed 36 fixtures and both published tables plus five independent suites. Eight-worker ThreadSanitizer and the 8,388,673-event single-alias test passed. [Cross-training, capped CPU paths and exact native Docker results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44920978277/job/136896725235)

The 0.88 s source measured 0.9537, 2.9853 and 2.9302 s in the same rotation. Compressing the stream markers and validating them in one pass measured 0.9671 and 3.0258 s at 20M and seed 47 for the best practice variant. Its AVX2-only counterpart measured 1.0436 and 3.2462 s. Vectorizing both timestamp conversions took 0.9300 and 2.9232 s on the full instruction set. The mixed-training eight-row decoder was faster on all three datasets. [Full portfolio and held-out measurements](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44920978277/job/136896725235)

### Clean eight-row Linux images

These release builds use a single automatic compiler training pass per input layout. Each native runner selected its fastest clean build using seed 23 and seed 47 before generating seed 733. Each cell compares the official 0.87 s source with that build. RAM is peak process RSS with eight workers on the held-out input. Each runner provided four pinned vCPUs.

| Intel CPU | Selected build | 20M seed 23 | 60M seed 47 | 60M seed 733 | Eight-worker RAM |
| --- | --- | ---: | ---: | ---: | ---: |
| [6973P-C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44922707304/job/136901997278) | `ship8bw-pgo` | 1.088 → 0.995 s (5/5) | 3.469 → 3.141 s (3/3) | 3.413 → 3.216 s (3/3) | 8.29 GiB |
| [Platinum 8370C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44922707304/job/136901997630) | `ship8lean-pgo` | 1.389 → 1.274 s (5/5) | 4.254 → 3.985 s (3/3) | 4.278 → 4.005 s (3/3) | 8.31 GiB |

All clean decoders passed 36 fixtures and both published reference tables. Each selected Docker image passed five independent comparison suites, full output comparisons on all three populations, the 8,388,673-event single-alias stress case, and eight-worker ThreadSanitizer. On the 6973P-C the input and partitioning phase fell from 0.607 to 0.522 s. Deduplication took 0.093 s, grouping with singleton sessions 0.190 s, graph resolution 0.027 s, linked sessions 0.158 s, and the final merge and output close 0.007 s. User Parquet writes also occur during session processing. One-pass CSV markers, immutable scatter ordinals and segmented linked-session bins all finished behind the release selected on that runner. [Full timings and profiles](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44922707304/job/136901997278)

Two additional experiments were isolated on the eight-worker Apple host. Storing most clicks in eight bytes using a lossless CSV position, campaign and whole seconds with a fractional-time escape changed the seven-run 20M median from 0.562 to 0.550 s, with overlapping ranges. A 12-byte variant took 0.571 s. The scalar ID and time oracle checked 800,000 comparisons, including cross-page fields and embedded null bytes. Five builds passed all fixtures and reference tables, four full 60M comparisons passed, and independent complete tables matched on adversarial click ties. Caching the first four digits of epoch-millisecond strings changed five-run 20M medians from 0.524 to 0.513 s. A five-digit cache took 0.525 s. All 503,329 generated timestamp spellings matched the scalar result. These figures describe development hardware.

## Marked event fields on x86-64

The release checks a 1 KiB CSV tile before exposing its seven fields per event. Each delimiter records whether the preceding field was quoted. Eight 16-bit lanes turn adjacent delimiters into exact lengths and starting positions for a complete row. The event callback can use those spans for views, carts and purchases, including invalid earliest revisions that suppress later copies. The optional reader requires AVX2, BMI, BMI2, POPCNT, PCLMUL and AVX-512F/BW/VBMI2. The scored reader handles older processors. Long rows, malformed tiles and IDs that cannot be encoded inline go through the existing general paths.

| Four pinned Intel vCPUs, fresh Docker process | Scored 0.87 s release | Marked fields | Pairs won |
| --- | ---: | ---: | ---: |
| Xeon 6975P-C, 20M seed 23 | 0.988 s | **0.941 s** | 5/5 |
| Xeon 6975P-C, 60M seed 47 | 3.033 s | **2.895 s** | 3/3 |
| Xeon 6975P-C, held-out 60M seed 733 | 3.055 s | **2.897 s** | 3/3 |
| Xeon 8370C, 20M seed 23 | 1.407 s | **1.361 s** | 5/5 |
| Xeon 8370C, 60M seed 47 | 4.336 s | **4.172 s** | 3/3 |
| Xeon 8370C, held-out 60M seed 733 | 4.433 s | **4.256 s** | 3/3 |

The median paired changes were −4.50%, −4.14% and −5.26%. Peak process RSS was 8,436.4 MiB. The actual container executable had SHA-256 `5a4cd068d6eabd9fa29075e285484e82e17da75b5460726a45321c059a0ab19d`. Its source and build inputs matched Git revision `0429302123e62543023b2b54a3ca8d3c27c9aa09`. The final portability guard excludes the 64-bit PEXT implementation from 32-bit x86 builds. GCC produced identical preprocessed linux/amd64 release source before and after the guard, SHA-256 `06787a2a8bb92fe03420b53966012dadf192529b7ad14d9a46119f6b2d7ae507`. Apple Clang also produced identical before-and-after source for each of arm64 and x86-64. [Native Git source verification, complete Docker pairs and CPU features](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44916326879/job/136882606414)

The independent Xeon Platinum 8370C reported median paired changes of −3.94%, −3.78% and −4.10%, with peak process RSS of 8,435.7 MiB. Its Debian-built binary passed the fixtures, both published reference tables, five independent suites and full-output comparisons at all three larger scales. [8370C release measurements](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44916326879/job/136882606602)

A third release-image run, on another 6975P-C, measured 0.974 versus 0.934 s at 20M, 3.012 versus 2.876 s at 60M and 3.039 versus 2.915 s on the held-out 60M population. All 33 measurements across three runners favored the marked reader. The official 0.88 s result ranks behind the entered 0.87 s result. An independent local build of the exact 25a940f Git archive produced a linux/amd64 ELF with SHA-256 `b55c65027cf1357b5bab59e44bc227719488dd90ac9f08b0ebdc540e418d0018`. The image passed the official tables and 20M/60M comparisons. Its eight-worker 60M run under x86 emulation and a 7 GiB Docker limit took 50.632 s at 7,137.8 MiB process RSS. The simulated Nehalem path passed both published 1M tables, and the released executable matched 24 independent scalar populations. [Third paired Intel measurement](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44916326879/job/136882606527)

The Docker executable passed all 36 supplied fixtures, both published 1M tables and five independent differential suites. Both complete tables matched the scored source at all three larger scales, including schema, nulls and unique daily keys. Eight-worker ThreadSanitizer passed the graph and attribution regressions. A one-worker input with 8,388,673 valid events on one alias followed by 64 competing revisions matched every expected value. The focused CSV regression covers all 128 quote masks, 1 KiB boundaries, long and empty fields, malformed input, missing final newlines and the direct callback declining a row. Run it with `c++ -std=c++20 -O2 tests/marked_events.cpp -o build/marked-events && build/marked-events`. [Qualification and skew results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44916326879/job/136882606414)

## Experiments against the official 0.87 s release

An alternative CSV reader collects delimiter positions in a 1 KiB tile, checks quote placement and verifies that every seventh delimiter ends a seven-field event. A tile that fails these checks is parsed through the general reader. On a four-vCPU Xeon 6975P-C the alternative beat the scored source in all eleven paired release-image measurements. Median process times fell from 1.027 to 1.007 s on 20M, from 3.135 to 3.063 s on 60M seed 47 and from 3.048 to 2.987 s on held-out 60M seed 733. Maximum candidate process RSS was 8,436.5 MiB. The image passed 36 fixtures, both published 1M tables, five independent differential suites, and complete large-table comparisons. The wide reader in this experiment uses AVX-512VBMI2. The competition permits Cascade Lake hosts with AVX-512F/BW, so the older-CPU path requires a separate comparison. [Paired release images, CPU features and complete-table verification](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44906285700/job/136852452678)

Making each input worker scatter payloads directly to their final destinations eliminated approximately 399 MB of copies at 60M. It increased held-out process time from 3.205 to 3.358 s on a 6975P-C and from 4.073 to 4.222 s on an 8573C. The control in these runs used separate revision columns before timeline reuse. In an independent representation experiment, eight-byte raw event rows with compact revision columns saved about 784 MiB at 60M and increased held-out process time from 3.145 to 3.470 s on the 6975P-C. [Scatter, 6975P-C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44903095305/job/136842931780) · [Scatter, 8573C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44903095305/job/136842931713) · [Compact rows, 6975P-C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44904823174/job/136848033406)

Two four-vCPU EPYC 9V74 runners provided an independent input-stage breakdown for the scored source. One exposed AVX-512BW and VBMI2. Its five-run medians were 0.331 s for structural field checks, 0.461 s with complete event validation and ID encoding, 0.511 s with raw-user writes, 0.530 s with revision writes and 0.603 s with both writes. The other exposed AVX2 and measured 0.409, 0.571, 0.620, 0.645 and 0.722 s respectively. Both probes parsed clicks and links normally. All five modes agreed on 21,505,871 event revisions and 20,624,893 direct callbacks. All four semantic modes agreed on every accumulated key, timestamp and purchase checksum. In the AVX2-only comparison the ordinal reader increased the 60M executable medians from 3.979 to 4.095 s and from 3.967 to 4.095 s. These AMD measurements establish fallback risk. The official scorer uses Intel. [EPYC with VBMI2](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44908287592/job/136858460333) · [EPYC with AVX2](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44908287592/job/136858460426)

On seed 47, 1,723,885 of 2,002,147 people had 1–31 surviving events, accounting for 22,301,599 events. A separate measurement of the valid input before deduplication found 17,268,004 distinct worker/user pairs on eight deterministic reader partitions. Allocating eight event slots at a time per worker/user pair would reserve 163,509,984 slots for 62,497,434 valid raw events. The open trials compare gathering linked-person records after deduplication, sorting short packed timelines in registers and extracting eight rows of checked CSV fields together. In 126,980 sampled 64-byte input blocks, 126,707 contained at most one comma per four-byte group. The corresponding parser checks the actual mask before using that case and decodes every other mask exactly.

Marking each delimiter with the closing-quote bit lets a reader derive all seven event spans together. A direct callback handles views, carts and purchases after checking the two ID widths and ingest field length. Malformed tiles and uncommon IDs retain the general decoder. In a 29-variant comparison on a Xeon 6975P-C this version had the fastest 60M and held-out 60M executable medians. It won all eleven alternating Docker pairs against the scored source. With four pinned vCPUs, process medians changed from 1.035 to 0.987 s at 20M, from 3.088 to 2.986 s at 60M and from 3.099 to 2.950 s on the held-out 60M. The paired median reductions were 4.52%, 3.30% and 3.90%. Peak candidate RSS was 8,435.0 MiB. Both complete tables matched on all three datasets. The release image passed 36 fixtures, both published 1M tables and five differential suites. The eight-worker sanitizer and skewed-revision checks also passed. These runs used AVX-512VBMI2. Forcing the earlier plain AVX2 ordinal reader on the same CPU increased the held-out executable median from 3.045 to 3.118 s. [Native Intel medians, Docker pairs and image fingerprints](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44909803261/job/136862990425)

The same Intel runner measured the scored source's isolated 20M input phase using five freshly trained probes per mode. It took 0.326 s to identify fields, 0.429 s with timestamps and IDs validated, 0.490 s with raw-user records written, 0.481 s with only revision records written and 0.533 s with both writes. Clicks and links were processed normally throughout. The modes agreed on 21,505,871 revision rows and 20,624,893 direct callbacks. All semantic modes agreed on 21,014,391 valid raw events and the complete key, time and purchase checksums. The event parser and validator still offer more potential than removing either individual write. [Release input probes](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44909803261/job/136862990425)

An eight-stage worker trace on the 6975P-C measured 520.8 ms for 20M input, including 0.2 ms of worker launch and join and 0.1 ms of average final-worker drain. Worker CPU utilization was 99.5%. Deduplication took 73.9 ms, grouping with singleton sessions 173.0 ms, union and list attachment 23.3 ms, and linked sessions 154.0 ms. Each of the last two session stages used over 99% of the available worker CPU time. These are instrumented stage times. In the same experiment, training both wide and AVX2 parsers and using GCC partial training improved the ordinary ordinal reader. Against the 0.87 s release, the image won all eleven pairs and changed the Docker medians from 1.044 to 1.005 s, 3.151 to 3.051 s and 3.096 to 2.999 s on 20M, 60M and the held-out 60M, respectively. Forcing the AVX2 parser in a binary trained only on the wide path took 1.079 s at 20M versus 1.022 s after both-path partial training. Ten readers using AVX-512F/BW without VBMI2 reached 1.023–1.075 s at 20M against the 1.014 s scored control. A separate EPYC 7763 without AVX-512 measured the marked rich reader at 1.345/4.094/4.132 s versus 1.321/4.053/4.026 s for the control. These observations motivated retaining the scored fallback and measuring compiler and reader combinations directly. [Intel stage and compiler comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44911732898/job/136868789132) · [AVX2-only EPYC comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44909803261/job/136862990626)

## Experiments against the 0.99 s release

The four-vCPU Intel Xeon 6975P-C spent 36.8% of sampled process CPU time in the seven-field event decoder and its inlined callback. Resolving duplicate revisions accounted for 7.9%. Separate warm input probes checksummed every decoded field length in 0.3626 s, then took 0.4850 s with semantic validation and 0.5831 s with validation and key encoding. Each figure is the median of five fresh processes. The probes measure cumulative workloads and omit timeline construction and output. The complete release took 3.341, 1.736 and 1.237 s at one, two and four workers. Consuming 1.918 GB in 0.50 s would require 3.84 GB/s while performing the rest of the pipeline. [Intel sampling, probe results and worker scaling](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44877968380/job/136766779170)

| Intel four-vCPU warm full-process medians | 20M, seed 23 | 60M, seed 47 |
| --- | ---: | ---: |
| Scored source, row-decoder series | 1.1873 s | 3.6091 s |
| Lazy field boundaries | 1.1815 s | 3.6160 s |
| Cache complete row layouts in 1,024 / 4,096 / 16,384 entries | 1.2193 / 1.2054 / 1.2404 s | — |
| Group aliases while parsing into blocks of 8 / 64 records | 3.9057 / 3.7324 s | — |
| Scored source, timeline-storage series | 1.1865 s | 3.6188 s |
| Second independent compiler profile from the same source | 1.1917 s | 3.6365 s |
| Consume linked spans directly in session bins | 1.1959 s | 3.6728 s |
| Replace contiguous scatter with per-alias chains, compact before processing | 1.6587 s | — |
| Replace contiguous scatter with per-alias chains, process bins directly | 1.6468 s | 5.3472 s |

Each pair used the same runner and files. Each build passed 36 supplied fixtures and both published reference tables before timing. The table reports five rotated 20M runs and three 60M runs for finalists. Removing the scatter pass increased traversal costs. The 4,096-entry layout cache recognized over 80% of event layouts in a separate input sample, but checking those layouts increased the Intel median. The release kept its original decoder and contiguous timeline storage. An eight-worker ThreadSanitizer run passed the extended graph and attribution regressions on native x86-64. An independent scalar interpreter also matched both complete tables on 24 generated edge-case datasets. [Row-decoder and fused grouping trials](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44877076608/job/136764023147) · [Contiguous and chained timelines](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44877968380/job/136766779170)

A separate AMD test varied the allocation order of the partition slabs. Interleaving only the raw-event and click partitions in tiles of 256 reduced the medians from 1.1712 to 1.1540 s at 20M and from 3.6514 to 3.5975 s at 60M on an EPYC 9V74. Both binaries in that comparison used interval attribution. Interleaving both revisions and raw records took 3.6981 s at 60M. [Full slab comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44876273411/job/136761546768)

### Compiler, revision index and CSV scanner comparisons

| Four-vCPU Intel 20M medians | Xeon 6975P-C | Xeon Platinum 8370C |
| --- | ---: | ---: |
| Official 0.99 s source | 1.1885 s | 1.6464 s |
| Add a hash tag to the winner locator | 1.1833 s | 1.6321 s |
| Encode short base-36 event IDs in a smaller revision record | 1.2156 s | 1.6730 s |
| GCC tuning for Skylake, baseline instruction set | 1.1929 s | 1.6518 s |
| GCC profile-guided build with link-time optimization | 1.1976 s | 1.6402 s |
| Third synthetic training population | 1.1837 s | 1.6342 s |
| Clang 16 profile-guided build, ordinary / ThinLTO | 1.2032 / 1.2366 s | unavailable |

The extra hash tag lost a separate seven-pair comparison on the 6975P-C, 1.1883 versus 1.1767 s. On the 8370C it took 1.6336 versus 1.6407 s. The third training population reduced the 8370C five-pair 60M median from 5.0360 to 5.0151 s. At 60M on the 6975P-C two independent builds of the scored source measured 3.6283 and 3.5550 s in the same rotation, bracketing the third-population build at 3.5667 s. The smaller revision record increased peak RSS from 8,283 to 8,531 MiB on the 6975P-C because it maintained separate compact and fallback partitions. [Intel 6975P-C comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44878671795/job/136768926855) · [Intel 8370C comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44878671795/job/136768927025)

Classifying each 64-byte input block once required carrying delimiter, quote and newline masks across records. On a four-vCPU EPYC 9V74, the AVX2 and AVX-512 implementations took 1.2116 and 1.2020 s against 1.1183 s for the existing row scanner. At 60M the same times were 3.6462, 3.6290 and 3.3762 s. Row-wise AVX-512 took 1.1337 and 3.4256 s. The AVX2-only 7763 also favored the original scanner. [AVX-512 host](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44878966869/job/136769828527) · [AVX2 host](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44878966869/job/136769828385)

Seed 733 supplies an independently generated second 60M population with 15M requested clicks. The scored implementation produced 6,621,736 user-day rows and 28,014 campaign-day rows. Both native runners compared the complete schema, all unique keys, nulls and values for this population. On the EPYC 9V45 a persistent worker team pinned within the allowed CPU set took 2.5834 versus 2.6429 s in the seed-47 paired rerun. On the EPYC 9V74, compressing delimiter positions into packed field boundaries using VBMI2 took 3.6022 versus 3.6570 s on seed 733, while the 9V45 measured 2.5911 versus 2.5912 s. Both CPUs advertised VBMI2. Short-ID load and worker-scheduling changes also changed rank between runners. [Seed 733 and worker comparisons on 9V45](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44880020704/job/136773037873) · [Seed 733 and parser comparisons on 9V74](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44880020704/job/136773038080)

### Timestamp conversion and session reduction

The four-vCPU Intel Xeon Platinum 8573C measured the scored source at 1.6876 s on seed 23. Sampling frequent aliases and routing them separately took 1.7124 s. Building partial half-hour bins for frequent aliases took 1.6989 or 1.7149 s at the two thresholds. Reading ingest clocks from the input only when resolving competing revisions took 1.7643 s. An order-preserving packed civil clock took 1.6911 s, then 5.1590 versus 5.1038 s on seed 47 and 5.0494 versus 4.9788 s on seed 733. The checked SSSE3 epoch decoder took 1.6974 s. On the EPYC 9V74 it won a seven-pair 20M comparison at 1.2194 versus 1.2283 s, but the actual Docker binary took 3.783 versus 3.738 s at 60M. Both runners matched the complete second 60M output. [Intel Xeon Platinum 8573C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44881200741/job/136776661521) · [AMD EPYC 9V74](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44881200741/job/136776661359)

Three native arm64 prototypes built exact per-alias session intervals as winning events arrived. Once the number of disjoint intervals exceeded one, three or eight, the alias kept subsequent events in a growable timeline. Later joins preserved inclusive 30-minute bridges and were attributed only after completing the identity graph. They matched 36 fixtures, both published tables, skewed and wide-window regressions and all rows at 20M and 60M. Their five-run warm medians were 0.575, 0.578 and 0.573 s against 0.494 s. In a separate seven-run arm64 series, summing worker campaign tables directly took 0.496 versus 0.503 s. The same reduction with compact category references took 0.491 s. These figures are development measurements.

The source-line profile on the 8573C attributed 14.89% of CPU samples to constructing the next record in a partition slab. The CSV field-cut loop accounted for 3.78% on its hottest line. Kernel page initialization accounted for 2.61%, and a page-fault handler for 1.81%. Compiler inlining and sampling skid limit attribution to individual instructions. [Native source samples, archived with the run](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44884734945/job/136787447641)

On the four-vCPU Xeon 6975P-C, the scored source took 1.1773 s at 20M. Bounding unsorted session union to one interval took 1.1873 s, extending it to eight took 1.2129 s, and direct campaign bitsets took 1.2100 s. Four-byte event records with indirect purchase payloads took 1.2016 s. On the 7763, pinning a persistent worker team reduced the seed-47 paired median from 4.5165 to 4.4421 s, but took 4.3440 versus 4.3203 s in the seed-733 rotation. Both 60M outputs matched. [Intel session and attribution comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44882396039/job/136780311063) · [AMD final-scale comparison](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44882396039/job/136780311089)

On a four-vCPU EPYC 9V45, emitting completed user-days directly to per-worker Parquet pages reduced the seven-pair 20M median from 0.9326 to 0.8762 s and the five-pair seed-47 median from 2.7655 to 2.6014 s. Seed 733 rotated medians were 2.6592 and 2.5105 s. The native Docker executions took 0.985 versus 0.936 s and 2.865 versus 2.679 s on seeds 23 and 47. The full outputs matched at all three scales. [Streaming writer comparisons on EPYC 9V45](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44883223002/job/136782839267)

On the four-vCPU EPYC 7763, the same streaming writer took 1.4346 versus 1.5041 s across seven 20M pairs. Rotated 60M medians were 4.6787 versus 4.8202 s on seed 47 and 4.1818 versus 4.3421 s on seed 733. A separate Linux amd64 release-image run on an Apple host limited to 7 GiB and eight emulated CPUs matched the original tables at 20M and 60M. At 60M that process peaked at 7,140 MiB and took 42.338 s. [Streaming writer on EPYC 7763](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44883223002/job/136782839424)

### Native Intel streaming and partition writes

The Xeon 6975P-C measured seven alternating 20M processes at 1.2161 s for the scored release and 1.1553 s with streamed user pages, contiguous partition write positions and direct campaign reduction. Its five-pair medians were 3.7021 versus 3.5168 s at 60M on seed 47 and 3.7327 versus 3.5913 s on seed 733. Inside the native release images, the medians were 1.259 versus 1.191 s, 3.841 versus 3.624 s and 3.636 versus 3.499 s, respectively. The candidate peaked at 8,316 MiB RSS on seed 733. The complete tables matched at all scales, and the eight-worker native ThreadSanitizer regressions passed. A separate EPYC 9V74 measured 3.5528 versus 3.4374 s across five pairs on seed 733. [Intel source and Docker comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44885535835/job/136789890234) · [Independent EPYC comparison](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44885535835/job/136789890463)

Batching event newline discovery reduced the 6975P-C Docker median from 1.218 to 1.166 s at 20M and from 3.665 to 3.539 s at 60M. The five-pair held-out executable median was 3.4428 versus 3.5692 s. The 8573C measured the same reader slightly slower at 20M, 1.7187 versus 1.7026 s. Splitting revision keys and payloads inside 256-row blocks had the opposite hardware sensitivity. Its 8573C Docker medians were 1.568 versus 1.681 s and 4.771 versus 5.133 s, and its five-pair held-out median was 4.6663 versus 5.0214 s. At 20M the input stage fell from 0.969 to 0.828 s while deduplication rose from 0.107 to 0.128 s. On the 6975P-C its 20M executable median was 1.1971 versus 1.1919 s. A separate bounded reserve and dense campaign reduction improved the seven-pair 6975P-C 20M median from 1.1906 to 1.1525 s and improved the rotated 60M medians on both Intel models. [Xeon 6975P-C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44884195964/job/136785807941) · [Xeon 8573C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44884195964/job/136785808088) · [Held-out medians and peak memory for every finalist](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44887403449/job/136795606967)

The 6975P-C hardware counters recorded 22.22 billion instructions, 18.34 billion cycles, 2.88 billion branches and 115.3 million branch misses on one full 20M control process. Measured across the four workers, IPC was 1.21 and the branch-miss rate was 4.0%. The independent 8573C source profile attributed a larger share of samples to partition writes. [Hardware counters and optimized source samples](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44885535835/job/136789890234)

### Batched event decoding and dense campaign reduction

On the four-vCPU Xeon 6975P-C, collecting 16 event line endings at a time, bounding each worker’s initial user-day allocation and summing dense campaign counters directly reduced the actual Linux amd64 Docker medians at all three scales. The event decoder checks every delimiter and quote before accepting a row. Short buffers, long records and processors without the required instructions have exact fallback paths. Sparse campaign populations still use the keyed reduction.

| Fresh process inside native Docker, warm input | Official 0.99 s source | Candidate | Candidate peak RSS |
| --- | ---: | ---: | ---: |
| 20M events, seed 23 | 1.248 s | **1.155 s** | 2,811 MiB |
| 60M events, seed 47 | 3.771 s | **3.474 s** | 8,281 MiB |
| 60M events, independent seed 733 | 3.720 s | **3.459 s** | 8,318 MiB |

The five 20M candidate times were 1.155, 1.150, 1.155, 1.150 and 1.158 s. Both tables matched for the 36 fixtures, both published 1M answers and the complete 20M and two 60M populations. The native tests also exercised an independent scalar oracle, wide click windows, identity bridges and eight-worker ThreadSanitizer regressions. On EPYC 9V45 the batched reader took 0.9128 s versus 0.9002 s for the scored source at 20M. The bounded allocation and dense reduction alone took 0.8530 s on that CPU. The production comparison targets Intel. The native executable inside the measured Intel candidate image had SHA-256 `2d8a06e5041b5be1836d345b2558b2c77699653d784139a30e63e8f33de9e3da`. [Intel comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44886735220/job/136793519592) · [EPYC comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44886735220/job/136793519366) · [Complete native Docker, second-population and memory results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44889289986/job/136801362027)

A competing pipeline let a dedicated consumer resolve event revisions while the other cores continued parsing. Its output and eight-worker race checks passed, but it increased the 20M Intel median from 1.1976 to 2.0907 s and the AMD median from 0.9002 to 1.5689 s. Write-combining raw partition records measured 1.3429 s on the same Intel runner. These designs were discarded. [Native pipeline comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44886735220)

An exact first-field pass that built a conservative duplicate filter before fully decoding the CSV increased the 6975P-C 20M median from 1.2208 to 1.6993 s. Accumulating events in per-alias arenas took 1.3464 s. Two event decoders that predicted field widths and validated them against the input took 1.3481 and 1.3536 s. Collecting up to one session before sorting the clicks took 1.2382 s. These changes failed the Intel comparison. [Native Intel results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44888229894/job/136798113020)

Streamed Parquet output plus 256-row revision blocks and the batched event reader took 1.1270 versus 1.2208 s in the Intel practice rotation. The first 60M rotation measured 3.4438 versus 3.6917 s. Five held-out pairs measured 3.3731 versus 3.6028 s, and the held-out Docker medians were 3.379 and 3.610 s with 8,319 MiB peak candidate RSS. On the EPYC 9V45 this combination increased the practice median from 0.8274 to 0.9031 s, whereas streaming with contiguous write counters took 0.7906 s. The batched-reader release and this streaming candidate require a same-run comparison to establish their relative speed. [Intel stage profiles and held-out tables](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44888229894/job/136798113020) · [AMD stage profiles and held-out tables](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44888229894/job/136798112801)

## Reversible IDs and profile-guided compilation

The Intel runner measured the exact source of the 1.13 s release against a lossless ID representation and a GCC profile-guided build. The ID permutation computes the hash at ingestion. Repeated shard and table lookups reuse it. Recovering the original bytes preserves lexicographic ordering for linked names and click ties. Indirect strings retain their full bytes and cache their hash. Two small synthetic build-time workloads exercise short and wide windows, ordinary and arbitrary IDs, and conflicting revisions. The container includes only the resulting executable and the Debian runtime.

| Four pinned vCPUs on Intel Xeon 6975P-C | 20M warm median | 60M warm median | Peak RSS at 60M |
| --- | ---: | ---: | ---: |
| Official 1.68 s release | 2.1143 s | 7.0492 s | 9,334 MiB |
| Official 1.13 s release | 1.3270 s | 4.1328 s | 8,287 MiB |
| Reversible IDs | 1.2927 s | — | — |
| Reversible IDs and build-time profiling | **1.2198 s** | **3.6877 s** | **8,285 MiB** |
| Same build with session-index interval attribution from 32 clicks | 1.2233 s | 3.7378 s | 8,284 MiB |
| Same interval build with chunk prefetch / sequential mapping advice | 1.2239 / 1.2224 s | 3.7511 / 3.7361 s | 8,284 / 8,283 MiB |

The candidate finished ahead of the 1.13 s source in every one of five rotated practice rounds and all three final-scale rounds. Its medians fell 8.1% at 20M and 10.8% at 60M. At 20M the individual times were 1.2024, 1.2053, 1.2198, 1.2246 and 1.2201 s. Tests with forced eviction confirmed 0% input residency at startup. The candidate took 3.782–3.801 s at 20M and 12.928–12.979 s at 60M. Chunk prefetch and Linux sequential advice failed to improve the 60M cold median. Separate trials with vectors found profile-guided compilation faster than link-time optimization on both Intel and AMD. [Paired Intel timings](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44874938746/job/136757467569) · [Compiler and attribution comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44874463331)

The 20M Intel phase medians were 0.627 s for concurrent input, validation and partitioning, 0.090 s for deduplication, 0.202 s for raw-user grouping and singleton processing, 0.026 s for the identity union, 0.187 s for linked sessions and attribution, and 0.045 s for merge and Parquet. At 60M those figures were 1.841, 0.354, 0.665, 0.089, 0.535 and 0.108 s. The 60M/20M full-process ratio was 3.02. A separate design that grouped aliases while scanning and stored records in per-alias blocks eliminated the later counting and scatter pass. In five warm arm64 rounds its two block sizes took 1.456 and 1.444 s, versus 0.499 s for the official 1.13 s source. The changes increased CPU time and peak memory. Its fixtures, published references and both 20M tables matched.

On the same native Intel runner, the actual linux/amd64 release image ran five 20M processes in 1.251, 1.250, 1.252, 1.279 and 1.262 s. Its 60M times were 3.820, 3.717 and 3.838 s. Peak process RSS was 2,811 MiB at 20M and 8,282 MiB at 60M. Including the `docker run` command increased the medians to 1.357 and 3.931 s. The old 1.68 s release image measured 2.156 and 7.130 s inside its container. The release binary in this Intel run had SHA-256 `76de9abe1b09be6f3a303eff41b78905629c544106d5260fec8393dd74da5617`. [Native Docker measurements](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44874938746/job/136757467569)

The local linux/amd64 image passed the 36 fixtures, extended regressions, both published 1M reference tables and full comparisons at 20M and 60M. Both tables had unique keys. The counts were 2,226,509 and 28,014 rows at 20M, then 6,684,549 and 28,014 at 60M. With Mac-host-mounted input, translated amd64 execution and a 7 GiB cgroup, the process took 4.788 and 43.560 s. The latter reached 7,140 MiB process RSS and the 7,168 MiB cgroup limit. The local executable had SHA-256 `1a1468538c686c3e099061e1da76865d12efd7f9bbc7a1be5cc87c40625a6a11`. Each Docker build collects a new multithreaded compiler profile.

## November 21 compact-partition release

The standalone linux/amd64 image passed all 36 fixtures, the extended regression cases, both published 1M tables, and row-for-row comparisons with the validated 20M and 60M tables. The regression cases cover long identifiers and rows, wide catalog and timestamp representations, competing invalid revisions, a 6,400-vertex cyclic identity graph, click ties and assist intervals, and signed 64-bit Parquet values. The pre-AVX2 image path matched the published answers. The decoder and timestamp checks passed with Linux UBSan in both CPU modes. The tested `/app/shopping` has SHA-256 `ccbe72b3b874b43ed44c7b9146c7237332222b83e31d213b92d550850faed249`.

| Input | Exact user-day rows | Exact campaign-day rows | Full process, Mac-hosted amd64 Docker | Peak RSS | Peak cgroup |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1M | 117,038 | 27,400 | — | — | — |
| 20M | 2,226,509 | 28,014 | 4.098 s | 2,707 MiB | 2,718 MiB |
| 60M | 6,684,549 | 28,014 | 31.461 s | 7,140 MiB | 7,168 MiB |

Both large tables had zero duplicate keys at both scales. The Mac Docker VM used eight pinned virtual CPUs, a 7 GiB cgroup with swap disabled, host-mounted input and translated amd64 execution. The 60M process incurred 13,024 major faults. On native four-vCPU Intel Linux the partition-slab implementation tested before source cleanup took 1.298 s at 20M and 4.062 s at 60M, with 2,813 and 8,287 MiB peak RSS. Its scored control took 2.076 and 6.467 s in the same experiment. [Native Intel timings and comparison](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44873434144/job/136752825580)

## Native x86-64 architecture comparisons

The official 1.68-second revision is the control in every series below. GitHub's free public runners supplied four Linux vCPUs. The scorer supplies eight hyperthreads on a GCP N2 Intel Xeon. Each warm comparison used the same generated files, GCC 12, one warm-up per executable, rotated order and fresh output directories. The parent measured startup, all reads and writes, and process exit. Inputs were preloaded before each timed process. The public benchmark branch contains the sources and commands for each experiment.

| 20M full-process median in seconds | Intel Xeon 6975P-C | AMD EPYC 9V45, run 2 | AMD EPYC 9V45, run 3 |
| --- | ---: | ---: | ---: |
| Official 1.68-second release | 2.150 | 1.956 | 2.015 |
| Half-hour bins for timelines with at least 32 events | 2.107 | | |
| Reduce hot timelines during scatter | 2.119 | | |
| Per-worker whole-input duplicate fingerprint bitmaps | 2.929 | | |
| Row-oriented AVX2, unchanged event layout | 2.123 | | |
| Whole-batch quote grammar / lazy field offsets | 2.171 / 2.135 | | |
| Lossless compact event and click timelines with half-hour bins | | 1.712 | |
| Same compact layout with an eight-record shard write buffer | | 1.718 | |
| Simpler key mixer and short-key load | | 1.694 | |
| Compact layout with row AVX2 / batch AVX-512 | | 1.629 / 1.648 | |
| Compact layout with direct delta / plain Parquet | | 1.682 / 1.619 | |
| Group by raw user and route one span per linked alias | | 1.454 | 1.522 |
| Separate revision index and fuse event/click ingestion | | | 1.512 |
| Raw-user spans, row AVX2 and direct delta / plain Parquet | | | 1.371 / 1.338 |
| Fused ingestion, row AVX2 and direct delta Parquet | | | 1.356 |
| Defer ingest-clock conversion and compare F1 bytes for duplicates | | | 1.419 |
| Two radix passes over fingerprints and exact-key resolution | | | 1.309 |

Each nonempty cell is the median of five timed processes. Compare changes within a column. Building the entire original binary for Haswell or Skylake AVX-512 produced 1.951 and 1.999 s against 1.956 s on the second runner. The portable builds keep CPU feature dispatch. A bounded atomic global winner table also lost to the official release on both arm64 (1.126 versus 0.787 s) and translated amd64 (4.361 versus 3.756 s). The whole-input bitmap cut the exact duplicate phase substantially, but collecting and consulting its fingerprints increased total time. [Intel screening](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44870757469) · [AMD compact-layout comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44871621885) · [AMD architecture comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44872341514)

On the first Intel runner, four workers summed every byte of the resident 1.918 GB input in 0.066 s, about 29 GB/s. Structural CSV decoding alone took 0.324 s. On that runner the control spent 0.843 s on events, 0.434 s on event deduplication and routing, 0.228 s on click input, 0.438 s on per-person processing, and 0.119 s on output. The concurrent identity build took about 0.34 s inside the event phase. A 0.65 s full run would need to consume the input at 2.95 GB/s while also performing validation, joins, deduplication, session attribution and output. The scanning probes use warm pages and exclude those other operations.

### Native final-scale control

| Four-vCPU AMD EPYC, seed 47 | 20M seed-23 median | Three 60M runs (s) | 60M median | Max 60M process RSS |
| --- | ---: | --- | ---: | ---: |
| Official 1.68-second release | 2.015 s | 6.520, 6.249, 6.439 | 6.439 s | 9,372 MiB |
| Raw-user grouping and compact timelines | 1.522 s | 4.340, 4.489, 4.343 | 4.343 s | 8,953 MiB |
| Raw-user grouping, row SIMD and direct delta Parquet | 1.371 s | 4.183, 3.927, 4.006 | 4.006 s | 8,622 MiB |
| Fused revision index, row SIMD and direct delta Parquet | 1.356 s | 3.978, 4.033, 3.922 | 3.978 s | 8,752 MiB |

The fused pipeline's 60M/20M time ratio was 2.93, against 3.20 for the control. At 60M the control's graph construction occupied 1.49 s of the event phase, while global event deduplication and person routing took 1.50 s. The fused pipeline spent 2.38 s on concurrent input and indexing, 0.24 s resolving losing revisions, 0.58 s grouping raw aliases, 0.37 s on linked-person timelines and 0.06 s writing Parquet. Both large datasets were checked by schema and sorted daily keys, including nulls and every value. Each table had zero duplicate keys. The optional memory-bounded benchmark generator produced byte-identical seed-1 files to the original published generator before generating seed 47. All competition executions read the supplied files directly. [Native large-scale comparison](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44872341514)

### Native Intel graph and memory trials

Each pair used identical inputs and CPU affinity. The Intel machine had AVX-512BW. The AMD EPYC 7763 used the portable AVX2 path. Each 20M cell is the median of five full processes and each 60M cell is the median of three. The graph trial interned link endpoints with the raw event and click aliases. Union operations then worked on resolved vertices, and only the aliases of a linked component had to combine their timeline spans. The slab allocator reserved a bounded address range per worker and shard, with growable overflow for skewed partitions.

| Change | Intel 20M | AMD 20M | Intel 60M | AMD 60M |
| --- | ---: | ---: | ---: | ---: |
| Scored release | 2.076 s | 2.910 s | 6.467 s | 8.981 s |
| Radix fingerprints and raw alias spans | 1.527 s | 2.012 s | — | — |
| Per-shard duplicate bitmap and exact verification | 1.433 s | 2.025 s | — | — |
| Compact revision index | 1.458 s | 2.071 s | — | — |
| Link endpoints interned with raw aliases, serial union | 1.439 s | 2.029 s | — | — |
| Parallel monotone union with path splitting | 1.434 s | 2.022 s | — | — |
| Sampled capacities with a separate graph | 1.426 s | 2.014 s | 4.508 s | 6.201 s |
| Sampled capacities with fused graph | 1.375 s | 1.984 s | 4.172 s | 5.928 s |
| Fused graph and per-worker partition slabs | 1.298 s | 1.767 s | 4.062 s | 5.440 s |
| Same slabs with PCLMUL/BMI2 quote validation | 1.319 s | 1.758 s | 4.098 s | 5.443 s |

On Intel, the simple row-oriented AVX2 parser with partition slabs took 1.293–1.299 s across five 20M runs, against 2.067–2.083 s for the scored release. Peak RSS was 2,813 MiB at 20M and 8,287 MiB at 60M. The scored release peaked at 3,281 and 9,321 MiB. The 20M candidate spent 0.680 s on concurrent input and initial partitioning, 0.115 s resolving losing revisions, 0.226 s grouping raw users and processing singletons, and 0.019 s on the identity union. The largest timeline contained 387,486 events and 72,005 clicks. Reducing the fused vector implementation from 512 to 128 shards or increasing it to 1,024 gave 1.397 and 1.395 s, against 1.375 s at 512. The alternate quote parser improved the AMD median by 0.009 s and increased the Intel median by 0.022 s.

The separate eviction probe requested eviction of this job's input files and verified 0% starting residency with `mincore`. The Intel 20M control and slab candidate took 3.240 and 3.660 s in single observations. At 60M they took 13.766 and 13.071 s. The AMD 60M observations were 17.710 and 16.397 s. Storage and page faults can dominate when the input starts cold. These individual observations motivated repeated cold trials with bounded readahead. [Intel measurements](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44873434144/job/136752825580) · [AMD measurements](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44873434144/job/136752825792)

## November 20 scored release

The candidate adds a runtime-dispatched AVX2/BMI/POPCNT CSV scanner, locates complete rows on the worker reading each chunk, and decodes common timestamp formats with fixed-width integer operations. Non-AVX2 processors retain the existing parser. The union-find pass assigns each linked person a permanent position in its shard. Each worker caches exact raw-ID routes, and catalog lookups use direct slots for canonical decimal identifiers with a full byte-string fallback. Parquet uses delta binary packing for dates and integers and omits optional column statistics. The executable reads the mounted inputs directly and writes only the two required output tables.

### Timed release binary

The actual Docker release executable and the `61e9ba6` baseline ran in alternating order inside the same Linux tmpfs environment. Both used the same seed-23 input, eight pinned virtual CPUs and fresh outputs, with one warm-up each. These timings cover process startup through exit under QEMU on Apple silicon. The candidate finished first in all five pairs. Its median was 14.1% lower and median process CPU time fell from 32.95 to 28.22 s.

| 20M translated amd64 | Five timed runs (s) | Median | Maximum process RSS | Combined Parquet size |
| --- | --- | ---: | ---: | ---: |
| `61e9ba6` | 4.451, 4.427, 4.466, 5.447, 5.192 | 4.466 s | 3,394 MiB | 97.3 MB |
| Candidate | 3.511, 3.836, 3.938, 3.790, 4.567 | **3.836 s** | 3,371 MiB | 43.0 MB |

On the native M3 Pro, an independent five-pair series used the same eight workers, a sequential preload and page-residency sampling. All timed processes started with effectively 100% input residency. The scored baseline took 1.025 s median (0.946–1.574 s). The candidate took **0.880 s median** (0.776–0.917 s), with a maximum peak RSS of 4,210 MiB. Both executables ran in a fresh process each time. These figures measure arm64 execution.

| Median elapsed phase on the same runs | `61e9ba6` | Candidate |
| --- | ---: | ---: |
| Event CSV, validation and partition | 2.247 s | 1.836 s |
| Identity resolution, concurrent with event input | 0.875 s | 0.788 s |
| Global event deduplication and routing | 0.602 s | 0.486 s |
| Click CSV and routing | 0.471 s | 0.392 s |
| Grouping, sorting, sessions, attribution and aggregation | 0.654 s | 0.547 s |
| Merge and Parquet output | 0.479 s | 0.374 s |

Event input is the largest elapsed phase. The candidate processed 1.65 GB of event CSV at roughly 0.90 GB/s in this phase, including field validation, partition writes and the concurrent link build. A separate diagnostic run measured 0.467 worker-seconds of structural CSV decoding over a 64.1 MB sample of 1.918 GB. The event partition held 860 MB of live revisions in 1.410 GB of vector capacity. Deduplication took 0.755 worker-seconds and routing, copying and releasing event bins took 3.554. Grouping took 1.138, sorting 1.842, session boundaries 0.780, attribution 0.816, aggregation 1.659 and user Parquet encoding and writes 2.542 worker-seconds. Sampled allocation time was 0.783 worker-seconds over 538,439 ordinary C++ allocations and 5.55 GB of cumulative requests. These diagnostic timings include instrumentation and overlap across threads.

The event workers handled 233–435 jobs each while their active times spanned only 3.091–3.094 s. The timeline workers handled 57–72 shards each and finished within 14 ms of one another. The largest person's timelines had 387,486 events and 72,005 clicks. In this run CSV decoding and moving surviving events consumed more time than waiting for stragglers.

### Release-image correctness and memory

Built with `docker buildx build --platform linux/amd64 --load`. The tested `/app/shopping` SHA-256 is `3d13f94c1963d5fe98148eab759ca7663f5b7de58f3ff6627dec8807970878c9`. All 36 published fixtures, the additional catalog/long-line case and both published 1M tables passed using the supplied Docker runner. The randomized decoder and timestamp tests also passed under Linux UBSan with AVX2 enabled and with a pre-AVX2 CPU model. The latter separately matched the 1M reference tables through the fallback parser.

| Input | Reference | Exact user-day rows | Exact campaign-day rows | Host-bind wall | Peak process RSS | Peak cgroup memory |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1M events, 250k clicks | Published answers | 117,038 | 27,400 | — | — | — |
| 20M events, 5M clicks | Previously validated output | 2,226,509 | 28,014 | 10.654 s | 3,361.1 MiB | 3,366.4 MiB |
| 60M events, 15M clicks | Previously validated output | 6,684,549 | 28,014 | 40.537 s | 7,134.6 MiB | 7,168.0 MiB |

Both larger comparisons checked schema, every value and duplicate keys for both tables. All keys were unique. The image read the Mac-host-mounted CSVs with eight pinned virtual CPUs, a read-only root, UID 1000, networking disabled and a 7 GiB cgroup with swap disabled. At 60M the cgroup reached that limit and the executable still completed. Event input took 24.59 s in that run and 7.48 s at 20M. The two host-bind runs incurred 13,089 and 4,230 major faults respectively. The five paired tmpfs runs above had two major faults each. The official scorer has 24 GB and runs natively on Intel. Its recorded time for this release is 1.68 s.

## Benchmark conditions

The 0.914 s native median below came from seven standalone processes after two warm-ups. The 2.156 s median came from five alternating rounds against the original string-key implementation at `9068330`, after one warm-up per executable. Both experiments used seed 23, eight application workers and fresh Parquet output. The timer included process launch, output close and process teardown. The harness created and removed output directories outside the timer. The older experiment did not record page residency, faults, clock frequency or competing load, so it cannot apportion the difference between those factors.

A repeat on the same M3 Pro recorded those missing page counters. The first 512-shard process started with 0% CSV residency, took 5.412 s and incurred 117,133 major faults. With 100% starting residency the five standalone times were 0.832, 0.960, 0.905, 0.865 and 0.796 s (median 0.865 s). Alternating against `9068330` gave 1.607, 1.043, 1.187 and 0.860 s for the same 512-shard binary (median 1.115 s), again with 100% starting residency. The original string-key executable took a median 5.598 s. Ordering and host conditions affect the result even when the input starts resident. The repeat used byte-verified transparent APFS compression on the generated practice input.

This command records resource counters for two executables in alternating order.

```sh
python3 perf/measure.py .bench/practice23 path/to/before path/to/after \
  --runs 5 --warmup 1 --profile --residency --log /tmp/shopping-comparison.jsonl
```

Each JSON line records wall, user and system time, peak process RSS, faults, block I/O, context switches and phase timings. The optional `--preload` reads the input before each process. The optional `--residency` samples mapped CSV pages with `mincore` before and after each process. Neither operation falls inside the timed interval.

### November 20 x86-64 experiments

Docker Desktop ran GCC 12 amd64 binaries through QEMU on the M3 Pro. The runner used Linux CPU affinity 0–7, an eight-CPU quota, a 7 GiB cgroup and disabled swap. The same 1.918 GB seed-23 input was staged in Linux tmpfs before timing. Each process wrote fresh Parquet in tmpfs. Each experiment rotated executable order after one warm-up per binary. The official machine has Intel cores and 24 GB. The measurements below establish local comparisons only.

| Change or comparison | Control median | Candidate median | Timed runs per binary |
| --- | ---: | ---: | ---: |
| Official baseline vs. the 512-shard release | 5.244 s | 5.138 s | 4 |
| Runtime-dispatched 32-byte AVX2 delimiter and quote indexing vs. the 512-shard release | 4.985 s | 3.789 s | 3 |
| Pack timestamps and ingest into 24-byte event revisions, with an overflow arena | 3.789 s | 4.489 s | 3 |
| Assign linked components dense positions in their person shard | 3.962 s | 3.852 s | 5 |
| Decode groups of ASCII clock digits using integer operations | 3.891 s | 4.186 s | 5 |
| Divide files by byte count and locate complete rows on the worker that reads them | 4.186 s | 3.546 s | 5 |
| Stream typed Parquet column batches while summarizing people | 4.174 s | 4.509 s | 5 |
| Store the timestamp, category and purchase price in one event word with an exact overflow path | 4.464 s | 4.645 s | 5 |
| Compile the entire executable for x86-64-v3 | 4.464 s | 4.075 s | 5 |

The 61-to-512 comparison overlapped substantially across runs (4.737–5.574 and 4.502–6.476 s, respectively). The dense person indexes reduced the median grouping and timeline phase from 0.579 to 0.519 s. The 24-byte revision experiment saved approximately 300 MiB at 20M. Streaming Parquet saved about 140 MiB. Both memory-saving candidates increased median elapsed time in their paired experiments.

A separate four-round rotation exercised different ways to remove row-level work. The shared control took 4.692 s. Partitioning by raw alias during parsing, indexing revisions separately, invalidating duplicate locations and combining per-alias timeline fragments took 4.074 s and raised median RSS from 3,360 to 4,189 MiB. Caching exact person routes took 4.213 s. Indexing canonical decimal catalog spellings with a full string-map fallback took 4.419 s and reduced median click parsing and routing from 0.499 to 0.404 s. Removing Parquet statistics took 4.453 s. Combining that with delta binary packing of integer columns took 4.549 s, with median output-phase time of 0.386 s against 0.475 s for the control. Wall times varied enough that the combined finalists were measured again against the scored revision.

A four-round finalist rotation measured 4.957 s for `61e9ba6`, 4.085 s for the worker-split, dense-group parser, 3.757 s after batching hash probes on that parser, 3.663 s for the combined route cache, catalog indexes and Parquet changes, and 3.746 s for the combined implementation with a single-pass AVX2 scanner. Increasing the combined implementation's bounded capacity hint gave 3.674 s and reduced the maximum RSS from 3,373 to 3,223 MiB. The alternative raw-alias architecture took 3.609 s and reached 4,270 MiB. It wrote 97.3 MB at 20M against 43.0 MB for the combined implementation.

The timestamp conversion was also isolated from changes in unrelated phases. Seven alternating measurements of 12 passes over 109,041 real pairs of timestamps produced identical checksums. Median decode time fell from 64.59 to 47.24 ms on translated amd64 and from 21.28 to 15.03 ms on native arm64. The regression test checks every byte substitution at all positions in a 13-byte epoch and in the clock portion of a UTC timestamp, plus every whole second of a day.

On the combined implementation, six further rotated runs gave 3.636 s for the existing probe loop, 3.836 s for batching probes with the larger capacity hint and 512 shards, and 3.662 s with the same changes and 128 shards.

Three additional native 60M runs per variant with Docker Desktop running gave medians of 10.008 s for the selected implementation, 10.483 s for the larger capacity hint, and 10.515 s for batched probes with 128 shards and the larger hint. The selected implementation retains 512 shards, the original bounded capacity hint and the simple probe loop.

### Why 60M can take much longer

The original seed-47 data was regenerated for this investigation. The benchmark-only generator emitted deterministic groups of part files with the original shuffling, row data and headers. A recursive byte comparison of the batched seed-1 generation against the original 1M files passed. The host stored eligible files using transparent APFS compression. Both 60M output tables matched the earlier validated implementation.

For `8b2f568` with eight native workers and the Docker VM offline, the standalone 20M series above had medians of 4.822 user CPU seconds, 0.677 system CPU seconds and one major fault. A three-round 60M rotation had medians of 15.461 user CPU seconds, 6.859 system CPU seconds and 44,242 major faults. Event and click input grew by 2.97×. User CPU grew 3.21×, system CPU grew 10.1× and wall time grew from 0.865 to 5.152 s. Process RSS medians were 4,163 and 8,380 MiB. The 60M run that started with 27% input residency took 21.849 s with 352,215 major faults. Its two runs starting at 100% took 5.131 and 5.152 s. Every 60M process was preceded by a sequential read outside the timer. The Mac could still evict the input before and during a run.

On the same 60M rotation, the dense routing, catalog and delta-encoding candidate took 6.513, 3.825 and 3.655 s (8,906 MiB maximum RSS). The raw-alias fragment design took 5.464, 4.053 and 4.277 s (8,686 MiB maximum RSS). Integer delta encoding reduced the two tables from approximately 288.9 MB to 127.8 MB. The 60M user-day output has 6,684,549 rows against 2,226,509 at 20M, a 3.00× increase. The major faults, system CPU time and starting-residency measurements establish a substantial memory-pressure effect.

Two further controls used the release executable and the same native preload and measurement harness. Each reports three timed runs after one warm-up.

| Native input | Event + click bytes | Link bytes | Median elapsed | Median user CPU | Median system CPU | Parquet bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original seed 23, five runs | 1.899 GB | 18.3 MB | 0.880 s | 4.68 s | 0.73 s | 43.0 MB |
| One third of seed 47 by event/click bytes, all seed-47 links | 1.949 GB | 55.0 MB | 1.498 s | 5.64 s | 1.19 s | 103.4 MB |
| Three copies of seed 23, original links | 5.697 GB logical | 18.3 MB | 3.049 s | 10.71 s | 5.58 s | 43.0 MB |

At comparable input byte counts, the seed-47 subset spent 0.583 s building linked identities against 0.220 s on seed 23. Its grouping and timeline phase took 0.339 s against 0.172 s. This subset contains two event files and two click files, so its composition and file distribution both differ from seed 23. The triplicate control used hard links and processed each event and click part three times. Exact full-table comparison confirmed the original seed-23 results. Its event deduplication and routing rose to 0.692 s while its final merge and Parquet took 0.053 s. Repeated input, a larger identity graph and more output rows each change the work in different phases. For the real seed-47 run under host pressure, 276,794–346,576 major faults and median system CPU of 20.86 s account for much of the excess over the resident controls.

## Reproducing the datasets and checks

The commands below use the original generator and checker from the challenge distribution. In this workspace they are under `generator/` and `starter-pyspark/scripts/`, with the seed-1 answers under `dev-answer/`. Releases through `6129109` loaded the Arrow and Parquet C++ libraries. The compact-partition release writes Parquet directly. The independent reference checkers use Python.

```sh
python3 -m venv .bench/env
.bench/env/bin/pip install pyarrow==21.0.0 polars==2.1.0
PYTHON=.bench/env/bin/python sh build.sh
generate --seed 1 --events 1000000 --clicks 250000 --output .bench/dev
generate --seed 23 --events 20000000 --clicks 5000000 --output .bench/practice23
generate --seed 47 --events 60000000 --clicks 15000000 --output .bench/final47
.bench/env/bin/python verify.py build/shopping .bench/dev
NCPU=8 python3 bench.py .bench/practice23 build/shopping --runs 7
```

Pass two executable paths to `bench.py` to alternate their order. Timing starts before launching the process and ends after it exits. Each invocation gets a fresh output directory. Cleanup and output comparison run outside the timer. For individual runs, use an empty output directory.

Use `--warmup 1` to run each binary once before collecting measurements. With three or more binaries the order rotates. For Linux, `IMAGE=shopping-cpp:final sh bench-container.sh INPUT FRESH_OUTPUT` pins CPUs 0–7, caps the container at eight CPUs and 24 GB, disables network access and uses a read-only root. It times a fresh executable process for each run inside one container and keeps each output for inspection.

Set `PROFILE=1` for elapsed times at phase boundaries. Build with `DIAGNOSTICS=1 PYTHON=.bench/env/bin/python sh build.sh` and run `PROFILE=1 build/shopping-profile INPUT OUTPUT` for worker-time breakdowns. Nested measurements overlap. CSV structural decoding samples chunks and subtracts the field callback time. Allocation timing samples one in 128 ordinary C++ allocations and excludes deallocation, aligned allocations, and Arrow's separate memory pool.

## Earlier native optimization experiments

These are native macOS arm64 measurements on an 18 GB M3 Pro with eight application workers. Each row compares two executables in the same experiment on seed 23. Runs use the filesystem cache. Thermal state, page residency and other activity varied across experiments, so compare medians **within** a row. Every candidate below passed the 36 fixtures and both published dev tables.

| Change | Before median | Candidate median | Runs per binary |
| --- | ---: | ---: | ---: |
| Open-addressed global event deduplication | 5.840 s | 4.871 s | 3 |
| Extra duplicate-filter scan (discarded) | 4.753 s | 5.287 s | 3 |
| SIMD delimiter and quote scanning | 7.128 s | 6.085 s | 3 |
| Per-worker map of person timelines (discarded) | 5.337 s | 9.829 s | 3 |
| Preliminary offset-only exact dedup scan (discarded) | 4.183 s | 4.808 s | 3 |
| Losslessly packed IDs and flat alias lookup | 4.141 s | 1.916 s | 3 |
| Direct campaign-day aggregation with sparse fallback | 2.244 s | 1.854 s | 3 |
| Cache civil dates and specialize guaranteed F1 ingest parsing | 1.845 s | 1.695 s | 5 |
| Count and scatter each person's timelines into contiguous spans | 1.607 s | 1.514 s | 5 |
| Block-allocate event candidates (discarded) | 1.661 s | 1.687 s | 5 |
| Bounded initial reserve for event candidates | 1.526 s | 1.453 s | 5 |
| Radix-sort large user timelines (discarded) | 1.418 s | 1.466 s | 5 |
| Build global linked identities concurrently with event parsing | 1.589 s | 1.275 s | 5 |
| Reuse campaign activity and category spend storage | 1.545 s | 1.367 s | 5 |
| Collect all field boundaries before decoding a row (discarded) | 1.373 s | 1.505 s | 5 |
| Reserve numeric Parquet builder capacity before appending | 1.685 s | 1.459 s | 5 |
| Store duplicate-table keys alongside winner pointers (discarded) | 0.911 s | 0.968 s | 7 |
| Snappy-compress Parquet output (discarded) | 1.119 s | 1.200 s | 5 |

The direct comparison of link-table preallocation and bytewise inline name comparison was essentially tied on seed 23 (1.515 vs 1.531 s). On seed 47 it improved all three paired 60M runs, with medians of 14.573 and 13.065 s. A six-run rotated comparison after one warm-up per binary found medians of 1.017, 0.934 and 0.901 s for 32, 128 and 512 shards, respectively.

Docker Desktop on Apple silicon gave a different result on the same 20M input. With eight pinned vCPUs, a 7 GiB cgroup limit and the Linux/arm64 image, direct host-mounted input took 5.869, 8.227 and 6.153 s. Copying identical input bytes to Linux tmpfs **before** measurement took 2.519, 2.398 and 2.086 s for the executable, including Parquet output to the host mount. Both configurations matched the baseline. The production executable always reads its supplied input directly. These machines and mounts differ from the Intel competition scorer.

## 512-shard release verification

* The native executable and the `linux/amd64` submission image passed all 36 published fixtures. Both matched all 117,038 user-day rows and 27,400 campaign-day rows of the independent published dev answer. The amd64 image ran under Docker Desktop using the supplied scorer script with a 7 GiB limit.
* The final 20M output matched the original C++ baseline at 2,226,509 user-day and 28,014 campaign-day rows. The 60M output matched the earlier string-key implementation at 6,684,549 and 28,014 rows. Both diffs reported zero differences and zero duplicate keys. The generated large datasets have no published independent answer here.
* In an alternating five-run native comparison, the original baseline median was 5.865 s and the retained binary median was 2.156 s. In the separate warm shard experiment above the retained binary achieved 0.901 s. Cross-run variation was substantial on this host.
* The standalone 512-shard warm-cache series used two warm-ups followed by seven timed processes: 1.313, 1.084, 0.914, 1.074, 0.874, 0.815 and 0.891 s (median **0.914 s**). These measurements used generated data on the Mac.
* Three final native runs on 60M events and 15M clicks took 17.602, 17.309 and 15.330 s. Peak process RSS from `wait4` was 4.603, 4.723 and 5.216 GiB. These are resident-process measurements on a memory-pressured Mac, not a Linux cgroup high-water mark. The seed-47 CSVs were removed after validation to reclaim space. Use the generator command above to recreate them.

On a separate instrumented warm 20M run, event CSV decoding, validation and sharding took 0.294 s elapsed, including the concurrent 0.198 s identity build. Global deduplication and person routing took 0.155 s, clicks took 0.107 s, grouped timeline processing took 0.212 s, and the final merge and Parquet writes took 0.063 s. The jobs overlap across workers. The diagnostic totals were 0.432 worker-s grouping, 0.477 sorting, 0.075 scanning session boundaries and purchases, 0.088 updating the attribution window, and 0.263 aggregating. Structural CSV scanning accounted for 0.0453 sampled worker-s over 62.4 MB of 1.918 GB. The ordinary C++ allocator handled about 926,000 allocations and 5.51 GB of cumulative requested bytes, with an estimated 0.134 worker-s in malloc. Process launch and teardown are included in the release benchmarks.

## Correctness invariants

* Event ID determines the deduplication shard, including revisions that change user. A CSV-valid but semantically invalid revision participates in earliest-ingest selection. The winning revision must pass validation before entering a person's timeline.
* Compact keys preserve byte identity. Long IDs, unusual prefixes and strings that cannot be represented inline take an exact string-view path. Hash collisions always compare keys. A linked component's display name includes IDs that never appear in events or clicks.
* Repeated identical clicks can remain in the sorted timeline. They preserve the latest timestamp and smallest click ID, and campaign counts contribute one assist while positive. Each person's session and click windows advance monotonically.
* The Parquet diff checks the schema, values, nulls and duplicate keys. The extra randomized parser test compares accepted fields with a scalar decoder and checks compact keys against byte strings. Run it with `c++ -std=c++20 -O2 tests/input.cpp -o build/input-check && build/input-check`.

### Row parsing, revision indexing and time-grouping comparisons

Two four-worker Intel Xeon 6975P-C runners reproduced the benefit of the shipping batched reader over the official 0.99-second source. Their five-run 20M medians were 1.0991 vs 1.1848 s and 1.1146 vs 1.2072 s. The same pairs on seed-47 60M were 3.4264 vs 3.6308 s and 3.4740 vs 3.7158 s. Peak RSS was 8.1 GiB. Reader selection by a short timing probe gave similar Intel results and improved the AMD comparison, but the reduced stage timings did not consistently beat the shipped code on both Intel populations. Preselecting the earliest revision in a separate pass regressed to 1.500 and 1.520 s at 20M. Warm, checksummed input-only byte scanning took 0.063–0.068 s on these Intel workers, and CSV tokenization alone took 0.323–0.327 s with the control reader. The official scorer supplies eight vCPUs.

The next comparison measures delimiter positions and quotes independently, introduces SIMD fingerprint groups for exact revision or user lookups, and coalesces alias events by a counting pass over the occupied half-hours. All candidates run against the independent scalar interpreter for malformed rows, earliest invalid revisions and every field-quote combination. The runner checks complete Parquet outputs before measuring and includes the current shipping Docker image in the same rotating run order. The production SHA is 7f7f2ae74eeeb946ba3a22e8f2587b3b32ec4458.

On the Xeon 6975P-C, the candidate using compact field bounds and a fused non-purchase path finished in 3.3849 s against 3.4217 s for the shipped reader on seed 47. It finished in 3.3131 against 3.3588 s on held-out seed 733, winning four of five adjacent pairs on each population. Its actual Docker medians against the official 0.99 s source were 1.100 vs 1.207 s at 20M, 3.403 vs 3.700 s at 60M, and 3.315 vs 3.606 s on the second 60M. Exact outputs and the eight-worker ThreadSanitizer cases passed. The 20M winner using the ordinary ID encoder had a 0.36% median paired gain against the shipped reader. The entrant reported 0.99 s for 33fb788. A scorer result for 7f7f2ae is pending.

The 20M population contained 19,535,163 surviving events, 6,248,290 distinct raw-alias half-hours and 4,280,182 distinct canonical-person half-hours. At 60M these counts were 58,731,817, 18,718,478 and 12,863,247. Smaller grouped event counts did not translate into shorter runtimes. The hash-based early-coalescing implementation took 4.3782 s vs 3.5303 s on seed 47, with 1.308 s in raw-user grouping. On 20M, the joint user/event-ID partition grid took 1.3582–1.4104 s vs 1.1044 s for the shipping reader. Materializing a 4 KiB delimiter index took 1.1778–1.2296 s across three implementations. Deferring campaign assists across sessions measured 1.1049 s for the day-reset version and 1.1080 s for the full-range version. The non-tiled streaming writer benefited the AMD 9V74, winning all 17 pairs against the shipping candidate across 20M and both 60M populations by 5.3–6.3%.

A separate exact-output diagnostic counted 5,131,778 clicks for people with surviving events at 20M. Among 1,410,835 distinct latest eligible click positions used for attribution, 73,733 had a timestamp tie and 36,078 involved different campaigns. The next comparison borrows the original click ID bytes and resolves bytewise ordering only when a surviving session needs it. It also measures homogeneous four-row timestamp validation, delayed partition writes, direct collision claims and whole-block quote certification. Every fast path retains the full CSV grammar and arbitrary identifiers.

### Decoder and revision comparisons against the pushed candidate

The direct release control was 7f7f2ae74eeeb946ba3a22e8f2587b3b32ec4458. On a four-vCPU Xeon 6975P-C, inspecting three event delimiter masks independently and emitting validated view/cart records directly reduced the actual Docker process medians below. Every accepted fast-path row proves its field boundaries and quoting against the input bytes. Other forms use the full decoder. The release binary inside the measured experimental image was SHA-256 `116b712ac7743f0e559c0f5304a9c0377e8e55863b2181457b9a93e613cbedf9`. It passed all 36 fixtures, both 1M reference tables, four independent differential suites, and complete output comparison on three larger inputs. [Intel image and paired measurements](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44893024764/job/136812723353)

| Generated population | Scored 0.99 s source | Pushed 7f7f2ae | Direct decoder | Wins against pushed image |
| --- | ---: | ---: | ---: | ---: |
| 20M, seed 23 | 1.232 s | 1.148 s | 1.127 s | 4/5 |
| 60M, seed 47 | 3.691 s | 3.539 s | 3.458 s | 3/3 |
| 60M, held-out seed 733 | 3.673 s | 3.392 s | 3.317 s | 3/3 |

Ten additional adjacent fresh-process comparisons at 60M all favored the direct decoder. The paired medians were 3.4443 versus 3.3826 s and 3.3519 versus 3.2759 s. Peak held-out RSS for the candidate image was 8,319 MiB. A separate 16-tag simultaneous lookup for event revisions won seven of seven practice pairs versus the pushed executable, 1.1317 versus 1.1128 s. Applying the tag lookup to raw-user grouping took 1.1873 s in the practice rotation, versus 1.1283 s for the pushed release. Counting or packing half-hour events by chronological bins took 1.2643 and 1.2678 s. [Full Intel experiment](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44893024764/job/136812723353)

Two independent 6975P-C runners measured a byte-only read at 0.063–0.068 s and structural tokenization at 0.323–0.327 s on the resident 1.918 GB practice input. With a checksummed unprofiled GCC build, totaling parsed field lengths took 0.354–0.358 s, adding semantic validation took 0.485–0.487 s in total, and validating plus constructing exact keys took 0.566–0.586 s. These diagnostics use different loop bodies and are cumulative. On the first runner the scored release measured 3.3473, 1.7161 and 1.2189 s at one, two and four workers. The selected batched reader with columnar revisions measured 3.0303, 1.5486 and 1.1264 s. [Paired native worker scaling and diagnostic checksums](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44895178350/job/136819178706)

Sampling 128 evenly spaced 32 KiB regions in the event files found valid whole-field quoting for 99.83% and 99.86% of rows when validated in 1,024-byte groups on seeds 23 and 47. A second sample over 192 separate 48 KiB regions found the direct nine-byte ID path applicable to 100,962 of 122,640 practice events and 102,327 of 122,564 final-scale events. The next controlled comparison checks whether certifying quote syntax once per byte group, recognizing normalized IDs of either eight or nine bytes, or pairing the decoder with simultaneous revision lookup reduces whole-process time. The instrumented build also assigns hardware counter samples to the executable's monotonic stage boundaries.

### Four-worker Intel results across two Xeon models

On the Xeon 8573C, storing revision IDs separately from their ingest and event locations in 256-row blocks and writing Parquet pages during aggregation won all 11 alternating Docker pairs against 7f7f2ae. The 20M median went from 1.660 to 1.538 s. The two 60M medians went from 5.084 to 4.697 s and from 4.934 to 4.627 s. Held-out peak RSS was 8,318 MiB. Every field in both output tables matched the scored executable on all three large populations. [8573C runner and release images](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44893024764/job/136812723494)

Adding fused nonpurchase validation to the column layout won every alternating Docker comparison against 7f7f2ae on another Xeon 6975P-C. The final image passed 36 fixtures, both published 1M answers, five independent differential suites and a full comparison on three larger inputs. Its binary SHA-256 was e7e2c20632495453c1941d125dc1ffb9da1cf0d6f7cab30445e8a429b7049dae. [6975P-C image timings and output verification](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44895178226/job/136819178666)

| Generated data | Official 0.99 s source | Pushed 7f7f2ae | Column layout and fused validation | Wins against pushed image |
| --- | ---: | ---: | ---: | ---: |
| 20M events, seed 23 | 1.252 s | 1.149 s | 1.132 s | 5/5 |
| 60M events, seed 47 | 3.805 s | 3.567 s | 3.514 s | 3/3 |
| 60M events, held-out seed 733 | 3.698 s | 3.432 s | 3.377 s | 3/3 |

In the 20M release executable rotation, the new layout reduced input from 0.587 to 0.546 s, increased deduplication from 0.090 to 0.115 s and reduced final merge and output from 0.040 to 0.013 s. At 60M, input took 1.627 s, deduplication 0.453 s, raw grouping and singleton sessions 0.655 s, linked components 0.098 s, and linked-person processing 0.543 s. Held-out Docker peak RSS was 8,318 MiB. Four-row timestamp validation measured 1.147 s against 1.115 s for the pushed practice control. Concurrent atomic first claims took 2.255 s. Whole-block mask certification with a fused reader took 1.204 s. These trials remain on the benchmark branch. [6975P-C full-process rotation and stage profiles](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44895178226/job/136819178666)

The next comparison isolates simultaneous revision tag lookup on the column layout and independent field-boundary extraction for clicks. It also compares two revision storage layouts chosen per worker from four paired input chunks, with separate fixed-layout controls and a forced mixed-layout correctness case. Linux/amd64 UBSan under Docker Desktop checked 180,000 click lines per decoder for all 16 quote patterns, malformed rows, arbitrary bytes and long fields. The raw alias quotient-map unit exercised 1,728,000 lookups, including wraparound, collisions, indirect keys and locator overflow. The full-process runs record per-phase CPU and page faults and compare a skewed input crossing the actual 32-bit revision-location boundary.

The 8,388,673-event single-user fixture crossed the 32-bit location boundary on the Mac with one worker. Both the pushed implementation and the direct decoder matched the independent counts and revenues after 64 conflicting revisions. The separate mixed-layout case forced 45,900 records through both physical formats, including conflicting owners and ingest dates in 1964 and 2180. Each of five configurations matched the independent oracle at both one and eight workers.

### Revision writes and hardware counters

The pushed release control for subsequent pairs is 4a6d895400924b9cd4d7d93e8f9e0e30dd1df8a6. Its streamed Parquet writer and separated revision keys improved the 6975P-C 60M image medians reported above. On a second Intel Xeon 8573C, it measured 1.4378, 4.3955 and 4.3801 s on the practice, final and held-out native rotations. The previous release measured 1.5767, 4.8693 and 4.7525 s. A clean archive of the pushed Git tree reproduced the locally qualified linux/amd64 binary, SHA-256 738fcd2d113504cd2bc94a9f8cb6480aa5c98f3c61e2be3ea1301fddb4eeb91f. [Cross-Intel release measurements and qualification](https://github.com/mwkhettab/shopping-sessions-cpp/blob/4a6d895400924b9cd4d7d93e8f9e0e30dd1df8a6/BENCHMARKS.md)

On the 8573C, prefetching the final raw-event and revision destinations and committing their writes 32 revisions later reduced the practice input stage from 0.929 to 0.740 s. The downstream stage medians stayed within 0.002 s. The image won every adjacent comparison against 7f7f2ae, with medians of 1.412 versus 1.617 s, 4.329 versus 4.851 s and 4.218 versus 4.759 s. On the 6975P-C, both executables measured 1.1145 s at 20M. [8573C deferred-write comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44895178226/job/136819178614) · [6975P-C control in the same source rotation](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44895178226/job/136819178666)

On another four-vCPU 6975P-C, verifying event fields with either eight- or nine-byte normalized IDs and using 16 simultaneous revision tags with compact checked locators won all 17 native pairs against 7f7f2ae. Actual image medians were 1.056 versus 1.114 s at 20M, 3.272 versus 3.452 s at 60M and 3.213 versus 3.346 s on the held-out 60M, winning all 11 image pairs. Held-out RSS peaked at 8,315 MiB. Two sibling vCPUs took 2.0019 versus 2.1439 s and two distinct cores took 1.4884 versus 1.5890 s. The block-level quote certificate variants took 1.159–1.162 s and extracting all delimiter positions using a byte lookup took 1.293 s against the 1.100 s native control. [6975P-C phase counters, topology and Docker results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44896825283/job/136824174416)

On that runner the practice control spent 54.1% of sampled cycles and 70.0% of retired instructions during concurrent input and partitioning. Global deduplication accounted for 8.8% of cycles, raw grouping and singleton sessions 17.5%, linked components 2.2%, linked-person processing 14.1% and final output 2.4%. With the variable-width decoder and revision tags, deduplication fell to 7.0% of cycles. The largest source sample was the partition slab append at 6.83%, followed by the vector mask operations. Kernel page initialization and the user page-fault handler accounted for 3.05% and 1.90% of samples. The next trials pair destination prefetch with the fused decoder, group writes inside bounded worker-local tiles and reuse retired revision mappings for contiguous timelines. [Per-phase and source samples](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44896825283/job/136824174416)

The bounded partition unit verified all 102,018 revision payloads and raw locations across six orderings, including invalid winners and extended ingest times. A separate 76,976-record unit exercised delayed writes across the primary slab boundary and repeated spill-vector growth. The row-bounds decoder passed 516,096 valid and mutated inputs spanning all 128 event quote masks in each local build, followed by 140,000 independent mutation cases. The x86-64 workflow repeats these checks on its detected instruction set.

### Event delimiter positions and linked-person copies

A second Intel model favored a different reader in the same source rotation. On the Xeon 8573C, certifying the CSV quote grammar across 256 bytes and extracting delimiter positions with AVX-512 VBMI2 reduced the 20M executable median from 1.6538 to 1.4631 s. On the Xeon 6975P-C this reader took 1.072 s, while the variable-width direct decoder with revision tags took 1.034 s. Each has a runtime-checked fallback. The follow-up experiment checks larger tiles, direct event emission, an AVX2 implementation, and an online choice measured on adjacent halves of a real input chunk. The complete workflow also measures the current production commit 4a6d895. [8573C profiles and image comparisons](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44896825283/job/136824174491) · [6975P-C comparison](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44896825283/job/136824174416)

| Xeon 8573C, four vCPUs, Docker process median | Official 0.99 s source | 7f7f2ae | 256-byte delimiter positions |
| --- | ---: | ---: | ---: |
| Generated 20M | 1.688 s | 1.642 s | 1.463 s |
| Generated 60M | 5.101 s | 5.082 s | 4.527 s |
| Held-out 60M | 5.080 s | 5.015 s | 4.504 s |

The reader won all 11 adjacent image pairs against 7f7f2ae and the candidate's held-out RSS reached 8,319 MiB. Input accounted for 61.5% of baseline cycle samples and 56.7% for this candidate. On this machine a resident byte sum took 0.131 s, and the separate tokenizer probe took 0.409 s. [Source samples and process counters](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44896825283/job/136824174491)

Instrumenting the exact 4a6d895 implementation on generated seed 23 counted 11,405,432 events and 2,773,071 clicks copied a second time while merging 358,877 linked people. The copies totaled 135,612,592 payload bytes. On seed 47 the same count was 399,014,520 bytes across 1,075,578 people. Only 219 and 699 valid click records belonged to people without surviving events. A competing architecture counts raw aliases, finds the component roots, assigns each alias a disjoint interval, then scatters directly into each final person's timeline. Four storage variants passed an independent whole-table oracle on a 1,537-alias chain and 512 additional components in compact and wide timestamp layouts at one and eight workers.

Four field-boundary implementations independently checked 131,712 original and mutated rows, including abbreviated IDs, short fields, malformed quotes, embedded NULs and all 128 quote masks, followed by 140,000 existing mutation cases. The wider ordinal readers matched 6,144 full byte-stream comparisons for each of the 512-, 1,024- and 2,048-byte tile sizes on the translated AVX2 path. An additional 97,200-row test exercised parser switches at actual input splits. The native workflow exercises AVX-512 VBMI2 when the CPU advertises it.

On another four-vCPU Xeon 6975P-C, adding 16-way revision fingerprint lookup to the 4a6d895 column layout reduced fresh executable medians from 1.0711 to 1.0367 s at 20M, from 3.3311 to 3.2007 s at seed-47 60M and from 3.2789 to 3.2218 s on held-out 60M. Fingerprint matches compare the complete key. The selected image passed all fixtures and references, both 60M populations, the 8,388,673-event single-alias overflow case and eight-worker ThreadSanitizer. Its held-out peak was 8,314 MiB. Automatically switching between two revision layouts increased held-out peak RSS to 9,422 MiB and measured 3.3762 s. [Same-host architecture rotation and Docker image checks](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44899148501/job/136831151195)

### Complete revision keys and raw event storage

The 20M input held 21,505,871 event revisions, counting duplicate and invalid rows. A diagnostic run represented all but 108 revisions in 12 bytes with an exact six-bit-per-character encoding, nine bits supplied by the shard, and a full fallback. At 60M it used 333 exceptions among 63,823,494 revisions. Compared with 16-byte records this saves 255.3 MB of revision payload before the exception vector, allocation rounding and other stages. An independent check covered all boundary timestamps, location overflows and arbitrary ID bytes. The native packet also compares an eight-byte raw event carrying a reversible alias and a whole-second offset. Every purchase, fractional timestamp and alias outside its representable range retains its full value in a side stream. The final metrics and Parquet writer receive the original event word. The 20M and 60M judges compare every output field.

The four-vCPU Intel 8573C fixed-column reader with the direct event-ID decoder ran in 1.2262, 3.9091 and 3.8071 s across 20M, 60M and held-out 60M. The same test measured 1.3075, 4.0837 and 4.0126 s for the production 4a6d895 source. On the 6975P-C a reducer that probes 16 tags at once gave 1.0367, 3.2007 and 3.2218 s versus 1.0711, 3.3311 and 3.2789 s for the same production source. The current packet includes both readers and reducers as controls. [8573C results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44899148501/job/136831151343) · [6975P-C results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44899148501/job/136831151195)

### Resident timeline comparison as the next control

The same candidate won the 20M, 60M and independent 60M Docker comparisons on both Intel models. On the 6975P-C the candidate's fresh-process medians were 0.989, 3.056 and 3.115 s, versus 1.097, 3.399 and 3.354 s for 4a6d895. On the 8573C they were 1.183, 3.686 and 3.686 s, versus 1.317, 4.080 and 4.044 s. It won 22 of 22 alternating Docker pairs and 34 of 34 native executable pairs. The allocator placed 238.4 MB of practice timelines and 712.1 MB of held-out timelines in the retired revision mapping. On the 6975P-C this reduced practice grouping's minor faults from 65,326 to 11,034. The new control has its own exact source manifest. The comparisons test whether placing final person spans directly in retired memory, storing alias counters in stable hash slots, jointly decoding both clocks, and dispatching purchases at the CSV boundary improve those results. [6975P-C results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44901181361/job/136837217984) · [8573C results](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44901181361/job/136837218198)
### Input partition counts and exact singleton detection

On the four-vCPU 6975P-C, scattering directly to the final person removed 399.0 MB of linked timeline copies at 60M, but increased executable medians from 1.0333 to 1.0616 s at 20M, 3.1807 to 3.3293 s at 60M and 3.2050 to 3.3581 s on held-out 60M. Keeping the same variable-width reader and choosing between 1 KiB ordinal decoders measured 1.0170, 3.1148 and 3.1555 s. A separate comparison combines that reader with the released 4db76be allocator. [Same-host process measurements](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44903095305/job/136842931780)

The new comparisons keep the 512 alias partitions fixed and vary the number of revision partitions independently. A bitmap can prove that a particular hash bucket has only one occurrence across all workers. Five versions test collecting that information during a separate ID scan or from the stored ID columns. All collisions remain subject to full ID and earliest-ingest comparisons. The arena trials also measure how many revision pages are touched, the cost of changing block order and releasing unused tails after allocating the final timelines. An independent measurement of the 4db76be callbacks checks physical rows, direct dispatch counts and decoded values while selectively enabling each of the two main event writes.

### Field boundaries, timestamp units and raw alias partitions

On the second Intel runner, the 8573C, the 1 KiB reader took 1.2974, 3.9736 and 4.0474 s at 20M, 60M and held-out 60M, versus 1.3130, 3.9993 and 4.0730 s with the same storage and the prior reader. The direct-person scatter took 1.3624, 4.2023 and 4.2216 s. On the 6975P-C the new reader also improved all three medians. Both machines advertise AVX-512 VBMI2. The next comparison includes the same ordinal reader restricted to its AVX2 implementation, since the hidden scorer's available extensions have yet to be confirmed. [8573C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44903095305/job/136842931713) · [6975P-C](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44903095305/job/136842931780)

The 6975P-C held-out run measured 8,316 MiB and 3.1447 s with separate regular-width revision columns. Twelve-byte compact columns reached 8,074 MiB and 3.2039 s. Eight-byte raw events plus compact columns reached 7,532 MiB and 3.4698 s. The complete compact-key hash slots took 3.3385 s. The regular-width control also led the same-host 20M and first 60M rotations. [All sizes and record representations](https://github.com/mwkhettab/shopping-sessions-cpp/actions/runs/44904823174/job/136848033406)

The field-boundary trials store quote flags with whole offsets or in a separate dense bit stream. An alternative stores only the low byte of each position, records the delimiter count at every 256 bytes, and recovers each true line length. A complete scalar decode handles long lines. Other trials compare coarse raw-alias grouping with a second partition of surviving payloads or compact row indices. The release amd64 disassembly contains a signed division-by-1,000 reciprocal in both tiled event callbacks. The timestamp trials carry ingest seconds into the revision record and test UTC seconds directly for whole-second event timestamps. Epoch-millisecond input retains its exact precision.

