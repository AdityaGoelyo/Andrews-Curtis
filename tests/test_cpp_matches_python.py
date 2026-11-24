import json
import subprocess
import sys

import pytest

def run(cmd, out):
    subprocess.run(cmd + ["-p", "data/ms_presentations.json",
                          "--max-nodes", "1000", "--out", str(out)], check=True)
    with open(out) as f:
        return json.load(f)

@pytest.mark.parametrize("py_script, cpp_binary", [
    ("python_scripts/breadth_first_search.py", "cpp_ports/breadth_first_search"),
    ("python_scripts/greedy_search.py",        "cpp_ports/greedy_search"),
    ("python_scripts/substitution.py",         "cpp_ports/substitution"),
])
def test_cpp_matches_python(py_script, cpp_binary, tmp_path):
    py  = run([sys.executable, py_script], tmp_path / "py.json")
    cpp = run([cpp_binary],                tmp_path / "cpp.json")
    assert py == cpp