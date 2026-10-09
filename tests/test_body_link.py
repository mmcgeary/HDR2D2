import unittest
from pathlib import Path

from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "shared"
CODEC = SHARED / "R2BodyLink/src/Codec.cpp"

PRELUDE = r'''
#include "R2BodyLink/Codec.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace r2link;
typedef std::vector<uint8_t> Bytes;

static Bytes cobs(const Bytes& in) {
    Bytes out; size_t at = 0; out.push_back(0); uint8_t code = 1;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == 0) { out[at] = code; at = out.size(); out.push_back(0); code = 1; }
        else { out.push_back(in[i]); if (++code == 0xFF) { out[at] = code; at = out.size(); out.push_back(0); code = 1; } }
    }
    out[at] = code; out.push_back(0); return out;
}
// Independently builds a wire frame with arbitrary header values and a valid CRC.
static Bytes raw(uint8_t version, uint8_t type, uint8_t flags, uint8_t reserved, uint16_t seq,
                 uint32_t src, uint32_t dst, int length_field, const Bytes& payload) {
    Bytes r;
    r.push_back(version); r.push_back(type); r.push_back(flags); r.push_back(reserved);
    r.push_back(seq); r.push_back(seq >> 8);
    for (int i = 0; i < 4; ++i) r.push_back(src >> (8 * i));
    for (int i = 0; i < 4; ++i) r.push_back(dst >> (8 * i));
    r.push_back(length_field); r.push_back(length_field >> 8);
    r.insert(r.end(), payload.begin(), payload.end());
    uint16_t crc = crc16(&r[0], r.size());
    r.push_back(crc); r.push_back(crc >> 8);
    return r;
}
static Bytes wire(const Bytes& r) { return cobs(r); }
static Frame makeFrame(uint16_t seq, size_t length, uint8_t fill) {
    Frame f; memset(&f, 0, sizeof f);
    f.version = 1; f.type = MessageType::Heartbeat; f.sequence = seq;
    f.source_session = 1; f.destination_session = 2; f.length = length;
    for (size_t i = 0; i < length; ++i) f.payload[i] = fill + i;
    return f;
}
static DecodeResult feedAll(Codec& c, const Bytes& b, uint32_t t, Frame& f, int* ready = 0) {
    DecodeResult last = DecodeResult::None;
    for (size_t i = 0; i < b.size(); ++i) {
        DecodeResult r = c.feed(b[i], t, f);
        if (r != DecodeResult::None) last = r;
        if (r == DecodeResult::FrameReady && ready) ++*ready;
    }
    return last;
}
'''


def run(body, **kw):
    result = run_cpp(PRELUDE + "int main() {\n" + body + "\nputs(\"ok\"); return 0; }\n",
                     extra_sources=[CODEC], include_dirs=[SHARED], **kw)
    return result


