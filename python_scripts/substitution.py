import common as ac
import heapq

def key(R):
    return (tuple(R[0]), tuple(R[1]))

def substitution_search(R, N, max_len = None):
    start = ac.red(R)

    if ac.is_trivial(start):
        return []

    queue = [(len(start[0]) + len(start[1]), 0, start)]
    parent = {start: (None, None)}

    def path_to(v):
        path = []
        while parent[v][0] is not None:
            u, (i, j, k1, k2) = parent[v]
            R, path_i = ac.subs_to_AC_path([list(v[0]), list(v[1])], u, i, j, k1, k2)
            # check for correctness
            if key(R) != parent[v][0]:
                print(f'{R} is not equals {parent[v][0]}')
            path.extend(path_i)
            v = u
        return path

    while len(parent) < N and queue:
        _, l, u = heapq.heappop(queue)
        for v, i, j, k1, k2 in ac.valid_subs([list(u[0]), list(u[1])]):
            v = key(v)
            if max_len is not None and max(len(v[0]), len(v[1])) > max_len:
                continue
            if ac.is_trivial(v):
                parent[v] = (u, (i, j, k1, k2))
                return path_to(v)
            if v not in parent:
                parent[v] = (u, (i, j, k1, k2))
                heapq.heappush(queue, (len(v[0]) + len(v[1]), l + 1, v))
    return None

if __name__ == "__main__":
    import json
    import argparse
    from pathlib import Path
    from tqdm import tqdm

    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--path", required=True, help = "path to the input JSON file")
    parser.add_argument("--max-nodes", type = int, default=100000, help="node limit (default = 100000)")
    parser.add_argument("--max-len", default=None, help="max total lengh of relator (default = None)")
    parser.add_argument("--out", default = "substitution_search_results.json", help = "filepath for the solutions output (defult substitution_search_results.json)")

    args = parser.parse_args()

    path = args.path
    max_len = args.max_len if args.max_len is None else int(args.max_len)
    N = args.max_nodes
    solutions_path = Path(args.out)

    with open(path, 'r') as f:
        presentations = json.load(f)

    print(f'Attempting {len(presentations)} presentations found in {path}\nmax_len = {max_len}\nmax_nodes = {N}')

    if solutions_path.exists():
        with open(solutions_path, "r", encoding="utf-8") as file:
            solutions = json.load(file)
    else:
        solutions = {}

    solved = 0
    pbar = tqdm(range(len(presentations)), desc = "substitution search", unit = "presentations")
    for i in pbar:
        soln = substitution_search(presentations[i]['relators'], N, max_len)
        if soln is not None:
            solved += 1
            solutions[i] = (soln, path, f'max-len = {max_len}', f'max-nodes = {N}')
            tqdm.write(f"solution found for problem {i}")
        else:
            tqdm.write(f"no solution found for problem {i}")
        pbar.set_postfix(solved=solved)

    with open(solutions_path, "w", encoding="utf-8") as file:
        json.dump(solutions, file, indent=4)