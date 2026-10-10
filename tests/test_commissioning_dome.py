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
#include "RadioCheck.h"
#include "AudioCheck.h"
#include <cstring>
#include <string>
#include "CommissionChecklist.h"
#include "CommissionKeepalive.h"
struct Sink : ICommissionSink {
    std::vector<r2link::CommissionRequest> sent; uint16_t seq = 0; size_t limit = size_t(-1);
    bool sendCommission(const r2link::CommissionRequest& r, uint32_t, uint16_t& s) override {
        if (sent.size() >= limit) return false;
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
static BodyRcState rc(std::initializer_list<std::pair<int, uint16_t>> set, bool valid = true) {
    BodyRcState s{}; s.valid = valid; for (auto& c : s.channels) c = 1500;
    s.channels[5] = s.channels[7] = s.channels[8] = 1000; s.channels[4] = 1000; s.channels[6] = 1000;
    for (auto& kv : set) s.channels[kv.first] = kv.second; return s;
}
// Models the Endpoint's ordinary request slots: a send fails while `cap` are in flight.
struct SlotSink : ICommissionSink {
    std::vector<r2link::CommissionRequest> sent; uint16_t seq = 0; size_t inflight = 0, cap = 6;
    bool sendCommission(const r2link::CommissionRequest& r, uint32_t, uint16_t& s) override {
        if (inflight >= cap) return false;
        ++inflight; sent.push_back(r); s = ++seq; return true;
    }
};
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
    assert(s.sent.size() == 1 && s.sent[0].operation == 6 && s.sent[0].value == 1);   // one in flight
    w.tick(11, status(0, 0, 0), true, 4);
    assert(s.sent.size() == 1);                                           // waits for the reply
    for (uint16_t q = 1; q <= 3; ++q) {
        w.onCompletion(done(q, r2link::Result::Accepted));
        w.tick(11 + q, status(0, 0, 0), true, 4);
        assert(s.sent.size() == size_t(q + 1) && w.state() == CommissionWizard::State::Running);
    }
    assert(s.sent[1].value == 2 && s.sent[2].value == 3);
    assert(s.sent[3].operation == 5 && s.sent[3].control_epoch == 4);
    w.onCompletion(done(4, r2link::Result::Accepted));
    assert(w.state() == CommissionWizard::State::Done);
    Sink n; CommissionWizard u(n); u.tick(0, status(0, 0, 0), true, 4);
    assert(u.nudgeNeutral(5, 1500, 10));
    assert(n.sent[0].operation == 4 && n.sent[0].field == 0 && n.sent[0].value == 1505);
    assert(n.sent[1].operation == 1 && n.sent[1].test == 1);
    assert(!u.nudgeNeutral(-200, 1500, 20));                               // 1300 out of 1400-1600
    Sink t; CommissionWizard z(t); z.tick(0, status(0, 0, 0), true, 4);
    assert(!z.startWheelTest(6, 0, false, 10) && t.sent.empty());          // must confirm raised
    assert(z.startWheelTest(6, 1, true, 10));
    assert(t.sent[0].test == 6 && t.sent[0].wheel == 1 && t.sent[0].value == 1);
    assert(!z.applyBaseline(20) && t.sent.size() == 1);                    // refused while a test is running
    z.tick(15, status(t.sent[0].run_id, 6, 2), true, 4);                   // wheel test completed
    z.onCompletion(done(t.seq, r2link::Result::Accepted));
    assert(z.state() == CommissionWizard::State::Done);
    assert(z.applyBaseline(20) && t.sent.back().operation == 7);
''')

    def test_wizard_surfaces_step_send_failure_and_status_watchdog(self):
        self.check(r'''
    Sink s; CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 9);
    w.startDomeCalibration(10);
    w.onCompletion(done(s.seq, r2link::Result::Accepted));
    s.limit = s.sent.size();                                              // next Begin cannot be sent
    w.tick(20, status(s.sent.back().run_id, 2, 2), true, 9);
    assert(w.state() == CommissionWizard::State::Failed && w.lastResult() == uint8_t(r2link::Result::NotReady));
    Sink a; CommissionWizard x(a); x.tick(0, status(0, 0, 0), true, 9);
    x.startNeutral(10);
    x.onCompletion(done(a.seq, r2link::Result::Accepted));
    x.tick(1000, status(a.sent.back().run_id + 1, 1, 1), true, 9);        // someone else's run
    x.tick(2009, status(0, 0, 0), false, 9);
    assert(x.state() == CommissionWizard::State::Running);
    x.tick(2010, status(0, 0, 0), false, 9);                              // 2000 ms since Begin
    assert(x.state() == CommissionWizard::State::Failed && x.lastResult() == uint8_t(r2link::Result::NotReady));
    Sink b; CommissionWizard y(b); y.tick(0, status(0, 0, 0), true, 9);
    y.startNeutral(10);
    y.tick(1500, status(b.sent.back().run_id, 1, 1), true, 9);            // our status resets the clock
    y.tick(3499, status(0, 0, 0), false, 9);
    assert(y.state() == CommissionWizard::State::Running);
    y.tick(3500, status(0, 0, 0), false, 9);
    assert(y.state() == CommissionWizard::State::Failed);
''')

    def test_wizard_refuses_overlapping_starts_and_drops_stale_replies(self):
        self.check(r'''
    Sink s; CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 9);
    assert(w.startNeutral(10));
    assert(!w.startDomeCalibration(11) && !w.startWheelTest(6, 0, true, 11) && !w.applyBaseline(11));
    const uint8_t bits[] = {1};
    assert(!w.acceptAndSave(bits, 1, 11) && s.sent.size() == 1);
    const uint16_t stale = s.seq;                                         // Begin reply never arrived
    assert(w.cancel(12) && w.state() == CommissionWizard::State::Idle);
    assert(w.startNeutral(13));                                           // clears stale pending
    w.onCompletion(done(stale, r2link::Result::Inhibited));
    w.onCompletion(done(2, r2link::Result::Inhibited));                   // the Cancel's reply is stale too
    assert(w.state() == CommissionWizard::State::Running);
    Sink f; CommissionWizard x(f); x.tick(0, status(0, 0, 0), true, 9);
    x.startDomeCalibration(10);
    x.tick(20, status(f.sent.back().run_id, 2, 4, 7), true, 9);
    assert(x.lastError() == 7);
    assert(x.startNeutral(30) && x.lastError() == 0 && x.lastResult() == 0);
