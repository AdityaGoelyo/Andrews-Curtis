// Greedy search (Algorithm 2 of "What makes math problems hard for RL") with substitution moves
// (Definition 2.1) instead of the 14 ac-r2-v1 moves. C++ version of Part 1 of substitution.ipynb.
//
//   ./subs_greedy                                     # every problem in competition/problems/ac.jsonl
//   ./subs_greedy --ids ac-00003,ac-00001 --N 100000  # a few problems, smaller budget
//   ./subs_greedy --limit 50 --threads 4              # first 50 unfinished problems on 4 threads
//
// Options are the same as greedy.cpp: --problems PATH, --N max visited states (default 1000000),
// --cap C max letters per relator (default "none": no cap; "paper" is the paper's rule,
// 2 * max initial relator length + 2, and "xM" scales it, M * max initial relator length + 2),
// --ids a,b,c, --limit K, --threads T, --out-dir DIR (default this folder).
// --radius R (default 1) searches in balls: each expansion takes R moves at once, so only states
// exactly R substitutions from the expanded one are queued (see greedy()); e.g. --radius 2 moves in pairs.
// --mem-gb G caps memory instead of (or as well as) N: G is shared by the threads, so each search
// stops with status "mem_limit" once its own containers (states, letters, tree and queue) reach
// G / threads. Use --N 0 for no state limit, e.g. --N 0 --mem-gb 9 --threads 3 gives each problem
// ~3 GB, roughly 10M states. Fewer threads = deeper searches.
//
// A substitution move (i, j, k1, k2), i in {0, 1}, j in {-1, 1}, replaces r_i by the free reduction
// of rot^k1(r_i) rot^k2(r_{1-i}^j), where rot^k(r) = r[k:] + r[:k]. It is valid only when the last
// letter of rot^k1(r_i) cancels the first letter of rot^k2(r_{1-i}^j). Neighbours are generated in
// the same order as allValidSubOuts in the notebook (i, then j = -1 before 1, then k1, then k2),
// and the first move reaching a state is kept.
//
// Each finished problem is appended to subs_results.jsonl, so a run can be stopped and restarted:
// problems with a result for the same N, cap and per-search memory are skipped. substitution_solutions.txt (shortest
// solution per problem, as lists of [i, j, k1, k2]) is rewritten after every new solve; Part 2 of
// substitution.ipynb translates it into ac-r2-v1 moves and checks each path with the verifier.
#include <omp.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "ac_core.hpp"

using namespace ac;
namespace fs = std::filesystem;

using Sub = std::array<int, 4>;                     // (i, j, k1, k2)

static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct StateHash { size_t operator()(const State& s) const { return hash_state(s); } };

static bool is_trivial(const State& s) {
    return s.r[0].size() == 1 && s.r[1].size() == 1 && (s.r[0][0] & 1) != (s.r[1][0] & 1);
}

// out = free reduction of rot^k1(r) rot^k2(o)
static void sub_word(const Word& r, size_t k1, const Word& o, size_t k2, Word& out) {
    out.clear();
    auto push = [&](u8 c) {
        if (!out.empty() && out.back() == (c ^ 2)) out.pop_back(); else out.push_back(c);
    };
    for (size_t t = 0; t < r.size(); t++) push(r[(k1 + t) % r.size()]);
    for (size_t t = 0; t < o.size(); t++) push(o[(k2 + t) % o.size()]);
}

struct Result {
    std::string id, status;
    std::vector<Sub> subs;
    size_t visited = 0;
    int cap = 0;                                     // 0: no cap
    int radius = 1;
    double secs = 0, gb = 0;                         // gb: estimated memory at the end
    bool mem_limit = false;
};

// Heap bytes glibc malloc uses for a vector of n letters (8-byte header, 16-byte chunks, 32 minimum).
static size_t word_bytes(size_t n) { return std::max<size_t>(32, (n + 8 + 15) & ~(size_t)15); }

