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


# ---------------------------------------------------------------------------
# Task 2: reliable endpoint and persistent profiles
# ---------------------------------------------------------------------------
TESTS_DIR = ROOT / "tests"
SHARED_LIB = SHARED / "R2BodyLink"
BODY_SRC = ROOT / "TEENSY_BODY_CONTROLLER/src"
ENDPOINT = SHARED_LIB / "src/Endpoint.cpp"
CONFIG_STORE = BODY_SRC / "body/ConfigStore.cpp"
LINK_INCLUDES = [SHARED, SHARED_LIB, TESTS_DIR, BODY_SRC]
LINK_SOURCES = [CODEC, ENDPOINT, CONFIG_STORE]

LINK_PRELUDE = PRELUDE + r'''
#include "body_fakes.h"
using namespace fakes;
static Frame audioPlay(uint8_t folder, uint16_t track) {
    AudioRequest a = {0, folder, track, 0, 0}; return frameOf(a);
}
static Frame stopAll() { ControlRequest c = {0, 0, 0, 0}; return frameOf(c); }
static void inject(FakePort& p, const Frame& f) { p.push(wireOf(f)); }
template <class T> static T as(const Frame& f) {
    T m; ErrorCounters c; Status s = decode(f, m, c); assert(s == Status::Ok); (void)s; return m;
}
// Endpoint alone on a port; the peer is scripted by injected frames.
struct Solo {
    FakePort p; Endpoint e; uint32_t now; uint16_t seq; bool is_body; uint32_t peer; uint32_t local;
    Solo(bool body_role, uint32_t local_session = 0)
        : p(), e(p, body_role ? kRoleBody : kRoleDome, local_session ? local_session : (body_role ? 0xB0B0 : 0xD0D0)), now(1000), seq(1),
          is_body(body_role), peer(body_role ? 0xD0D0 : 0xB0B0), local(local_session ? local_session : (body_role ? 0xB0B0 : 0xD0D0)) {}
    void hello(uint32_t src, uint8_t role, uint32_t caps) {
        Hello h = {role, caps, 1};
        inject(p, stamp(frameOf(h), seq++, src, 0, 0));
    }
    void heartbeat(uint32_t src, uint32_t dst, uint8_t mode = 1, uint8_t ready = 1) {
        Heartbeat h = {mode, ready};
        inject(p, stamp(frameOf(h), seq++, src, dst, 0));
    }
    void connect() {
        hello(peer, is_body ? kRoleDome : kRoleBody, is_body ? 0x04 : 0x1B);
        e.tick(now);
        heartbeat(peer, local);
        e.tick(now);
        assert(e.connected(now));
    }
    // Frame from the scripted peer addressed to this endpoint.
    void from(const Frame& f, uint16_t s, uint8_t flags) { inject(p, stamp(f, s, peer, local, flags)); }
    std::vector<Frame> sent() {
        std::vector<Frame> out; Codec c; Frame f;
        for (size_t i = 0; i < p.tx.size(); ++i)
            if (c.feed(p.tx[i], now, f) == DecodeResult::FrameReady) out.push_back(f);
        p.tx.clear();
        return out;
    }
};
static size_t countType(const std::vector<Frame>& v, MessageType t) {
    size_t n = 0; for (size_t i = 0; i < v.size(); ++i) if (v[i].type == t) ++n; return n;
}
'''


def run_link(body, sources=None, helpers="", **kw):
    program = LINK_PRELUDE + helpers + "int main() {\n" + body + "\nputs(\"ok\"); return 0; }\n"
    return run_cpp(program, extra_sources=sources or LINK_SOURCES, include_dirs=LINK_INCLUDES, **kw)


