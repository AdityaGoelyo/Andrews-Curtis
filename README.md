This repo contains cleaned up code for recrating much of my experiments on the Andrews - Curtis Conjecture.

Approaches that I used:
- Meet in the middle search from a pre-computed library of AC trivial presentations from the trivial presentation and backward search from some given non-trivial pair of relators.
- Breadth First Search from the non-trivial pair of relators.
- Greedy Search.
- bfs and greedy search using macros from known paths (this didnt give good results)
- Greedy Search using substitution moves.
- DeepCubeA algorithm, learning a cost-to-go function with representations of the trivial gruop as states.

Approaches that you'll find here
- Breadth first search
- Greedy search
- Greedy search with substitution moves
- DeepCubeA (under progress: in a seperate folder)

I also built the ms_presentations dataset, which contains many of the pairs of relators from the Miller-Schupp family of counterexamples, as described in [Shehper et al., 2024](https://arxiv.org/abs/2408.15332). To generate it yourself, run:

    python data/make_miller_schupp.py -w <max_word_length> -n <n>

where $n$ is the exponent in the relator $x^{-1}y^{n}xy^{-(n+1)}$.

I wrote the python versions myself and had Claude write the C++ versions for me.

I owe a lot to the papers on the Andrews - Curtis Conjecture by the Caltech Math-AI group. Much of my experiments are basically implementing the algorithms that they talked about.