// Calls f(move, v) for every substitution move out of u (in allValidSubOuts order) whose new relator
// is non-empty and within cap; v is the resulting state. Stops early once f returns true.
template <class F>
static bool for_each_sub(const State& u, int cap, F&& f) {
    State v;
    Word inv_o, w;
    for (int i = 0; i < 2; i++) {
        const Word& r = u.r[i];
        if (r.empty()) continue;
        for (int j : {-1, 1}) {
            const Word* o = &u.r[1 - i];
            if (j == -1) { binv(*o, inv_o); o = &inv_o; }
            for (size_t k1 = 0; k1 < r.size(); k1++) {
                u8 a = r[(k1 + r.size() - 1) % r.size()];
                for (size_t k2 = 0; k2 < o->size(); k2++) {
                    if ((*o)[k2] != (a ^ 2)) continue;
                    sub_word(r, k1, *o, k2, w);
                    if (w.empty() || (cap && (int)w.size() > cap)) continue;
                    v.r[i] = w;
                    v.r[1 - i] = u.r[1 - i];
                    if (f(Sub{i, j, (int)k1, (int)k2}, v)) return true;
                }
            }
        }
    }
    return false;
}

// Algorithm 2 over balls of substitution moves. Expanding a state u runs a BFS of depth `radius`
// around it, and only the states at depth exactly `radius` (the sphere) become children of u: the
// search advances `radius` moves at a time (radius 1 is the plain one-move search). The BFS
// dedupes within each depth only, so a state at depth 1 that is also 2 moves away is still a child.
// States at depth < radius are not queued or marked visited, only checked for being trivial.
// Returns the substitutions reaching a trivial state, or nothing.
// Stops after N visited states (0: no limit) or once the estimated memory passes max_bytes (0: none).
static bool greedy(const State& start, size_t N, size_t max_bytes, int cap, int radius, std::vector<Sub>& subs,
                   Result& r) {
    struct Node { const State* s; int parent; u32 mv; u32 nmv; };    // moves[mv, mv + nmv) from parent
    std::unordered_map<State, int, StateHash> visited;
    std::vector<Node> nodes;
    std::vector<Sub> moves;
    using Entry = std::tuple<size_t, int, int>;      // (k, l, node index)
    struct Queue : std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> {
        size_t capacity() const { return c.capacity(); }
    } queue;
    size_t state_bytes = 0;                          // map nodes (80 bytes each) + their letters
    size_t ball_bytes = 0;                           // the current expansion's ball and level set
    auto mem = [&] {
        return state_bytes + visited.bucket_count() * 8 + nodes.capacity() * sizeof(Node) +
               moves.capacity() * sizeof(Sub) + queue.capacity() * sizeof(Entry) + ball_bytes;
    };
    auto finish = [&](bool found) {
        r.visited = visited.size();
        r.gb = mem() / 1e9;
        return found;
    };

    auto path_to = [&](int idx) {
        subs.clear();
        for (int i = idx; nodes[i].parent >= 0; i = nodes[i].parent)
            for (u32 t = nodes[i].nmv; t-- > 0;) subs.push_back(moves[nodes[i].mv + t]);
        std::reverse(subs.begin(), subs.end());
    };

    auto it = visited.emplace(start, 0).first;
    nodes.push_back({&it->first, -1, 0, 0});
    if (is_trivial(start)) { subs.clear(); return finish(true); }
    queue.emplace(total_length(start), 0, 0);

    struct Step { State s; int parent; Sub move; };  // the BFS ball around the expanded state
    std::vector<Step> ball;
    std::unordered_set<State, StateHash> level;
    std::vector<Sub> local;
    auto local_path = [&](int b, const Sub& last) {   // moves from the ball's centre to b, then last
        local.assign(1, last);
        for (; ball[b].parent >= 0; b = ball[b].parent) local.push_back(ball[b].move);
        std::reverse(local.begin(), local.end());
    };
    bool found = false, stop = false;                // stop: a limit was hit inside an expansion
    auto over = [&] {
        if (N && visited.size() >= N) return true;
        if (max_bytes && mem() >= max_bytes) { r.mem_limit = true; return true; }
        return false;
    };
    while ((N == 0 || visited.size() < N) && !queue.empty()) {
        if (max_bytes && mem() >= max_bytes) { r.mem_limit = true; break; }
        auto [k, l, ui] = queue.top();
        queue.pop();
        ball.assign(1, {*nodes[ui].s, -1, {}});      // copy: nodes/visited grow below
        ball_bytes = 0;
        size_t lo = 0, hi = 1;
        for (int d = 1; d <= radius && !found && lo < hi; d++, lo = hi, hi = ball.size()) {
            level.clear();
            for (size_t b = lo; b < hi && !found; b++) {
                const State x = ball[b].s;           // copy: ball grows below
                found = for_each_sub(x, cap, [&](const Sub& m, const State& v) {
                    if (d < radius) {                // inside the ball: keep for the next depth
                        if (!level.insert(v).second) return false;
                        ball.push_back({v, (int)b, m});
                        ball_bytes += 2 * (sizeof(Step) + word_bytes(v.r[0].size()) + word_bytes(v.r[1].size()));
                        if ((ball.size() & 1023) == 0 && over()) return stop = true;
                        if (!is_trivial(v)) return false;
                        local_path((int)b, m);
                        path_to(ui);
                        subs.insert(subs.end(), local.begin(), local.end());
                        return true;
                    }
                    auto [pos, fresh] = visited.emplace(v, (int)nodes.size());
                    if (!fresh) return false;
                    state_bytes += 80 + word_bytes(v.r[0].size()) + word_bytes(v.r[1].size());
                    local_path((int)b, m);
                    nodes.push_back({&pos->first, ui, (u32)moves.size(), (u32)local.size()});
                    moves.insert(moves.end(), local.begin(), local.end());
                    if (is_trivial(v)) { path_to((int)nodes.size() - 1); return true; }
                    queue.emplace(total_length(v), l + 1, (int)nodes.size() - 1);
                    return (visited.size() & 1023) == 0 && over() && (stop = true);
                });
            }
        }
        if (stop) return finish(false);
        if (found) return finish(true);
    }
    return finish(false);
}

