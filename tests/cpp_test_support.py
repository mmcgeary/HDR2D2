import subprocess
import tempfile
from pathlib import Path


def run_cpp(program, std="c++11", extra_sources=(), include_dirs=()):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        source.write_text(program)
        command = ["c++", f"-std={std}", "-Wall", "-Wextra", str(source)]
        command += [str(path) for path in extra_sources]
        for include in include_dirs:
            command += ["-I", str(include)]
        command += ["-o", str(binary)]
        compilation = subprocess.run(command, capture_output=True, text=True)
        if compilation.returncode:
            raise AssertionError(compilation.stderr)
        return subprocess.run([str(binary)], capture_output=True, text=True)