class EndpointTests(unittest.TestCase):
    def check(self, body):
        result = run_link(body)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_handshake_request_retry_and_cached_reply(self):
        self.check(r'''
    Rig r;
    Frame f = audioPlay(1, 2); uint16_t seq = 99;
    assert(!r.dome.request(f, r.now, seq) && seq == 99);
    assert(r.dome.lastReject() == Reject::NotConnected);
    r.connect();
    assert(r.body.peerSession() == 0xD0D0 && r.dome.peerSession() == 0xB0B0);
    assert(r.dome.request(f, r.now, seq));
    assert(f.flags == 1 && f.sequence == seq && f.source_session == 0xD0D0 && f.destination_session == 0xB0B0);
    assert(r.dome.pendingCount() == 1);
    r.drop_to_dome[static_cast<int>(MessageType::Reply)] = 1;     // first reply is lost
    r.run(3);
    Frame got;
    assert(r.body.takeReceived(got) && got.type == MessageType::AudioRequest && got.sequence == seq);
    assert(r.body.reply(got, Result::Accepted, 0));
    r.run(300);
    assert(!r.body.takeReceived(got));                                // exactly one action
    assert(r.body.stats().duplicates == 1);
    assert(r.countToBody(MessageType::AudioRequest) == 2);            // original + one retry
    assert(r.countToDome(MessageType::Reply) == 2);                   // lost reply + cached replay
    Completion c;
    assert(r.dome.takeCompletion(c) && c.outcome == Outcome::Replied && c.sequence == seq);
    assert(c.type == MessageType::AudioRequest && c.result == 0 && c.detail == 0);
    assert(!r.dome.takeCompletion(c) && r.dome.pendingCount() == 0);
''')

    def test_peer_restart_changes_session_and_clears_pending(self):
        self.check(r'''
    Rig r; r.connect();
    Event ev = {0, 0x21, 5, 0};
    Frame f = frameOf(ev); uint16_t seq = 0;
    r.drop_to_body[static_cast<int>(MessageType::Reply)] = 100;
    assert(r.body.request(f, r.now, seq));
    r.step(3);
    assert(r.body.pendingCount() == 1);
    uint32_t now = r.now;
    assert(r.body.connected(now));
    const uint32_t oldPeer = r.body.peerSession();
    const uint32_t newDomeSession = 0xD1D1;
    r.dport.tx.clear();
    Endpoint newDome(r.dport, kRoleDome, newDomeSession);
    assert(newDomeSession != oldPeer);
    newDome.tick(now + 1);
    r.bport.push(r.dport.tx); r.dport.tx.clear();
    r.body.tick(now + 1);
    assert(r.body.peerSession() == newDomeSession);
    assert(r.body.pendingCount() == 0);
    assert(!r.body.connected(now + 1));
    Completion c; assert(r.body.takeCompletion(c) && c.outcome == Outcome::SessionChanged && c.sequence == seq);
    assert(r.body.peerGeneration() == 2 && r.body.stats().session_changes == 1);
    // The restarted dome and body complete a fresh handshake.
    for (int i = 0; i < 400 && !(newDome.connected(now + 400) && r.body.connected(now + 400)); ++i) {
        ++now;
        r.dport.tx.clear();
        newDome.tick(now); r.bport.push(r.dport.tx); r.dport.tx.clear();
        r.body.tick(now); r.dport.push(r.bport.tx); r.bport.tx.clear();
        if (newDome.connected(now) && r.body.connected(now)) break;
    }
    assert(newDome.connected(now) && r.body.connected(now));
''')

    def test_heartbeat_cadence_timeout_and_reconnect(self):
        self.check(r'''
    Rig r; r.connect();
    size_t before = r.countToBody(MessageType::Heartbeat);
    size_t hellos = r.countToBody(MessageType::Hello);
    r.run(1000);
    size_t beats = r.countToBody(MessageType::Heartbeat) - before;
    assert(beats >= 9 && beats <= 11);
    assert(r.countToBody(MessageType::Hello) == hellos);               // handshake complete: no more HELLO
    // exact 300ms boundary with scripted heartbeats
    Solo s(true); s.connect();
    s.now = 1100; s.heartbeat(0xD0D0, 0xB0B0); s.e.tick(s.now);
    assert(s.e.connected(1100 + 299) && !s.e.connected(1100 + 300));
    // state is carried by the heartbeat
    s.heartbeat(0xD0D0, 0xB0B0, 2, 0); s.e.tick(s.now);
    assert(s.e.peerMode() == 2 && s.e.peerReady() == 0);
    // pending requests fail visibly and the queue is cleared when the link drops
    Solo d(false); d.connect();
    Frame f = audioPlay(1, 1); uint16_t q = 0;
    assert(d.e.request(f, d.now, q)); d.e.tick(d.now);
    d.now += 400; d.e.tick(d.now);
    assert(!d.e.connected(d.now) && d.e.pendingCount() == 0 && d.e.stats().link_losses == 1);
    Completion c; assert(d.e.takeCompletion(c) && c.outcome == Outcome::PeerLost && c.sequence == q);
    d.heartbeat(d.peer, d.local); d.e.tick(d.now);
    assert(d.e.connected(d.now));
''')

    def test_hello_role_capability_and_session_checks(self):
        self.check(r'''
    { Solo s(true); s.hello(0xD0D0, kRoleBody, 0x1B); s.e.tick(s.now);          // same role
      assert(s.e.peerSession() == 0 && s.e.stats().hello_rejected == 1); }
    { Solo s(true); s.hello(0xD0D0, kRoleDome, 0x1B); s.e.tick(s.now);          // wrong capabilities
      assert(s.e.peerSession() == 0 && s.e.stats().hello_rejected == 1); }
    { Solo s(true); Hello h = {kRoleDome, 0x04, 1};                              // other safety revision
      Frame bad = stamp(frameOf(h), 1, 0xD0D0, 0, 0); bad.payload[5] = 2;
      inject(s.p, bad); s.e.tick(s.now);
      assert(s.e.peerSession() == 0 && s.e.counters().range == 1); }
    { Solo s(true); Hello h = {kRoleDome, 0x04, 1};                              // destination must be zero
      inject(s.p, stamp(frameOf(h), 1, 0xD0D0, 0xB0B0, 0)); s.e.tick(s.now);
      assert(s.e.peerSession() == 0 && s.e.counters().session == 1); }
    { Solo s(true); s.hello(0, kRoleDome, 0x04); s.e.tick(s.now);                // session zero is reserved
      assert(s.e.peerSession() == 0 && s.e.counters().session == 1); }
    { Solo s(true); s.hello(0xB0B0, kRoleDome, 0x04); s.e.tick(s.now);           // equal counters across roles are legal
      assert(s.e.peerSession() == 0xB0B0 && s.e.counters().session == 0); }
    { Solo s(true); s.hello(0xB0B0, kRoleBody, 0x1B); s.e.tick(s.now);           // loopback: same role is still refused
      assert(s.e.peerSession() == 0 && s.e.stats().hello_rejected == 1); }
    { Solo s(true); s.hello(0xD0D0, kRoleDome, 0x04); s.e.tick(s.now);
      assert(s.e.peerSession() == 0xD0D0 && !s.e.connected(s.now)); }            // hello alone is not a heartbeat
    { Solo s(true); s.e.tick(s.now);                                              // sends HELLO with zero destination
      std::vector<Frame> v = s.sent(); assert(v.size() == 1 && v[0].type == MessageType::Hello);
      Hello h = as<Hello>(v[0]); assert(h.role == kRoleBody && h.capabilities == 0x1B && h.safety_revision == 1);
      assert(v[0].destination_session == 0 && v[0].source_session == 0xB0B0);
      s.now += 499; s.e.tick(s.now); assert(s.sent().empty());
      s.now += 1; s.e.tick(s.now); assert(countType(s.sent(), MessageType::Hello) == 1); }
    { Solo s(false); s.e.tick(s.now); Hello h = as<Hello>(s.sent()[0]);
      assert(h.role == kRoleDome && h.capabilities == 0x04); }
''')

    def test_retry_limit_deadline_and_unsent_expiry(self):
        self.check(r'''
    Rig r; r.connect();
    Frame f = audioPlay(1, 2); uint16_t seq = 0;
    assert(r.dome.request(f, r.now, seq));
    r.run(340);
    assert(r.dome.pendingCount() == 1 && r.countToBody(MessageType::AudioRequest) == 3);
    r.run(20);
    Completion c; assert(r.dome.takeCompletion(c) && c.outcome == Outcome::TimedOut && c.sequence == seq);
    assert(r.dome.pendingCount() == 0 && r.countToBody(MessageType::AudioRequest) == 3);
    assert(r.dome.stats().retransmits == 2 && r.dome.stats().request_timeouts == 1);
    // retries are byte-identical
    std::vector<Frame> ws;
    for (size_t i = 0; i < r.to_body.size(); ++i) if (r.to_body[i].type == MessageType::AudioRequest) ws.push_back(r.to_body[i]);
    assert(wireOf(ws[0]) == wireOf(ws[1]) && wireOf(ws[1]) == wireOf(ws[2]));
    // a request that never gets a transmit window fails visibly and never executes
    Rig q; q.connect();
    q.dport.window = 0;
    Frame g = audioPlay(1, 3);
    assert(q.dome.request(g, q.now, seq));
    q.run(345);
    assert(q.dome.pendingCount() == 1);
    q.run(10);
    assert(q.dome.takeCompletion(c) && c.outcome == Outcome::Unsent && c.sequence == seq && q.dome.stats().unsent_expired == 1);
    q.dport.window = 1u << 30;
    q.run(50);
    assert(q.countToBody(MessageType::AudioRequest) == 0);
    Frame h; assert(!q.body.takeReceived(h));
''')

    def test_queue_limits_and_safety_priority(self):
        self.check(r'''
    Rig r; r.connect();
    r.dport.window = 0;
    uint16_t seq; uint16_t first_audio = 0, first_stop = 0;
    for (int i = 0; i < 6; ++i) { Frame f = audioPlay(1, 10 + i); assert(r.dome.request(f, r.now, seq)); if (!i) first_audio = seq; }
    Frame over = audioPlay(1, 99);
    assert(!r.dome.request(over, r.now, seq) && r.dome.lastReject() == Reject::Busy);
    assert(r.dome.counters().queue == 1 && r.dome.stats().busy_local == 1);
    Frame s1 = stopAll(); assert(r.dome.request(s1, r.now, seq)); first_stop = seq;
    DomeRequest cancel = {0, 0, 0, 0, 0, 0, 0}; Frame s2 = frameOf(cancel);
    assert(r.dome.request(s2, r.now, seq));
    Frame s3 = stopAll(); assert(!r.dome.request(s3, r.now, seq) && r.dome.lastReject() == Reject::Busy);
    assert(r.dome.pendingCount() == 8);
    assert(newer16(first_stop, first_audio));      // safety items were queued later...
    r.dport.window = 1u << 30;
    r.run(5);
    std::vector<MessageType> order;                // ...but are transmitted first
    for (size_t i = 0; i < r.to_body.size(); ++i) {
        MessageType t = r.to_body[i].type;
        if (t == MessageType::ControlRequest || t == MessageType::DomeRequest || t == MessageType::AudioRequest) order.push_back(t);
    }
    assert(order.size() == 8 && order[0] == MessageType::ControlRequest && order[1] == MessageType::DomeRequest);
    for (size_t i = 2; i < 8; ++i) assert(order[i] == MessageType::AudioRequest);
    // streaming velocity and discrete types are not interchangeable
    DriveRequest dr = {0, 0, 10, 0}; Frame df = frameOf(dr);
    assert(!r.dome.request(df, r.now, seq) && r.dome.lastReject() == Reject::WrongKind);
    assert(!r.dome.publishLatest(audioPlay(1, 1)));
    DomeRequest vel = {1, 5, 100, 0, 0, 0, 0}; Frame vf = frameOf(vel);
    assert(!r.dome.request(vf, r.now, seq) && r.dome.lastReject() == Reject::WrongKind);
    // semantic validation happens before queueing
    AudioRequest bad = {0, 1, 1, 0, 0}; Frame bf = frameOf(bad); bf.payload[4] = 99;
    assert(!r.dome.request(bf, r.now, seq) && r.dome.lastReject() == Reject::BadFrame);
''')

    def test_reply_matching_expected_type_sequence_and_session(self):
        self.check(r'''
    Rig r; r.connect();
    Frame f = audioPlay(1, 2); uint16_t seq = 0;
    assert(r.dome.request(f, r.now, seq));
    r.step(1);
    Reply wrongType = {0x24, seq, 0, 0}, wrongSeq = {0x21, static_cast<uint16_t>(seq + 1), 0, 0};
    Reply good = {0x21, seq, 6, 12};
    inject(r.dport, stamp(frameOf(wrongType), 50, 0xB0B0, 0xD0D0, 0));
    inject(r.dport, stamp(frameOf(wrongSeq), 51, 0xB0B0, 0xD0D0, 0));
    inject(r.dport, stamp(frameOf(good), 52, 0xBAD, 0xD0D0, 0));     // wrong source session
    inject(r.dport, stamp(frameOf(good), 53, 0xB0B0, 0xBAD, 0));     // wrong destination
    r.dome.tick(r.now);
    assert(r.dome.pendingCount() == 1 && r.dome.stats().unmatched_replies == 2 && r.dome.counters().session == 2);
    Completion c; assert(!r.dome.takeCompletion(c));
    inject(r.dport, stamp(frameOf(good), 54, 0xB0B0, 0xD0D0, 0));
    r.dome.tick(r.now);
    assert(r.dome.takeCompletion(c) && c.outcome == Outcome::Replied && c.result == 6 && c.detail == 12);
    inject(r.dport, stamp(frameOf(good), 55, 0xB0B0, 0xD0D0, 0));    // duplicate reply
    r.dome.tick(r.now);
    assert(!r.dome.takeCompletion(c) && r.dome.stats().unmatched_replies == 3);
    // a reply never asks for acknowledgement and none is generated
    size_t before = r.countToBody(MessageType::Reply);
    r.run(50); assert(r.countToBody(MessageType::Reply) == before);
''')

    def test_malformed_wrong_session_and_stale_frames_do_not_refresh_link(self):
        self.check(r'''
    Solo s(true); s.connect();
    s.now = 1100; s.heartbeat(0xD0D0, 0xB0B0); s.e.tick(s.now);        // last valid heartbeat at 1100
    uint16_t good_seq = s.seq - 1;
    s.now = 1250;
    s.heartbeat(0xD0D0, 0xBAD); s.heartbeat(0xBAD, 0xB0B0);              // wrong sessions
    Frame mal = stamp(frameOf(Heartbeat{1, 1}), s.seq++, 0xD0D0, 0xB0B0, 0); mal.payload[0] = 9; inject(s.p, mal);
    Frame trunc = stamp(frameOf(Heartbeat{1, 1}), s.seq++, 0xD0D0, 0xB0B0, 0); trunc.length = 1; inject(s.p, trunc);
    inject(s.p, stamp(frameOf(Heartbeat{1, 1}), good_seq, 0xD0D0, 0xB0B0, 0));   // not newer
    Bytes crc = wireOf(stamp(frameOf(Heartbeat{1, 1}), s.seq++, 0xD0D0, 0xB0B0, 0)); crc[crc.size() - 3] ^= 1; s.p.push(crc);
    s.e.tick(s.now);
    assert(s.e.counters().session == 2 && s.e.counters().enum_value == 1 && s.e.counters().payload_length == 1);
    assert(s.e.counters().crc == 1 && s.e.stats().stale_streams == 1);
    assert(s.e.connected(1399) && !s.e.connected(1400));                 // still timed from 1100
''')

    def test_changed_content_duplicate_is_rejected_without_replay(self):
        self.check(r'''
    Rig r; r.connect();
    Frame f = audioPlay(1, 2); uint16_t seq = 0;
    assert(r.dome.request(f, r.now, seq)); r.run(3);
    Frame got; assert(r.body.takeReceived(got)); assert(r.body.reply(got, Result::Accepted, 0));
    r.run(3);
    Frame changed = stamp(audioPlay(1, 3), seq, 0xD0D0, 0xB0B0, 1);   // same sequence, different track
    inject(r.bport, changed); r.run(3);
    assert(!r.body.takeReceived(got) && r.body.stats().sequence_conflicts == 1 && r.body.stats().protocol_failures == 1);
    // A conflicting frame is answered with the ORIGINAL cached result, never a rejection of the executed action.
    Reply rp = as<Reply>(r.to_dome.back());
    assert(rp.request_type == 0x21 && rp.request_seq == seq && rp.result == 0);
    inject(r.bport, stamp(audioPlay(1, 2), seq, 0xD0D0, 0xB0B0, 1)); r.run(3);
    rp = as<Reply>(r.to_dome.back()); assert(rp.result == 0 && !r.body.takeReceived(got));
    assert(r.body.stats().duplicates == 1);
    // While the original is still pending, a conflicting frame gets no reply on the shared key.
    Solo s(true); s.connect();
    s.from(audioPlay(1, 2), 40, 1); s.e.tick(s.now); assert(s.e.takeReceived(got)); s.sent();
    s.from(audioPlay(1, 3), 40, 1); s.e.tick(s.now + 1);
    assert(!s.e.takeReceived(got) && s.e.stats().protocol_failures == 1);
    assert(countType(s.sent(), MessageType::Reply) == 0);
''')

    def test_streams_wraparound_retransmit_and_lease_not_refreshed(self):
        self.check(r'''
    Solo s(true); s.connect();
    DriveRequest d = {100, 100, 100, 0}; Frame f = frameOf(d);
    uint16_t seqs[] = {0xFFFE, 0xFFFF, 0, 1};
    Frame got;
    for (int i = 0; i < 4; ++i) { s.from(f, seqs[i], 0); s.e.tick(s.now); assert(s.e.takeReceived(got) && got.sequence == seqs[i]); }
    s.from(f, 1, 0); s.from(f, 0, 0); s.from(f, 0xFFFF, 0); s.e.tick(s.now);     // retransmission, older, wrapped-older
    assert(!s.e.takeReceived(got) && s.e.stats().stale_streams == 3);
    s.from(f, 2, 0); s.e.tick(s.now); assert(s.e.takeReceived(got));
    // separate records per message type
    HallState h = {3, 1, 5, 0}; s.from(frameOf(h), 2, 0); s.e.tick(s.now); assert(s.e.takeReceived(got) && got.type == MessageType::HallState);
    // a flagged stream frame is answered, never retried or cached
    s.from(f, 3, 1); s.e.tick(s.now); assert(s.e.takeReceived(got));
    assert(s.e.reply(got, Result::Inhibited, 0));
    s.e.tick(s.now + 1);
    std::vector<Frame> out = s.sent(); size_t n = countType(out, MessageType::Reply);
    assert(n == 1); Reply rp; for (size_t i = 0; i < out.size(); ++i) if (out[i].type == MessageType::Reply) rp = as<Reply>(out[i]);
    assert(rp.request_type == 0x22 && rp.request_seq == 3 && rp.result == 4);
    // an unread newer snapshot supersedes an unread older one
    s.from(f, 4, 0); s.from(f, 5, 0); s.e.tick(s.now);
    assert(s.e.takeReceived(got) && got.sequence == 5 && !s.e.takeReceived(got));
''')

    def test_per_wheel_keys_coalescing_and_stream_sequences(self):
        self.check(r'''
    Rig r; r.connect();
    VescStatus l = {}; l.wheel = 0; l.valid_fields = 0xFF; l.pack_cV = 1200;
    VescStatus rt = l; rt.wheel = 1;
    r.bport.window = 0;
    l.pack_cV = 1100; assert(r.body.publishLatest(frameOf(l)));
    rt.pack_cV = 1201; assert(r.body.publishLatest(frameOf(rt)));
    l.pack_cV = 1102; assert(r.body.publishLatest(frameOf(l)));          // replaces the first left snapshot
    RcStatus rc = {}; rc.sample_counter = 1; assert(r.body.publishLatest(frameOf(rc)));
    rc.sample_counter = 2; assert(r.body.publishLatest(frameOf(rc)));
    r.bport.window = 1u << 30; r.run(5);
    std::vector<VescStatus> got; std::vector<RcStatus> rcs; Frame f;
    while (r.dome.takeReceived(f)) {
        if (f.type == MessageType::VescStatus) got.push_back(as<VescStatus>(f));
        if (f.type == MessageType::RcStatus) rcs.push_back(as<RcStatus>(f));
    }
    assert(got.size() == 2 && rcs.size() == 1 && rcs[0].sample_counter == 2);
    assert(got[0].wheel != got[1].wheel);
    for (size_t i = 0; i < 2; ++i) assert(got[i].wheel == 0 ? got[i].pack_cV == 1102 : got[i].pack_cV == 1201);
    assert(r.countToDome(MessageType::VescStatus) == 2);               // replaced snapshot never reached the wire
    // sustained traffic: both wheels keep arriving
    int seen[2] = {0, 0};
    for (int i = 0; i < 100; ++i) {
        l.pack_cV = 1000 + i; rt.pack_cV = 2000 + i;
        r.body.publishLatest(frameOf(l)); r.body.publishLatest(frameOf(rt));
        rc.sample_counter = 10 + i; r.body.publishLatest(frameOf(rc));
        ControlRequest none = {0, 0, 0, 0}; (void)none;
        r.step();
        while (r.dome.takeReceived(f)) if (f.type == MessageType::VescStatus) ++seen[as<VescStatus>(f).wheel];
    }
    assert(seen[0] >= 95 && seen[1] >= 95);
    // transmitted stream sequences increase wrap-safely
    uint16_t last = 0; bool have = false;
    for (size_t i = 0; i < r.to_dome.size(); ++i) if (r.to_dome[i].type == MessageType::VescStatus) {
        if (have) assert(newer16(r.to_dome[i].sequence, last));
        last = r.to_dome[i].sequence; have = true;
    }
''')

    def test_age_saturation_and_queue_residence(self):
        self.check(r'''
    Rig r; r.connect();
    r.bport.window = 0;
    VescStatus v = {}; v.wheel = 0; v.valid_fields = 0xFF; v.source_age_ms = 65400;
    assert(r.body.publishLatest(frameOf(v)));
    HallState h = {3, 1, 7, 100}; assert(r.body.publishLatest(frameOf(h)));
    r.run(300);
    r.bport.window = 1u << 30; r.run(5);
    Frame f; bool sv = false, sh = false;
    while (r.dome.takeReceived(f)) {
        if (f.type == MessageType::VescStatus) { assert(as<VescStatus>(f).source_age_ms == 65535); sv = true; }
        if (f.type == MessageType::HallState) { uint16_t a = as<HallState>(f).source_age_ms; assert(a >= 400 && a < 420); sh = true; }
    }
    assert(sv && sh);
''')

    def test_unknown_type_replies_unsupported_and_framing_rejects_out_of_range(self):
        self.check(r'''
    Solo s(true); s.connect();
    Frame u; memset(&u, 0, sizeof u); u.version = 1; u.type = static_cast<MessageType>(0x3F); u.length = 0;
    s.from(u, 20, 1); s.e.tick(s.now);
    Frame got; assert(!s.e.takeReceived(got));
    assert(s.e.stats().unsupported == 1 && s.e.counters().type == 1);
    std::vector<Frame> out = s.sent(); Reply rp = {}; bool found = false;
    for (size_t i = 0; i < out.size(); ++i) if (out[i].type == MessageType::Reply) { rp = as<Reply>(out[i]); found = true; }
    assert(found && rp.request_type == 0x3F && rp.request_seq == 20 && rp.result == 5);
    Frame bad = u; bad.type = static_cast<MessageType>(0x77);
    // out-of-range types are refused by framing and by the encoder, never by the endpoint
    uint8_t buf[200]; assert(Codec::encode(bad, buf, sizeof buf) == 0);
''')

    def test_event_reliability_and_semantic_validation(self):
        self.check(r'''
    Rig r; r.connect();
    Event ev = {0, 0x21, 5, 3}; Frame f = frameOf(ev); uint16_t seq = 0;
    assert(r.body.request(f, r.now, seq));
    r.drop_to_body[static_cast<int>(MessageType::Reply)] = 1;
    r.run(3);
    Frame got; assert(r.dome.takeReceived(got) && got.type == MessageType::Event && !r.dome.takeReceived(got));
    r.run(300);
    Completion c; assert(r.body.takeCompletion(c) && c.outcome == Outcome::Replied && c.result == 0);
    // malformed requests: never delivered, flagged ones answered with invalid-argument
    Solo s(true); s.connect();
    Frame bad = stamp(audioPlay(1, 1), 30, s.peer, s.local, 1); bad.payload[4] = 200;   // volume enum
    inject(s.p, bad); s.e.tick(s.now);
    assert(!s.e.takeReceived(got) && s.e.stats().invalid_payload == 1);
    std::vector<Frame> out = s.sent(); Reply rp = {};
    for (size_t i = 0; i < out.size(); ++i) if (out[i].type == MessageType::Reply) rp = as<Reply>(out[i]);
    assert(rp.request_seq == 30 && rp.result == 1);
    Frame hall = stamp(frameOf(HallState{3, 1, 1, 0}), 31, s.peer, s.local, 0); hall.length = 7;   // obsolete layout
    inject(s.p, hall); s.e.tick(s.now); assert(!s.e.takeReceived(got) && s.e.stats().invalid_payload == 2);
''')

    def test_reply_handle_semantics(self):
        self.check(r'''
    Solo s(true); s.connect(); Frame got;
    s.from(audioPlay(1, 2), 40, 1); s.e.tick(s.now); assert(s.e.takeReceived(got));
    assert(s.e.reply(got, Result::Busy, 7));
    assert(!s.e.reply(got, Result::Accepted, 0));                       // second answer refused
    Frame other = got; other.source_session = 0x1234;
    assert(!s.e.reply(other, Result::Accepted, 0));                     // wrong session
    assert(!s.e.reply(got, static_cast<Result>(99), 0));
    s.e.tick(s.now); std::vector<Frame> out = s.sent();
    // replays after an answer use the stored result
    s.from(audioPlay(1, 2), 40, 1); s.e.tick(s.now + 1);
    out = s.sent(); Reply rp = {};
    for (size_t i = 0; i < out.size(); ++i) if (out[i].type == MessageType::Reply) rp = as<Reply>(out[i]);
    assert(rp.result == 6 && rp.detail == 7 && !s.e.takeReceived(got));
''')

    def test_session_change_invalidates_cache_queues_and_streams(self):
        self.check(r'''
    Solo s(true); s.connect(); Frame got;
    s.from(audioPlay(1, 2), 40, 1); s.e.tick(s.now);
    DriveRequest d = {1, 1, 100, 0}; s.from(frameOf(d), 41, 0); s.e.tick(s.now);
    s.hello(0xD1D1, kRoleDome, 0x04); s.e.tick(s.now);
    assert(s.e.peerSession() == 0xD1D1 && !s.e.takeReceived(got) && !s.e.connected(s.now));
    s.peer = 0xD1D1; s.heartbeat(0xD1D1, s.local); s.e.tick(s.now); assert(s.e.connected(s.now));
    // the same sequence is fresh in the new session
    s.from(audioPlay(1, 2), 40, 1); s.from(frameOf(d), 41, 0); s.e.tick(s.now);
    assert(s.e.takeReceived(got) && s.e.takeReceived(got) && s.e.stats().duplicates == 0);
''')

    def test_partial_tx_and_coalesced_streams(self):
        self.check(r'''
    for (int per = 1; per <= 3; per += 2) {
        Rig r; r.connect(); r.dport.per_call = per; r.bport.per_call = per;
        Frame f = audioPlay(1, 2); uint16_t seq;
        assert(r.dome.request(f, r.now, seq));
        r.run(120);
        Frame got; assert(r.body.takeReceived(got) && got.sequence == seq);
        assert(r.body.stats().partial_writes + r.dome.stats().partial_writes > 0);
        assert(r.body.reply(got, Result::Accepted, 0)); r.run(120);
        Completion c; assert(r.dome.takeCompletion(c) && c.outcome == Outcome::Replied);
    }
    // two frames in one chunk plus a fragment
    Solo s(true); s.connect();
    Bytes a = wireOf(stamp(frameOf(HallState{3, 1, 1, 0}), 60, s.peer, s.local, 0));
    Bytes b = wireOf(stamp(audioPlay(1, 2), 61, s.peer, s.local, 1));
    Bytes both = a; both.insert(both.end(), b.begin(), b.end());
    s.p.push(both); s.e.tick(s.now);
    Frame got; assert(s.e.takeReceived(got) && s.e.takeReceived(got) && !s.e.takeReceived(got));
''')

    def test_cache_expiry_and_receive_queue_full(self):
        self.check(r'''
    Solo s(true); s.connect(); Frame got;
    s.from(audioPlay(1, 2), 70, 1); s.e.tick(s.now); assert(s.e.takeReceived(got)); assert(s.e.reply(got, Result::Accepted, 0));
    s.now += 2100; s.heartbeat(s.peer, s.local); s.e.tick(s.now);
    s.from(audioPlay(1, 2), 70, 1); s.e.tick(s.now);
    assert(s.e.takeReceived(got));                                      // expired: treated as new
    Solo q(true); q.connect();
    for (int i = 0; i < 10; ++i) q.from(audioPlay(1, 10 + i), 80 + i, 1);
    q.e.tick(q.now);
    int n = 0; while (q.e.takeReceived(got)) ++n;
    assert(n == 8 && q.e.stats().rx_queue_full == 2);
    std::vector<Frame> out = q.sent(); int busy = 0;
    for (size_t i = 0; i < out.size(); ++i) if (out[i].type == MessageType::Reply && as<Reply>(out[i]).result == 6) ++busy;
    assert(busy == 2);
''')

    def test_sender_sequence_wraps(self):
        self.check(r'''
    Solo s(false); s.connect();
    Frame f = audioPlay(1, 2); uint16_t q = 0, prev = 0; bool have = false;
    for (int i = 0; i < 70000; ++i) {
        f = audioPlay(1, 2);
        assert(s.e.request(f, s.now, q));
        if (have) assert(newer16(q, prev));
        prev = q; have = true;
        s.e.tick(s.now);
        Reply rp = {0x21, q, 0, 0};
        s.from(frameOf(rp), static_cast<uint16_t>(1000 + i), 0);
        s.p.tx.clear(); s.e.tick(s.now);
        if (i % 20 == 0) { s.now += 50; s.heartbeat(s.peer, s.local); s.e.tick(s.now); }
        Completion c; while (s.e.takeCompletion(c)) {}
    }
''')

    def test_equal_boot_sessions_on_opposite_roles_connect(self):
        self.check(r"""
    Rig r(1, 1); r.connect();
    assert(r.body.peerSession() == 1 && r.dome.peerSession() == 1);
    Frame f = audioPlay(1, 2); uint16_t seq = 0;
    assert(r.dome.request(f, r.now, seq)); r.run(3);
    Frame got; assert(r.body.takeReceived(got) && got.sequence == seq);
    assert(r.body.reply(got, Result::Accepted, 0)); r.run(3);
    Completion c; assert(r.dome.takeCompletion(c) && c.outcome == Outcome::Replied);
""")

    def test_dropout_clears_state_and_blocks_old_replays(self):
        self.check(r"""
    Solo s(true); s.connect(); Frame got;
    s.heartbeat(s.peer, s.local, 2, 1); s.e.tick(s.now);
    assert(s.e.peerMode() == 2 && s.e.peerReady() == 1 && s.e.peerGeneration() == 1);
    s.from(audioPlay(1, 2), 40, 1); s.e.tick(s.now);
    assert(s.e.takeReceived(got) && got.sequence == 40);                  // delivered, never answered
    s.from(audioPlay(1, 4), 42, 1); s.e.tick(s.now);                      // queued, not yet taken
    DriveRequest d = {1, 1, 100, 0}; s.from(frameOf(d), 41, 0); s.e.tick(s.now);
    s.sent();
    s.now += 400; s.e.tick(s.now);
    assert(!s.e.connected(s.now) && s.e.peerGeneration() == 2);
    assert(s.e.peerMode() == 0 && s.e.peerReady() == 0);
    assert(!s.e.takeReceived(got));                                      // queues and streams cleared
    assert(!s.e.reply(got, Result::Accepted, 0));                         // old authority cannot answer later
    s.heartbeat(s.peer, s.local); s.e.tick(s.now); assert(s.e.connected(s.now)); s.sent();
    s.from(audioPlay(1, 2), 40, 1); s.from(audioPlay(1, 4), 42, 1); s.e.tick(s.now + 1);
    assert(!s.e.takeReceived(got));                                      // same-session replay never executes
    int wrong = 0; std::vector<Frame> out = s.sent();
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i].type == MessageType::Reply && as<Reply>(out[i]).result == 7) ++wrong;
    assert(wrong == 2 && s.e.stats().protocol_failures == 0);
    s.from(frameOf(d), 41, 0); s.e.tick(s.now + 2);
    assert(s.e.takeReceived(got) && got.type == MessageType::DriveRequest);   // stream sequence restarted
    // a renewed same-session HELLO while down keeps the guard
    s.now += 400; s.e.tick(s.now); assert(!s.e.connected(s.now));
    s.hello(s.peer, kRoleDome, 0x04); s.heartbeat(s.peer, s.local); s.e.tick(s.now + 1); s.sent();
    assert(s.e.connected(s.now + 1) && s.e.peerSession() == s.peer);
    s.from(audioPlay(1, 4), 42, 1); s.e.tick(s.now + 2);
    assert(!s.e.takeReceived(got));
""")

    def test_answered_but_unsent_reply_is_dropped_at_dropout(self):
        self.check(r"""
    Solo s(true); s.connect(); Frame got;
    s.from(audioPlay(1, 2), 40, 1); s.e.tick(s.now); assert(s.e.takeReceived(got));
    s.p.window = 0;
    assert(s.e.reply(got, Result::Accepted, 0));
    s.now += 400; s.e.tick(s.now);
    s.p.window = 1u << 30; s.sent();
    s.heartbeat(s.peer, s.local); s.e.tick(s.now + 1);
    assert(countType(s.sent(), MessageType::Reply) == 0);               // no stale answer after the dropout
    s.from(audioPlay(1, 2), 40, 1); s.e.tick(s.now + 2);
    assert(!s.e.takeReceived(got));
""")

    def test_event_is_acknowledged_only_after_the_application_takes_it(self):
        self.check(r"""
    Solo s(true); s.connect(); Frame got;
    Event ev = {0, 0x21, 5, 3};
    s.from(frameOf(ev), 50, 1); s.e.tick(s.now);
    assert(countType(s.sent(), MessageType::Reply) == 0);               // received, not yet applied
    s.from(frameOf(ev), 50, 1); s.e.tick(s.now + 1);                     // sender retry while queued
    assert(countType(s.sent(), MessageType::Reply) == 0 && s.e.stats().duplicates == 1);
    assert(s.e.takeReceived(got) && got.type == MessageType::Event && !s.e.takeReceived(got));
    s.e.tick(s.now + 2);
    std::vector<Frame> out = s.sent(); Reply rp = {}; int n = 0;
    for (size_t i = 0; i < out.size(); ++i) if (out[i].type == MessageType::Reply) { rp = as<Reply>(out[i]); ++n; }
    assert(n == 1 && rp.request_type == static_cast<uint8_t>(MessageType::Event) && rp.request_seq == 50 && rp.result == 0);
    s.from(frameOf(ev), 50, 1); s.e.tick(s.now + 3);                     // later retry replays the receipt only
    assert(!s.e.takeReceived(got) && countType(s.sent(), MessageType::Reply) == 1);
    // an event that was never taken is not acknowledged across a dropout and fails explicitly
    s.from(frameOf(ev), 51, 1); s.e.tick(s.now + 4); s.sent();
    s.now += 400; s.e.tick(s.now);
    s.heartbeat(s.peer, s.local); s.e.tick(s.now + 1); s.sent();
    s.from(frameOf(ev), 51, 1); s.e.tick(s.now + 2);
    assert(!s.e.takeReceived(got));
    out = s.sent(); n = 0;
    for (size_t i = 0; i < out.size(); ++i) if (out[i].type == MessageType::Reply) { rp = as<Reply>(out[i]); ++n; }
    assert(n == 1 && rp.result == 7);                                    // WrongEpoch, never Accepted
""")

    def test_partial_transmit_is_delimited_before_the_next_frame_after_reset(self):
        self.check(r"""
    for (int closed = 0; closed < 2; ++closed) {
        Solo s(false); s.connect(); s.sent();
        s.p.per_call = 3;
        s.now += 100; s.e.tick(s.now);                                   // heartbeat due: 3 bytes leave
        const size_t partial = s.p.tx.size();
        assert(partial == 3);
        s.p.per_call = 1u << 30;
        if (closed) s.p.window = 0;
        s.hello(0xB1B1, kRoleBody, 0x1B); s.e.tick(s.now + 1);           // peer rebooted: wire is dropped
        if (closed) { assert(s.p.tx.size() == partial); s.p.window = 1u << 30; s.e.tick(s.now + 2); }
        assert(s.p.tx.size() > partial && s.p.tx[partial] == 0);         // delimiter closes the fragment
        std::vector<Frame> out = s.sent();
        assert(countType(out, MessageType::Hello) >= 1 || countType(out, MessageType::Heartbeat) >= 1);
    }
""")

    def test_dropout_closes_partial_transmit_too(self):
        self.check(r"""
    Solo s(false); s.connect(); s.sent();
    s.p.per_call = 3; s.now += 100; s.e.tick(s.now);
    assert(s.p.tx.size() == 3);
    s.p.per_call = 1u << 30; s.now += 400; s.e.tick(s.now);
    assert(s.p.tx.size() > 3 && s.p.tx[3] == 0);
""")

    def test_bootstrap_adapter_links_the_endpoint_without_hardware(self):
        result = run_cpp(r"""
#include "body/LinkBootstrap.h"
#include <cassert>
#include <cstdio>
int main() {
    body::LinkBootstrap link(0);
    for (unsigned t = 0; t < 2000; t += 10) link.tick(t);
    assert(!link.connected(2000) && link.endpoint().localSession() == 0);
    body::LinkBootstrap on(5);
    on.tick(1000);
    assert(on.endpoint().localSession() == 5);
    puts("ok"); return 0;
}
""", extra_sources=LINK_SOURCES + [BODY_SRC / "body/LinkBootstrap.cpp"], include_dirs=LINK_INCLUDES)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_teensy_main_references_the_bootstrap(self):
        main = (BODY_SRC / "main.cpp").read_text()
        self.assertIn("LinkBootstrap", main)
        header = (BODY_SRC / "body/LinkBootstrap.h").read_text() + (BODY_SRC / "body/LinkBootstrap.cpp").read_text()
        self.assertNotIn("Arduino.h", header)
        self.assertNotIn("Serial", header)


