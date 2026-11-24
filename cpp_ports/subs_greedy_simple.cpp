// subs_greedy_simple.cpp — fast greedy search over substitution moves.
//
// Does exactly what python_scripts/substitution.py and substitution.cpp do (same neighbour
// order, same tie-breaking, so the same solutions), but much faster and with less memory.
//
// Build (the -fopenmp flag runs several presentations at once on all CPU cores;
// leave it out and the program still works, just on one core):
//     g++ -O2 -std=c++17 -fopenmp subs_greedy_simple.cpp -o subs_greedy_simple
//
// Run (same flags as substitution.cpp):
//     ./subs_greedy_simple -p ../ms_presentations.json --max-nodes 100000 --max-len 30
//
// Use fewer cores with:  OMP_NUM_THREADS=4 ./subs_greedy_simple ...
//
// What makes it faster than substitution.cpp (each one is marked "SPEED" below):
//   1. Each state is stored once, as a key of the `parent` map. The parent link and the
//      queue hold a pointer to that key instead of a full copy of the presentation.
//   2. Letters are 1-byte int8_t instead of 4-byte int.
//   3. Neighbours are built straight into one reused scratch presentation, with no
//      rotated / inverted / concatenated temporary copies. Only new states are copied.
//   4. A better hash function (fewer collisions in the map).
//   5. Different presentations run on different cores (OpenMP).

#include "common.hpp"     // subs_to_AC_path, read_relators, to_json

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

// SPEED 2: letters are 1, -1, 2, -2 as in Python, but stored in one byte each.
using Word = std::vector<int8_t>;

// A presentation: R[0] and R[1]. std::array compares like a Python tuple,
// which the priority queue needs for breaking ties (see QueueEntry).
using Presentation = std::array<Word, 2>;

using ac::SubsMove;   // the four numbers (i, j, k1, k2), from common.hpp

// SPEED 4: FNV-1a hash. For every letter: XOR it into h, then multiply by a big prime.
struct PresentationHash {
    size_t operator()(const Presentation& R) const {
        uint64_t h = 14695981039346656037ULL;
        for (const Word& w : R) {
            for (int8_t x : w) {
                h ^= (uint64_t)(x + 3);      // letters become 1..5
                h *= 1099511628211ULL;
            }
            h ^= 7;                          // separator, so ([1],[2,2]) != ([1,2],[2])
            h *= 1099511628211ULL;
        }
        return h;
    }
};

// Python: parent[v] = (u, (i, j, k1, k2)), or (None, None) for the start.
// SPEED 1: `parent` points at u's key inside the map instead of holding a copy of u.
// This is safe because std::unordered_map never moves its elements: a pointer to a key
// stays valid while the map grows.
struct ParentInfo {
    const Presentation* parent = nullptr;   // nullptr = Python's None (the start state)
    SubsMove move = {0, 0, 0, 0};
};

using ParentMap = std::unordered_map<Presentation, ParentInfo, PresentationHash>;

// Python: the heap entries (len(v[0]) + len(v[1]), l, v)
// SPEED 1: `state` points at v's key in the parent map (no copy of v).
struct QueueEntry {
    int total_len;
    int depth;
    const Presentation* state;
};

// Python's heapq compares (total_len, l, v) element by element; we do the same.
// The states are compared only when total_len and depth are both equal.
bool operator>(const QueueEntry& a, const QueueEntry& b) {
    return std::tie(a.total_len, a.depth, *a.state) > std::tie(b.total_len, b.depth, *b.state);
}

// std::greater turns std::priority_queue into a min-heap, like heapq.
using MinHeap = std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>>;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

bool is_trivial(const Presentation& R) {
    return R[0].size() == 1 && R[1].size() == 1 && std::abs(R[0][0]) != std::abs(R[1][0]);
}

// Python: [-x for x in r[::-1]]
Word inverse(const Word& r) {
    Word out(r.size());
    for (size_t t = 0; t < r.size(); ++t) {
        out[t] = -r[r.size() - 1 - t];
    }
    return out;
}

// Appends letter x to out, cancelling it against the last letter if they are inverse.
// Doing this letter by letter is the same as Python's reduce().
void push_reduced(Word& out, int8_t x) {
    if (!out.empty() && out.back() == -x) {
        out.pop_back();
    } else {
        out.push_back(x);
    }
}

// out = reduce(rot(a, k1) + rot(b, k2))
// SPEED 3: reads the letters in rotated order with % instead of building rotated copies,
// and reuses out's memory (clear() keeps the capacity).
void build_sub(const Word& a, int k1, const Word& b, int k2, Word& out) {
    out.clear();
    for (size_t t = 0; t < a.size(); ++t) push_reduced(out, a[(k1 + t) % a.size()]);
    for (size_t t = 0; t < b.size(); ++t) push_reduced(out, b[(k2 + t) % b.size()]);
}

