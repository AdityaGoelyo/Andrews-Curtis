// common.hpp — C++ port of python_scripts/common.py (the parts substitution search needs),
// plus a tiny JSON reader/writer so we don't depend on any external library.
//
// Every function keeps the same name as in common.py, and the Python line it
// replaces is shown in a comment where the translation isn't obvious.

#pragma once

#include <array>
#include <cctype>     // std::isspace
#include <cstdlib>    // std::abs
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
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

}  // namespace ac
