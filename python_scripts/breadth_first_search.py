from collections import deque
import common as ac

# adapted from the breadth first search psuedocode in WHAT MAKES MATH PROBLEMS HARD FOR REINFORCEMENT LEARNING: A CASE STUDY by A. Sheper et. al.

def bfs_algorithm(repr, N, max_len = None):
    queue = deque()
    start = ac.red(repr)

    if ac.is_trivial(start):
        return []
    
    parent = {start: (None, None)}
    queue.append(start)

    def path_to(v):
        path = []
        while parent[v][1] is not None:
            path.append(parent[v][1])
            v = parent[v][0]
        return path[::-1]

    n=0
    while n < N:
        u = queue.popleft()
        for m, v in enumerate(ac.neighbours(u)):
            if max_len is not None and max(len(v[0]), len(v[1])) > max_len:
                continue
            if ac.is_trivial(v) == True:
                parent[v] = (u, m)
                return path_to(v)
            if v not in parent:
                parent[v] = (u, m)
                n += 1
                queue.append(v)
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
    parser.add_argument("--out", default = "bfs_results.json", help = "filepath for the solutions output (defult bfs_results.json)")

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

    if solutions_path.exists():
        with open(solutions_path, "r", encoding="utf-8") as file:
            solutions = json.load(file)
    else:
        solutions = {}

    solved = 0
    pbar = tqdm(range(len(presentations)), desc = "greedy search", unit = "presentations")
    for i in pbar:
        soln = bfs_algorithm(presentations[i]['relators'], N, max_len)
        if soln is not None:
            solved += 1
            solutions[i] = (soln, path, f'max-len = {max_len}', f'max-nodes = {N}')
            tqdm.write(f"solution found for problem {i}")
        else:
            tqdm.write(f"no solution found for problem {i}")
        pbar.set_postfix(solved=solved)

    with open(solutions_path, "w", encoding="utf-8") as file:
        json.dump(solutions, file, indent=4)