// Conversions between our int8_t presentations and common.hpp's int presentations.
ac::Presentation to_common(const Presentation& R) {
    return {ac::Word(R[0].begin(), R[0].end()), ac::Word(R[1].begin(), R[1].end())};
}

Presentation from_common(const ac::Presentation& R) {
    return {Word(R[0].begin(), R[0].end()), Word(R[1].begin(), R[1].end())};
}

// ---------------------------------------------------------------------------
// The search
// ---------------------------------------------------------------------------

// Everything one search needs, kept together so the helper functions below can share it.
// (Python keeps these as local variables of substitution_search.)
struct Search {
    size_t N;                          // node limit
    std::optional<int> max_len;        // Python's max_len (None = no limit)

    ParentMap parent;
    MinHeap queue;
    Presentation scratch;              // SPEED 3: every neighbour is built in here
    const Presentation* found = nullptr;   // the trivial state, once we reach it

    // Handles one neighbour v (= scratch) of u. This is the body of Python's
    //     for v, i, j, k1, k2 in ac.valid_subs(u):
    // loop. Returns true when v is trivial, which ends the search.
    bool consider(const Presentation& v, const Presentation* u, SubsMove move, int u_depth) {
        if (max_len.has_value() && (int)std::max(v[0].size(), v[1].size()) > *max_len) {
            return false;
        }
        if (is_trivial(v)) {
            auto it = parent.insert_or_assign(v, ParentInfo{u, move}).first;
            found = &it->first;
            return true;
        }
        // Python: if v not in parent: parent[v] = ...
        // try_emplace copies v into the map only when v is new.
        auto [it, is_new] = parent.try_emplace(v, ParentInfo{u, move});
        if (is_new) {
            const Presentation* key = &it->first;   // v's permanent copy, inside the map
            queue.push({(int)(v[0].size() + v[1].size()), u_depth + 1, key});
        }
        return false;
    }

    // Tries every valid substitution move from u, in the same order as Python's valid_subs:
    // i, then k1, then k2, and for each (k1, k2) first j = +1, then j = -1.
    // Returns true when a trivial state was found.
    bool expand(const Presentation* u_ptr, int u_depth) {
        const Presentation& u = *u_ptr;
        for (int i : {0, 1}) {
            const Word& r_i = u[i];
            const Word& r_o = u[1 - i];          // "o" = the other relator
            const Word r_o_inv = inverse(r_o);   // computed once, not once per (k1, k2)
            const int n_i = (int)r_i.size();
            const int n_o = (int)r_o.size();

            scratch[1 - i] = r_o;                // the other relator stays the same

            for (int k1 = 0; k1 < n_i; ++k1) {
                const int last_letter = r_i[(k1 - 1 + n_i) % n_i];   // Python: R[i][k1-1]

                for (int k2 = 0; k2 < n_o; ++k2) {
                    // j = +1.  Python: R[i][k1-1] == -R[1-i][k2]
                    if (last_letter == -r_o[k2]) {
                        build_sub(r_i, k1, r_o, k2, scratch[i]);
                        if (consider(scratch, u_ptr, {i, 1, k1, k2}, u_depth)) return true;
                    }
                    // j = -1.  Python: R[i][k1-1] == R[1-i][-(1+k2)]
                    if (last_letter == r_o[n_o - 1 - k2]) {
                        build_sub(r_i, k1, r_o_inv, k2, scratch[i]);
                        if (consider(scratch, u_ptr, {i, -1, k1, k2}, u_depth)) return true;
                    }
                }
            }
        }
        return false;
    }

    // Python: the inner function path_to(v). Walks from the trivial state back to the
    // start, turning each substitution into AC moves.
    std::vector<int> path_to(const Presentation* v) {
        std::vector<int> path;
        while (parent.at(*v).parent != nullptr) {
            const ParentInfo& info = parent.at(*v);
            const Presentation* u = info.parent;

            ac::ACPath step = ac::subs_to_AC_path(to_common(*v), to_common(*u), info.move);

            // check for correctness (cheap: runs once per step of the solution only)
            if (from_common(step.end) != *u) {
                std::cout << "warning: subs_to_AC_path did not reach the parent state\n";
            }
            path.insert(path.end(), step.moves.begin(), step.moves.end());
            v = u;
        }
        return path;
    }