class BodyLinkTests(unittest.TestCase):
    def check(self, body):
        result = run(body)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_brief_heartbeat_round_trip(self):
        self.check(r'''
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    assert(crc16(check, sizeof(check)) == 0x31c3);
    Frame frame{};
    frame.version = 1; frame.type = MessageType::Heartbeat;
    frame.source_session = 1; frame.destination_session = 2;
    frame.length = 2; frame.payload[0] = 1; frame.payload[1] = 1;
    uint8_t w[116]{};
    const size_t count = Codec::encode(frame, w, sizeof(w));
    assert(count && w[count - 1] == 0);
    Codec parser; Frame decoded{}; bool received = false;
    for (size_t i = 0; i < count; ++i)
        received |= parser.feed(w[i], 10, decoded) == DecodeResult::FrameReady;
    assert(received && decoded.source_session == 1 && decoded.length == 2);
    assert(newer16(0, 65535));
''')

    def test_golden_wire_vectors(self):
        self.check(r'''
    const uint8_t hb[] = {0x03,0x01,0x02,0x01,0x01,0x01,0x02,0x01,0x01,0x01,0x02,0x02,0x01,0x01,0x02,0x02,0x05,0x01,0x01,0x5e,0x93,0x00};
    Frame f = makeFrame(0, 2, 1); f.payload[0] = 1; f.payload[1] = 1;
    uint8_t w[116];
    assert(Codec::encode(f, w, sizeof w) == sizeof hb && memcmp(w, hb, sizeof hb) == 0);
    Codec p; Frame d; int ready = 0;
    for (size_t i = 0; i < sizeof hb; ++i) ready += p.feed(hb[i], 1, d) == DecodeResult::FrameReady;
    assert(ready == 1 && d.type == MessageType::Heartbeat);
    // CommissionRequest: set_field, flags bit0, little-endian 0x1234 sequence, embedded zeros, negative value.
    const uint8_t cr[] = {0x04,0x01,0x24,0x01,0x07,0x34,0x12,0x44,0x33,0x22,0x11,0x01,0x01,0x01,0x02,0x0e,0x02,0x04,0x0f,0xd0,0xc0,0xb0,0xa0,0x07,0x01,0xfe,0xff,0xff,0xff,0xef,0xbe,0x2f,0x47,0x00};
    Frame g; memset(&g, 0, sizeof g);
    g.version = 1; g.type = MessageType::CommissionRequest; g.flags = 1; g.sequence = 0x1234;
    g.source_session = 0x11223344; g.destination_session = 0;
    ErrorCounters c; CommissionRequest m = {4, 0, 0xA0B0C0D0u, 7, 1, -2, 0xBEEF};
    assert(encode(m, g, c) == Status::Ok && g.length == 14);
    uint8_t w2[116];
    assert(Codec::encode(g, w2, sizeof w2) == sizeof cr && memcmp(w2, cr, sizeof cr) == 0);
''')

    def test_every_split_point_back_to_back_and_embedded_zeros(self):
        self.check(r'''
    Frame a = makeFrame(7, 20, 0); a.payload[3] = 0; a.payload[10] = 0;
    Frame b = makeFrame(8, 96, 0);
    uint8_t wa[116], wb[116];
    size_t na = Codec::encode(a, wa, sizeof wa), nb = Codec::encode(b, wb, sizeof wb);
    assert(na && nb == 116);
    Bytes both(wa, wa + na); both.insert(both.end(), wb, wb + nb);
    for (size_t split = 0; split <= both.size(); ++split) {
        Codec p; Frame d; int ready = 0; uint16_t seqs[2] = {0, 0};
        for (size_t i = 0; i < both.size(); ++i) {
            if (p.feed(both[i], 1000 + (i < split ? 0 : 5), d) == DecodeResult::FrameReady) seqs[ready++ % 2] = d.sequence;
        }
        assert(ready == 2 && seqs[0] == 7 && seqs[1] == 8);
        assert(d.length == 96 && memcmp(d.payload, b.payload, 96) == 0);
    }
    // Decoded payload bytes beyond length are zero.
    Codec p; Frame d; memset(&d, 0xAA, sizeof d);
    feedAll(p, Bytes(wa, wa + na), 1, d);
    assert(d.payload[20] == 0 && d.payload[95] == 0 && d.payload[3] == 0 && d.payload[10] == 0);
''')

    def test_malformed_frames_count_and_never_produce_frames(self):
        self.check(r'''
    Bytes ok = Bytes(2, 1);
    Frame d; memset(&d, 0x55, sizeof d);
    { Codec p; assert(feedAll(p, wire(raw(2, 2, 0, 0, 1, 1, 2, 2, ok)), 1, d) == DecodeResult::Error); assert(p.counters().version == 1); }
    { Codec p; assert(feedAll(p, wire(raw(1, 2, 0, 0, 1, 1, 2, 3, ok)), 1, d) == DecodeResult::Error); assert(p.counters().length == 1); }
    { Codec p; assert(feedAll(p, wire(raw(1, 2, 0, 0, 1, 1, 2, 1, ok)), 1, d) == DecodeResult::Error); assert(p.counters().length == 1); }
    { Codec p; assert(feedAll(p, wire(raw(1, 2, 0, 0, 1, 1, 2, 97, Bytes(97, 1))), 1, d) == DecodeResult::Error); assert(p.counters().overflow == 1); }
    { Codec p; assert(feedAll(p, wire(raw(1, 2, 0, 0, 1, 1, 2, 96, Bytes(95, 1))), 1, d) == DecodeResult::Error); assert(p.counters().length == 1); }
    { Codec p; assert(feedAll(p, wire(raw(1, 2, 0, 0, 1, 1, 2, 98, Bytes(94, 1))), 1, d) == DecodeResult::Error); assert(p.counters().length == 1); }
    { Codec p; assert(feedAll(p, wire(raw(1, 2, 2, 0, 1, 1, 2, 2, ok)), 1, d) == DecodeResult::Error); assert(p.counters().reserved == 1); }
    { Codec p; assert(feedAll(p, wire(raw(1, 2, 0, 1, 1, 1, 2, 2, ok)), 1, d) == DecodeResult::Error); assert(p.counters().reserved == 1); }
    { Codec p; assert(feedAll(p, wire(raw(1, 0x77, 0, 0, 1, 1, 2, 2, ok)), 1, d) == DecodeResult::Error); assert(p.counters().type == 1); }
    { Bytes w = wire(raw(1, 2, 0, 0, 1, 1, 2, 2, ok)); w[w.size() - 3] ^= 1;
      Codec p; assert(feedAll(p, w, 1, d) == DecodeResult::Error); assert(p.counters().crc == 1); }
    { Bytes w; w.push_back(0x09); w.push_back(1); w.push_back(0);   // truncated COBS block
      Codec p; assert(feedAll(p, w, 1, d) == DecodeResult::Error); assert(p.counters().cobs == 1); }
    { Bytes w = wire(Bytes(5, 1)); Codec p; assert(feedAll(p, w, 1, d) == DecodeResult::Error); assert(p.counters().length == 1); }
    for (size_t i = 0; i < sizeof d.payload; ++i) assert(d.payload[i] == 0x55);   // output untouched
    { Codec p; assert(p.feed(0, 1, d) == DecodeResult::None); assert(p.feed(0, 1, d) == DecodeResult::None);
      assert(p.counters().crc + p.counters().length + p.counters().cobs == 0); }
''')

    def test_oversized_input_discards_until_delimiter_then_recovers(self):
        self.check(r'''
    Codec p; Frame d; Bytes big(200, 0x11);
    int errors = 0;
    for (size_t i = 0; i < big.size(); ++i) errors += p.feed(big[i], 1, d) == DecodeResult::Error;
    assert(errors == 1 && p.counters().overflow == 1);
    assert(p.feed(0, 1, d) == DecodeResult::None);
    Frame f = makeFrame(3, 2, 1); uint8_t w[116]; size_t n = Codec::encode(f, w, sizeof w);
    int ready = 0; for (size_t i = 0; i < n; ++i) ready += p.feed(w[i], 2, d) == DecodeResult::FrameReady;
    assert(ready == 1 && p.counters().overflow == 1);
    // A frame at the exact maximum is accepted (checked in the split test); one raw byte more overflows.
    Bytes r = raw(1, 2, 0, 0, 1, 1, 2, 97, Bytes(97, 0x33));
    Codec q; feedAll(q, wire(r), 1, d); assert(q.counters().length + q.counters().overflow >= 1);
''')

    def test_timeout_and_timestamp_rollover(self):
        self.check(r'''
    Frame f = makeFrame(3, 2, 1); uint8_t w[116]; size_t n = Codec::encode(f, w, sizeof w);
    Frame d;
    { Codec p; for (size_t i = 0; i + 1 < n; ++i) p.feed(w[i], 100, d);
      assert(p.feed(w[n - 1], 119, d) == DecodeResult::FrameReady);   // 19ms: still alive
      }
    { Codec p; for (size_t i = 0; i < n - 1; ++i) p.feed(w[i], 100, d);
      p.tick(119); assert(p.counters().timeout == 0);
      p.tick(120); assert(p.counters().timeout == 1);
      assert(p.feed(0, 120, d) == DecodeResult::None);           // nothing left to complete
      int ready = 0; for (size_t i = 0; i < n; ++i) ready += p.feed(w[i], 121, d) == DecodeResult::FrameReady;
      assert(ready == 1); }
    { Codec p; for (size_t i = 0; i + 1 < n; ++i) p.feed(w[i], 100, d);
      assert(p.feed(w[n - 1], 125, d) == DecodeResult::None); assert(p.counters().timeout == 1); }
    // Each byte arriving inside 20ms keeps the frame alive across rollover.
    { Codec p; int ready = 0; uint32_t t = 0xFFFFFFF0u;
      for (size_t i = 0; i < n; ++i) { ready += p.feed(w[i], t, d) == DecodeResult::FrameReady; t += 15; }
      assert(ready == 1 && p.counters().timeout == 0); }
    { Codec p; p.feed(w[0], 0xFFFFFFFEu, d); p.tick(17); assert(p.counters().timeout == 0);
      p.tick(18); assert(p.counters().timeout == 1); }
    assert(newer16(1, 65535) && newer16(100, 65500) && !newer16(65535, 0) && !newer16(5, 5)
           && newer16(32767, 0) && !newer16(32768, 0) && newer16(10, 9) && !newer16(9, 10));
''')

    def test_encode_rejects_invalid_arguments_and_counts_them(self):
        self.check(r'''
    ErrorCounters& c = Codec::encodeCounters(); c.clear();
    Frame f = makeFrame(1, 2, 0); uint8_t w[116];
    assert(Codec::encode(f, 0, 116) == 0 && c.null_argument == 1);
    assert(Codec::encode(f, w, 0) == 0 && c.capacity == 1);
    assert(Codec::encode(f, w, 10) == 0 && c.capacity == 2);
    Frame g = f; g.version = 2; assert(Codec::encode(g, w, 116) == 0 && c.version == 1);
    g = f; g.length = 97; assert(Codec::encode(g, w, 116) == 0 && c.length == 1);
    g = f; g.flags = 2; assert(Codec::encode(g, w, 116) == 0 && c.reserved == 1);
    g = f; g.type = static_cast<MessageType>(0x99); assert(Codec::encode(g, w, 116) == 0 && c.type == 1);
    assert(Codec::encode(f, w, 116) != 0);
    g = makeFrame(1, 96, 0); assert(Codec::encode(g, w, 115) == 0 && c.capacity == 3);
    assert(Codec::encode(g, w, 116) == 116);
''')