''')

    def test_wizard_nudge_cancels_a_running_neutral_and_cancel_reports_failure(self):
        self.check(r'''
    Sink s; CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 4);
    assert(w.startNeutral(10));
    const uint32_t run = s.sent[0].run_id;
    assert(!w.canNudge(500, 1500) && w.canNudge(-5, 1500) && w.canNudge(100, 1500) && !w.canNudge(-101, 1500));
    assert(!w.nudgeNeutral(500, 1500, 20) && s.sent.size() == 1);        // out of range: nothing sent
    assert(w.nudgeNeutral(-5, 1500, 20));
    assert(s.sent.size() == 4 && s.sent[1].operation == 3 && s.sent[1].run_id == run);
    assert(s.sent[2].operation == 4 && s.sent[2].value == 1495 && s.sent[3].operation == 1 && s.sent[3].test == 1);
    assert(w.state() == CommissionWizard::State::Running && w.currentTest() == 1);
    Sink t; CommissionWizard x(t); x.tick(0, status(0, 0, 0), true, 4);
    assert(x.startWheelTest(7, 0, true, 10));
    assert(!x.canNudge(5, 1500));                                          // checked before any dome stop
    assert(!x.nudgeNeutral(5, 1500, 20) && t.sent.size() == 1);          // other tests cannot be nudged
    t.limit = t.sent.size();
    assert(!x.cancel(30) && x.state() == CommissionWizard::State::Running);
    t.limit = size_t(-1);
    assert(x.cancel(40) && x.state() == CommissionWizard::State::Idle && t.sent.back().operation == 3);
