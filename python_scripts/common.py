def inv(word):
    return [-x for x in word[::-1]]

def reduce(word):
    out = []
    for x in word:
        if out and out[-1] == -x:
            out.pop()
        else:
            out.append(x)
    return out

def red(representation):
    return (tuple(reduce(representation[0])), tuple(reduce(representation[1])))

# sets the hit of trivial state to a broader set, so that backward search takes less number of moves to hit it
def is_trivial(repr):
    r1, r2 = repr
    return len(r1) == 1 and len(r2) == 1 and abs(r1[0]) != abs(r2[0])

def neighbours(u):
    r1, r2 = list(u[0]), list(u[1])
    moves = [[inv(r1), r2],
             [r1, inv(r2)],
             [r1 + r2, r2],
             [r1 + inv(r2), r2],
             [r1, r2 + r1],
             [r1, r2 + inv(r1)]]
    for g in [1, 2]:
        moves.append([[g] + r1 + [-g], r2])
        moves.append([[-g] + r1 + [g], r2])
    for g in [1, 2]:
        moves.append([r1, [g] + r2 + [-g]])
        moves.append([r1, [-g] + r2 + [g]])

    return [red(v) for v in moves]

def max_len_Sheper_MS(n, w):
    return 2 * max(2 * n + 3, len(w) + 1) + 2