TYPED = r'''
static void ex(Status s, Status want, const ErrorCounters& c, uint32_t total) {
    uint32_t sum = c.null_argument + c.capacity + c.payload_length + c.enum_value + c.reserved_bits + c.range + c.type;
    assert(s == want); assert(sum == total);
}
'''


class TypedPayloadTests(unittest.TestCase):
    def check(self, body):
        program = PRELUDE + TYPED + "int main() {\n" + body + "\nputs(\"ok\"); return 0; }\n"
        result = run_cpp(program, extra_sources=[CODEC], include_dirs=[SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_payload_sizes_and_round_trips(self):
        self.check(r'''
    ErrorCounters c; Frame f; uint8_t buf[96]; size_t n;
    Hello h = {kRoleBody, kCapDualVesc | kCapAudio | kCapTelemetry | kCapRemoteDrive, 1};
    Heartbeat hb = {1, 1};
    RcStatus rc = {0xFFFFFFFEu, 65535, 0x0F, {900,1000,1500,2000,2100,1,2,3,4,5}, 3, 2, 513};
    VescStatus v = {1, 0xFF, 65535, 6, 7, 5040, -123456, 654321, -77777, -50, 420, 3, -1000};
    BodyStatus b = {0x0FFF, 5, 4, 0x05, 1, 9, 3, 5, 0xCAFEBABEu, 1, -1800};
    HallState hs = {3, 2, 77, 150};
    DomeRequest dr = {1, -100, 150, 4, 1, 0, 99};
    AudioRequest ar = {0, 1, 12, 0, 1};
    DriveRequest dv = {-1000, 1000, 1, 8};
    ControlRequest cq = {4, 2, 0xABCD, 5};
    CommissionRequest cm = {4, 0, 0x01020304u, 9, 1, -2147483647 - 1, 7};
    Reply rp = {0x24, 0xFFFF, 8, 15};
    AudioStatus as = {2, 1, 44, 30, 3, 1000, 5000, 6};
    Event ev = {5, 0x20, 12, 15};
    CommissionStatus cs = {0xDEADBEEFu, 2, 4, 77, 0x80000001u, 1500, 25, 3000, 3100, {1,2,3}, 9, 1};
    Diagnostics d0 = {0, 5, {1,2,3,4,5,6,7,0xFFFFFFFFu}, 0, 0, 0};
    Diagnostics d1 = {1, 6, {0}, 3, 1, -9};
#define RT(T, m, size) { T out; memset(&out, 0, sizeof out); \
    assert(encodePayload(m, buf, sizeof buf, n, c) == Status::Ok && n == size); \
    assert(decodePayload(buf, n, out, c) == Status::Ok); \
    uint8_t again[96]; size_t n2; assert(encodePayload(out, again, sizeof again, n2, c) == Status::Ok); \
    assert(n2 == n && memcmp(buf, again, n) == 0); \
    Frame fr = makeFrame(0,0,0); assert(encode(m, fr, c) == Status::Ok && fr.type == T::type() && fr.length == size); \
    T out2; memset(&out2, 0, sizeof out2); assert(decode(fr, out2, c) == Status::Ok); \
    uint8_t a3[96]; size_t n3; assert(encodePayload(out2, a3, sizeof a3, n3, c) == Status::Ok && memcmp(buf, a3, n) == 0); }
    RT(Hello, h, 7) RT(Heartbeat, hb, 2) RT(RcStatus, rc, 32) RT(VescStatus, v, 28) RT(BodyStatus, b, 19)
    RT(HallState, hs, 8) RT(DomeRequest, dr, 13) RT(AudioRequest, ar, 6) RT(DriveRequest, dv, 8)
    RT(ControlRequest, cq, 6) RT(CommissionRequest, cm, 14) RT(Reply, rp, 6) RT(AudioStatus, as, 16)
    RT(Event, ev, 6) RT(CommissionStatus, cs, 36) RT(Diagnostics, d0, 37) RT(Diagnostics, d1, 11)
    assert(c.payload_length + c.enum_value + c.range + c.reserved_bits + c.type + c.capacity + c.null_argument == 0);
    // Field values survive.
    RcStatus rc2; encodePayload(rc, buf, sizeof buf, n, c); decodePayload(buf, n, rc2, c);
    assert(rc2.sample_counter == 0xFFFFFFFEu && rc2.channels[4] == 2100 && rc2.control_epoch == 513);
    VescStatus v2; encodePayload(v, buf, sizeof buf, n, c); decodePayload(buf, n, v2, c);
    assert(v2.motor_mA == -123456 && v2.duty_permille == -1000 && v2.erpm == -77777);
    // Wire order check.
    encodePayload(h, buf, sizeof buf, n, c);
    const uint8_t hello[] = {1, 0x1b, 0, 0, 0, 1, 0}; assert(memcmp(buf, hello, 7) == 0);
    CommissionStatus cs2; encodePayload(cs, buf, sizeof buf, n, c);
    assert(buf[0] == 0xEF && buf[4] == 2 && buf[5] == 4 && buf[6] == 77 && buf[8] == 0x01 && buf[11] == 0x80 &&
           buf[12] == 0xDC && buf[13] == 5 && buf[14] == 25 && buf[35] == 1);
''')

    def test_length_and_argument_validation(self):
        self.check(r'''
    ErrorCounters c; uint8_t buf[96]; memset(buf, 0, sizeof buf); size_t n = 123;
    Heartbeat hb = {1, 1}, out = {9, 9};
    assert(encodePayload(hb, 0, 96, n, c) == Status::NullArgument && c.null_argument == 1);
    assert(encodePayload(hb, buf, 1, n, c) == Status::BadCapacity && c.capacity == 1 && n == 123);
    assert(decodePayload((const uint8_t*)0, 2, out, c) == Status::NullArgument && c.null_argument == 2);
    assert(decodePayload(buf, 0, out, c) == Status::BadLength);
    assert(decodePayload(buf, 1, out, c) == Status::BadLength);
    assert(decodePayload(buf, 3, out, c) == Status::BadLength && c.payload_length == 3);
    assert(out.mode == 9 && out.ready == 9);
    Frame f = makeFrame(1, 0, 0); f.type = MessageType::Hello; f.length = 2;
    assert(decode(f, out, c) == Status::BadType && c.type == 1);
    f.type = MessageType::Heartbeat; f.length = 97;
    assert(decode(f, out, c) == Status::BadLength && c.payload_length == 4);
    f.length = 7; assert(decode(f, out, c) == Status::BadLength && c.payload_length == 5);
    Diagnostics d = {0}; d.subtype = 1;
    Frame df = makeFrame(1, 12, 0); df.type = MessageType::Diagnostics;
    assert(decode(df, d, c) == Status::BadLength);
    df.length = 36; assert(decode(df, d, c) == Status::BadLength);
    df.length = 38; assert(decode(df, d, c) == Status::BadLength);
    CommissionRequest cr; Frame cf = makeFrame(1, 13, 0); cf.type = MessageType::CommissionRequest;
    assert(decode(cf, cr, c) == Status::BadLength);
    cf.length = 15; assert(decode(cf, cr, c) == Status::BadLength);
    CommissionStatus cs; Frame sf = makeFrame(1, 35, 0); sf.type = MessageType::CommissionStatus;
    assert(decode(sf, cs, c) == Status::BadLength); sf.length = 37; assert(decode(sf, cs, c) == Status::BadLength);
    // A failed typed encode does not modify the frame.
    Frame keep = makeFrame(9, 2, 4); Frame before = keep;
    Heartbeat bad = {3, 0}; assert(encode(bad, keep, c) == Status::BadEnum && memcmp(&keep, &before, sizeof keep) == 0);
''')

    def test_enum_range_and_reserved_rejection_with_counters(self):
        self.check(r'''
    ErrorCounters c; uint8_t buf[96]; size_t n; Frame f;
#define BAD(m, want, ctr) { uint32_t before = c.ctr; assert(encodePayload(m, buf, sizeof buf, n, c) == want); assert(c.ctr == before + 1); \
    assert(encode(m, f, c) == want); assert(c.ctr == before + 2); }
    Hello h = {kRoleBody, kCapDualVesc, 1};
    Hello x = h; x.role = 0; BAD(x, Status::BadEnum, enum_value) x.role = 3; BAD(x, Status::BadEnum, enum_value)
    x = h; x.capabilities = 0x20; BAD(x, Status::BadReserved, reserved_bits)
    x = h; x.safety_revision = 2; BAD(x, Status::BadRange, range)
    Heartbeat hb = {1, 2}; BAD(hb, Status::BadEnum, enum_value)
    hb.ready = 1; hb.mode = 3; BAD(hb, Status::BadEnum, enum_value)
    RcStatus rc; memset(&rc, 0, sizeof rc);
    rc.flags = 0x10; BAD(rc, Status::BadReserved, reserved_bits) rc.flags = 0; rc.drive_state = 6; BAD(rc, Status::BadEnum, enum_value)
    rc.drive_state = 0; rc.dome_state = 5; BAD(rc, Status::BadEnum, enum_value)
    VescStatus v; memset(&v, 0, sizeof v);
    v.wheel = 2; BAD(v, Status::BadEnum, enum_value) v.wheel = 0; v.valid_fields = 0x100; BAD(v, Status::BadReserved, reserved_bits)
    BodyStatus b; memset(&b, 0, sizeof b);
    b.faults = 0x1000; BAD(b, Status::BadReserved, reserved_bits) b.faults = 0; b.lock_reasons = 0x02; BAD(b, Status::BadReserved, reserved_bits)
    b.lock_reasons = 0x08; BAD(b, Status::BadReserved, reserved_bits) b.lock_reasons = 0;
    b.drive_state = 6; BAD(b, Status::BadEnum, enum_value) b.drive_state = 0; b.dome_state = 5; BAD(b, Status::BadEnum, enum_value)
    b.dome_state = 0; b.profile_ready = 2; BAD(b, Status::BadEnum, enum_value) b.profile_ready = 0;
    b.drive_intent = 4; BAD(b, Status::BadEnum, enum_value) b.drive_intent = 0; b.dome_owner = 6; BAD(b, Status::BadEnum, enum_value)
    b.dome_owner = 0; b.angle_valid = 1; b.estimated_angle_ddeg = 1800; BAD(b, Status::BadRange, range)
    b.estimated_angle_ddeg = -1801; BAD(b, Status::BadRange, range)
    b.angle_valid = 0; b.estimated_angle_ddeg = 5; BAD(b, Status::BadReserved, reserved_bits)
    HallState hs = {4, 0, 0, 0}; BAD(hs, Status::BadReserved, reserved_bits)
    hs.valid_mask = 1; hs.active_mask = 2; BAD(hs, Status::BadReserved, reserved_bits)
    hs.active_mask = 4; BAD(hs, Status::BadReserved, reserved_bits)
    DomeRequest dr = {1, 50, 100, 0, 0, 0, 0};
    dr.operation = 3; BAD(dr, Status::BadEnum, enum_value) dr.operation = 1; dr.owner = 2; BAD(dr, Status::BadEnum, enum_value)
    dr.owner = 0; dr.reference = 2; BAD(dr, Status::BadEnum, enum_value) dr.reference = 1; BAD(dr, Status::BadReserved, reserved_bits)
    dr.reference = 0; dr.speed_percent = 101; BAD(dr, Status::BadRange, range) dr.speed_percent = -101; BAD(dr, Status::BadRange, range)
    dr.speed_percent = 0; dr.lease_ms = 0; BAD(dr, Status::BadRange, range) dr.lease_ms = 151; BAD(dr, Status::BadRange, range)
    DomeRequest cancel = {0, 0, 0, 0, 0, 0, 0}; assert(encodePayload(cancel, buf, sizeof buf, n, c) == Status::Ok);
    cancel.lease_ms = 5; BAD(cancel, Status::BadReserved, reserved_bits)
    cancel.lease_ms = 0; cancel.speed_percent = 1; BAD(cancel, Status::BadReserved, reserved_bits)
    DomeRequest seek = {2, 0, 0, 3, 1, 1, 8}; assert(encodePayload(seek, buf, sizeof buf, n, c) == Status::Ok);
    AudioRequest ar = {0, 1, 5, 0, 0}; assert(encodePayload(ar, buf, sizeof buf, n, c) == Status::Ok);
    ar.operation = 6; BAD(ar, Status::BadEnum, enum_value) ar.operation = 0; ar.priority = 2; BAD(ar, Status::BadEnum, enum_value)
    ar.priority = 0; ar.track = 0; BAD(ar, Status::BadRange, range) ar.track = 1; ar.folder = 0; BAD(ar, Status::BadRange, range)
    ar.folder = 1; ar.volume = 1; BAD(ar, Status::BadReserved, reserved_bits)
    AudioRequest vol = {4, 0, 0, 30, 0}; assert(encodePayload(vol, buf, sizeof buf, n, c) == Status::Ok);
    vol.volume = 31; BAD(vol, Status::BadEnum, enum_value)
    AudioRequest stop = {1, 1, 0, 0, 0}; BAD(stop, Status::BadReserved, reserved_bits) stop.folder = 0; stop.volume = 3; BAD(stop, Status::BadReserved, reserved_bits)
    DriveRequest dv = {0, 0, 50, 0}; dv.left_permille = 1001; BAD(dv, Status::BadRange, range)
    dv.left_permille = 0; dv.right_permille = -1001; BAD(dv, Status::BadRange, range)
    dv.right_permille = 0; dv.lease_ms = 0; BAD(dv, Status::BadRange, range) dv.lease_ms = 151; BAD(dv, Status::BadRange, range)
    ControlRequest cq = {5, 0, 0, 0}; BAD(cq, Status::BadEnum, enum_value) cq.operation = 2; cq.reason = 1; BAD(cq, Status::BadReserved, reserved_bits)
    cq.reason = 3; BAD(cq, Status::BadEnum, enum_value) cq.reason = 2; assert(encodePayload(cq, buf, sizeof buf, n, c) == Status::Ok);
    CommissionRequest cm = {7, 0, 0, 0, 0, 0, 0}; BAD(cm, Status::BadEnum, enum_value)
    cm.operation = 1; cm.test = 7; BAD(cm, Status::BadEnum, enum_value) cm.test = 0; BAD(cm, Status::BadRange, range)
    cm.operation = 0; cm.test = 1; BAD(cm, Status::BadRange, range)
    cm.operation = 4; cm.test = 0; cm.wheel = 2; BAD(cm, Status::BadEnum, enum_value)
    cm.operation = 5; cm.wheel = 0; cm.field = 1; BAD(cm, Status::BadReserved, reserved_bits)
    cm.field = 0; cm.value = 1; BAD(cm, Status::BadReserved, reserved_bits)
    Reply rp = {0x24, 1, 9, 0}; BAD(rp, Status::BadEnum, enum_value) rp.result = 0; rp.detail = 16; BAD(rp, Status::BadEnum, enum_value)
    rp.detail = 0; rp.request_type = 0x55; BAD(rp, Status::BadType, type)
    AudioStatus as = {6, 1, 1, 10, 0, 0, 0, 0}; BAD(as, Status::BadEnum, enum_value) as.state = 0; as.volume = 31; BAD(as, Status::BadEnum, enum_value)
    as.volume = 0; as.validity = 4; BAD(as, Status::BadReserved, reserved_bits) as.validity = 1; as.duration_ms = 1; BAD(as, Status::BadReserved, reserved_bits)
    Event ev = {6, 0x20, 1, 0}; BAD(ev, Status::BadEnum, enum_value) ev.kind = 0; ev.detail = 16; BAD(ev, Status::BadEnum, enum_value)
    ev.detail = 0; ev.request_type = 0; BAD(ev, Status::BadType, type)
    CommissionStatus cs; memset(&cs, 0, sizeof cs);
    cs.state = 6; BAD(cs, Status::BadEnum, enum_value) cs.state = 0; cs.test = 7; BAD(cs, Status::BadEnum, enum_value)
    cs.test = 0; cs.saved = 2; BAD(cs, Status::BadEnum, enum_value) cs.saved = 0; cs.trial_speed_percent = 101; BAD(cs, Status::BadEnum, enum_value)
    cs.trial_speed_percent = 0; cs.trial_neutral_us = 1399; BAD(cs, Status::BadRange, range) cs.trial_neutral_us = 1601; BAD(cs, Status::BadRange, range)
    Diagnostics d; memset(&d, 0, sizeof d); d.subtype = 2; BAD(d, Status::BadEnum, enum_value)
    d.subtype = 0; d.wheel = 1; BAD(d, Status::BadReserved, reserved_bits) d.wheel = 0; d.field = 1; BAD(d, Status::BadReserved, reserved_bits)
    d.field = 0; d.value = 1; BAD(d, Status::BadReserved, reserved_bits)
    d.value = 0; d.subtype = 1; d.counters[3] = 1; BAD(d, Status::BadReserved, reserved_bits)
    d.counters[3] = 0; d.wheel = 2; BAD(d, Status::BadEnum, enum_value)
''')

    def test_commission_request_operation_schema(self):
        self.check(r'''
    ErrorCounters c;
    uint8_t bytes[96]; size_t length = 0;
    const CommissionRequest valid[] = {
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 1, 1, 20, 0},
        {1, 1, 42, 0, 0, 0, 0},
        {2, 0, 42, 0, 0, 0, 0},
        {3, 0, 42, 0, 0, 0, 0},
        {4, 0, 0, 0, 0, 123, 0},
        {4, 0, 0, 20, 1, 123, 0},
        {5, 0, 0, 0, 0, 0, 0},
        {6, 0, 0, 0, 0, 31, 0}
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        assert(encodePayload(valid[i], bytes, sizeof bytes, length, c) == Status::Ok);
        CommissionRequest decoded = {};
        assert(decodePayload(bytes, length, decoded, c) == Status::Ok);
        assert(decoded.operation == valid[i].operation && decoded.field == valid[i].field &&
               decoded.wheel == valid[i].wheel && decoded.value == valid[i].value);
    }
#define REJECT(m) assert(encodePayload(m, bytes, sizeof bytes, length, c) != Status::Ok)
    CommissionRequest request = {0, 0, 0, 1, 1, 21, 0}; REJECT(request);
    request.value = -1; REJECT(request);
    request = {0, 1, 0, 0, 0, 0, 0}; REJECT(request);
    request = {1, 0, 42, 0, 0, 0, 0}; REJECT(request);
    request = {2, 1, 42, 0, 0, 0, 0}; REJECT(request);
    request = {3, 1, 42, 0, 0, 0, 0}; REJECT(request);
    request = {4, 1, 0, 0, 0, 1, 0}; REJECT(request);
    request = {4, 0, 0, 21, 0, 1, 0}; REJECT(request);
    request = {5, 1, 0, 0, 0, 0, 0}; REJECT(request);
    request = {6, 1, 0, 0, 0, 1, 0}; REJECT(request);
    request = {6, 0, 0, 0, 0, 32, 0}; REJECT(request);
    request = {6, 0, 0, 1, 0, 1, 0}; REJECT(request);
#undef REJECT

    Diagnostics profile = {};
    profile.subtype = 1; profile.field = 20; profile.wheel = 1;
    assert(encodePayload(profile, bytes, sizeof bytes, length, c) == Status::Ok);
    profile.field = 21;
    assert(encodePayload(profile, bytes, sizeof bytes, length, c) == Status::BadRange);
''')

    def test_decode_rejects_invalid_bytes_without_touching_output(self):
        self.check(r'''
    ErrorCounters c; uint8_t buf[96] = {0}; 
    BodyStatus out; memset(&out, 0x7e, sizeof out); BodyStatus before = out;
    BodyStatus good = {1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0}; size_t n;
    assert(encodePayload(good, buf, sizeof buf, n, c) == Status::Ok && n == 19);
    buf[6] = 0x02;                                  // reserved lock bit
    assert(decodePayload(buf, n, out, c) == Status::BadReserved && c.reserved_bits == 1);
    buf[6] = 1; buf[4] = 9;                         // drive_state
    assert(decodePayload(buf, n, out, c) == Status::BadEnum && c.enum_value == 1);
    assert(memcmp(&out, &before, sizeof out) == 0);
    Diagnostics d; uint8_t dg[37] = {0}; dg[0] = 2;
    assert(decodePayload(dg, 11, d, c) == Status::BadEnum && c.enum_value == 2);
    uint8_t cs[36] = {0}; cs[4] = 6; CommissionStatus st;
    assert(decodePayload(cs, 36, st, c) == Status::BadEnum);
    uint8_t cr[14] = {0}; cr[0] = 4; cr[1] = 0; CommissionRequest rq;
    assert(decodePayload(cr, 14, rq, c) == Status::Ok && rq.operation == 4);
    cr[0] = 5; cr[6] = 1; assert(decodePayload(cr, 14, rq, c) == Status::BadReserved && rq.operation == 4);
    // Invalid frames never decode into state: freshness-bearing fields stay unchanged.
    RcStatus rc; memset(&rc, 0, sizeof rc); rc.sample_counter = 10;
    uint8_t rb[32] = {0}; rb[4] = 0xFF; rb[5] = 0xFF; rb[6] = 0x10;
    assert(decodePayload(rb, 32, rc, c) == Status::BadReserved && rc.sample_counter == 10 && rc.source_age_ms == 0);
''')


if __name__ == "__main__":
    unittest.main()