''')

    def test_wizard_accept_and_save_paces_through_a_six_slot_endpoint(self):
        self.check(r'''
    SlotSink s; s.inflight = 1;                                           // ProfileMirror Read outstanding
    CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 4);
    const uint8_t bits[] = {6, 7, 8, 9, 10, 11};
    assert(w.acceptAndSave(bits, 6, 10));
    size_t replied = 0; uint32_t t = 10;
    while (w.state() == CommissionWizard::State::Running && t < 1000) {
        ++t; w.tick(t, status(0, 0, 0), true, 4);
        while (replied < s.sent.size()) {                                 // body answers each request
            w.onCompletion(done(uint16_t(++replied), r2link::Result::Accepted)); --s.inflight;
        }
    }
    assert(s.sent.size() == 7 && w.state() == CommissionWizard::State::Done);
    for (uint8_t i = 0; i < 6; ++i) assert(s.sent[i].operation == 6 && s.sent[i].value == bits[i]);
    assert(s.sent[6].operation == 5);
''')

    def test_wizard_accept_and_save_sends_save_after_a_refusal_and_fails_after_it(self):
        self.check(r'''
    Sink s; CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 4);
    const uint8_t bits[] = {6, 7, 8};
    assert(w.acceptAndSave(bits, 3, 10));
    w.onCompletion(done(1, r2link::Result::Accepted));  w.tick(11, status(0, 0, 0), true, 4);
    r2link::Completion refused = done(2, r2link::Result::Inhibited);
    w.onCompletion(refused);                            w.tick(12, status(0, 0, 0), true, 4);
    assert(w.state() == CommissionWizard::State::Running && s.sent.size() == 3);   // keeps going
    w.onCompletion(done(3, r2link::Result::NotReady));  w.tick(13, status(0, 0, 0), true, 4);
    assert(s.sent.size() == 4 && s.sent[3].operation == 5);               // Save always sent last
    assert(w.state() == CommissionWizard::State::Running);                // not until Save completes
    w.onCompletion(done(4, r2link::Result::Accepted));
    assert(w.state() == CommissionWizard::State::Failed);
    assert(w.lastResult() == uint8_t(r2link::Result::Inhibited) && w.lastError() == 0);   // first refusal
    // A refused Save fails even when every Accept landed; a lost reply counts as NotReady.
    Sink r; CommissionWizard v(r); v.tick(0, status(0, 0, 0), true, 4);
    const uint8_t one[] = {4};
    v.acceptAndSave(one, 1, 10);
    v.onCompletion(done(1, r2link::Result::Accepted)); v.tick(11, status(0, 0, 0), true, 4);
    r2link::Completion lost = done(2, r2link::Result::Accepted); lost.outcome = r2link::Outcome::TimedOut;
    v.onCompletion(lost);
    assert(v.state() == CommissionWizard::State::Failed && v.lastResult() == uint8_t(r2link::Result::NotReady));
