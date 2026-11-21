#include "input.hpp"
#include "metrics.hpp"
#include "output.hpp"
#include <climits>
#include <map>
#include <regex>
#include <span>

template <class Generic, class Common, class Rich> struct EventCallbacks {
  Generic generic;
  Common common;
  Rich rich;
  template <class Row> void operator()(const Row &row) { generic(row); }
};
size_t bucket(Key k) { return k.hash() % shards; }
#include "event_layout.hpp"
struct RevisionIndex {
  Key id;
  uint32_t ingest, location;
};
// Extended entries preserve the full ingest second and row position when
// either falls outside the compact index. Invalid revisions have location 0.
struct ExtendedRevision {
  Time seconds;
  uint64_t location;
};
template <class T> using Bins = std::array<std::vector<T>, shards>;
#include "partition_slab.hpp"
#include "rev_columns.hpp"
#include "grouped_control.hpp"
template <class Layout> struct ScanWorker {
  using EventData = typename Layout::E;
  using ClickData = typename Layout::C;
  using Candidate = RevisionIndex;
  std::vector<ExtendedRevision> extended;
  RevisionColumns<256> candidates;
  PartitionSlab<std::pair<Key, EventData>> events;
  PartitionSlab<std::pair<Key, ClickData>> clicks;
  struct Vertex {
    Key name;
    Vertex *parent, *next;
    size_t weight;
    std::span<EventData> events;
    std::span<ClickData> clicks;
  };
  std::vector<std::array<Vertex *, 2>> edges;
  Bins<std::pair<Key, size_t>> endpoints;
  KeyPool pool;
#ifdef SHOPPING_DIAGNOSTICS
  size_t revisions{}, valid_events{}, duplicates{}, valid_clicks{};
#endif
};
struct Catalog {
  struct IndexedColumn {
    Dict<int> catalog;
    Str prefix;
    std::vector<int> dense;
    explicit IndexedColumn(Str p) : prefix(p) {}
    unsigned index(Str key) const {
      // Only one exact byte spelling can occupy each numeric slot. Padded
      // IDs, other prefixes and arbitrary text stay in the string dictionary.
      if (!key.starts_with(prefix))
        return UINT_MAX;
      key.remove_prefix(prefix.size());
      if (key.empty() || key.size() > 9 ||
          (key.size() > 1 && key.front() == '0'))
        return UINT_MAX;
      unsigned i = 0;
      for (unsigned char c : key) {
        unsigned d = unsigned(c) - '0';
        if (d > 9)
          return UINT_MAX;
        i = i * 10 + d;
      }
      return i;
    }
    void build() {
      dense.assign(std::min<size_t>(catalog.size() * 2 + 1, 1000000), -1);
      for (const auto &[key, value] : catalog) {
        auto i = index(key);
        if (i < dense.size())
          dense[i] = value;
      }
    }
    int find(Str key) const {
      unsigned i = index(key);
      if (i < dense.size())
        return dense[i];
      auto it = catalog.find(key);
      return it == catalog.end() ? -1 : it->second;
    }
  };
  IndexedColumn product{"p"}, campaign{"c_"};
  Dict<int> category;
  std::vector<Str> categories{"unknown"}, campaigns{"organic"},
      channels{"organic"};
  template <class F> void read(Input &input, const fs::path &file, F f) {
    auto s = input.map(file);
    auto pos = s.find('\n');
    if (pos != Str::npos)
      rows<2>(s.substr(pos + 1), f);
  }
  Catalog(Input &input, const fs::path &dir) {
    read(input, dir / "products.csv", [&](auto &r) {
      auto [it, added] = category.try_emplace(r[1], categories.size());
      if (added)
        categories.push_back(r[1]);
      product.catalog.emplace(r[0], it->second);
    });
    read(input, dir / "campaigns.csv", [&](auto &r) {
      campaign.catalog.emplace(r[0], campaigns.size());
      campaigns.push_back(r[0]);
      channels.push_back(r[1]);
    });
    product.build();
    campaign.build();
  }
};
struct Window {
  Time start, end;
  explicit Window(Str json) {
    auto get = [&](std::string key) {
      std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
      std::match_results<Str::const_iterator> match;
      if (!std::regex_search(json.begin(), json.end(), match, re))
        throw std::runtime_error("missing " + key);
      return timestamp(match[1].str());
    };
    start = get("window_start");
    end = get("window_end");
    if (start == bad || end <= start)
      throw std::runtime_error("invalid manifest window");
  }
};
template <class Row>
Event validate(const Row &r, Time ingest, const Catalog &catalog, Window w) {
  Event drop{bad, 0, 0};
  if (r[1].empty() || r[5].empty())
    return drop;
  Time ts = timestamp(r[2]);
  if (ts < w.start || ts >= w.end || ingest - ts > day_ms)
    return drop;
  if (r[4] == "view" || r[4] == "cart")
    return {ts, 0, 0};
  if (r[4] != "purchase")
    return drop;
  Time price = number(r[6], 10000000);
  if (price < 1)
    return drop;
  return {ts, int32_t(price), std::max(0, catalog.product.find(r[5]))};
}
using Totals = std::map<std::pair<int, int32_t>, Counts>;
int32_t utc_day(Time ts) { return int32_t(ts / day_ms - (ts % day_ms < 0)); }
template <class T> struct ScratchArray {
  std::vector<T> dense;
  std::unordered_map<int, T> sparse;
  void configure(size_t size) {
    if (size <= 65536)
      dense.resize(size);
  }
  T &operator[](int i) { return dense.empty() ? sparse[i] : dense[i]; }
};
struct ActiveCampaign {
  size_t count{}, slot{};
};
#include "session_bins.hpp"
struct Result {
  SessionBins bins;
#ifdef SHOPPING_DIAGNOSTICS
  size_t people{}, events_seen{}, clicks_seen{}, max_events{}, max_clicks{};
#endif
  ScratchArray<ActiveCampaign> activity;
  ScratchArray<int64_t> spend;
  std::vector<int> active, purchased;
  std::unique_ptr<UserFile> user_file;
  Totals sparse;
  std::vector<Counts> table;
  int32_t first_day{};
  size_t campaigns{};
  void configure(Window w, const Catalog &cat, unsigned threads) {
    bins.configure(w);
    size_t count = cat.campaigns.size();
    activity.configure(count);
    spend.configure(cat.categories.size());
    first_day = utc_day(w.start);
    campaigns = count;
    size_t days = size_t(utc_day(w.end - 1) - first_day) + 1;
    if (days <= 4'000'000 / threads / count)
      table.resize(days * count);
  }
  Counts &at(int campaign, int32_t day) {
    if (table.empty())
      return sparse[{campaign, day}];
    return table[size_t(day - first_day) * campaigns + campaign];
  }
};

