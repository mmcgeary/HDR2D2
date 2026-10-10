#include "CommissionWizard.h"

CommissionWizard::CommissionWizard(ICommissionSink& sink) : sink_(sink) {}

bool CommissionWizard::send(r2link::CommissionRequest req, uint32_t now) {
    req.control_epoch = epoch_;
    uint16_t seq = 0;
    if (pending_count_ >= kPendingCap || !sink_.sendCommission(req, now, seq)) return false;
    pending_[pending_count_++] = seq;
    return true;
}

bool CommissionWizard::begin(uint8_t test, uint8_t wheel, int32_t value, uint32_t now) {
    r2link::CommissionRequest req{};
    req.operation = 1; req.test = test; req.wheel = wheel; req.value = value;
    if (++run_counter_ == 0) run_counter_ = 1;
    req.run_id = (now << 8) ^ run_counter_;
    if (req.run_id == 0) req.run_id = 1;
    if (!send(req, now)) return false;
    test_ = test; run_id_ = req.run_id; waiting_status_ = true; status_seen_ms_ = now; state_ = State::Running;
    return true;
}

bool CommissionWizard::prepareStart() {
    if (state_ == State::Running) return false;
    pending_count_ = 0; last_error_ = 0; last_result_ = 0;
    return true;
}

bool CommissionWizard::startDomeCalibration(uint32_t now) {
    if (!prepareStart()) return false;
    const uint8_t order[] = {2, 3, 4, 5};
    for (uint8_t i = 0; i < 4; ++i) queue_[i] = order[i];
    queue_len_ = 4; queue_pos_ = 0;
    return begin(queue_[queue_pos_++], 0, 0, now);
}
bool CommissionWizard::startNeutral(uint32_t now) {
    if (!prepareStart()) return false;
    queue_len_ = queue_pos_ = 0;
    return begin(1, 0, 0, now);
}
bool CommissionWizard::startWheelTest(uint8_t test, uint8_t wheel, bool raised, uint32_t now) {
    if (!raised || test < 6 || test > 8 || wheel > 1) return false;
    if (!prepareStart()) return false;
    queue_len_ = queue_pos_ = 0;
    return begin(test, wheel, 1, now);
}
bool CommissionWizard::nudgeNeutral(int16_t delta, int32_t current, uint32_t now) {
    const int32_t next = current + delta;
    if (next < 1400 || next > 1600) return false;
    if (state_ == State::Running) {
        if (test_ != 1) return false;                                   // only a Neutral hold may be nudged
        r2link::CommissionRequest c{}; c.operation = 3; c.run_id = run_id_;
        if (!send(c, now)) return false;
        queue_len_ = queue_pos_ = 0; waiting_status_ = false; state_ = State::Idle;
    }
    if (!prepareStart()) return false;
    r2link::CommissionRequest set{}; set.operation = 4; set.field = 0; set.value = next;
    if (!send(set, now)) return false;
    queue_len_ = queue_pos_ = 0;
    return begin(1, 0, 0, now);
}
bool CommissionWizard::acceptAndSave(const uint8_t* bits, uint8_t count, uint32_t now) {
    if (!prepareStart()) return false;
    if (count >= kPendingCap) return false;                             // count Accepts + Save must fit
    queue_len_ = queue_pos_ = 0; test_ = 0; waiting_status_ = false;
    for (uint8_t i = 0; i < count; ++i) {
        r2link::CommissionRequest acc{}; acc.operation = 6; acc.value = bits[i];
        if (!send(acc, now)) { fail(0, uint8_t(r2link::Result::NotReady)); return false; }
    }
    r2link::CommissionRequest save{}; save.operation = 5;
    if (!send(save, now)) { fail(0, uint8_t(r2link::Result::NotReady)); return false; }
    state_ = State::Running;
    return true;
}
bool CommissionWizard::applyBaseline(uint32_t now) {
    if (!prepareStart()) return false;
    r2link::CommissionRequest b{}; b.operation = 7;
    if (!send(b, now)) return false;
    state_ = State::Running; waiting_status_ = false; test_ = 0;
    return true;
}
bool CommissionWizard::cancel(uint32_t now) {
    r2link::CommissionRequest c{}; c.operation = 3; c.run_id = run_id_;
    if (!send(c, now)) return false;                                    // state unchanged: still cancellable
    queue_len_ = queue_pos_ = 0; waiting_status_ = false; state_ = State::Idle;
    return true;
}
void CommissionWizard::fail(uint16_t error, uint8_t result) {
    last_error_ = error; last_result_ = result; state_ = State::Failed;
    queue_len_ = queue_pos_ = 0; waiting_status_ = false; pending_count_ = 0;
}
void CommissionWizard::onCompletion(const r2link::Completion& c) {
    if (c.type != r2link::MessageType::CommissionRequest) return;
    for (uint8_t i = 0; i < pending_count_; ++i) {
        if (pending_[i] != c.sequence) continue;
        for (uint8_t j = i + 1; j < pending_count_; ++j) pending_[j - 1] = pending_[j];
        --pending_count_;
        const bool ok = c.outcome == r2link::Outcome::Replied && c.result == uint8_t(r2link::Result::Accepted);
        if (!ok) { fail(0, c.outcome == r2link::Outcome::Replied ? c.result : uint8_t(r2link::Result::NotReady)); return; }
        if (state_ == State::Running && !waiting_status_ && queue_pos_ == queue_len_ && pending_count_ == 0) state_ = State::Done;
        return;
    }
}
void CommissionWizard::tick(uint32_t now, const r2link::CommissionStatus& s, bool fresh, uint16_t epoch) {
    epoch_ = epoch;
    if (state_ != State::Running || !waiting_status_) return;
    if (fresh && s.run_id == run_id_) {
        status_seen_ms_ = now;
        if (s.state == 1) return;                                       // Running
        if (s.state == 2) {                                             // Completed
            waiting_status_ = false;
            if (queue_pos_ < queue_len_) {
                if (!begin(queue_[queue_pos_++], 0, 0, now)) fail(0, uint8_t(r2link::Result::NotReady));
                return;
            }
            if (pending_count_ == 0) state_ = State::Done;
            return;
        }
        fail(s.error, 0);                                               // Cancelled / Failed / TimedOut
        return;
    }
    if (uint32_t(now - status_seen_ms_) >= kStatusTimeoutMs) fail(0, uint8_t(r2link::Result::NotReady));
}
CommissionWizard::State CommissionWizard::state() const { return state_; }
uint8_t CommissionWizard::currentTest() const { return test_; }
uint16_t CommissionWizard::lastError() const { return last_error_; }
uint8_t CommissionWizard::lastResult() const { return last_result_; }
