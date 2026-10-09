import unittest
from pathlib import Path

from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
SHARED = ROOT / "shared/R2BodyLink"
SOURCES = [BODY / "body/IbusInput.cpp", BODY / "body/IbusTelemetry.cpp"]
PRELUDE = r'''
#include "body/IbusInput.h"
#include "body/IbusTelemetry.h"
#include <cassert>
#include <cstring>
#include <climits>
#include <vector>
#include <deque>
using namespace body;
using Bytes = std::vector<uint8_t>;
static void checksum(Bytes& b) {
    unsigned sum = 0;
    for (size_t i = 0; i < b.size() - 2; ++i) sum += b[i];
    unsigned c = 0xffff - sum;
    b[b.size()-2] = c; b.back() = c >> 8;
}
static Bytes frame(int bad = -1, unsigned value = 1500) {
    Bytes b(32, 0); b[0] = 0x20; b[1] = 0x40;
    for (unsigned i = 0; i < 14; ++i) {
        unsigned v = int(i) == bad ? value : 1500;
        b[2+2*i] = v; b[3+2*i] = v >> 8;
    }
    checksum(b); return b;
}
static Bytes poll(uint8_t command) {
    Bytes b{4, command, 0, 0}; checksum(b); return b;
}
static void feed(IbusInput& p, const Bytes& b, uint32_t t) { p.feed(b.data(), b.size(), t); }
static void feed(IbusTelemetry& p, const Bytes& b, uint32_t t) {
    for (auto c : b) p.feed(c, t);
}
struct Port : r2link::BytePort {
    Bytes tx;
    size_t capacity = 100, per_call = 100, calls = 0, reads = 0;
    std::deque<uint8_t> rx;
    int read() override { ++reads; if (rx.empty()) return -1; int c=rx.front(); rx.pop_front(); return c; }
    size_t writable() const override { return capacity; }
    size_t write(const uint8_t* b, size_t n) override {
        ++calls; assert(n <= capacity);
        size_t take = n < per_call ? n : per_call;
        tx.insert(tx.end(), b, b+take); capacity -= take; return take;
    }
};
static SensorSnapshot sensors(int32_t v=1280, int32_t t=250) {
    SensorSnapshot s{}; s.voltage_cV=v; s.hottest_mosfet_dC=t;
    s.voltage_valid=true; s.temperature_valid=true; return s;
}
'''


