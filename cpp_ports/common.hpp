// common.hpp — C++ port of python_scripts/common.py, plus the pieces the C++
// searches share (hashing, the parent map, command line, running every
// presentation, writing results) and a tiny JSON reader/writer so we don't
// depend on any external library.
//
// Used by: substitution.cpp, greedy_search.cpp, breadth_first_search.cpp
//
// Every function keeps the same name as in common.py, and the Python line it
// replaces is shown in a comment where the translation isn't obvious.

#pragma once

#include <algorithm>  // std::max
#include <array>
#include <cctype>     // std::isspace
#include <cstdlib>    // std::abs, std::exit
#include <fstream>
#include <functional> // std::function, std::greater
#include <iostream>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>      // std::tie
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace ac {

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------

// A relator (word) is a list of nonzero ints:  1 = x, -1 = x^-1, 2 = y, -2 = y^-1.
// Python: a list / tuple of ints.
using Word = std::vector<int8_t>;

// A presentation is a pair of relators, indexed R[0] and R[1] just like in Python.
// std::array compares lexicographically, exactly like a Python tuple, which
// matters because the priority queue breaks ties by comparing states.
using Presentation = std::array<Word, 2>;

// Concatenate two words.  Python: a + b
inline Word concat(const Word& a, const Word& b) {
    Word out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

// ---------------------------------------------------------------------------
// Word operations
// ---------------------------------------------------------------------------

// Python: [-x for x in r[::-1]]
inline Word inv(const Word& r) {
    Word out;
    for (int idx = (int)r.size() - 1; idx >= 0; --idx) {
        out.push_back(-r[idx]);
    }
    return out;
}

// Free reduction: cancel adjacent x x^-1 pairs.
inline Word reduce(const Word& r) {
    Word out;
    for (int x : r) {
        if (!out.empty() && out.back() == -x) {
            out.pop_back();
        } else {
            out.push_back(x);
        }
    }
    return out;
}

// Reduce both relators.  (Covers both red() and reduce_subs() from Python.)
inline Presentation red(const Presentation& R) {
    return {reduce(R[0]), reduce(R[1])};
}

inline bool is_trivial(const Presentation& R) {
    return R[0].size() == 1 && R[1].size() == 1 &&
           std::abs(R[0][0]) != std::abs(R[1][0]);
}

// Cyclic rotation left by k.  Python: r[k:] + r[:k]
inline Word rot(const Word& r, int k) {
    if (r.empty()) return r;
    int n = (int)r.size();
    k = ((k % n) + n) % n;   // Python's % never returns a negative number; C++'s can
    Word out(r.begin() + k, r.end());
    out.insert(out.end(), r.begin(), r.begin() + k);
    return out;
}

// ---------------------------------------------------------------------------
// Building reduced words letter by letter (no temporary copies)
// ---------------------------------------------------------------------------

// Appends letter x to out, cancelling it against the last letter if they are inverse.
// Doing this letter by letter is exactly what Python's reduce() does.
inline void push_reduced(Word& out, int8_t x) {
    if (!out.empty() && out.back() == -x) {
        out.pop_back();
    } else {
        out.push_back(x);
    }
}

// out = reduce(out + w)
inline void append_reduced(Word& out, const Word& w) {
    for (int8_t x : w) push_reduced(out, x);
}

// out = reduce(out + inv(w))      (reads w backwards and negates, no copy of inv(w))
inline void append_inverse_reduced(Word& out, const Word& w) {
    for (size_t t = w.size(); t-- > 0;) push_reduced(out, -w[t]);
}

// out = reduce([g] + w + [-g])
inline void conjugate_reduced(Word& out, int8_t g, const Word& w) {
    push_reduced(out, g);
    append_reduced(out, w);
    push_reduced(out, -g);
}

// ---------------------------------------------------------------------------
// AC moves (used by greedy search and breadth first search)
// ---------------------------------------------------------------------------

constexpr int NUM_AC_MOVES = 14;

// Python: neighbours(u)[move]   (same numbering as apply_AC in common.py)
//
// SPEED: instead of returning a list of 14 new presentations, this writes one
// neighbour into `out`. The caller reuses the same `out` for every neighbour,
// so its memory is allocated once and then just cleared (clear() keeps capacity).
inline void neighbour(const Presentation& u, int move, Presentation& out) {
    const Word& r0 = u[0];
    const Word& r1 = u[1];
    Word& o0 = out[0];
    Word& o1 = out[1];
    o0.clear();
    o1.clear();

    switch (move) {
        case 0:  append_inverse_reduced(o0, r0);                          append_reduced(o1, r1); break;  // r0 <- r0^-1
        case 1:  append_reduced(o0, r0);                          append_inverse_reduced(o1, r1); break;  // r1 <- r1^-1
        case 2:  append_reduced(o0, r0); append_reduced(o0, r1);          append_reduced(o1, r1); break;  // r0 <- r0 r1
        case 3:  append_reduced(o0, r0); append_inverse_reduced(o0, r1);  append_reduced(o1, r1); break;  // r0 <- r0 r1^-1
        case 4:  append_reduced(o0, r0);  append_reduced(o1, r1); append_reduced(o1, r0);         break;  // r1 <- r1 r0
        case 5:  append_reduced(o0, r0);  append_reduced(o1, r1); append_inverse_reduced(o1, r0); break;  // r1 <- r1 r0^-1
        case 6:  conjugate_reduced(o0,  1, r0);                           append_reduced(o1, r1); break;  // r0 <- x r0 x^-1
        case 7:  conjugate_reduced(o0, -1, r0);                           append_reduced(o1, r1); break;  // r0 <- x^-1 r0 x
        case 8:  conjugate_reduced(o0,  2, r0);                           append_reduced(o1, r1); break;  // r0 <- y r0 y^-1
        case 9:  conjugate_reduced(o0, -2, r0);                           append_reduced(o1, r1); break;  // r0 <- y^-1 r0 y
        case 10: append_reduced(o0, r0);                           conjugate_reduced(o1,  1, r1); break;  // r1 <- x r1 x^-1
        case 11: append_reduced(o0, r0);                           conjugate_reduced(o1, -1, r1); break;  // r1 <- x^-1 r1 x
        case 12: append_reduced(o0, r0);                           conjugate_reduced(o1,  2, r1); break;  // r1 <- y r1 y^-1
        case 13: append_reduced(o0, r0);                           conjugate_reduced(o1, -2, r1); break;  // r1 <- y^-1 r1 y
        default: throw std::invalid_argument("move must be in 0..13");
    }
}

// Python: max_len is not None and max(len(v[0]), len(v[1])) > max_len
inline bool too_long(const Presentation& v, std::optional<int> max_len) {
    return max_len.has_value() && (int)std::max(v[0].size(), v[1].size()) > *max_len;
}

// ---------------------------------------------------------------------------
// Substitution moves
// ---------------------------------------------------------------------------

// The four numbers that describe one substitution move.
// Python passes these around as the tuple (i, j, k1, k2).
struct SubsMove {
    int i;    // which relator gets replaced (0 or 1)
    int j;    // +1: use the other relator, -1: use its inverse
    int k1;   // rotation applied to R[i]
    int k2;   // rotation applied to the other relator
};

// Replace R[i] with rot(R[i], k1) + rot(R[1-i]^j, k2); R[1-i] stays the same.
inline Presentation subs(const Presentation& R, int i, int j, int k1, int k2) {
    const Word& other = R[1 - i];
    Word other_j = (j == 1) ? other : inv(other);

    Presentation out;
    out[i]     = concat(rot(R[i], k1), rot(other_j, k2));
    out[1 - i] = other;
    return out;
}

// One entry of the list returned by valid_subs.
// Python: the tuple (reduced_presentation, i, j, k1, k2)
struct SubsResult {
    Presentation result;
    SubsMove move;
};

// All substitutions where at least one letter cancels at the join point.
inline std::vector<SubsResult> valid_subs(const Presentation& R) {
    std::vector<SubsResult> val_subs;
    for (int i : {0, 1}) {
        const Word& r_i = R[i];
        const Word& r_o = R[1 - i];      // "o" = the other relator
        int n_i = (int)r_i.size();
        int n_o = (int)r_o.size();

        for (int k1 = 0; k1 < n_i; ++k1) {
            for (int k2 = 0; k2 < n_o; ++k2) {
                // Python: R[i][k1-1]   (k1-1 == -1 means the last letter)
                int last_letter = r_i[(k1 - 1 + n_i) % n_i];

                // Python: R[i][k1-1] == -R[1-i][k2]
                if (last_letter == -r_o[k2]) {
                    val_subs.push_back({red(subs(R, i, 1, k1, k2)), {i, 1, k1, k2}});
                }
                // Python: R[i][k1-1] == R[1-i][-(1+k2)]
                if (last_letter == r_o[n_o - 1 - k2]) {
                    val_subs.push_back({red(subs(R, i, -1, k1, k2)), {i, -1, k1, k2}});
                }
            }
        }
    }
    return val_subs;
}

// ---------------------------------------------------------------------------
// Turning one substitution back into ordinary AC moves (0..13)
// ---------------------------------------------------------------------------

// Python: CONJ = {1: 0, -1: 1, 2: 2, -2: 3}
inline int conj_offset(int g) {
    switch (g) {
        case  1: return 0;
        case -1: return 1;
        case  2: return 2;
        case -2: return 3;
    }
    throw std::invalid_argument("conj_offset: generator must be +-1 or +-2");
}

// r <- g r g^-1, and record the matching AC move in `path`.
inline Word conj(const Word& r, int g, int idx, std::vector<int>& path) {
    path.push_back(6 + 4 * idx + conj_offset(g));
    Word w = {(int8_t)g};
    w.insert(w.end(), r.begin(), r.end());
    w.push_back(-g);
    return reduce(w);
}

// What subs_to_AC_path returns.  Python: the tuple (R, path)
struct ACPath {
    Presentation end;        // presentation reached after applying the moves
    std::vector<int> moves;  // AC moves (0..13)
};

// V = the presentation after the substitution, U = the one before it.
// Produces AC moves that take V back to U.
inline ACPath subs_to_AC_path(const Presentation& V, const Presentation& U, const SubsMove& m) {
    const int i = m.i, j = m.j, k1 = m.k1, k2 = m.k2;
    std::vector<int> path;

    Word r_i = V[i];
    Word r_o = U[1 - i];
    const Word w_o = (j == 1) ? r_o : inv(r_o);   // original word r_{1-i}^j
    const Word w_i = U[i];                         // original word r_i

    if (j == -1) {
        r_o = inv(r_o);
        path.push_back(1 - i);
    }
    // Python: for a in w_o[:k2]      (rotate r_o left by k2)
    for (int idx = 0; idx < k2; ++idx) {
        r_o = conj(r_o, -w_o[idx], 1 - i, path);
    }

    r_i = reduce(concat(r_i, inv(r_o)));
    path.push_back(3 + 2 * i);

    // Python: for a in reversed(w_o[:k2])      (rotate r_o back)
    for (int idx = k2 - 1; idx >= 0; --idx) {
        r_o = conj(r_o, w_o[idx], 1 - i, path);
    }
    if (j == -1) {
        r_o = inv(r_o);
        path.push_back(1 - i);
    }
    // Python: for a in reversed(w_i[:k1])      (undo rot(r_i, k1))
    for (int idx = k1 - 1; idx >= 0; --idx) {
        r_i = conj(r_i, w_i[idx], i, path);
    }

    Presentation end = (i == 0) ? Presentation{r_i, r_o} : Presentation{r_o, r_i};
    return {end, path};
}

// ---------------------------------------------------------------------------
// Minimal JSON helpers
// ---------------------------------------------------------------------------
// These only handle what this project uses. read_relators() finds every
// "relators": [[...], [...]] entry in a file such as ms_presentations.json
// and ignores the other fields.

namespace json_detail {

inline void skip_spaces(const std::string& s, size_t& pos) {
    while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
}

inline void expect(const std::string& s, size_t& pos, char c) {
    skip_spaces(s, pos);
    if (pos >= s.size() || s[pos] != c) {
        throw std::runtime_error(std::string("JSON parse error: expected '") + c +
                                 "' at position " + std::to_string(pos));
    }
    ++pos;
}

// Parses e.g. "[1, -2, 3]"
inline Word parse_int_list(const std::string& s, size_t& pos) {
    Word out;
    expect(s, pos, '[');
    skip_spaces(s, pos);
    if (s[pos] == ']') { ++pos; return out; }   // empty list
    while (true) {
        skip_spaces(s, pos);
        size_t used = 0;
        out.push_back(std::stoi(s.substr(pos, 20), &used));
        pos += used;
        skip_spaces(s, pos);
        if (s[pos] == ',') { ++pos; continue; }
        expect(s, pos, ']');
        return out;
    }
}

}  // namespace json_detail

// Returns the "relators" of every presentation in the file, in order.
inline std::vector<Presentation> read_relators(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("could not open " + filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    std::vector<Presentation> result;
    const std::string key = "\"relators\"";
    size_t pos = text.find(key);
    while (pos != std::string::npos) {
        pos += key.size();
        json_detail::expect(text, pos, ':');
        json_detail::expect(text, pos, '[');
        Presentation R;
        R[0] = json_detail::parse_int_list(text, pos);
        json_detail::expect(text, pos, ',');
        R[1] = json_detail::parse_int_list(text, pos);
        json_detail::expect(text, pos, ']');
        result.push_back(R);
        pos = text.find(key, pos);
    }
    return result;
}

// Word -> "[1, -2, 3]"
inline std::string to_json(const std::vector<int>& w) {
    std::string out = "[";
    for (size_t idx = 0; idx < w.size(); ++idx) {
        if (idx > 0) out += ", ";
        out += std::to_string(w[idx]);
    }
    return out + "]";
}

// Text -> "\"text\"" (escapes quotes and backslashes)
inline std::string to_json(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// ---------------------------------------------------------------------------
// Pieces shared by the searches
// ---------------------------------------------------------------------------

// FNV-1a hash. For every letter: XOR it into h, then multiply by a big prime.
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

// Python: parent[v] = (u, m), or (None, None) for the start.   (m = AC move 0..13)
// SPEED: `parent` points at u's key inside the map instead of holding a copy of u.
// This is safe because std::unordered_map never moves its elements: a pointer to a key
// stays valid while the map grows.
struct ACParentInfo {
    const Presentation* parent = nullptr;   // nullptr = Python's None (the start state)
    int move = -1;                          // -1 = Python's None
};

// The parent map is also the visited set: "v in parent" == "v was already seen".
using ACParentMap = std::unordered_map<Presentation, ACParentInfo, PresentationHash>;

// Python: the inner function path_to(v) of greedy_search / bfs_algorithm.
// Walks from v back to the start, then reverses.
inline std::vector<int> ac_path_to(const ACParentMap& parent, const Presentation* v) {
    std::vector<int> path;
    while (parent.at(*v).parent != nullptr) {
        const ACParentInfo& info = parent.at(*v);
        path.push_back(info.move);
        v = info.parent;
    }
    return std::vector<int>(path.rbegin(), path.rend());   // Python: path[::-1]
}

// One entry of a greedy (priority) queue.   Python: the heap entry (total_len, l, v)
// SPEED: total_len and depth are computed once when pushed, and `state` points at
// v's key in the parent map (no copy of v).
struct QueueEntry {
    int total_len;
    int depth;
    const Presentation* state;
};

// Python's heapq compares (total_len, l, v) element by element; we do the same.
// The states are compared only when total_len and depth are both equal.
inline bool operator>(const QueueEntry& a, const QueueEntry& b) {
    return std::tie(a.total_len, a.depth, *a.state) > std::tie(b.total_len, b.depth, *b.state);
}

// std::greater turns std::priority_queue into a min-heap, like heapq.
using MinHeap = std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>>;

// What every search returns: the list of moves, or std::nullopt (= Python's None).
using SearchResult = std::optional<std::vector<int>>;

// ---------------------------------------------------------------------------
// Command line (same flags as the Python scripts)
// ---------------------------------------------------------------------------

struct Args {
    std::string path;
    size_t max_nodes = 100000;
    std::optional<int> max_len;   // empty means None
    std::string out;
};

inline void print_usage(const std::string& program, const std::string& default_out) {
    std::cout << "usage: " << program << " -p PATH [--max-nodes N] [--max-len L] [--out FILE]\n"
                 "  -p, --path    path to the input JSON file (required)\n"
                 "  --max-nodes   node limit (default = 100000)\n"
                 "  --max-len     max length of each relator (default = None)\n"
                 "  --out         output file (default = " << default_out << ")\n";
}

inline Args parse_args(int argc, char** argv, const std::string& default_out) {
    const std::string program = argv[0];
    Args args;
    args.out = default_out;
    for (int a = 1; a < argc; ++a) {
        std::string flag = argv[a];
        if (a + 1 >= argc) {          // every flag needs a value after it
            print_usage(program, default_out);
            std::exit(1);
        }
        std::string value = argv[++a];

        if (flag == "-p" || flag == "--path")  args.path = value;
        else if (flag == "--max-nodes")        args.max_nodes = std::stoul(value);
        else if (flag == "--max-len")          args.max_len = std::stoi(value);
        else if (flag == "--out")              args.out = value;
        else {
            std::cout << "unknown argument: " << flag << "\n";
            print_usage(program, default_out);
            std::exit(1);
        }
    }
    if (args.path.empty()) {
        print_usage(program, default_out);
        std::exit(1);
    }
    return args;
}

inline std::string max_len_text(const Args& args) {
    return args.max_len ? std::to_string(*args.max_len) : "None";
}

// ---------------------------------------------------------------------------
// Running every presentation and saving the results
// ---------------------------------------------------------------------------

// Python: the `for i in pbar:` loop in __main__.
// `search` is called once per presentation; it gets R and returns the moves or nullopt.
//
// SPEED: with -fopenmp the presentations are shared out between the CPU cores.
// schedule(dynamic) hands out one presentation at a time, so a core that finishes
// early takes the next one. Without -fopenmp this is an ordinary loop.
inline std::vector<SearchResult> solve_all(const std::vector<Presentation>& presentations,
                                           const std::function<SearchResult(const Presentation&)>& search) {
    const int count = (int)presentations.size();
    std::vector<SearchResult> solutions(count);   // solutions[i] is filled in by whichever core handles i
    int solved = 0, done = 0;

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < count; ++i) {
        solutions[i] = search(presentations[i]);

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
    return solutions;
}

// Writes the solved problems as
//   "i": [[moves...], "input path", "max-len = ...", "max-nodes = ..."]
// (same entries as the Python scripts; this overwrites the file instead of merging).
inline void write_results(const Args& args, const std::vector<SearchResult>& solutions) {
    std::ofstream file(args.out);
    file << "{\n";
    bool first = true;
    int solved = 0;
    for (size_t i = 0; i < solutions.size(); ++i) {
        if (!solutions[i].has_value()) continue;
        ++solved;
        if (!first) file << ",\n";
        first = false;
        file << "    " << to_json(std::to_string(i)) << ": [" << to_json(*solutions[i]) << ", "
             << to_json(args.path) << ", " << to_json("max-len = " + max_len_text(args)) << ", "
             << to_json("max-nodes = " + std::to_string(args.max_nodes)) << "]";
    }
    file << "\n}\n";

    std::cout << "solved " << solved << " / " << solutions.size()
              << ", results written to " << args.out << "\n";
}

}  // namespace ac
