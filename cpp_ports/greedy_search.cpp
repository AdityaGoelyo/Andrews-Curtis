// greedy_search.cpp — fast greedy search over the 14 AC moves.
//
// Does exactly what python_scripts/greedy_search.py does (same neighbour
// order, same tie-breaking, so the same solutions), but much faster and with less memory.
//
// Build (the -fopenmp flag runs several presentations at once on all CPU cores;
// leave it out and the program still works, just on one core):
//     g++ -O2 -std=c++17 -fopenmp greedy_search.cpp -o greedy_search
//
// Run (same flags as greedy_search.py):
//     ./greedy_search -p ../ms_presentations.json --max-nodes 100000 --max-len 30
//
// Use fewer cores with:  OMP_NUM_THREADS=4 ./greedy_search ...
//
// Shared pieces (neighbour, ACParentMap, QueueEntry, MinHeap, parse_args,
// solve_all, write_results, ...) live in common.hpp.

#include "common.hpp"

#include <optional>
#include <vector>

using ac::Presentation;

// Everything one search needs, kept together so the helper functions below can share it.
// (Python keeps these as local variables of greedy_search.)
struct GreedySearch {
    size_t N;                          // node limit
    std::optional<int> max_len;        // Python's max_len (None = no limit)

    ac::ACParentMap parent;            // also the visited set: "v in parent"
    ac::MinHeap queue;                 // holds pointers into `parent`, not copies
    Presentation scratch;              // SPEED: every neighbour is built in here
    const Presentation* found = nullptr;   // the trivial state, once we reach it

    // Handles one neighbour v (= scratch) of u. This is the body of Python's
    //     for m, v in enumerate(ac.neighbours(u)):
    // loop. Returns true when v is trivial, which ends the search.
    bool consider(const Presentation& v, const Presentation* u, int m, int u_depth) {
        if (ac::too_long(v, max_len)) {
            return false;
        }
        if (ac::is_trivial(v)) {
            auto it = parent.insert_or_assign(v, ac::ACParentInfo{u, m}).first;
            found = &it->first;
            return true;
        }
        // Python: if v not in parent: parent[v] = (u, m); heapq.heappush(...)
        // try_emplace copies v into the map only when v is new.
        auto [it, is_new] = parent.try_emplace(v, ac::ACParentInfo{u, m});
        if (is_new) {
            const Presentation* key = &it->first;   // v's permanent copy, inside the map
            queue.push({(int)(v[0].size() + v[1].size()), u_depth + 1, key});
        }
        return false;
    }

    // Tries all 14 AC moves from u, in the same order as Python's neighbours(u).
    // Returns true when a trivial state was found.
    bool expand(const Presentation* u, int u_depth) {
        for (int m = 0; m < ac::NUM_AC_MOVES; ++m) {
            ac::neighbour(*u, m, scratch);
            if (consider(scratch, u, m, u_depth)) return true;
        }
        return false;
    }

    // Python: greedy_search(R, N, max_len)
    ac::SearchResult run(const Presentation& R) {
        const Presentation start = ac::red(R);
        if (ac::is_trivial(start)) {
            return std::vector<int>{};
        }

        // Python: queue = [(len(start[0]) + len(start[1]), 0, start)]
        //         parent = {start: (None, None)}
        auto it = parent.try_emplace(start, ac::ACParentInfo{}).first;
        queue.push({(int)(start[0].size() + start[1].size()), 0, &it->first});

        while (parent.size() < N && !queue.empty()) {
            const ac::QueueEntry top = queue.top();     // Python: k, l, u = heapq.heappop(queue)
            queue.pop();
            if (expand(top.state, top.depth)) {
                return ac::ac_path_to(parent, found);
            }
        }
        return std::nullopt;
    }
};

// One search per presentation; a fresh GreedySearch each time, so memory is freed in between.
ac::SearchResult greedy_search(const Presentation& R, size_t N, std::optional<int> max_len) {
    GreedySearch search;
    search.N = N;
    search.max_len = max_len;
    return search.run(R);
}

int main(int argc, char** argv) {
    const ac::Args args = ac::parse_args(argc, argv, "greedy_search_results_cpp.json");

    const std::vector<Presentation> presentations = ac::read_relators(args.path);
    std::cout << "Attempting " << presentations.size() << " presentations found in " << args.path
              << "\nmax_len = " << ac::max_len_text(args) << "\nmax_nodes = " << args.max_nodes << "\n";

    const std::vector<ac::SearchResult> solutions = ac::solve_all(presentations,
        [&](const Presentation& R) { return greedy_search(R, args.max_nodes, args.max_len); });

    ac::write_results(args, solutions);
    return 0;
}