// Replays the substitutions and checks they are valid and end in a trivial state.
static bool replay_ok(const State& start, const std::vector<Sub>& subs) {
    State s = start;
    Word inv_o, w;
    for (const Sub& m : subs) {
        int i = m[0], j = m[1];
        size_t k1 = m[2], k2 = m[3];
        const Word& r = s.r[i];
        const Word* o = &s.r[1 - i];
        if (j == -1) { binv(*o, inv_o); o = &inv_o; }
        if (k1 >= r.size() || k2 >= o->size()) return false;
        if ((*o)[k2] != (r[(k1 + r.size() - 1) % r.size()] ^ 2)) return false;
        sub_word(r, k1, *o, k2, w);
        s.r[i] = w;
    }
    return is_trivial(s);
}

// ---------------------------------------------------------------------------
// Results file and solutions (same JSON-line style as greedy.cpp)
// ---------------------------------------------------------------------------

static std::string subs_str(const std::vector<Sub>& subs, bool nested) {
    std::ostringstream o;
    o << "[";
    for (size_t t = 0; t < subs.size(); t++) {
        if (t) o << ", ";
        const Sub& m = subs[t];
        if (nested) o << "[" << m[0] << ", " << m[1] << ", " << m[2] << ", " << m[3] << "]";
        else o << m[0] << ", " << m[1] << ", " << m[2] << ", " << m[3];
    }
    o << "]";
    return o.str();
}

