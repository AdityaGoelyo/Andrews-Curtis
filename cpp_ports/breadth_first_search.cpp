// breadth_first_search.cpp — fast breadth first search over the 14 AC moves.
//
// Does exactly what python_scripts/breadth_first_search.py does (same neighbour
// order, so the same solutions), but much faster and with less memory.
//
// Build (the -fopenmp flag runs several presentations at once on all CPU cores;
// leave it out and the program still works, just on one core):
//     g++ -O2 -std=c++17 -fopenmp cpp_ports/breadth_first_search.cpp -o cpp_ports/breadth_first_search
//
// Run (same flags as breadth_first_search.py):
//     cpp_ports/breadth_first_search -p data/ms_presentations.json --max-nodes 100000 --max-len 30
//
// Use fewer cores with:  OMP_NUM_THREADS=4 ./breadth_first_search ...
//
// Shared pieces (neighbour, ACParentMap, parse_args, solve_all, write_results, ...)
// live in common.hpp.

#include "common.hpp"

#include <deque>
#include <optional>
#include <vector>

using ac::Presentation;

// Everything one search needs, kept together so the helper functions below can share it.
// (Python keeps these as local variables of bfs_algorithm.)
struct BreadthFirstSearch {
    size_t N;                          // node limit
    std::optional<int> max_len;        // Python's max_len (None = no limit)

    ac::ACParentMap parent;            // also the visited set: "v in parent"
    std::deque<const Presentation*> queue;   // SPEED: pointers into `parent`, not copies
    Presentation scratch;              // SPEED: every neighbour is built in here
    const Presentation* found = nullptr;   // the trivial state, once we reach it
    size_t n = 0;                      // Python's n: how many new states were added

    // Handles one neighbour v (= scratch) of u. This is the body of Python's
    //     for m, v in enumerate(ac.neighbours(u)):
    // loop. Returns true when v is trivial, which ends the search.
    bool consider(const Presentation& v, const Presentation* u, int m) {
        if (ac::too_long(v, max_len)) {
            return false;
        }
        if (ac::is_trivial(v)) {
            auto it = parent.insert_or_assign(v, ac::ACParentInfo{u, m}).first;
            found = &it->first;
            return true;
        }
        // Python: if v not in parent: parent[v] = (u, m); n += 1; queue.append(v)
        // try_emplace copies v into the map only when v is new.
        auto [it, is_new] = parent.try_emplace(v, ac::ACParentInfo{u, m});
        if (is_new) {
            ++n;
            queue.push_back(&it->first);   // v's permanent copy, inside the map
        }
        return false;
    }

    // Tries all 14 AC moves from u, in the same order as Python's neighbours(u).
    // Returns true when a trivial state was found.
    bool expand(const Presentation* u) {
        for (int m = 0; m < ac::NUM_AC_MOVES; ++m) {
            ac::neighbour(*u, m, scratch);
            if (consider(scratch, u, m)) return true;
        }
        return false;
    }

    // Python: bfs_algorithm(repr, N, max_len)
    ac::SearchResult run(const Presentation& R) {
        const Presentation start = ac::red(R);
        if (ac::is_trivial(start)) {
            return std::vector<int>{};
        }

        // Python: parent = {start: (None, None)}; queue.append(start)
        auto it = parent.try_emplace(start, ac::ACParentInfo{}).first;
        queue.push_back(&it->first);

        // Python: while n < N:
        // (Python has no empty-queue check and would crash with IndexError;
        //  here an empty queue just means "no solution".)
        while (n < N && !queue.empty()) {
            const Presentation* u = queue.front();   // Python: u = queue.popleft()
            queue.pop_front();
            if (expand(u)) {
                return ac::ac_path_to(parent, found);
            }
        }
        return std::nullopt;
    }
};

// One search per presentation; a fresh BreadthFirstSearch each time, so memory is freed in between.
ac::SearchResult bfs_algorithm(const Presentation& R, size_t N, std::optional<int> max_len) {
    BreadthFirstSearch search;
    search.N = N;
    search.max_len = max_len;
    return search.run(R);
}

int main(int argc, char** argv) {
    const ac::Args args = ac::parse_args(argc, argv, "bfs_results_cpp.json");

    const std::vector<Presentation> presentations = ac::read_relators(args.path);
    std::cout << "Attempting " << presentations.size() << " presentations found in " << args.path
              << "\nmax_len = " << ac::max_len_text(args) << "\nmax_nodes = " << args.max_nodes << "\n";

    const std::vector<ac::SearchResult> solutions = ac::solve_all(presentations,
        [&](const Presentation& R) { return bfs_algorithm(R, args.max_nodes, args.max_len); });

    ac::write_results(args, solutions);
    return 0;
}
