import common as ac
import pandas as pd
import ast
import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', required=True, help="path for known solutions")
    parser.add_argument('--out', required=True, help="path to save csv eg: recursive_nn/data/modelxyz_state_depths.csv")
    args = parser.parse_args()

    PATH_TO_SOLUTIONS = args.input
    TRIVIAL_STATE = [[1], [2]]
    PATH_TO_SAVE_DATA = args.out

    # get path for challenge ids

    rows = []
    with open(PATH_TO_SOLUTIONS) as f:
        for line in f:
            if not line.startswith("ac-"):
                continue                                        # skip the header comment
            cid, rest = line.split(":", 1)
            path = ast.literal_eval(rest.split("#")[0].strip())  # drop the "# 97 moves, from ..." note
            rows.append({"challenge_id": cid, "path": path})

    paths_df = pd.DataFrame(rows)

    # generate the states and depths dataset

    states = []


    for path_i in paths_df['path']:
        state_i = TRIVIAL_STATE
        for depth, move in enumerate(reversed(path_i), start=1):
            state_i = ac.apply_AC(state_i, ac.inverse_move(move))
            states.append((ac.red(state_i), depth))

    df = pd.DataFrame(states, columns=['state', 'depth'])
    df.drop_duplicates(keep='first', inplace=True)

    df.to_csv(PATH_TO_SAVE_DATA, index=False)
    print(f'{len(df)} examples saved to {PATH_TO_SAVE_DATA}')