class BodyRadioTests(unittest.TestCase):
    def check(self, body):
        result = run_cpp(PRELUDE + "\nint main() {\n" + body + "\n}\n",
                         extra_sources=SOURCES, include_dirs=[BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_all_fourteen_fields_preserve_snapshot_and_time(self):
        self.check(r'''
    for (int field = 0; field < 14; ++field) for (unsigned bad : {0u, 899u, 2101u, 65535u}) {
        IbusInput p; assert(!p.snapshot(0).valid);
        feed(p, frame(0, 900), 10);
        RcSnapshot old = p.snapshot(10);
        assert(old.valid && old.sample_counter == 1 && old.sample_ms == 10);
        assert(old.channels[0] == 1000);
        feed(p, frame(field, bad), 100);
        RcSnapshot now = p.snapshot(100);
        assert(now.valid && now.sample_ms == old.sample_ms && now.sample_counter == old.sample_counter);
        assert(!memcmp(now.channels, old.channels, sizeof old.channels));
        assert(p.counters().valid_frames == 1 && p.counters().channel_errors == 1);
        assert(p.snapshot(260).valid);
        assert(!p.snapshot(261).valid);
    }
    IbusInput p; feed(p, frame(9, 2100), 10);
    assert(p.snapshot(10).channels[9] == 2000);
    feed(p, frame(13, 900), 11); assert(p.snapshot(11).sample_counter == 2);
    feed(p, frame(13, 2100), 12); assert(p.snapshot(12).sample_counter == 3);
''')

    def test_checksum_headers_and_flags_use_only_ch6_and_ch9(self):
        self.check(r'''
    IbusInput p;
    Bytes b = frame(5, 1750); b[18]=0xd6; b[19]=0x06; checksum(b); // CH9=1750
    feed(p, b, 0); auto s = p.snapshot(0);
    assert(s.valid && s.flags == 0x0b); // no qualification/manual/holo bit
    assert(s.channels[kFeetEnable] == 1750 && s.channels[kAutoDome] == 1750);
    for (int ch : {2,9}) { b=frame(ch,2000); feed(p,b,1); assert(p.snapshot(1).flags == 1); }
    b=frame(5,1749); feed(p,b,2); assert(p.snapshot(2).flags == 1);
    b=frame(8,1749); feed(p,b,3); assert(p.snapshot(3).flags == 1);
    b=frame(); b[31]^=1; feed(p,b,4);
    assert(p.counters().checksum_errors == 1 && p.snapshot(4).sample_ms == 3);
    b=frame(); b[1]=0x41; checksum(b); feed(p,b,5);
    assert(p.snapshot(5).sample_ms == 3 && p.counters().header_errors > 0);
    assert(p.snapshot(254).flags == 0);
''')

    def test_overlap_partial_expiry_and_rollover(self):
        self.check(r'''
    IbusInput p; auto b=frame();
    p.feed(0x20,0); feed(p,b,0); assert(p.snapshot(0).sample_counter==1);
    // A truncated candidate contains the next full header. Recover its suffix.
    Bytes prefix{0x20,0x40,1,2,3,4,5};
    feed(p,prefix,1); feed(p,b,1); assert(p.snapshot(1).sample_counter==2);
    p.feed(b.data(),7,2); p.tick(7);
    assert(p.counters().partial_timeouts==1);
    p.feed(b.data()+7,25,7); assert(p.snapshot(7).sample_counter==2);
    feed(p,b,8); assert(p.snapshot(8).sample_counter==3);
    IbusInput q; feed(q,b,UINT32_MAX-100);
    assert(q.snapshot(149).valid && !q.snapshot(150).valid);
    q.feed(b.data(),10,UINT32_MAX-2); q.tick(2);
    assert(q.counters().partial_timeouts==1);
    IbusInput r; r.feed(b.data(),10,1); r.feed(b.data()+10,22,6);
    assert(!r.snapshot(6).valid && r.counters().partial_timeouts==1);
''')

    def test_golden_sensors_and_scheduler_boundaries(self):
        self.check(r'''
    const Bytes expected[] = {
        {4,0x81,0x7a,0xff}, {6,0x91,3,2,0x63,0xff}, {6,0xa1,0,5,0x53,0xff},
        {4,0x82,0x79,0xff}, {6,0x92,1,2,0x64,0xff}, {6,0xa2,0x8a,2,0xcb,0xfe}
    };
    const uint8_t commands[]={0x81,0x91,0xa1,0x82,0x92,0xa2};
    for (int i=0;i<6;++i) {
        IbusTelemetry t; Port p; t.setMeasurements(sensors()); feed(t,poll(commands[i]),1000);
        t.tick(1099,p); assert(p.tx.empty());
        t.tick(1100,p); assert(p.tx==expected[i]);
        assert(t.counters().responses==1);
    }
    for (uint32_t elapsed : {100u,999u,1000u,1001u}) {
        IbusTelemetry t; Port p; feed(t,poll(0x81),100);
        t.tick(100+elapsed,p);
        assert((!p.tx.empty()) == (elapsed<=1000));
        assert(t.counters().deadline_misses==(elapsed>1000));
    }
    IbusTelemetry t; Port p; feed(t,poll(0x81),UINT32_MAX-50);
    t.tick(48,p); assert(p.tx.empty()); t.tick(49,p); assert(p.tx==expected[0]);
''')

    def test_bad_measurements_never_overflow_and_validity_independent(self):
        self.check(r'''
    for (int32_t v : {-1,65536,INT32_MIN,INT32_MAX}) {
        IbusTelemetry t; Port p; t.setMeasurements(sensors(v,250));
        feed(t,poll(0xa1),0); t.tick(100,p); assert(p.tx.empty());
        feed(t,poll(0xa2),2000); t.tick(2100,p); assert(p.tx.size()==6);
    }
    for (int32_t v : {-401,65136,INT32_MIN,INT32_MAX}) {
        IbusTelemetry t; Port p; t.setMeasurements(sensors(1280,v));
        feed(t,poll(0xa2),0); t.tick(100,p); assert(p.tx.empty());
    }
    for (int32_t v : {0,65535}) {
        IbusTelemetry t; Port p; t.setMeasurements(sensors(v,0));
        feed(t,poll(0xa1),0); t.tick(100,p);
        assert(p.tx.size()==6 && p.tx[2]==uint8_t(v) && p.tx[3]==uint8_t(v>>8));
    }
    for (int32_t v : {-400,65135}) {
        IbusTelemetry t; Port p; t.setMeasurements(sensors(0,v));
        feed(t,poll(0xa2),0); t.tick(100,p); unsigned encoded=unsigned(v+400);
        assert(p.tx.size()==6 && p.tx[2]==uint8_t(encoded) && p.tx[3]==uint8_t(encoded>>8));
    }
    IbusTelemetry t; Port p; auto s=sensors(); t.setMeasurements(s);
    feed(t,poll(0xa1),0); s.voltage_valid=false; t.setMeasurements(s);
    t.tick(100,p); assert(p.tx.empty()); // invalidated while waiting
    s.temperature_valid=false; t.setMeasurements(s);
    feed(t,poll(0xa2),2000); t.tick(2100,p); assert(p.tx.empty());
    feed(t,poll(0x91),4000); t.tick(4100,p); assert(p.tx.size()==6);
    p.tx.clear(); feed(t,poll(0x82),6000); t.tick(6100,p); assert(p.tx.size()==4);
    IbusTelemetry empty; Port no_values;
    feed(empty,poll(0xa1),0); empty.tick(100,no_values); assert(no_values.tx.empty());
''')

    def test_all_or_none_capacity_deadline_and_partial_writes_no_mix(self):
        self.check(r'''
    IbusTelemetry t; Port p; p.capacity=5; t.setMeasurements(sensors());
    feed(t,poll(0xa1),0); t.tick(100,p); assert(p.tx.empty() && p.calls==0);
    t.tick(1000,p); assert(p.tx.empty());
    p.capacity=6; t.tick(1001,p); assert(p.tx.empty() && t.counters().deadline_misses==1);
    feed(t,poll(0xa1),2000); p.capacity=6; p.per_call=2;
    t.tick(2100,p); assert(p.tx==Bytes({6,0xa1}) && p.calls==1);
    feed(t,poll(0xa2),2101); // cannot replace/interleave an active response
    t.tick(2200,p); t.tick(2300,p);
    assert(p.tx==Bytes({6,0xa1,0,5,0x53,0xff}));
    assert(t.counters().partial_writes==2 && t.counters().busy_polls==1);
    feed(t,poll(0xa2),4000); p.capacity=6; p.per_call=100; t.tick(4100,p);
    assert(p.tx==Bytes({6,0xa1,0,5,0x53,0xff,6,0xa2,0x8a,2,0xcb,0xfe}));
    // A stalled started frame retains its suffix; no later reply may mix in.
    IbusTelemetry u; Port q; q.per_call=2; feed(u,poll(0x91),0); u.tick(100,q);
    q.capacity=0; u.tick(1001,q); u.tick(2000,q);
    assert(u.counters().deadline_misses==1 && q.tx.size()==2);
    feed(u,poll(0x82),2100); q.capacity=6; q.per_call=100; u.tick(2200,q);
    assert(q.tx==Bytes({6,0x91,3,2,0x63,0xff}) && u.counters().busy_polls==1);
''')

    def test_checksum_unknown_poll_echo_and_response_echo(self):
        self.check(r'''
    IbusTelemetry t; Port p; t.setMeasurements(sensors());
    auto bad=poll(0x81); bad[3]^=1; feed(t,bad,0); t.tick(100,p);
    assert(p.tx.empty() && t.counters().checksum_errors==1);
    for (uint8_t c : {0x80,0x83,0x93,0xa3,0xb1,0xf2}) feed(t,poll(c),2000);
    t.tick(2100,p); assert(p.tx.empty() && t.counters().unsupported_polls==6);
    feed(t,poll(0x81),4000); feed(t,poll(0x81),4001); // duplicate poll while pending
    t.tick(4100,p); assert(p.tx==Bytes({4,0x81,0x7a,0xff}));
    feed(t,p.tx,4200); t.tick(4300,p);
    assert(p.tx.size()==4 && t.counters().echo_bytes==4);
    feed(t,poll(0x91),6000); t.tick(6100,p); Bytes reply(p.tx.begin()+4,p.tx.end());
    feed(t,reply,6200); t.tick(6300,p); assert(p.tx.size()==10 && t.counters().echo_bytes==10);
    // Unsolicited length-6 responses are never treated as polls.
    feed(t,reply,8000); t.tick(8100,p); assert(p.tx.size()==10);
    assert(t.counters().ignored_responses==1);
    // No echo observed: expire the guard and accept the next identical poll.
    feed(t,poll(0x81),10000); t.tick(10100,p); assert(p.tx.size()==14);
    feed(t,poll(0x81),12000); t.tick(12100,p); assert(p.tx.size()==18);
''')

    def test_telemetry_overlap_partial_expiry_and_loopback_each_partial_write(self):
        self.check(r'''
    IbusTelemetry t; Port p;
    t.feed(4,0); feed(t,poll(0x81),0); t.tick(100,p); assert(p.tx.size()==4);
    feed(t,Bytes{6,0x91},2000); feed(t,poll(0x82),2001);
    t.tick(2101,p); assert(p.tx.size()==8); // recover embedded valid poll after bad candidate
    auto b=poll(0x91); feed(t,Bytes(b.begin(),b.begin()+2),4000);
    t.tick(9000,p); feed(t,Bytes(b.begin()+2,b.end()),9000);
    t.tick(9100,p); assert(p.tx.size()==8 && t.counters().partial_timeouts==1);
    IbusTelemetry u; Port q; q.per_call=2; feed(u,poll(0x91),UINT32_MAX-50);
    u.tick(49,q); feed(u,q.tx,50);
    u.tick(60,q); feed(u,Bytes(q.tx.begin()+2,q.tx.end()),61);
    u.tick(70,q); feed(u,Bytes(q.tx.begin()+4,q.tx.end()),71); u.tick(171,q);
    assert(q.tx==Bytes({6,0x91,3,2,0x63,0xff}) && u.counters().echo_bytes==6);
''')

    def test_echo_shared_prefix_does_not_eat_real_poll_and_recovery_keeps_suffix(self):
        self.check(r'''
    IbusTelemetry t; Port p;
    feed(t,poll(0x81),0); t.tick(100,p);
    // No local echo arrived: a different receiver poll shares its first byte.
    feed(t,poll(0x82),200); t.tick(300,p);
    assert(p.tx==Bytes({4,0x81,0x7a,0xff,4,0x82,0x79,0xff}));
    // The next poll shares length AND command; a bad checksum isn't echo.
    auto bad=poll(0x82); bad[3]^=1; feed(t,bad,400); t.tick(500,p);
    assert(t.counters().checksum_errors==1 && p.tx.size()==8);
    IbusTelemetry u; Port q;
    u.feed(6,0); feed(u,poll(0x81),0);
    // Completing a false 6-byte candidate leaves a whole 4-byte poll
    // followed by the first byte of a second poll in the recovery buffer.
    feed(u,poll(0x82),0); u.tick(100,q);
    assert(q.tx==Bytes({4,0x81,0x7a,0xff}));
    assert(u.counters().valid_polls==2 && u.counters().busy_polls==1);
''')

    def test_hardware_startup_rx_only_bounded_pump_and_write_clipping(self):
        program = r'''
#include "body/HardwareAdapters.h"
#include <cassert>
HardwareSerialIMXRT Serial5, Serial6;
int main() {
    body::ReceiverPort rc(Serial5); body::TelemetryPort sensor(Serial6);
    rc.begin(); sensor.begin();
    assert(Serial5.rx_pin==21 && Serial5.tx_pin==-1 && Serial5.baud==115200);
    assert(Serial6.format==SERIAL_8N1_HALF_DUPLEX && Serial6.tx_pin==24);
    assert(Serial6.open_drain && Serial6.open_drain_after_begin && Serial6.rx_pin==-1);
    uint8_t bytes[10]{}; Serial6.space=3;
    assert(sensor.write(bytes,10)==3 && Serial6.written==3);
    Serial6.space=0; assert(sensor.write(bytes,10)==0 && Serial6.calls==1);
    assert(rc.write(bytes,10)==0 && rc.writable()==0);
    body::IbusInput input;
    Serial5.bytes.assign(1000,0x55); rc.pump(input,10);
    assert(Serial5.bytes.size()==936 && Serial5.reads==64);
    body::IbusTelemetry telemetry;
    Serial6.bytes.assign(1000,0x55); sensor.pump(telemetry,100);
    assert(Serial6.bytes.size()==984 && Serial6.reads==16);
}
'''
        result = run_cpp(program, extra_sources=SOURCES,
                         include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_main_starts_radio_only_without_waiting_or_actuators(self):
        program = PRELUDE + r'''
#include "main.cpp"
HardwareSerial Serial;
HardwareSerialIMXRT Serial5, Serial6;
uint32_t fake_ms=0, fake_us=0;
int main() {
    setup();
    assert(fake_ms==0 && Serial5.baud==115200 && Serial6.baud==115200);
    assert(Serial6.open_drain_after_begin && Serial5.tx_pin==-1 && Serial6.rx_pin==-1);
    assert(!g_link.connected(0));
    auto b=frame(5,1000); Serial5.bytes.insert(Serial5.bytes.end(),b.begin(),b.end());
    auto request=poll(0x81); Serial6.bytes.insert(Serial6.bytes.end(),request.begin(),request.end());
    loop(); assert(g_input.snapshot(0).valid && Serial6.written==0);
    fake_us=100; loop(); assert(Serial6.written==4);
    assert(g_telemetry.counters().responses==1);
    Serial.space=0; fake_ms=5000; fake_us=5000000; loop();
    assert(Serial.written==0 && !g_input.snapshot(fake_ms).valid);
    Serial.space=512; fake_ms=10000; fake_us=10000000; loop();
    assert(Serial.written>0 && Serial.written<512);
}
'''
        result = run_cpp(program,
                         extra_sources=SOURCES + [
                             BODY / "body/LinkBootstrap.cpp",
                             SHARED / "src/Endpoint.cpp", SHARED / "src/Codec.cpp"],
                         include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