''')

    def test_wizard_accept_and_save_retries_a_busy_sink_within_a_bound(self):
        self.check(r'''
    uint8_t bits[13]; for (uint8_t i = 0; i < 13; ++i) bits[i] = i;
    Sink c; CommissionWizard q(c); q.tick(0, status(0, 0, 0), true, 4);
    assert(!q.acceptAndSave(bits, 13, 10) && c.sent.empty());           // only 12 acceptance bits exist
    assert(q.state() == CommissionWizard::State::Idle);
    SlotSink s; s.inflight = s.cap;                                       // endpoint full
    CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 4);
    assert(w.acceptAndSave(bits, 2, 10) && s.sent.empty());
    w.tick(500, status(0, 0, 0), true, 4);
    assert(w.state() == CommissionWizard::State::Running && s.sent.empty());
    s.inflight = 0;                                                       // a slot frees: retried
    w.tick(600, status(0, 0, 0), true, 4);
    assert(s.sent.size() == 1 && s.sent[0].value == 0);
    w.onCompletion(done(1, r2link::Result::Accepted)); s.inflight = s.cap;
    w.tick(700, status(0, 0, 0), true, 4);
    w.tick(2699, status(0, 0, 0), true, 4);
    assert(w.state() == CommissionWizard::State::Running && s.sent.size() == 1);
    w.tick(2700, status(0, 0, 0), true, 4);                               // 2000 ms without a slot
    assert(w.state() == CommissionWizard::State::Failed && w.lastResult() == uint8_t(r2link::Result::NotReady));
    // A request whose reply never arrives cannot hold the wizard in Running forever.
    Sink n; CommissionWizard x(n); x.tick(0, status(0, 0, 0), true, 4);
    x.acceptAndSave(bits, 1, 10);
    x.tick(2009, status(0, 0, 0), true, 4);
    assert(x.state() == CommissionWizard::State::Running);
    x.tick(2010, status(0, 0, 0), true, 4);
    assert(x.state() == CommissionWizard::State::Failed && x.lastResult() == uint8_t(r2link::Result::NotReady));
    assert(x.startNeutral(2020));                                         // usable again
''')

    def test_radio_check_walks_every_prompt_and_detects_failsafe_frames(self):
        self.check(r'''
    RadioCheck r; r.start(0); uint32_t t = 0;
    auto feed = [&](BodyRcState s) { t += 20; r.tick(s, t); };
    feed(rc({{1, 1900}}));                      // right stick up
    feed(rc({{0, 1900}}));                      // right stick right
    feed(rc({{3, 1900}}));                      // left stick right
    feed(rc({{5, 1900}}));                      // SwA down
    feed(rc({{4, 1000}})); feed(rc({{4, 1500}})); feed(rc({{4, 2000}}));   // SwC three positions
    feed(rc({{7, 1900}}));                      // SwB down
    feed(rc({{8, 1900}}));                      // SwD down
    feed(rc({{6, 1000}})); feed(rc({{6, 2000}}));                          // knob sweep
    assert(r.step() == 8 && r.state() == RadioCheck::State::Prompting);    // failsafe prompt
    // Switches already in failsafe positions must not pass on their own.
    for (int i = 0; i < 100; ++i) feed(rc({}));
    assert(r.state() == RadioCheck::State::Prompting);
    feed(rc({{5, 1900}, {7, 1900}, {8, 1900}}));                           // armed first
    for (int i = 0; i < 60; ++i) feed(rc({}));                             // TX off: failsafe frames
    assert(r.state() == RadioCheck::State::Passed && r.failsafe() == RadioCheck::Failsafe::FrameValues);
''', sources=[ASTRO / "RadioCheck.cpp"])

    def test_radio_check_reports_missing_frames_bad_values_and_timeouts(self):
        self.check(r'''
    { RadioCheck r; r.start(0); r.tick(rc({}), 20000);
      assert(r.state() == RadioCheck::State::Failed && r.step() == 0); }
    auto toFailsafe = [](RadioCheck& r, uint32_t& t) {
        const std::initializer_list<std::pair<int, uint16_t>> steps[] = {
            {{1, 1900}}, {{0, 1900}}, {{3, 1900}}, {{5, 1900}}, {{4, 1000}}, {{4, 1500}}, {{4, 2000}},
            {{7, 1900}}, {{8, 1900}}, {{6, 1000}}, {{6, 2000}}};
        for (auto& s : steps) { t += 20; r.tick(rc(s), t); }
        t += 20; r.tick(rc({{5, 1900}, {7, 1900}, {8, 1900}}), t);
    };
    { RadioCheck r; r.start(0); uint32_t t = 0; toFailsafe(r, t);
      for (int i = 0; i < 20; ++i) { t += 20; r.tick(rc({}, false), t); }
      assert(r.state() == RadioCheck::State::Passed && r.failsafe() == RadioCheck::Failsafe::NoFrames); }
    { RadioCheck r; r.start(0); uint32_t t = 0; toFailsafe(r, t);
      for (int i = 0; i < 60; ++i) { t += 20; r.tick(rc({{3, 1800}}), t); }   // CH4 held off-centre
      assert(r.state() == RadioCheck::State::Failed && r.failsafe() == RadioCheck::Failsafe::BadValues); }