class ConfigStoreTests(unittest.TestCase):
    def check(self, body):
        result = run_link(body, helpers=self.PROFILE)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    PROFILE = r'''
static void fill(body::CommissioningProfile& p) {
    using namespace body;
    p = CommissioningProfile();
    struct F { uint8_t id, wheel; int32_t v; };
    const F g[] = {{0,0,1500},{1,0,1100},{2,0,1900},{3,0,10},{4,0,300},{19,0,9000},{20,0,9000}};
    for (size_t i = 0; i < sizeof g / sizeof g[0]; ++i) assert(setField(p, g[i].id, g[i].wheel, g[i].v) == FieldResult::Ok);
    const F w[] = {{5,0,1},{6,0,6},{7,0,7},{8,0,1},{9,0,40000},{10,0,10000},{11,0,5000},{12,0,40000},{13,0,1100},
                   {14,0,1500},{15,0,150},{16,0,40000},{17,0,2000},{18,0,200}};
    for (int wh = 0; wh < 2; ++wh)
        for (size_t i = 0; i < sizeof w / sizeof w[0]; ++i) assert(setField(p, w[i].id, wh, w[i].v) == FieldResult::Ok);
}
static body::AcceptanceEvidence evidence(const body::CommissioningProfile& p, uint8_t bit) {
    body::AcceptanceEvidence e;
    e.ch6_off = e.ch9_off = e.sticks_centered = e.stationary = e.operator_confirmed = true;
    e.config_digest = body::acceptanceDigest(p, bit);
    if (bit <= body::kAcceptAutoTiming) {
        e.test_completed = true; e.observed_run_id = e.commanded_run_id = 7;
        if (bit == body::kAcceptAutoTiming) e.cw_completed = e.ccw_completed = true;
    } else {
        e.vesc_operator_observed = true;
    }
    return e;
}
static body::AcceptResult accept(body::CommissioningProfile& p, uint8_t bit) {
    return body::acceptBit(p, bit, evidence(p, bit));
}
static void acceptAll(body::CommissioningProfile& p) {
    for (int b = 0; b <= 11; ++b) assert(accept(p, b) == body::AcceptResult::Ok);
}
'''

    def test_blank_corrupt_and_io_error_are_distinct(self):
        self.check(r'''
    using namespace body;
    FakeStorage st; ConfigStore cs(st); CommissioningProfile p;
    assert(cs.load(p) == ConfigResult::Uncommissioned);
    assert(!readiness(p).drive && !readiness(p).manual_dome && !readiness(p).auto_dome && p.allow_remote_drive == 0);
    st.fail_read = true; assert(cs.load(p) == ConfigResult::IoError);
    st.fail_read = false;
    fill(p); acceptAll(p);
    assert(!cs.save(p));                                                // gate closed by default
    assert(cs.lastSave() == SaveResult::GateClosed);
    assert(cs.trySave(p, true, true) == SaveResult::Ok);
    CommissioningProfile q; assert(cs.load(q) == ConfigResult::Ready && readiness(q).drive && q.generation == 1);
    for (size_t i = 0; i < st.mem.size(); ++i) if (st.mem[i] != 0xFF) st.mem[i] ^= 0x5A;
    assert(cs.load(q) == ConfigResult::Corrupt);
    assert(!readiness(q).drive);
    st.fail_read = true; assert(cs.load(q) == ConfigResult::IoError);
''')

    def test_save_gate_validation_and_verify(self):
        self.check(r'''
    using namespace body;
    FakeStorage st; ConfigStore cs(st); CommissioningProfile p; fill(p);
    assert(cs.trySave(p, false, true) == SaveResult::GateClosed && cs.trySave(p, true, false) == SaveResult::GateClosed);
    assert(st.writes == 0);
    CommissioningProfile bad = p; bad.allow_remote_drive = 1;
    assert(cs.trySave(bad, true, true) == SaveResult::InvalidProfile);
    bad = p; bad.servo_min = 1600; assert(cs.trySave(bad, true, true) == SaveResult::InvalidProfile);
    bad = p; bad.acceptance = 1u << 4;                                    // accepted bit without its prerequisites
    bad.wheel[0].set_mask = 0; assert(cs.trySave(bad, true, true) == SaveResult::InvalidProfile);
    st.drop_writes = true; assert(cs.trySave(p, true, true) == SaveResult::VerifyFailed);
    st.drop_writes = false; st.fail_write_after = 3; assert(cs.trySave(p, true, true) == SaveResult::IoError);
    CommissioningProfile q; st.fail_write_after = -1;
    assert(cs.load(q) != ConfigResult::Ready);                            // a torn first save never becomes a profile
    assert(cs.trySave(p, true, true) == SaveResult::Ok && cs.load(q) == ConfigResult::Ready);
    assert(!readiness(q).drive);                                         // saving is not acceptance
''')

    def test_torn_write_sweep_keeps_previous_profile(self):
        self.check(r'''
    using namespace body;
    FakeStorage st; ConfigStore cs(st); CommissioningProfile a, b, q; fill(a); b = a;
    assert(cs.trySave(a, true, true) == SaveResult::Ok);
    assert(setField(b, 4, 0, 400) == FieldResult::Ok);
    std::vector<uint8_t> snapshot = st.mem;
    int saw_old = 0, saw_new = 0;
    for (int cut = 0; cut < 400; ++cut) {
        st.mem = snapshot; st.fail_write_after = cut;
        ConfigStore c2(st); CommissioningProfile cur;
        assert(c2.load(cur) == ConfigResult::Ready);
        SaveResult r = c2.trySave(b, true, true);
        st.fail_write_after = -1;
        ConfigStore c3(st);
        assert(c3.load(q) == ConfigResult::Ready);                        // never corrupt, never blank
        if (q.duty_slew_permille_per_s == 300) ++saw_old; else { assert(q.duty_slew_permille_per_s == 400); ++saw_new; }
        if (r == SaveResult::Ok) { assert(q.duty_slew_permille_per_s == 400); break; }
    }
    assert(saw_old > 0 && saw_new > 0);
    // alternation: two saves occupy different slots
    st.mem = snapshot; ConfigStore c4(st); CommissioningProfile cur; assert(c4.load(cur) == ConfigResult::Ready);
    uint32_t g0 = c4.generation();
    assert(c4.trySave(b, true, true) == SaveResult::Ok && c4.generation() == g0 + 1);
    assert(c4.trySave(a, true, true) == SaveResult::Ok && c4.generation() == g0 + 2);
    assert(c4.load(q) == ConfigResult::Ready && q.duty_slew_permille_per_s == 300);
''')

    def test_boot_counter_double_buffered(self):
        self.check(r'''
    using namespace body;
    FakeStorage st; uint32_t s1 = 0, s2 = 0, s3 = 0;
    { ConfigStore cs(st); assert(cs.nextBootSession(s1) && s1 == 1); size_t w = st.writes; assert(cs.nextBootSession(s2) && s2 == 1 && st.writes == w); }
    { ConfigStore cs(st); assert(cs.nextBootSession(s2) && s2 == 2); }
    { ConfigStore cs(st); assert(cs.nextBootSession(s3) && s3 == 3); }
    // torn counter write: next boot still gets a session that moves forward or repeats at most the older value
    std::vector<uint8_t> snap = st.mem;
    for (int cut = 0; cut < 40; ++cut) {
        st.mem = snap; st.fail_write_after = cut;
        { ConfigStore cs(st); uint32_t t; cs.nextBootSession(t); }
        st.fail_write_after = -1; ConfigStore cs2(st); uint32_t u = 0;
        assert(cs2.nextBootSession(u) && u >= 3 && u != 0);
    }
    // unreadable: no session
    st.mem = snap; st.fail_read = true; { ConfigStore cs(st); uint32_t t = 77; assert(!cs.nextBootSession(t) && t == 77); }
    st.fail_read = false;
    // both slots corrupt but non-blank: refuse rather than reuse a session
    for (size_t i = 0; i < 64; ++i) st.mem[i] ^= 0x33;
    { ConfigStore cs(st); uint32_t t = 77; assert(!cs.nextBootSession(t)); }
    // wrap skips zero
    FakeStorage w; { ConfigStore cs(w); uint32_t t; cs.nextBootSession(t); }
    { uint8_t slot[ConfigStore::kBootSlotSize]; ConfigStore::encodeBootSlot(1000, 0xFFFFFFFFu, slot);
      for (size_t i = 0; i < sizeof slot; ++i) w.mem[ConfigStore::bootSlotAddress(0) + i] = slot[i]; }
    { ConfigStore cs(w); uint32_t t = 9; assert(cs.nextBootSession(t) && t == 1); }
''')

    def test_acceptance_requires_explicit_operator_and_stationary_evidence(self):
        self.check(r"""
    using namespace body;
    CommissioningProfile p; fill(p);
    AcceptanceEvidence none;                                              // no permissive defaults
    assert(!none.ch6_off && !none.ch9_off && !none.sticks_centered && !none.stationary && !none.operator_confirmed);
    assert(!none.test_completed && !none.test_cancelled && !none.cw_completed && !none.ccw_completed && !none.vesc_operator_observed);
    assert(none.observed_run_id == 0 && none.commanded_run_id == 0 && none.config_digest == 0);
    for (int b = 0; b < 12; ++b) assert(acceptBit(p, b, none) != AcceptResult::Ok && p.acceptance == 0);
    const uint8_t bits[] = {0, 4};
    for (int field = 0; field < 5; ++field) {
        for (int k = 0; k < 2; ++k) {
            AcceptanceEvidence e = evidence(p, bits[k]);
            if (field == 0) e.ch6_off = false;
            if (field == 1) e.ch9_off = false;
            if (field == 2) e.sticks_centered = false;
            if (field == 3) e.stationary = false;
            if (field == 4) e.operator_confirmed = false;
            assert(acceptBit(p, bits[k], e) != AcceptResult::Ok && p.acceptance == 0);
        }
    }
    assert(accept(p, 0) == AcceptResult::Ok && (p.acceptance & 1u));
""")

    def test_acceptance_neutral_reference_and_timing_need_matching_completed_runs(self):
        self.check(r"""
    using namespace body;
    for (int bit = 0; bit <= 3; ++bit) {
        CommissioningProfile p; fill(p);
        if (bit > 0) assert(accept(p, 0) == AcceptResult::Ok);
        AcceptanceEvidence e = evidence(p, bit);
        e.test_completed = false; assert(acceptBit(p, bit, e) == AcceptResult::TestEvidence);
        e = evidence(p, bit); e.test_cancelled = true; assert(acceptBit(p, bit, e) == AcceptResult::TestEvidence);
        e = evidence(p, bit); e.observed_run_id = 8; assert(acceptBit(p, bit, e) == AcceptResult::TestEvidence);
        e = evidence(p, bit); e.observed_run_id = e.commanded_run_id = 0; assert(acceptBit(p, bit, e) == AcceptResult::TestEvidence);
        e = evidence(p, bit); e.vesc_operator_observed = true; e.test_completed = false;
        assert(acceptBit(p, bit, e) == AcceptResult::TestEvidence);       // operator VESC observation is not a dome run
        e = evidence(p, bit); e.config_digest ^= 1; assert(acceptBit(p, bit, e) == AcceptResult::ConfigMismatch);
        if (bit == 3) {
            e = evidence(p, bit); e.cw_completed = false; assert(acceptBit(p, bit, e) == AcceptResult::TestEvidence);
            e = evidence(p, bit); e.ccw_completed = false; assert(acceptBit(p, bit, e) == AcceptResult::TestEvidence);
        }
        assert(((p.acceptance >> bit) & 1u) == 0);
        const uint32_t before = acceptanceDigest(p, bit);
        assert(accept(p, bit) == AcceptResult::Ok);
        assert(acceptanceDigest(p, bit) == before);
    }
    // evidence collected for one stage config is stale after the staged values change
    CommissioningProfile p; fill(p);
    AcceptanceEvidence e = evidence(p, 0);
    assert(setField(p, 1, 0, 1200) == FieldResult::Ok);
    assert(acceptBit(p, 0, e) == AcceptResult::ConfigMismatch);
    assert(accept(p, 0) == AcceptResult::Ok && accept(p, 3) == AcceptResult::Ok);
    CommissioningProfile q = p; assert(setField(q, 3, 0, 11) == FieldResult::Ok);
    assert(acceptBit(q, 3, evidence(p, 3)) == AcceptResult::ConfigMismatch);
""")

    def test_acceptance_external_vesc_checks_need_operator_observation(self):
        self.check(r"""
    using namespace body;
    CommissioningProfile p; fill(p);
    assert(accept(p, 4) == AcceptResult::Ok && accept(p, 5) == AcceptResult::Ok);
    for (int bit = 4; bit <= 11; ++bit) {
        AcceptanceEvidence e = evidence(p, bit);
        e.vesc_operator_observed = false;
        assert(acceptBit(p, bit, e) == AcceptResult::TestEvidence);
        e = evidence(p, bit); e.config_digest ^= 1;
        assert(acceptBit(p, bit, e) == AcceptResult::ConfigMismatch);
    }
    CommissioningProfile wheel1; fill(wheel1);
    assert(accept(wheel1, 5) == AcceptResult::Ok && ((wheel1.acceptance >> 4) & 1u) == 0);   // wheel bits are independent
    AcceptanceEvidence left = evidence(wheel1, 4);
    assert(setField(wheel1, 9, 0, 30000) == FieldResult::Ok);          // staged record changed after observation
    assert(acceptBit(wheel1, 4, left) == AcceptResult::ConfigMismatch);
    assert(std::strcmp(acceptanceBitName(0), "servo_neutral") == 0 && std::strcmp(acceptanceBitName(11), "reversal_right") == 0);
    assert(acceptanceBitName(12) == 0);
""")

    def test_vesc_values_layout_is_a_named_protocol_shape(self):
        self.check(r"""
    using namespace body;
    assert(kLayoutUnknown == 0 && kLayoutLegacyGetValues == 1);
    assert(!layoutSupported(0) && layoutSupported(1) && !layoutSupported(2));
    CommissioningProfile p; fill(p);
    assert(setField(p, 8, 0, kLayoutUnknown) == FieldResult::OutOfRange);
    int32_t v; assert(getField(p, 8, 0, v) && v == kLayoutLegacyGetValues);
    CommissioningProfile fresh; assert(!getField(fresh, 8, 0, v) && fresh.wheel[0].layout == kLayoutUnknown);
    for (int b = 0; b <= 3; ++b) assert(accept(p, b) == AcceptResult::Ok);
    p.wheel[0].layout = kLayoutUnknown;                                  // an unknown layout can never be drive-ready
    assert(!validateProfile(p) || !readiness(p).drive);
    for (int b = 4; b <= 11; ++b) accept(p, b);
    assert(!readiness(p).drive);
""")

    def test_first_ever_torn_boot_write_recovers_with_a_safe_counter(self):
        self.check(r"""
    using namespace body;
    for (int cut = 0; cut <= 32; ++cut) {
        FakeStorage st; st.fail_write_after = cut;
        uint32_t s0 = 0; { ConfigStore cs(st); if (cut < 32) assert(!cs.nextBootSession(s0)); else assert(cs.nextBootSession(s0)); }
        st.fail_write_after = -1;
        uint32_t s1 = 0; ConfigStore cs2(st);
        assert(cs2.nextBootSession(s1) && s1 >= 1);
        assert(cs2.bootResult() == BootResult::Ok);
        uint32_t s2 = 0; ConfigStore cs3(st); assert(cs3.nextBootSession(s2) && s2 == s1 + 1);
    }
    // a torn rewrite beside a valid record never reuses the valid counter
    FakeStorage st; uint32_t a = 0, b = 0, c = 0;
    { ConfigStore cs(st); assert(cs.nextBootSession(a)); }
    { ConfigStore cs(st); assert(cs.nextBootSession(b) && b == a + 1); }
    for (int cut = 0; cut < 33; ++cut) {
        FakeStorage t; t.mem = st.mem; t.fail_write_after = cut;
        { ConfigStore cs(t); uint32_t x; cs.nextBootSession(x); }
        t.fail_write_after = -1; ConfigStore cs2(t);
        assert(cs2.nextBootSession(c) && c > b);
    }
    // two committed-but-corrupt records are real corruption: no recovery, explicit boot fault
    FakeStorage k; { ConfigStore cs(k); uint32_t t; cs.nextBootSession(t); } { ConfigStore cs(k); uint32_t t; cs.nextBootSession(t); }
    for (size_t i = 0; i < 64; ++i) if (i % 32 != 31) k.mem[i] ^= 0x33;
    { ConfigStore cs(k); uint32_t t = 9; assert(!cs.nextBootSession(t) && t == 9 && cs.bootResult() == BootResult::Corrupt); }
""")

    def test_boot_storage_fault_is_separate_from_profile_diagnostics(self):
        self.check(r"""
    using namespace body;
    FakeStorage st; ConfigStore cs(st); CommissioningProfile p; uint32_t t;
    assert(cs.bootResult() == BootResult::NotAttempted);
    assert(faultBits(ConfigResult::Ready) == 0 && faultBits(ConfigResult::Uncommissioned) == 1);
    assert(faultBits(ConfigResult::Corrupt) == 2 && faultBits(ConfigResult::IoError) == 2);
    assert(cs.load(p) == ConfigResult::Uncommissioned && cs.faultMask(ConfigResult::Uncommissioned) == 1);
    st.fail_read_at = ConfigStore::bootSlotAddress(0); st.fail_read = true;
    assert(!cs.nextBootSession(t) && cs.bootResult() == BootResult::IoError);
    assert(cs.faultMask(ConfigResult::Ready) == 4 && cs.faultMask(ConfigResult::Corrupt) == 6);
    st.fail_read = false;                                              // profile region was never touched
    assert(cs.load(p) == ConfigResult::Uncommissioned);
    assert(cs.faultMask(ConfigResult::Uncommissioned) == 5);           // missing profile + boot-session error
    assert(cs.nextBootSession(t) && cs.bootResult() == BootResult::Ok && cs.faultMask(ConfigResult::Ready) == 0);
    // a profile read error leaves boot diagnostics healthy
    st.fail_read_at = ConfigStore::profileSlotAddress(0); st.fail_read = true;
    assert(cs.load(p) == ConfigResult::IoError && cs.faultMask(ConfigResult::IoError) == 2);
""")

    def test_field_envelopes_readiness_and_acceptance_prerequisites(self):
        self.check(r'''
    using namespace body;
    CommissioningProfile p;
    struct E { uint8_t id; bool wheel; int32_t lo, hi; };
    const E t[] = {{0,0,1400,1600},{1,0,1000,1499},{2,0,1501,2000},{3,0,1,25},{4,0,1,1000},{19,0,1,36000},{20,0,1,36000},
                   {6,1,0,255},{7,1,0,255},{9,1,1,100000},{10,1,1,20000},{11,1,0,20000},{12,1,1,100000},{13,1,1000,1500},
                   {14,1,1300,1600},{16,1,1,100000},{17,1,1,10000},{18,1,20,1000}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; ++i) {
        CommissioningProfile x;
        assert(setField(x, t[i].id, 0, t[i].lo) == FieldResult::Ok);
        assert(setField(x, t[i].id, 0, t[i].hi) == FieldResult::Ok);
        assert(setField(x, t[i].id, 0, t[i].lo - 1) == FieldResult::OutOfRange);
        assert(setField(x, t[i].id, 0, t[i].hi + 1) == FieldResult::OutOfRange);
        assert(setField(x, t[i].id, t[i].wheel ? 1 : 0, t[i].lo) == FieldResult::Ok);
        assert(setField(x, t[i].id, t[i].wheel ? 2 : 1, t[i].lo) == FieldResult::BadWheel);
    }
    assert(setField(p, 15, 0, 150) == FieldResult::Ok && setField(p, 15, 0, 149) == FieldResult::OutOfRange && setField(p, 15, 0, 151) == FieldResult::OutOfRange);
    assert(setField(p, 5, 0, 1) == FieldResult::Ok && setField(p, 5, 0, -1) == FieldResult::Ok && setField(p, 5, 0, 0) == FieldResult::OutOfRange);
    assert(setField(p, 8, 0, 1) == FieldResult::Ok && setField(p, 8, 0, 2) == FieldResult::OutOfRange);
    assert(setField(p, 99, 0, 1) == FieldResult::UnknownField);
    // acceptance needs its prerequisites and distinct readiness
    p = CommissioningProfile();
    assert(accept(p, 0) == AcceptResult::Prerequisite && accept(p, 12) == AcceptResult::UnsupportedBit);
    fill(p);
    for (int b = 0; b <= 2; ++b) assert(accept(p, b) == AcceptResult::Ok);
    assert(readiness(p).manual_dome && !readiness(p).auto_dome && !readiness(p).drive);
    assert(accept(p, 3) == AcceptResult::Ok && readiness(p).auto_dome && !readiness(p).drive);
    for (int b = 4; b <= 11; ++b) assert(accept(p, b) == AcceptResult::Ok);
    assert(readiness(p).drive && validateProfile(p));
    // changing a field clears only the acceptances it owns
    assert(setField(p, 4, 0, 250) == FieldResult::Ok && readiness(p).drive);
    assert(setField(p, 9, 1, 30000) == FieldResult::Ok && !readiness(p).drive && readiness(p).manual_dome && readiness(p).auto_dome);
    assert(setField(p, 0, 0, 1510) == FieldResult::Ok && !readiness(p).manual_dome && !readiness(p).auto_dome);
    int32_t v; assert(getField(p, 9, 1, v) && v == 30000);
''')