template <class Layout>
void summarize(Key user, std::span<typename Layout::E> events,
               std::span<typename Layout::C> clicks, const Catalog &cat,
               Result &result, Layout layout) {
  if (events.empty())
    return;
#ifdef SHOPPING_DIAGNOSTICS
  ++result.people;
  result.events_seen += events.size();
  result.clicks_seen += clicks.size();
  result.max_events = std::max(result.max_events, events.size());
  result.max_clicks = std::max(result.max_clicks, clicks.size());
#endif
  const bool use_bins = !result.bins.cells.empty() && events.size() >= 32;
  {
    MEASURE(sorting);
    if (!use_bins)
      std::sort(events.begin(), events.end(),
                [&](auto a, auto b) { return layout.less(a, b); });
    std::sort(clicks.begin(), clicks.end(), [](const auto &a, const auto &b) {
      return a.ts == b.ts ? b.id.user_less(a.id, 'k') : a.ts < b.ts;
    });
  }
  size_t left = 0, right = 0;
  UserDay daily{user, 0, 0, 0, 0, {}};
  auto finish_day = [&] {
    MEASURE(aggregation);
    int64_t best = 0;
    for (int k : result.purchased) {
      auto &v = result.spend[k];
      if (v > best || (v == best && cat.categories[k] < *daily.category)) {
        best = v;
        daily.category = &cat.categories[k];
      }
      v = 0;
    }
    result.user_file->append(daily);
    result.purchased.clear();
  };
  auto begin_session = [&](Time start) {
    auto day = utc_day(start);
    if (daily.sessions && day != daily.day) {
      finish_day();
      daily = {user, day, 0, 0, 0, {}};
    }
    daily.day = day;
  };
  auto spend = [&](int category, int64_t price) {
    auto &value = result.spend[category];
    if (!value)
      result.purchased.push_back(category);
    value += price;
  };
  auto end_session = [&](Time start, int64_t count, int64_t revenue) {
    auto day = daily.day;
    ++daily.sessions;
    daily.events += count;
    daily.revenue += revenue;
    {
      MEASURE(attribution);
      while (right < clicks.size() &&
             layout.click_time(clicks[right]) <= start) {
        int campaign = clicks[right++].campaign;
        auto &state = result.activity[campaign];
        if (state.count++ == 0) {
          state.slot = result.active.size();
          result.active.push_back(campaign);
        }
      }
      while (left < right && layout.click_time(clicks[left]) < start - reach) {
        auto &state = result.activity[clicks[left++].campaign];
        if (--state.count == 0) {
          int moved = result.active.back();
          result.active[state.slot] = moved;
          result.activity[moved].slot = state.slot;
          result.active.pop_back();
        }
      }
    }
    MEASURE(aggregation);
    int winner = left < right ? clicks[right - 1].campaign : 0;
    auto &counts = result.at(winner, day);
    ++counts.sessions;
    counts.purchases += revenue != 0;
    counts.revenue += revenue;
    for (int campaign : result.active)
      ++result.at(campaign, day).assists;
  };
  if (use_bins) {
    MEASURE(session);
    result.bins.scan(events, begin_session, spend, end_session, layout);
  } else {
    size_t i = 0;
    while (i < events.size()) {
      Time start = layout.time(events[i]);
      begin_session(start);
      size_t first = i;
      int64_t revenue = 0;
      {
        MEASURE(session);
        do {
          auto e = layout.unpack(events[i++]);
          if (e.price) {
            spend(e.category, e.price);
            revenue += e.price;
          }
        } while (i < events.size() &&
                 layout.time(events[i]) - layout.time(events[i - 1]) <=
                     session_gap);
      }
      end_session(start, i - first, revenue);
    }
  }
  finish_day();
  for (int campaign : result.active)
    result.activity[campaign].count = 0;
  result.active.clear();
}
struct Timer {
  using Clock = std::chrono::steady_clock;
  Clock::time_point last = Clock::now();
  void mark(const char *name) {
    auto now = Clock::now();
    if (std::getenv("PROFILE"))
      std::cerr << name << " "
                << std::chrono::duration<double>(now - last).count() << " s\n";
    last = now;
  }
};
template <class Layout>
void resolve_revisions(std::vector<ScanWorker<Layout>> &workers,
                       unsigned threads, Time anchor) {
  using Candidate = typename ScanWorker<Layout>::Candidate;
  parallel(shards, threads, [&](size_t b, [[maybe_unused]] unsigned t) {
    size_t count = 0;
    for (auto &w : workers)
      count += w.candidates[b].size();
    // The low hash bits selected this shard. Use the remaining bits
    // for open addressing and compare the original bytes on a hash match.
    size_t capacity = 1;
    while (capacity < count + count / 2)
      capacity *= 2;
    MEASURE(dedup);
    auto resolve = [&]<class Value>() {
      ControlIndex<Value> best(capacity);
      auto seconds = [&](const Candidate &r, unsigned owner) {
        return r.ingest & 0x80000000u
                   ? workers[owner].extended[r.location].seconds
                   : Time(r.ingest) - Time(1u << 30) + anchor;
      };
      auto discard = [&](const Candidate &r, unsigned owner) {
        auto &w = workers[owner];
        uint64_t location = r.ingest & 0x80000000u
                                ? w.extended[r.location].location
                                : r.location;
        if (location) {
          --location;
          w.events[location % shards][location / shards].first.bits = 0;
        }
      };
      for (unsigned owner = 0; owner < workers.size(); ++owner) {
        auto &refs = workers[owner].candidates[b];
        // Reserve zero for an empty slot. Six low bits identify one of at most
        // 64 input workers, and the remaining bits locate its immutable
        // revision.
        for (size_t i = 0; i < refs.size(); ++i) {
          Key id = refs.key(i);
          auto hash = id.hash();
          auto location = 1 + (uint64_t(i) << 6) + owner;
          auto [slot, found] = best.locate(hash, [&](uint64_t code) {
            auto prior = code - 1;
            return workers[prior & 63].candidates[b].key(prior >> 6) == id;
          });
          if (!found) {
            best.insert(slot, hash, location);
            continue;
          }
          auto prior = best.values[slot] - 1;
          auto old_owner = unsigned(prior & 63);
          const auto r = refs[i];
          const auto old = workers[old_owner].candidates[b][prior >> 6];
#ifdef SHOPPING_DIAGNOSTICS
          ++workers[t].duplicates;
#endif
          bool before = (r.ingest | old.ingest) & 0x80000000u
                            ? seconds(r, owner) < seconds(old, old_owner)
                            : r.ingest < old.ingest;
          if (before) {
            discard(old, old_owner);
            best.values[slot] = location;
          } else
            discard(r, owner);
        }
      }
    };
    // One location contains the revision's offset and its six-bit worker.
    // Bound the maximum encoded value using the actual shard populations.
    bool small = std::all_of(workers.begin(), workers.end(), [&](auto &w) {
      return w.candidates[b].size() <= (UINT32_MAX >> 6);
    });
    if (small)
      resolve.template operator()<uint32_t>();
    else
      resolve.template operator()<uint64_t>();
    for (auto &w : workers)
      w.candidates[b].release();
  });
  // Let the raw grouping pass reclaim the mapped revision pages.
}
#define SHOP_REUSE_REGION 1
#define SHOP_LEND_REGION 1
#include "timeline_reuse.hpp"
template <class Layout>
void process_people(std::vector<ScanWorker<Layout>> &workers,
                    std::vector<Result> &results, const Catalog &cat,
                    unsigned threads, Timer &timer, Layout layout) {
  using Worker = ScanWorker<Layout>;
  using Vertex = typename Worker::Vertex;
  using E = typename Layout::E;
  using C = typename Layout::C;
  std::vector<TimelineRegion> pool(threads);
  for (unsigned i = 0; i < workers.size(); ++i) {
    auto &w = workers[i];
    auto &revision = w.candidates.slab;
    if (revision.region != MAP_FAILED) {
      pool[i].base = static_cast<std::byte *>(revision.region);
      pool[i].bytes = revision.bytes;
    }
  }
  struct Store {
    TimelineArray<E> events;
    TimelineArray<C> clicks;
  };
  std::array<Store, shards> storage;
  std::array<std::vector<Vertex>, shards> vertices;
  parallel(shards, threads, [&](size_t b, unsigned t) {
    MEASURE(grouping);
    struct Group {
      Key key;
      size_t event_end{}, click_end{};
      size_t vertex{};
    };
    struct Slot {
      Key key;
      size_t group;
    };
    std::vector<Group> groups;
    size_t n = 0;
    for (auto &w : workers)
      n += w.events[b].size() + w.clicks[b].size();
    size_t initial = std::bit_ceil(std::clamp<size_t>(n / 12, 128, 65536));
    std::vector<Slot> slots(initial);
    auto locate = [&](Key k) {
      size_t at = (k.hash() / shards) & (slots.size() - 1);
      while (slots[at].key.bits && !(slots[at].key == k))
        at = (at + 1) & (slots.size() - 1);
      return at;
    };
    auto intern = [&](Key k) {
      if (groups.size() * 10 >= slots.size() * 7) {
        auto old = std::move(slots);
        slots.clear();
        slots.resize(old.size() * 2);
        for (auto &x : old)
          if (x.key.bits)
            slots[locate(x.key)] = x;
      }
      auto &x = slots[locate(k)];
      if (!x.key.bits) {
        x = {k, groups.size()};
        groups.push_back({k});
      }
      return x.group;
    };
    // First count each alias, then scatter into disjoint contiguous spans.
    // A linked person may own spans in several of these raw-user shards.
    auto &store = storage[b];
    size_t event_count = 0, click_count = 0;
    for (auto &w : workers) {
      for (auto part : w.events[b].spans())
        for (auto &[u, e] : part) {
          if (!u.bits) {
            u.bits = UINTPTR_MAX;
            continue;
          }
          u.bits = intern(Key{u.bits});
          ++groups[u.bits].event_end;
          ++event_count;
        }
      for (auto part : w.clicks[b].spans())
        for (auto &[u, c] : part) {
          u.bits = intern(Key{u.bits});
          ++groups[u.bits].click_end;
        }
      click_count += w.clicks[b].size();
    }
    auto &nodes = vertices[b];
    size_t nv = 0;
    for (auto &w : workers)
      for (auto &[key, loc] : w.endpoints[b]) {
        auto &g = groups[intern(key)];
        if (!g.vertex)
          g.vertex = ++nv;
        key.bits = g.vertex - 1;
      }
    nodes.resize(nv);
    for (auto &w : workers) {
      for (auto &[key, loc] : w.endpoints[b])
        w.edges[loc / 2][loc % 2] = &nodes[key.bits];
      typename std::remove_reference_t<decltype(w.endpoints)>::value_type()
          .swap(w.endpoints[b]);
    }
    store.events.prepare(event_count, pool, t);
    store.clicks.prepare(click_count, pool, t);
    size_t event_offset = 0, click_offset = 0;
    for (auto &g : groups) {
      auto events_in_group = g.event_end, clicks_in_group = g.click_end;
      g.event_end = event_offset;
      g.click_end = click_offset;
      event_offset += events_in_group;
      click_offset += clicks_in_group;
    }
    for (auto &w : workers) {
      for (auto part : w.events[b].spans())
        for (auto &[u, e] : part)
          if (u.bits != UINTPTR_MAX)
            store.events[groups[u.bits].event_end++] = e;
      for (auto part : w.clicks[b].spans())
        for (auto &[u, c] : part)
          store.clicks[groups[u.bits].click_end++] = c;
      w.events[b].release();
      w.clicks[b].release();
    }
    event_offset = click_offset = 0;
    for (auto &g : groups) {
      auto ev =
          store.events.view().subspan(event_offset, g.event_end - event_offset);
      auto cl =
          store.clicks.view().subspan(click_offset, g.click_end - click_offset);
      if (g.vertex) {
        auto &v = nodes[g.vertex - 1];
        v = {g.key, &v, &v, 1, ev, cl};
      } else
        summarize(g.key, ev, cl, cat, results[t], layout);
      event_offset = g.event_end;
      click_offset = g.click_end;
    }
  });
  for (auto &w : workers) {
    w.events.release();
    w.clicks.release();
  }
  timer.mark("raw user grouping, link endpoints and singleton sessions");
  if (std::getenv("SHOP_RECYCLE_TRACE")) {
    size_t assigned = 0, heap = 0;
    for (auto &store : storage) {
      auto count = [&](auto &entries) {
        size_t n = entries.size * sizeof(*entries.data);
        assigned += n;
        if (entries.owned)
          heap += n;
      };
      count(store.events);
      count(store.clicks);
    }
    std::cerr << "SHOP_TIMELINE_REUSE " << assigned - heap << " " << heap
              << "\n";
  }

  // Roots move strictly downward in bytewise order. Path splitting keeps long
  // chains short even while other edge workers are linking components.
  auto root = [](Vertex *v) {
    for (;;) {
      std::atomic_ref<Vertex *> ref(v->parent);
      auto *p = ref.load(std::memory_order_relaxed);
      if (v == p)
        return v;
      auto *g =
          std::atomic_ref<Vertex *>(p->parent).load(std::memory_order_relaxed);
      ref.compare_exchange_weak(p, g, std::memory_order_relaxed);
      v = p;
    }
  };
  using Edge = std::array<Vertex *, 2>;
  std::vector<std::span<Edge>> tasks;
  for (auto &w : workers) {
    std::span<Edge> edges(w.edges);
    while (!edges.empty()) {
      auto n = std::min<size_t>(edges.size(), 4096);
      tasks.push_back(edges.first(n));
      edges = edges.subspan(n);
    }
  }
  parallel(tasks.size(), threads, [&](size_t i, unsigned) {
    for (auto edge : tasks[i]) {
      auto *x = edge[0], *y = edge[1];
      for (;;) {
        x = root(x);
        y = root(y);
        if (x == y)
          break;
        if (x->name.user_less(y->name))
          std::swap(x, y);
        auto *expected = x;
        if (std::atomic_ref<Vertex *>(x->parent).compare_exchange_weak(
                expected, y, std::memory_order_relaxed))
          break;
      }
    }
  });
  // After the union barrier append every other alias to the root's list.
  parallel(shards, threads, [&](size_t b, unsigned) {
    for (auto &v : vertices[b]) {
      auto *r = root(&v);
      if (r == &v)
        continue;
      auto &head = r->next;
      v.next = std::atomic_ref<Vertex *>(head).exchange(
          &v, std::memory_order_relaxed);
    }
  });
  timer.mark("linked components");
  parallel(shards, threads, [&](size_t b, unsigned t) {
    std::vector<E> events_buffer;
    std::vector<C> click_buffer;
    for (auto &head : vertices[b]) {
      if (head.parent != &head)
        continue;
      std::span<E> ev;
      std::span<C> cl;
      size_t event_count = 0, click_count = 0;
      Vertex *v = &head;
      do {
        if (!v->events.empty()) {
          ev = v->events;
          event_count += ev.size();
        }
        if (!v->clicks.empty()) {
          cl = v->clicks;
          click_count += cl.size();
        }
        v = v->next;
      } while (v != &head);
      if (!event_count)
        continue;
      if (event_count > ev.size() || click_count > cl.size()) {
        bool merge_e = event_count > ev.size(),
             merge_c = click_count > cl.size();
        events_buffer.clear();
        click_buffer.clear();
        if (merge_e)
          events_buffer.reserve(event_count);
        if (merge_c)
          click_buffer.reserve(click_count);
        do {
          if (merge_e)
            events_buffer.insert(events_buffer.end(), v->events.begin(),
                                 v->events.end());
          if (merge_c)
            click_buffer.insert(click_buffer.end(), v->clicks.begin(),
                                v->clicks.end());
          v = v->next;
        } while (v != &head);
        if (merge_e)
          ev = events_buffer;
        if (merge_c)
          cl = click_buffer;
      }
      summarize(head.name, ev, cl, cat, results[t], layout);
    }
  });
  timer.mark("linked-person sessions, attribution and aggregation");
}