static std::string to_json(const Result& r, size_t N, const std::string& cap_arg, const std::string& mem_arg) {
    std::ostringstream o;
    o << "{\"id\": \"" << r.id << "\", \"status\": \"" << r.status << "\"";
    if (r.status == "solved")                        // flat [i, j, k1, k2, i, j, ...] for easy parsing
        o << ", \"length\": " << r.subs.size() << ", \"subs\": " << subs_str(r.subs, false);
    char secs[32], gb[32];
    snprintf(secs, sizeof secs, "%.2f", r.secs);
    snprintf(gb, sizeof gb, "%.2f", r.gb);
    o << ", \"visited\": " << r.visited << ", \"N\": " << N << ", \"cap\": " << r.cap
      << ", \"cap_arg\": \"" << cap_arg << "\"";
    if (r.radius != 1) o << ", \"radius\": " << r.radius;
    if (!mem_arg.empty()) o << ", \"mem_gb\": \"" << mem_arg << "\"";
    o << ", \"gb\": " << gb << ", \"secs\": " << secs << "}";
    return o.str();
}

static std::string json_field(const std::string& line, const std::string& key) {
    std::string pat = "\"" + key + "\": ";
    auto p = line.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    if (line[p] == '"') return line.substr(p + 1, line.find('"', p + 1) - p - 1);
    if (line[p] == '[') return line.substr(p + 1, line.find(']', p) - p - 1);
    return line.substr(p, line.find_first_of(",}", p) - p);
}

struct Prior {
    bool solved = false;
    std::vector<Sub> subs;
    std::set<std::string> tried;                     // "N/cap_arg[/mem_gb][/rR]" runs already finished
};

static std::map<std::string, Prior> read_results(const fs::path& path) {
    std::map<std::string, Prior> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line.back() != '}') continue;    // cut off by an interruption
        Prior& p = out[json_field(line, "id")];
        std::string status = json_field(line, "status");
        if (status == "error") continue;
        std::string mem = json_field(line, "mem_gb");
        std::string rad = json_field(line, "radius");
        p.tried.insert(json_field(line, "N") + "/" + json_field(line, "cap_arg") + (mem.empty() ? "" : "/" + mem) +
                       (rad.empty() ? "" : "/r" + rad));
        if (status != "solved") continue;
        std::vector<int> flat;
        std::istringstream in(json_field(line, "subs"));
        std::string t;
        while (std::getline(in, t, ',')) if (!t.empty()) flat.push_back(std::stoi(t));
        std::vector<Sub> ss;
        for (size_t q = 0; q + 3 < flat.size(); q += 4) ss.push_back({flat[q], flat[q + 1], flat[q + 2], flat[q + 3]});
        if (!p.solved || ss.size() < p.subs.size()) { p.solved = true; p.subs = ss; }
    }
    return out;
}

static int write_solutions(const std::map<std::string, Prior>& results, const fs::path& path) {
    fs::path tmp = path.string() + ".tmp";
    int n = 0;
    {
        std::ofstream f(tmp);
        f << "# substitution greedy search (paper_wmmphfrl/subs_greedy.cpp), moves [i, j, k1, k2].\n";
        for (const auto& [id, p] : results) {
            if (!p.solved) continue;
            f << id << ": " << subs_str(p.subs, true) << "  # " << p.subs.size() << " substitutions\n";
            n++;
        }
    }
    fs::rename(tmp, path);
    return n;
}

// ---------------------------------------------------------------------------

// --cap value -> letters per relator: "none" = 0 (no cap), "paper" = x2,
// "xM" = M * longer initial relator + 2, else a number.
static int cap_for(const std::string& cap_arg, const State& s) {
    if (cap_arg == "none") return 0;
    int longest = (int)std::max(s.r[0].size(), s.r[1].size());
    if (cap_arg == "paper") return 2 * longest + 2;
    if (cap_arg[0] == 'x') return (int)(std::stod(cap_arg.substr(1)) * longest) + 2;
    return std::stoi(cap_arg);
}

