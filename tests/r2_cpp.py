"""Compiles and runs a C++ test body against the dome firmware headers.

Test bodies use CHECK(cond) and CHECK_NEAR(a, b, tol). Random sources:
lowest (always lo), highest (always hi - 1), seeded (repeatable LCG).
"""

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r"""
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (std::fabs(_a - _b) > (tol)) { \
    std::printf("FAIL line %d: %s = %f, expected %f +/- %f\n", __LINE__, #a, _a, _b, (double)(tol)); failures++; } } while (0)
static uint32_t lowest(uint32_t lo, uint32_t) { return lo; }
static uint32_t highest(uint32_t lo, uint32_t hi) { return hi > lo ? hi - 1 : lo; }
static uint32_t sLcg = 12345;
static uint32_t seeded(uint32_t lo, uint32_t hi)
{
    sLcg = sLcg * 1103515245u + 12345u;
    return hi > lo ? lo + (sLcg >> 8) % (hi - lo) : lo;
}
"""


def run_cpp(includes, body, helpers="", optimize=False):
    """Builds PRELUDE + includes + helpers + main(){body} and runs it. Raises on failure."""
    source = PRELUDE
    source += "".join(f'#include "{name}"\n' for name in includes)
    source += "using namespace r2;\n" + helpers
    source += "\nint main() {\n" + body + "\n    return failures ? 1 : 0;\n}\n"
    with tempfile.TemporaryDirectory() as directory:
        src = Path(directory) / "test.cpp"
        exe = Path(directory) / "test"
        src.write_text(source)
        command = ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                   "-I", str(ROOT / "AstroPixelsPlus"), "-I", str(ROOT / "tests")]
        if optimize:
            command.append("-O2")
        compiled = subprocess.run(command + [str(src), "-o", str(exe)], capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)
        if result.returncode:
            raise AssertionError(result.stdout[-4000:])
        return result.stdout