template <class Layout>
void run_layout(const fs::path &data, const fs::path &output, unsigned threads,
                Input &input, Window window, Catalog &cat, Timer &timer,
                Layout layout) {
  using Candidate = RevisionIndex;
  using Worker = ScanWorker<Layout>;
  auto links = input.parts(data / "links");
  timer.mark("catalogs and link mapping");
  std::vector<Worker> workers(threads);
  auto events = input.parts(data / "events");
  timer.mark("map event files");
  auto clicks = input.parts(data / "clicks");
  // Sample physical line lengths only to size initial buffers. Skewed shards
  // and records of any width can grow independently.
  auto estimate = [](const std::vector<InputChunk> &chunks) {
    size_t total = 0, sampled = 0, count = 0;
    for (auto &c : chunks)
      total += c.size();
    size_t samples = std::min<size_t>(chunks.size(), 32);
    for (size_t i = 0; i < samples; ++i) {
      auto &c = chunks[i * chunks.size() / samples];
      auto bytes = c.file.substr(c.begin, std::min<size_t>(c.size(), 8192));
      sampled += bytes.size();
      count += std::count(bytes.begin(), bytes.end(), '\n');
    }
    return sampled ? size_t(double(total) * count / sampled) : 0;
  };
  auto event_count = estimate(events), click_count = estimate(clicks);
  auto hint = [&](size_t count, size_t row_bytes, size_t budget) {
    return std::min(count + count / 8, budget / row_bytes) / threads / shards;
  };
  auto rev_hint = hint(event_count, sizeof(Candidate), size_t(1536) << 20);
  auto event_hint =
      hint(event_count, sizeof(std::pair<Key, typename Layout::E>),
           size_t(1536) << 20);
  auto click_hint =
      hint(click_count, sizeof(std::pair<Key, typename Layout::C>),
           size_t(768) << 20);
  for (auto &w : workers) {
    w.candidates.reserve(rev_hint);
    w.events.reserve(event_hint);
    w.clicks.reserve(click_hint);
  }
  const Time anchor = window.start / 1000;
  parallel(links.size() + events.size() + clicks.size(), threads,
           [&](size_t job, unsigned t) {
             if (job < links.size()) {
               auto &w = workers[t];
               rows<2>(links[job].body(), [&](auto &r) {
                 if (r[0].empty() || r[1].empty())
                   return;
                 size_t index = w.edges.size() * 2;
                 w.edges.push_back({});
                 for (unsigned side = 0; side < 2; ++side) {
                   Key key = w.pool.encode(r[side], 'u');
                   w.endpoints[key.hash() % shards].emplace_back(key,
                                                                 index + side);
                 }
               });
               return;
             }
             job -= links.size();
             if (job >= events.size()) {
               rows<4>(clicks[job - events.size()].body(), [&](auto &r) {
                 if (r[0].empty() || r[1].empty())
                   return;
                 Time ts = timestamp(r[3]);
                 auto c = cat.campaign.find(r[2]);
                 if (ts < window.start - reach || ts >= window.end || c < 0)
                   return;
                 auto alias = workers[t].pool.encode(r[1], 'u');
#ifdef SHOPPING_DIAGNOSTICS
                 ++workers[t].valid_clicks;
#endif
                 workers[t].clicks[alias.hash() % shards].emplace_back(
                     Key{alias.bits},
                     layout.click(workers[t].pool.encode(r[0], 'k'), ts, c));
               });
               return;
             }
             auto put = [&](Key id, Key user, Event e, Time ingest) {
               auto &w = workers[t];
               uint64_t location = 0;
               if (user.bits) {
                 size_t b = user.hash() % shards;
                 auto &rows = w.events[b];
                 location = uint64_t(rows.size()) * shards + b + 1;
                 rows.emplace_back(Key{user.bits}, layout.pack(e));
               }
               Time seconds = ingest / 1000;
               Time relative = seconds - anchor;
               // Bias signed seconds into 31 bits. The high bit selects the
               // overflow vector.
               uint64_t when = uint64_t(relative) + (uint64_t{1} << 30);
               Candidate ref{id, 0, 0};
               if (when < 0x80000000ULL && location <= UINT32_MAX) {
                 ref.ingest = uint32_t(when);
                 ref.location = uint32_t(location);
               } else {
                 if (w.extended.size() > UINT32_MAX)
                   throw std::length_error("revision index exhausted");
                 ref.ingest = 0x80000000u;
                 ref.location = uint32_t(w.extended.size());
                 w.extended.push_back({seconds, location});
               }
               w.candidates[bucket(id)].push_back(ref);
             };
             auto generic_event = [&](auto const &r) __attribute__((noinline)) {
               if (r[0].empty())
                 return;
               auto &w = workers[t];
               Time ingest = ingest_time(r[3]);
               Key id = w.pool.encode(r[0], 'e');
               Event e = validate(r, ingest, cat, window);
#ifdef SHOPPING_DIAGNOSTICS
               ++w.revisions;
               w.valid_events += e.ts != bad;
#endif
               Key user = e.ts == bad ? Key{} : w.pool.encode(r[1], 'u');
               put(id, user, e, ingest);
             };
             auto common_event = [&](const char *id, unsigned id_width,
                                     const char *user, unsigned user_width,
                                     Str event_time, Str arrival) {
               if constexpr (sizeof(uintptr_t) != 8 ||
                             std::endian::native != std::endian::little)
                 return false;
               if (id[0] != 'e' || user[0] != 'u')
                 return false;
               auto load = [](const char *p, unsigned n) {
                 const uint64_t mask = UINT64_MAX >> (8 * (9 - n));
                 return little_word(p + 1) & mask;
               };
               auto event_word = load(id, id_width),
                    user_word = load(user, user_width);
               if ((event_word | user_word) >> 63 || !id[id_width - 1] ||
                   !user[user_width - 1])
                 return false;
               Time ingest = ingest_time(arrival), ts = timestamp(event_time);
               bool good = ts >= window.start && ts < window.end &&
                           ingest - ts <= day_ms;
#ifdef SHOPPING_DIAGNOSTICS
               ++workers[t].revisions;
               workers[t].valid_events += good;
#endif
               put(Key{uintptr_t(permute_key(event_word) << 1)},
                   good ? Key{uintptr_t(permute_key(user_word) << 1)} : Key{},
                   Event{good ? ts : bad, 0, 0}, ingest);
               return true;
             };
             auto rich_event = [&](const char *id, unsigned id_width,
                                   const char *user, unsigned user_width,
                                   Str event_time, Str arrival, Str type,
                                   Str product, Str price) {
               if constexpr (sizeof(uintptr_t) != 8 ||
                             std::endian::native != std::endian::little)
                 return false;
               if (id[0] != 'e' || user[0] != 'u')
                 return false;
               auto load = [](const char *p, unsigned n) {
                 const uint64_t mask = UINT64_MAX >> (8 * (9 - n));
                 return little_word(p + 1) & mask;
               };
               auto event_word = load(id, id_width),
                    user_word = load(user, user_width);
               if ((event_word | user_word) >> 63 || !id[id_width - 1] ||
                   !user[user_width - 1])
                 return false;
               Time ingest = ingest_time(arrival);
               bool purchase = type == "purchase";
               Time cents = purchase ? number(price, 10000000) : 0;
               bool useful =
                   !product.empty() && ((purchase && cents >= 1) ||
                                        type == "cart" || type == "view");
               Time ts = useful ? timestamp(event_time) : bad;
               bool good = useful && ts >= window.start && ts < window.end &&
                           ingest - ts <= day_ms;
#ifdef SHOPPING_DIAGNOSTICS
               ++workers[t].revisions;
               workers[t].valid_events += good;
#endif
               put(Key{uintptr_t(permute_key(event_word) << 1)},
                   good ? Key{uintptr_t(permute_key(user_word) << 1)} : Key{},
                   Event{good ? ts : bad, good && purchase ? int32_t(cents) : 0,
                         good && purchase
                             ? std::max(0, cat.product.find(product))
                             : 0},
                   ingest);
               return true;
             };
             auto callbacks =
                 EventCallbacks{generic_event, common_event, rich_event};
             rows<7>(events[job].body(), callbacks);
           });
  timer.mark("concurrent input, raw user partition and revision index");
#ifdef SHOPPING_DIAGNOSTICS
  size_t revision_count = 0, revision_capacity = 0;
  for (const auto &worker : workers)
    for (const auto &bin : worker.candidates) {
      revision_count += bin.size();
      revision_capacity += bin.capacity();
    }
  std::cerr << "revision storage " << revision_count * sizeof(Candidate)
            << " used bytes, " << revision_capacity * sizeof(Candidate)
            << " capacity bytes\n";
#endif
  resolve_revisions(workers, threads, anchor);
  timer.mark("global event dedup and loser removal");
  std::vector<Result> results(threads);
  fs::create_directories(output / "user_daily");
  fs::create_directories(output / "campaign_daily");
  parallel(results.size(), threads, [&](size_t i, unsigned) {
    auto &result = results[i];
    result.configure(window, cat, threads);
    result.user_file = std::make_unique<UserFile>(
        output / "user_daily" / ("part-" + std::to_string(i) + ".parquet"));
  });
  process_people(workers, results, cat, threads, timer, layout);
  for (auto &w : workers)
    w.candidates.release();
  parallel(results.size(), threads,
           [&](size_t i, unsigned) { results[i].user_file->close(); });
  std::vector<CampaignDay> campaigns;
  auto append = [&](size_t id, int32_t day, const Counts &counts) {
    campaigns.push_back({cat.campaigns[id], cat.channels[id], day, counts});
  };
  const auto &first = results.front();
  if (first.table.empty()) {
    Totals merged;
    for (const auto &part : results)
      for (const auto &[key, counts] : part.sparse)
        merged[key].add(counts);
    for (const auto &[key, counts] : merged)
      append(key.first, key.second, counts);
  } else {
    for (size_t start = 0; start < first.table.size();
         start += first.campaigns) {
      const int32_t day = first.first_day + int32_t(start / first.campaigns);
      for (size_t id = 0; id < first.campaigns; ++id) {
        Counts counts;
        for (const auto &part : results)
          counts.add(part.table[start + id]);
        if (counts.sessions || counts.assists)
          append(id, day, counts);
      }
    }
  }
  write_campaigns(output / "campaign_daily" / "part-0.parquet", campaigns);
  timer.mark("merge and Parquet output");
#ifdef SHOPPING_DIAGNOSTICS
  size_t revisions = 0, valid = 0, duplicates = 0, clicks_kept = 0, people = 0;
  size_t events_seen = 0, clicks_seen = 0, max_events = 0, max_clicks = 0;
  for (auto &w : workers) {
    revisions += w.revisions;
    valid += w.valid_events;
    duplicates += w.duplicates;
    clicks_kept += w.valid_clicks;
  }
  for (auto &r : results) {
    people += r.people;
    events_seen += r.events_seen;
    clicks_seen += r.clicks_seen;
    max_events = std::max(max_events, r.max_events);
    max_clicks = std::max(max_clicks, r.max_clicks);
  }
  std::cerr << "revisions " << revisions << ", valid before dedup " << valid
            << ", duplicate revisions " << duplicates << ", valid clicks "
            << clicks_kept << ", active people " << people
            << ", grouped events " << events_seen << ", grouped clicks "
            << clicks_seen << ", largest timelines " << max_events
            << " events / " << max_clicks << " clicks\n";
  metrics::report();
#endif
}
void run(const fs::path &data, const fs::path &output, unsigned threads) {
  Timer timer;
  Input input;
  Window window(input.map(data / "manifest.json"));
  Catalog cat(input, data);
  uint64_t width = uint64_t(window.end) - uint64_t(window.start);
  if (width <= UINT32_MAX - uint64_t(reach) && cat.categories.size() <= 256)
    run_layout(data, output, threads, input, window, cat, timer,
               WordLayout{window.start});
  else
    run_layout(data, output, threads, input, window, cat, timer, WideLayout{});
}
int main(int argc, char **argv) {
  auto env = [](const char *key, const char *fallback) {
    auto s = std::getenv(key);
    return s ? s : fallback;
  };
  try {
    run(argc > 1 ? argv[1] : env("DATA_DIR", "/data"),
        argc > 2 ? argv[2] : env("OUTPUT_DIR", "/output"),
        std::clamp(std::stoi(env("NCPU", "8")), 1, 64));
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
