// substitution.cpp — C++ port of python_scripts/substitution.py
//
// Build:
//     g++ -O2 -std=c++17 substitution.cpp -o substitution
//
// Run (same flags as the Python script):
//     ./substitution -p ../ms_presentations.json --max-nodes 100000 --max-len 30
//
// Differences from the Python version:
//   * Default output file is substitution_search_results_cpp.json, so the
//     Python results are never overwritten.
//   * The output file is replaced, not merged with an existing one.
//   * A plain progress line replaces the tqdm progress bar.

#include "common.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

using ac::Presentation;
using ac::SubsMove;
using ac::Word;

// ---------------------------------------------------------------------------
// Data structures for the search
// ---------------------------------------------------------------------------

// Python's dict needs keys it can hash; for a C++ unordered_map we have to say
// how to hash a Presentation. This mixes every letter into one number.
struct PresentationHash {
    size_t operator()(const Presentation& R) const {
        size_t h = 0;
        for (const Word& w : R) {
            for (int x : w) {
                h = h * 31 + (size_t)(x + 3);
            }
            h = h * 31 + 7;   // separator so ([1],[2,2]) and ([1,2],[2]) hash differently
        }
        return h;
    }
};

// Python: parent[v] = (u, (i, j, k1, k2)), or (None, None) for the start.
struct ParentInfo {
    bool has_parent;     // false only for the start node (Python's None)
    Presentation parent; // u
    SubsMove move;       // (i, j, k1, k2)
};

using ParentMap = std::unordered_map<Presentation, ParentInfo, PresentationHash>;

// Python: the heap entries (len(v[0]) + len(v[1]), l, v)
struct QueueEntry {
    int total_len;
    int depth;
    Presentation state;
};

// Python's heapq compares tuples element by element; std::tie does the same.
bool operator>(const QueueEntry& a, const QueueEntry& b) {
    return std::tie(a.total_len, a.depth, a.state) > std::tie(b.total_len, b.depth, b.state);
}

// std::priority_queue is a max-heap by default; std::greater makes it a
// min-heap, like heapq.
using MinHeap = std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>>;

// ---------------------------------------------------------------------------
// The search
// ---------------------------------------------------------------------------

// Python: the inner function path_to(v)
std::vector<int> path_to(Presentation v, const ParentMap& parent) {
    std::vector<int> path;
    while (parent.at(v).has_parent) {
        const ParentInfo& info = parent.at(v);
        const Presentation u = info.parent;

        ac::ACPath step = ac::subs_to_AC_path(v, u, info.move);

        // check for correctness
        if (step.end != u) {
            std::cout << "warning: subs_to_AC_path did not reach the parent state\n";
        }
        path.insert(path.end(), step.moves.begin(), step.moves.end());
        v = u;
    }
    return path;
}

// Returns the AC moves on success, or std::nullopt (Python's None) on failure.
// max_len is optional, like Python's max_len=None.
std::optional<std::vector<int>> substitution_search(const Presentation& R, size_t N,
                                                    std::optional<int> max_len) {
    const Presentation start = ac::red(R);

    if (ac::is_trivial(start)) {
        return std::vector<int>{};
    }

    MinHeap queue;
    queue.push({(int)(start[0].size() + start[1].size()), 0, start});

    ParentMap parent;
    parent[start] = {false, {}, {}};

    while (parent.size() < N && !queue.empty()) {
        // Python: _, l, u = heapq.heappop(queue)
        const QueueEntry top = queue.top();
        queue.pop();
        const Presentation& u = top.state;

        for (const ac::SubsResult& s : ac::valid_subs(u)) {
            const Presentation& v = s.result;

            if (max_len.has_value() &&
                (int)std::max(v[0].size(), v[1].size()) > *max_len) {
                continue;
            }
            if (ac::is_trivial(v)) {
                parent[v] = {true, u, s.move};
                return path_to(v, parent);
            }
            if (parent.find(v) == parent.end()) {   // Python: if v not in parent
                parent[v] = {true, u, s.move};
                queue.push({(int)(v[0].size() + v[1].size()), top.depth + 1, v});
            }
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Command line (replaces argparse)
// ---------------------------------------------------------------------------

struct Args {
    std::string path;
    size_t max_nodes = 100000;
    std::optional<int> max_len;   // empty means None
    std::string out = "substitution_search_results_cpp.json";
};

void print_usage() {
    std::cout << "usage: substitution -p PATH [--max-nodes N] [--max-len L] [--out FILE]\n"
                 "  -p, --path    path to the input JSON file (required)\n"
                 "  --max-nodes   node limit (default = 100000)\n"
                 "  --max-len     max length of each relator (default = None)\n"
                 "  --out         output file (default = substitution_search_results_cpp.json)\n";
}

Args parse_args(int argc, char** argv) {
    Args args;
    for (int a = 1; a < argc; ++a) {
        std::string flag = argv[a];
        // every flag needs a value after it
        if (a + 1 >= argc) {
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
// main (Python: if __name__ == "__main__":)
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);

    const std::string max_len_text = args.max_len ? std::to_string(*args.max_len) : "None";

    const std::vector<Presentation> presentations = ac::read_relators(args.path);

    std::cout << "Attempting " << presentations.size() << " presentations found in " << args.path
              << "\nmax_len = " << max_len_text << "\nmax_nodes = " << args.max_nodes << "\n";

    // Build the output JSON as we go. Each entry looks like the Python output:
    //   "i": [[moves...], "input path", "max-len = ...", "max-nodes = ..."]
    std::vector<std::string> entries;

    int solved = 0;
    for (size_t i = 0; i < presentations.size(); ++i) {
        std::optional<std::vector<int>> soln =
            substitution_search(presentations[i], args.max_nodes, args.max_len);

        if (soln.has_value()) {
            ++solved;
            entries.push_back("    " + ac::to_json(std::to_string(i)) + ": [" +
                              ac::to_json(*soln) + ", " +
                              ac::to_json(args.path) + ", " +
                              ac::to_json("max-len = " + max_len_text) + ", " +
                              ac::to_json("max-nodes = " + std::to_string(args.max_nodes)) + "]");
            std::cout << "solution found for problem " << i;
        } else {
            std::cout << "no solution found for problem " << i;
        }
        std::cout << "   [" << (i + 1) << "/" << presentations.size()
                  << ", solved=" << solved << "]" << std::endl;
    }

    std::ofstream file(args.out);
    file << "{\n";
    for (size_t e = 0; e < entries.size(); ++e) {
        file << entries[e] << (e + 1 < entries.size() ? ",\n" : "\n");
    }
    file << "}\n";

    std::cout << "solved " << solved << " / " << presentations.size()
              << ", results written to " << args.out << "\n";
    return 0;
}
