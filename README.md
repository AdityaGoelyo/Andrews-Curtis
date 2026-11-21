This repo contains cleaned up code for recrating much of my experiments on the Andrews - Curtis Conjecture.

Approaches that I used:
- Meet in the middle search from a pre-computed library of AC trivial presentations from the trivial presentation and backward search from some given non-trivial pair of relators.
- Breadth First Search from the non-trivial pair of relators.
- Greedy Search.
- Greedy Search using substitution moves.
- DeepCubeA algorithm, learning a cost-to-go function with representations of the trivial gruop as states.

I wrote the python versions myself and had Claude write the C++ versions for me.

I owe a lot to the papers on the Andrews - Curtis Conjecture by the Caltech Math-AI group. Much of my experiments are basically implementing the algorithms that they talked about.