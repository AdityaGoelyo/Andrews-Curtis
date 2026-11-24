import json
from itertools import product
import argparse

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument('-n', '--n', type = int, default=7)
    parser.add_argument('-w', '--w', type = int, default=7)
    args = parser.parse_args()

    M = args.n
    N = args.w

    if M == 7:
        print(f'Using default M = {M}')
    if N == 7:
        print(f'Using default N = {N}')

    # relator family r1
    r1s = [[-1] + [2] * (i + 1) + [1] + [-2] * (i + 2) for i in range(N)]

    # words of length 1..M with x-exponent sum 0
    ws = [list(w) for L in range(1, M + 1) for w in product([1, -1, 2, -2], repeat=L)]
    words = [w for w in ws if sum(a for a in w if abs(a) == 1) == 0]

    def cyclic_reduce(word):
        out = []
        for a in word:
            if out and out[-1] == -a:
                out.pop()
            else:
                out.append(a)
        while len(out) > 1 and out[0] == -out[-1]:
            out = out[1:-1]
        return out

    def canonical(word):
        return min(tuple(word[i:] + word[:i]) for i in range(len(word)))

    # keep one word per cyclic class, skipping words where r2 is trivial
    seen, unique = set(), []
    for w in words:
        r2 = cyclic_reduce([-1] + w)
        if len(r2) > 1 and canonical(r2) not in seen:
            seen.add(canonical(r2))
            unique.append(w)

    presentations = [
        {"n": i + 1, "w": w, "relators": [r1, cyclic_reduce([-1] + w)]}
        for i, r1 in enumerate(r1s)
        for w in unique
    ]

    with open("data/ms_presentations.json", "w") as f:
        json.dump(presentations, f, indent=2)

    print(f"A total of {len(presentations)} Mullier Schupp presentations saved in ms_presentations.json with M = {M} and N = {N}")