int main(int argc, char** argv) {
    fs::path here = fs::absolute(argv[0]).parent_path();
    std::string problems_path = (here / "../competition/problems/ac.jsonl").string();
    std::string out_dir = here.string(), ids, cap_arg = "none";
    size_t N = 1000000;
    int limit = -1, threads = 0, radius = 1;
    double mem_gb = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--problems") problems_path = val();
        else if (a == "--N") N = std::stoull(val());
        else if (a == "--cap") cap_arg = val();
        else if (a == "--ids") ids = val();
        else if (a == "--limit") limit = std::stoi(val());
        else if (a == "--threads") threads = std::stoi(val());
        else if (a == "--mem-gb") mem_gb = std::stod(val());
        else if (a == "--radius") radius = std::stoi(val());
        else if (a == "--out-dir") out_dir = val();
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (radius < 1) { fprintf(stderr, "--radius must be at least 1\n"); return 2; }
    if (threads > 0) omp_set_num_threads(threads);

    std::vector<Problem> problems = load_problems(problems_path);
    std::set<std::string> wanted;
    std::istringstream in(ids);
    for (std::string t; std::getline(in, t, ',');) if (!t.empty()) wanted.insert(t);

    fs::path out = out_dir;
    fs::create_directories(out);
    fs::path results_path = out / "subs_results.jsonl", solutions_path = out / "substitution_solutions.txt";
    auto prior = read_results(results_path);
    // Memory share of one search; part of the run key, so a new share reruns problems.
    double share_gb = mem_gb / omp_get_max_threads();
    std::string mem_arg;
    if (mem_gb > 0) { char b[32]; snprintf(b, sizeof b, "%.2f", share_gb); mem_arg = b; }
    std::string run_key = std::to_string(N) + "/" + cap_arg + (mem_arg.empty() ? "" : "/" + mem_arg) +
                          (radius == 1 ? "" : "/r" + std::to_string(radius));

    std::vector<const Problem*> todo;
    for (const auto& p : problems) {
        if (!wanted.empty() && !wanted.count(p.id)) continue;
        auto it = prior.find(p.id);
        if (it != prior.end() && it->second.tried.count(run_key)) continue;
        todo.push_back(&p);
        if (limit >= 0 && (int)todo.size() >= limit) break;
    }
    printf("%zu problems to run (N = %s, cap = %s, radius %d, %d threads", todo.size(),
           N ? std::to_string(N).c_str() : "no limit", cap_arg.c_str(), radius, omp_get_max_threads());
    if (mem_gb > 0) printf(", %.1f GB = %s GB per search", mem_gb, mem_arg.c_str());
    printf(")\n");

    std::ofstream results(results_path, std::ios::app);
    int solved = 0, done = 0;
    double t_all = now();

#pragma omp parallel for schedule(dynamic, 1)
    for (size_t i = 0; i < todo.size(); i++) {
        const Problem& p = *todo[i];
        Result r;
        r.id = p.id;
        r.cap = cap_for(cap_arg, p.s);
        r.radius = radius;
        double t0 = now();
        bool found = greedy(p.s, N, (size_t)(share_gb * 1e9), r.cap, radius, r.subs, r);
        r.secs = now() - t0;
        r.status = !found ? (r.mem_limit ? "mem_limit" : "not_found") : replay_ok(p.s, r.subs) ? "solved" : "error";

#pragma omp critical
        {
            results << to_json(r, N, cap_arg, mem_arg) << "\n" << std::flush;
            done++;
            if (r.status == "solved") {
                solved++;
                Prior& q = prior[r.id];
                if (!q.solved || r.subs.size() < q.subs.size()) {
                    q.solved = true;
                    q.subs = r.subs;
                    write_solutions(prior, solutions_path);
                }
            }
            printf("[%d/%zu] %s  %-9s", done, todo.size(), r.id.c_str(), r.status.c_str());
            if (r.status == "solved") printf("  %4zu subs", r.subs.size());
            else printf("  %9s", "");
            printf("  visited %8zu  cap %4s  %.2f GB  %.1fs\n", r.visited,
                   r.cap ? std::to_string(r.cap).c_str() : "none", r.gb, r.secs);
            fflush(stdout);
        }
    }

    int n = write_solutions(prior, solutions_path);
    printf("solved %d / %zu this run in %.0fs; %s now has %d solutions\n", solved, todo.size(), now() - t_all,
           solutions_path.c_str(), n);
}
