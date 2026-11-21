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

print(bfs_algorithm(((1,),(-2,1,2,-1,2)), 1000, 10))

if __name__ == "__main__":
    import sys
    import json
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--path", required=True, help = "path to the input JSON file")
    parser.add_argument("--max-nodes", type = int, default=100000, help="node limit (default = 100000)")
    parser.add_argument("--max-len", default=None, help="max total lengh of relator (default = None)")

    args = parser.parse_args()

    path = args.path
    max_len = args.max_len if args.max_len is None else int(args.max_len)
    N = args.max_nodes

    with open(path, 'r') as f:
        presentations = json.load(f)

    print(f'Attempting {len(presentations)} presentations found in {path}\nmax_len = {max_len}\nmax_nodes = {N}')

    for i in range(len(presentations)):
        print(bfs_algorithm(presentations[i]['relators'], N, max_len))