    // Python: substitution_search(R, N, max_len)
    std::optional<std::vector<int>> run(const Presentation& R) {
        Presentation start = {Word(), Word()};
        for (int r = 0; r < 2; ++r) {               // Python: start = ac.red(R)
            for (int8_t x : R[r]) push_reduced(start[r], x);
        }
        if (is_trivial(start)) {
            return std::vector<int>{};
        }

        auto it = parent.try_emplace(start, ParentInfo{}).first;   // parent = {start: (None, None)}
        queue.push({(int)(start[0].size() + start[1].size()), 0, &it->first});

        while (parent.size() < N && !queue.empty()) {
            const QueueEntry top = queue.top();     // Python: _, l, u = heapq.heappop(queue)
            queue.pop();
            if (expand(top.state, top.depth)) {
                return path_to(found);
            }
        }
        return std::nullopt;
    }
};

// One search per presentation; a fresh Search each time, so memory is freed in between.
std::optional<std::vector<int>> substitution_search(const Presentation& R, size_t N,
                                                    std::optional<int> max_len) {
    Search search;
    search.N = N;
    search.max_len = max_len;
    return search.run(R);
}

// ---------------------------------------------------------------------------
// Command line (same as substitution.cpp)
// ---------------------------------------------------------------------------

struct Args {
    std::string path;
    size_t max_nodes = 100000;
    std::optional<int> max_len;   // empty means None
    std::string out = "substitution_search_results_simple.json";
};

void print_usage() {
    std::cout << "usage: subs_greedy_simple -p PATH [--max-nodes N] [--max-len L] [--out FILE]\n"
                 "  -p, --path    path to the input JSON file (required)\n"
                 "  --max-nodes   node limit (default = 100000)\n"
                 "  --max-len     max length of each relator (default = None)\n"
                 "  --out         output file (default = substitution_search_results_simple.json)\n";
}

Args parse_args(int argc, char** argv) {
    Args args;
    for (int a = 1; a < argc; ++a) {
        std::string flag = argv[a];
        if (a + 1 >= argc) {          // every flag needs a value after it
            print_usage();
            std::exit(1);
        }
        std::string value = argv[++a];

        if (flag == "-p" || flag == "--path")  args.path = value;
        else if (flag == "--max-nodes")        args.max_nodes = std::stoul(value);
        else if (flag == "--max-len")          args.max_len = std::stoi(value);
        else if (flag == "--out")              args.out = value;
        else {
            std::cout << "unknown argument: " << flag << "\n";
            print_usage();
            std::exit(1);
        }
    }
    if (args.path.empty()) {
        print_usage();
        std::exit(1);
    }
    return args;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    const std::string max_len_text = args.max_len ? std::to_string(*args.max_len) : "None";

    const std::vector<ac::Presentation> presentations = ac::read_relators(args.path);
    const int count = (int)presentations.size();

    std::cout << "Attempting " << count << " presentations found in " << args.path
              << "\nmax_len = " << max_len_text << "\nmax_nodes = " << args.max_nodes << "\n";

    // solutions[i] is filled in by whichever core handles presentation i.
    std::vector<std::optional<std::vector<int>>> solutions(count);
    int solved = 0, done = 0;

    // SPEED 5: the loop iterations are shared out between the cores. schedule(dynamic)
    // hands out one presentation at a time, so a core that finishes early takes the next one.
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < count; ++i) {
        solutions[i] = substitution_search(from_common(presentations[i]), args.max_nodes, args.max_len);

        // Only one core at a time may run this block (shared counters and printing).
        #pragma omp critical
        {
            ++done;
            if (solutions[i].has_value()) {
                ++solved;
                std::cout << "solution found for problem " << i;
            } else {
                std::cout << "no solution found for problem " << i;
            }
            std::cout << "   [" << done << "/" << count << ", solved=" << solved << "]" << std::endl;
        }
    }

    // Same output format as substitution.cpp:
    //   "i": [[moves...], "input path", "max-len = ...", "max-nodes = ..."]
    std::ofstream file(args.out);
    file << "{\n";
    bool first = true;
    for (int i = 0; i < count; ++i) {
        if (!solutions[i].has_value()) continue;
        if (!first) file << ",\n";
        first = false;
        file << "    " << ac::to_json(std::to_string(i)) << ": [" << ac::to_json(*solutions[i]) << ", "
             << ac::to_json(args.path) << ", " << ac::to_json("max-len = " + max_len_text) << ", "
             << ac::to_json("max-nodes = " + std::to_string(args.max_nodes)) << "]";
    }
    file << "\n}\n";

    std::cout << "solved " << solved << " / " << count << ", results written to " << args.out << "\n";
    return 0;
}