''', sources=[ASTRO / "RadioCheck.cpp"])

    def test_mirror_forgets_every_cached_field_when_the_link_drops(self):
        self.check(r'''
    Reader r; ProfileMirror m(r); r2link::Diagnostics none{};
    m.tick(1000, true, none, 0);
    m.tick(1010, true, reply(0, 0, 1512), 1010);
    int32_t v = 0; assert(m.value(0, 0, v) && v == 1512 && m.known(0, 0));
    m.tick(1100, false, none, 0);                                         // link lost
    assert(!m.known(0, 0) && !m.value(0, 0, v));
    for (uint8_t w = 0; w < 2; ++w) for (uint8_t f = 5; f <= 18; ++f) assert(!m.known(f, w));
    const size_t asked = r.asked.size();
    m.tick(1200, true, none, 0);                                          // link back: reads resume
    assert(r.asked.size() == asked + 1);
''')

    def test_wizard_ignores_late_replies_once_it_is_no_longer_running(self):
        self.check(r'''
    Sink s; CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 9);
    assert(w.startNeutral(10));
    const uint16_t begin_seq = s.seq;
    assert(w.cancel(20) && w.state() == CommissionWizard::State::Idle);
    w.onCompletion(done(begin_seq, r2link::Result::Inhibited));          // late refusal of the Begin
    assert(w.state() == CommissionWizard::State::Idle && w.lastResult() == 0);
    w.onCompletion(done(s.seq, r2link::Result::NotReady));               // the Cancel refused late
    assert(w.state() == CommissionWizard::State::Idle && w.lastResult() == 0);
    // A Failed wizard keeps its first error.
    Sink f; CommissionWizard x(f); x.tick(0, status(0, 0, 0), true, 9);
    x.startDomeCalibration(10);
    x.tick(20, status(f.sent.back().run_id, 2, 4, 3), true, 9);
    assert(x.state() == CommissionWizard::State::Failed && x.lastError() == 3);
    x.onCompletion(done(f.seq, r2link::Result::Busy));
    assert(x.state() == CommissionWizard::State::Failed && x.lastError() == 3 && x.lastResult() == 0);
''')

    def test_browser_heartbeat_gates_the_wheel_test_keepalive_only(self):
        self.check(r'''
    static_assert(BrowserHeartbeat::kPeriodMs == 150 && BrowserHeartbeat::kWindowMs == 500, "spec values");
    BrowserHeartbeat hb;
    assert(!hb.fresh(0) && !hb.fresh(1000));                              // never seen
    for (uint8_t t = 1; t <= 5; ++t) assert(commissionKeepaliveAllowed(t, false));   // dome tests: ESP32 keepalive
    for (uint8_t t = 6; t <= 8; ++t) assert(!commissionKeepaliveAllowed(t, false) && commissionKeepaliveAllowed(t, true));
    hb.beat(1000);
    assert(hb.fresh(1000) && hb.fresh(1500) && !hb.fresh(1501));
    hb.beat(0);                                                           // a beat at t=0 still counts
    assert(hb.fresh(400));
    hb.beat(0xFFFFFF00u);                                                 // millis() wrap
    assert(hb.fresh(0x000000F0u) && !hb.fresh(0x00000200u));
