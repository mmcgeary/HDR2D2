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
#include "CommissionWizard.h"
struct Sink : ICommissionSink {
    std::vector<r2link::CommissionRequest> sent; uint16_t seq = 0;
    bool sendCommission(const r2link::CommissionRequest& r, uint32_t, uint16_t& s) override {
        sent.push_back(r); s = ++seq; return true;
    }
};
static r2link::CommissionStatus status(uint32_t run, uint8_t test, uint8_t state, uint16_t error = 0) {
    r2link::CommissionStatus s{}; s.run_id = run; s.test = test; s.state = state; s.error = error; return s;
}
static r2link::Completion done(uint16_t seq, r2link::Result r) {
    r2link::Completion c{}; c.type = r2link::MessageType::CommissionRequest; c.sequence = seq;
    c.outcome = r2link::Outcome::Replied; c.result = uint8_t(r); return c;
}
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
                         extra_sources=[ASTRO / "ProfileMirror.cpp", ASTRO / "CommissionWizard.cpp"] + list(sources), include_dirs=[ASTRO, SHARED])
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

    def test_wizard_runs_the_dome_sequence_and_stops_on_failure(self):
        self.check(r'''
    Sink s; CommissionWizard w(s);
    w.tick(0, status(0, 0, 0), true, 9);
    assert(w.startDomeCalibration(10));
    const uint8_t order[] = {2, 3, 4, 5};
    for (uint8_t i = 0; i < 4; ++i) {
        const r2link::CommissionRequest& b = s.sent.back();
        assert(b.operation == 1 && b.test == order[i] && b.control_epoch == 9 && b.run_id != 0);
        w.onCompletion(done(s.seq, r2link::Result::Accepted));
        w.tick(20 + i, status(b.run_id, b.test, 1), true, 9);           // running
        assert(w.state() == CommissionWizard::State::Running && s.sent.size() == size_t(i + 1));
        w.tick(30 + i, status(b.run_id, b.test, 2), true, 9);           // completed -> next
    }
    assert(w.state() == CommissionWizard::State::Done);
    Sink f; CommissionWizard x(f); x.tick(0, status(0, 0, 0), true, 9);
    x.startDomeCalibration(10);
    x.tick(20, status(f.sent.back().run_id, 2, 4, 2), true, 9);           // Failed, hall stale
    assert(x.state() == CommissionWizard::State::Failed && x.lastError() == 2 && f.sent.size() == 1);
    Sink g; CommissionWizard y(g); y.tick(0, status(0, 0, 0), true, 9);
    y.startDomeCalibration(10);
    y.onCompletion(done(g.seq, r2link::Result::Inhibited));               // Begin refused
    assert(y.state() == CommissionWizard::State::Failed && y.lastResult() == uint8_t(r2link::Result::Inhibited));
''')

    def test_wizard_accept_and_save_batch_and_single_steps(self):
        self.check(r'''
    Sink s; CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 4);
    const uint8_t bits[] = {1, 2, 3};
    assert(w.acceptAndSave(bits, 3, 10));
    assert(s.sent.size() == 4 && s.sent[0].operation == 6 && s.sent[0].value == 1 && s.sent[2].value == 3);
    assert(s.sent[3].operation == 5 && s.sent[3].control_epoch == 4);
    for (uint16_t q = 1; q <= 3; ++q) w.onCompletion(done(q, r2link::Result::Accepted));
    assert(w.state() == CommissionWizard::State::Running);
    w.onCompletion(done(4, r2link::Result::Accepted));
    assert(w.state() == CommissionWizard::State::Done);
    Sink r; CommissionWizard v(r); v.tick(0, status(0, 0, 0), true, 4);
    v.acceptAndSave(bits, 3, 10);
    v.onCompletion(done(2, r2link::Result::Inhibited));
    assert(v.state() == CommissionWizard::State::Failed && v.lastResult() == uint8_t(r2link::Result::Inhibited));
    Sink n; CommissionWizard u(n); u.tick(0, status(0, 0, 0), true, 4);
    assert(u.nudgeNeutral(5, 1500, 10));
    assert(n.sent[0].operation == 4 && n.sent[0].field == 0 && n.sent[0].value == 1505);
    assert(n.sent[1].operation == 1 && n.sent[1].test == 1);
    assert(!u.nudgeNeutral(-200, 1500, 20));                               // 1300 out of 1400-1600
    Sink t; CommissionWizard z(t); z.tick(0, status(0, 0, 0), true, 4);
    assert(!z.startWheelTest(6, 0, false, 10) && t.sent.empty());          // must confirm raised
    assert(z.startWheelTest(6, 1, true, 10));
    assert(t.sent[0].test == 6 && t.sent[0].wheel == 1 && t.sent[0].value == 1);
    assert(z.applyBaseline(20) && t.sent.back().operation == 7);
''')


if __name__ == "__main__":
    unittest.main()
