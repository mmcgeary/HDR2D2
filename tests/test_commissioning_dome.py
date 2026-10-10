"""Guided commissioning: dome-side mirror, wizard, radio/audio checks, checklist."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
ASTRO = ROOT / "ASTROPIXELS_PLUS_UNIFIED"
SHARED = ROOT / "shared/R2BodyLink"
PRELUDE = r'''
#include <cassert>
#include <vector>
#include "Messages.h"
#include "ProfileMirror.h"
struct Reader : IFieldReader {
    std::vector<std::pair<uint8_t, uint8_t>> asked; bool ok = true;
    bool requestRead(uint8_t f, uint8_t w, uint32_t) override { if (ok) asked.push_back({f, w}); return ok; }
};
static r2link::Diagnostics reply(uint8_t f, uint8_t w, int32_t v, uint8_t known = 1) {
    r2link::Diagnostics d{}; d.subtype = 1; d.field = f; d.wheel = w; d.value = v; d.known = known; return d;
}
'''


class DomeCommissioningTests(unittest.TestCase):
    def check(self, body, sources=()):
        result = run_cpp(PRELUDE + "int main() {\n" + body + "\nreturn 0;\n}\n",
                         extra_sources=[ASTRO / "ProfileMirror.cpp"] + list(sources), include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_mirror_reads_every_field_once_per_pass_and_caches_replies(self):
        self.check(r'''
    Reader r; ProfileMirror m(r);
    r2link::Diagnostics none{}; uint32_t now = 1000;
    m.tick(now, true, none, 0);
    assert(r.asked.size() == 1 && r.asked[0].first == 0 && r.asked[0].second == 0);   // servo_neutral first
    m.tick(now + 10, true, reply(0, 0, 1512), now + 10);                             // reply arrives
    int32_t v = 0; assert(m.value(0, 0, v) && v == 1512);
    for (uint32_t t = now + 60; t < now + 60 + 35 * 60; t += 60) {
        const auto& last = r.asked.back();
        m.tick(t, true, reply(last.first, last.second, 7, last.first == 5 ? 0 : 1), t);
    }
    assert(r.asked.size() >= 35);
    assert(!m.value(5, 1, v) && m.known(5, 1));                                       // direction unset
    assert(m.value(18, 1, v) && v == 7);
''')

    def test_mirror_times_out_pauses_without_link_and_rereads_invalidated(self):
        self.check(r'''
    Reader r; ProfileMirror m(r); r2link::Diagnostics none{};
    m.tick(1000, false, none, 0); assert(r.asked.empty());
    m.tick(1000, true, none, 0); assert(r.asked.size() == 1);
    m.tick(1100, true, none, 0); assert(r.asked.size() == 1);           // waiting for reply
    m.tick(1300, true, none, 0); assert(r.asked.size() == 2);           // 250ms timeout, moved on
    m.invalidate(12, 1);
    m.tick(1600, true, none, 0);
    assert(r.asked.back().first == 12 && r.asked.back().second == 1);
''')


if __name__ == "__main__":
    unittest.main()
