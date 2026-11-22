def inv(r):
    return [-x for x in r[::-1]]

def reduce(r):
    out = []
    for x in r:
        if out and out[-1] == -x:
            out.pop()
        else:
            out.append(x)
    return out

def red(R):
    return (tuple(reduce(R[0])), tuple(reduce(R[1])))

def reduce_subs(R):
    return [reduce(R[0]),reduce(R[1])]

# sets the hit of trivial state to a broader set, so that backward search takes less number of moves to hit it
def is_trivial(R):
    r1, r2 = R
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

def rot(r, k):
    if not r:
        return r
    k %= len(r)
    return r[k:] + r[:k]    

# helper functions for substitution search

def subs(R, i, j, k1, k2):
    if i == 0:
        if j == 1:
            return [rot(R[i], k1) + rot(R[1-i], k2), R[1-i]]
        else:
            return [rot(R[i], k1) + rot(inv(R[1-i]), k2), R[1-i]]
    else:
        if j == 1:
            return [R[1-i], rot(R[i], k1) + rot(R[1-i], k2)]
        else:
            return [R[1-i], rot(R[i], k1) + rot(inv(R[1-i]), k2)]

def valid_subs(R):
    val_subs = []
    for i in (0,1):
        for k1 in range(len(R[i])):
            for k2 in range(len(R[1-i])):
                if R[i][k1-1] == -R[1-i][k2]:
                    val_subs.append((reduce_subs(subs(R, i, 1, k1, k2)), i, 1, k1, k2))
                if R[i][k1-1] == R[1-i][-(1+k2)]:
                    val_subs.append((reduce_subs(subs(R, i, -1, k1, k2)), i, -1, k1, k2))
    return val_subs

def apply_AC(R, move):
    r0, r1 = list(R[0]), list(R[1])
    if   move == 0:  r0 = inv(r0)                  # r0 <- r0^-1
    elif move == 1:  r1 = inv(r1)                  # r1 <- r1^-1
    elif move == 2:  r0 = r0 + r1                  # r0 <- r0 r1
    elif move == 3:  r0 = r0 + inv(r1)             # r0 <- r0 r1^-1
    elif move == 4:  r1 = r1 + r0                  # r1 <- r1 r0
    elif move == 5:  r1 = r1 + inv(r0)             # r1 <- r1 r0^-1
    elif move == 6:  r0 = [1] + r0 + [-1]          # r0 <- x r0 x^-1
    elif move == 7:  r0 = [-1] + r0 + [1]          # r0 <- x^-1 r0 x
    elif move == 8:  r0 = [2] + r0 + [-2]          # r0 <- y r0 y^-1
    elif move == 9:  r0 = [-2] + r0 + [2]          # r0 <- y^-1 r0 y
    elif move == 10: r1 = [1] + r1 + [-1]          # r1 <- x r1 x^-1
    elif move == 11: r1 = [-1] + r1 + [1]          # r1 <- x^-1 r1 x
    elif move == 12: r1 = [2] + r1 + [-2]          # r1 <- y r1 y^-1
    elif move == 13: r1 = [-2] + r1 + [2]          # r1 <- y^-1 r1 y
    else:
        raise ValueError(f"move must be in 0..13, got {move}")
    return [reduce(r0), reduce(r1)]

"""
def subs_to_AC_path(R, i, j, k1, k2):
    path = []
    # invert the given R that way made by applying i, j, k1, k2
    # conjugate R[1-i] i.e. make rot^k2(r^j_1-i)
    r_1_i = R[1-i]
    r_i = R[i]
    if j == -1:
        r_1_i = inv(r_1_i)
        path.append(1-i)

    for l in range(k2):
        a = r_1_i[0]
        match a:
            case -1:
                r_1_i = apply_AC([r_1_i, [1]], 6)[0]
                path.append(6 + (1-i)*4)
            case 1:
                r_1_i = apply_AC([r_1_i, [1]], 7)[0]
                path.append(7 + (1-i)*4)
            case -2:
                r_1_i = apply_AC([r_1_i, [1]], 8)[0]
                path.append(8 + (1-i)*4)
            case 2:
                r_1_i = apply_AC([r_1_i, [1]], 9)[0]
                path.append(9 + (1-i)*4)

    # get R^k1[i]
    r_i = reduce(r_i + inv(r_1_i))
    path.append(3 + (i*2))
    
    # reverse R[1-i]
    for l in range(k2):
        a = r_1_i[-1]
        match a:
            case 1:
                r_1_i = apply_AC([r_1_i, [1]], 6)[0]
                path.append(6 + (1-i)*4)
            case -1:
                r_1_i = apply_AC([r_1_i, [1]], 7)[0]
                path.append(7 + (1-i)*4)
            case 2:
                r_1_i = apply_AC([r_1_i, [1]], 8)[0]
                path.append(8 + (1-i)*4)
            case -2:
                r_1_i = apply_AC([r_1_i, [1]], 9)[0]
                path.append(9 + (1-i)*4)
    # reverse R[i]
    for l in range(k1):
        a = r_i[-1]
        match a:
            case 1:
                r_i = apply_AC([r_i, [1]], 6)[0]
                path.append(6 + (1-i)*4)
            case -1:
                r_i = apply_AC([r_i, [1]], 7)[0]
                path.append(7 + (1-i)*4)
            case 2:
                r_i = apply_AC([r_i, [1]], 8)[0]
                path.append(8 + (1-i)*4)
            case -2:
                r_i = apply_AC([r_i, [1]], 9)[0]
                path.append(9 + (1-i)*4)
    if j == -1:
        r_1_i = inv(r_1_i)
        path.append(1-i)
    if i == 0:
        R = [r_i, r_1_i]
    else:
        R = [r_1_i, r_i]
    return R, path
"""

CONJ = {1: 0, -1: 1, 2: 2, -2: 3}

def conj(r, g, idx, path):
    path.append(6 + 4*idx + CONJ[g])          # move "r_idx <- g r_idx g^-1"
    return reduce([g] + r + [-g])

def subs_to_AC_path(V, U, i, j, k1, k2):
    path = []
    r_i, r_o = list(V[i]), list(U[1-i])
    w_o = r_o if j == 1 else inv(r_o)         # original word r_{1-i}^j
    w_i = list(U[i])                          # original word r_i
    if j == -1:
        r_o = inv(r_o); path.append(1-i)
    for a in w_o[:k2]:                        # rotate r_o left by k2
        r_o = conj(r_o, -a, 1-i, path)
    r_i = reduce(r_i + inv(r_o)); path.append(3 + 2*i)
    for a in reversed(w_o[:k2]):              # rotate r_o back
        r_o = conj(r_o, a, 1-i, path)
    if j == -1:
        r_o = inv(r_o); path.append(1-i)
    for a in reversed(w_i[:k1]):              # undo rot(r_i, k1)
        r_i = conj(r_i, a, i, path)
    return ([r_i, r_o] if i == 0 else [r_o, r_i]), path