''')

    def test_radio_check_stick_prompts_need_swa_up_and_prompts_are_js_safe(self):
        self.check(r'''
    RadioCheck r; r.start(0); uint32_t t = 0;
    auto feed = [&](BodyRcState s) { t += 20; r.tick(s, t); };
    assert(std::strstr(r.prompt(), "SwA UP"));
    feed(rc({{1, 1900}, {5, 1900}})); assert(r.step() == 0);            // SwA DOWN: does not count
    assert(std::strstr(r.prompt(), "SwA UP"));
    feed(rc({{1, 1900}})); assert(r.step() == 1);
    assert(std::strstr(r.prompt(), "SwA UP"));
    feed(rc({{0, 1900}, {5, 1300}})); assert(r.step() == 1);            // CH6 not below 1250
    feed(rc({{0, 1900}})); assert(r.step() == 2);
    assert(std::strstr(r.prompt(), "SwA UP"));
    feed(rc({{3, 1900}, {5, 1900}})); assert(r.step() == 2);
    feed(rc({{3, 1900}})); assert(r.step() == 3);
    feed(rc({{5, 1900}}));
    feed(rc({{4, 1000}})); feed(rc({{4, 1500}})); feed(rc({{4, 2000}}));
    feed(rc({{7, 1900}}));
    assert(r.step() == 6 && std::strstr(r.prompt(), "dome may turn"));
    // ReelTwo puts prompts into single-quoted JavaScript strings without escaping.
    RadioCheck all; all.start(0); uint32_t u = 0;
    const std::initializer_list<std::pair<int, uint16_t>> steps[] = {
        {{1, 1900}}, {{0, 1900}}, {{3, 1900}}, {{5, 1900}}, {{4, 1000}}, {{4, 1500}}, {{4, 2000}},
        {{7, 1900}}, {{8, 1900}}, {{6, 1000}}, {{6, 2000}}};
    auto safe = [](const char* p) { return !std::strchr(p, '\'') && !std::strchr(p, '"') && !std::strchr(p, '\\'); };
    assert(safe(all.prompt()));
    for (auto& s : steps) { u += 20; all.tick(rc(s), u); assert(safe(all.prompt())); }
    assert(all.step() == 8);
''', sources=[ASTRO / "RadioCheck.cpp"])

    def test_audio_check(self):
        self.check(r'''
    { AudioCheck a; a.begin(7, true, 100);
      r2link::Event ev{}; ev.kind = uint8_t(r2link::EventKind::PlaybackStarted);
      ev.request_type = uint8_t(r2link::MessageType::AudioRequest); ev.request_seq = 7;
      a.onEvent(ev); assert(a.state() == AudioCheck::State::Passed); }
    { AudioCheck a; a.begin(7, true, 100); a.tick(3200); assert(a.state() == AudioCheck::State::Failed); }
    { AudioCheck a; a.begin(0, false, 100); assert(a.state() == AudioCheck::State::Failed); }
    { AudioCheck a; a.begin(7, true, 100);
      r2link::Completion c{}; c.type = r2link::MessageType::AudioRequest; c.sequence = 7;
      c.outcome = r2link::Outcome::Replied; c.result = uint8_t(r2link::Result::NotReady);
      a.onCompletion(c); assert(a.state() == AudioCheck::State::Failed); }
''', sources=[ASTRO / "AudioCheck.cpp"])


    def test_checklist_lines_and_next_step(self):
        self.check(r'''
    ChecklistInput in{}; in.status_fresh = true;
    char buf[512];
    formatChecklist(in, buf, sizeof buf);
    assert(std::strstr(buf, "[ ] Baseline filled") && std::strstr(buf, "[ ] VESC config L"));
    assert(std::string(nextChecklistStep(in)) == "Baseline filled");
    in.baseline_filled = true; in.saved_acceptance = 0x0FFF; in.radio_passed = in.failsafe_passed = in.audio_passed = true;
    formatChecklist(in, buf, sizeof buf);
    assert(!std::strstr(buf, "[ ]") && std::string(nextChecklistStep(in)) == "All done");
    in.unsaved = true; assert(std::string(nextChecklistStep(in)) == "Save profile");
    in.unsaved = false; in.saved_acceptance = 0x0FFF & ~(1u << 9);
    assert(std::string(nextChecklistStep(in)) == "Direction R");
    assert(formatChecklist(in, buf, 8) <= 7 && std::strlen(buf) <= 7);   // truncates safely
''', sources=[ASTRO / "CommissionChecklist.cpp"])


if __name__ == "__main__":
    unittest.main()
