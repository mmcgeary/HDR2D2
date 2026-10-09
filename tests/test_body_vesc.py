"""Synthetic protocol fixtures only: none originate from installed hardware."""
import unittest
from pathlib import Path

from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
SHARED = ROOT / "shared/R2BodyLink"
SOURCES = [BODY / "body/VescLink.cpp", BODY / "body/ConfigStore.cpp", SHARED / "src/Codec.cpp"]
PRELUDE = r'''
#include "body/VescLink.h"
#include <cassert>
#include <cstring>
#include <climits>
#include <vector>
#include <deque>
using namespace body;
using Bytes = std::vector<uint8_t>;
struct Port : r2link::BytePort {
    Bytes tx; std::deque<uint8_t> rx;
    size_t capacity=100, per_call=100, calls=0;
    int read() override { if(rx.empty()) return -1; int b=rx.front();rx.pop_front();return b; }
    size_t writable() const override { return capacity; }
    size_t write(const uint8_t* b,size_t n) override {
        assert(n<=capacity); ++calls; size_t k=n<per_call?n:per_call;
        tx.insert(tx.end(),b,b+k); return k;
    }
};
// Independent reference CRC and byte packing, not calls to the production codec.
static uint16_t crc(const Bytes& b) {
    uint16_t c=0; for(auto x:b) { c ^= uint16_t(x)<<8;
        for(int i=0;i<8;++i) c=(c&0x8000)?uint16_t((c<<1)^0x1021):uint16_t(c<<1); }
    return c;
}
static Bytes wire(Bytes p,bool lng=false) {
    Bytes b{uint8_t(lng?3:2)};
    if(lng) b.push_back(p.size()>>8);
    b.push_back(p.size()); b.insert(b.end(),p.begin(),p.end());
    uint16_t c=crc(p); b.push_back(c>>8);b.push_back(c);b.push_back(3);return b;
}
static void put(Bytes& p,size_t at,uint32_t v,size_t n) {
    for(size_t i=0;i<n;++i) p[at+i]=v>>((n-i-1)*8);
}
static Bytes values() {
    Bytes p(54,0);p[0]=4;
    put(p,1,static_cast<uint16_t>(-125),2);put(p,3,999,2);
    put(p,5,static_cast<uint32_t>(-12345),4);put(p,9,234,4);
    put(p,13,0x41424344,4);put(p,17,0x51525354,4); // ignored id/iq
    put(p,21,static_cast<uint16_t>(-350),2);
    put(p,23,static_cast<uint32_t>(-98765),4);put(p,27,128,2);
    p[53]=0;return p;
}
static CommissioningProfile saved() {
    CommissioningProfile p;
    for(int w=0;w<2;++w) {
        const int v[]={1,42,19,1,1000,1000,0,1500,1050,1500,150,1500,100,50};
        for(int id=5;id<=18;++id) assert(setField(p,id,w,v[id-5])==FieldResult::Ok);
        p.acceptance |= (1u<<(kAcceptVescConfig+w))|(1u<<(kAcceptTimeoutBrake+w))|
                        (1u<<(kAcceptDirection+w))|(1u<<(kAcceptReversal+w));
    }
    assert(validateProfile(p));return p; // Test-only injected acceptance, not a commissioning observation.
}
static void inject(Port& p,const Bytes& b) { p.rx.insert(p.rx.end(),b.begin(),b.end()); }
static void feed(Port& p,VescLink& l,const Bytes& b,uint32_t t) {
    inject(p,b); l.tick(t); while(!p.rx.empty()) l.tick(t);
}
static void ready(Port& p,VescLink& l,uint32_t t=0) {
    l.setProfile(VescProfile::fromSaved(saved(),0));l.tick(t);
    feed(p,l,wire({0,42,19}),t+1); l.tick(t+100);
    feed(p,l,wire(values()),t+101);assert(l.sample(t+101).valid);p.tx.clear();
}
'''


class VescTests(unittest.TestCase):
    def check(self, code):
        result = run_cpp(PRELUDE + "\nint main(){\n" + code + "\n}\n",
                         extra_sources=SOURCES, include_dirs=[BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_encoders_exact_signed_big_endian_and_bounds(self):
        self.check(r'''
    uint8_t packet[32]{};
    const size_t count = body::VescCodec::encodeBrake(1500, packet, sizeof(packet));
    assert(count == 10);
    assert(packet[0] == 2 && packet[1] == 5 && packet[2] == 7);
    assert(packet[3] == 0 && packet[4] == 0);
    assert(packet[5] == 0x05 && packet[6] == 0xdc);
    assert(packet[count - 1] == 3);
    auto expected=wire({7,0,0,5,0xdc});assert(!memcmp(packet,expected.data(),10));
    assert(VescCodec::encodeDuty(-350,packet,sizeof packet)==10);
    expected=wire({5,0xff,0xff,0x77,0x48});assert(!memcmp(packet,expected.data(),10));
    assert(!VescCodec::encodeDuty(1001,packet,sizeof packet));
    assert(!VescCodec::encodeDuty(-1001,packet,sizeof packet));
    assert(!VescCodec::encodeDuty(10,packet,9));
    assert(!VescCodec::encodeBrake(UINT32_MAX,packet,sizeof packet));
    assert(!VescCodec::encodeBrake(0,packet,sizeof packet));
    assert(!VescCodec::encodeBrake(1,nullptr,32));
''')

    def test_decoder_offsets_signed_units_acceptance_and_truncation(self):
        self.check(r'''
    auto p=VescProfile::fromSaved(saved(),0);auto b=values();VescSample s{};
    assert(VescCodec::decodeValues(b.data(),b.size(),p,s));
    assert(s.pack_cV==1280 && s.motor_mA==-123450 && s.input_mA==2340);
    assert(s.erpm==-98765 && s.mosfet_dC==-125 && s.duty_permille==-350);
    assert(s.valid_fields==0xdf && !s.motor_temperature_valid);
    for(size_t n=0;n<54;++n) {
        VescSample sentinel=s;assert(!VescCodec::decodeValues(b.data(),n,p,s));
        assert(s.motor_mA==sentinel.motor_mA && s.pack_cV==sentinel.pack_cV);
    }
    auto cfg=saved();cfg.acceptance=0;
    assert(!VescCodec::decodeValues(b.data(),54,VescProfile::fromSaved(cfg,0),s));
    cfg=saved();cfg.wheel[0].layout=kLayoutUnknown;
    assert(!VescCodec::decodeValues(b.data(),54,VescProfile::fromSaved(cfg,0),s));
    assert(!VescCodec::decodeValues(b.data(),54,VescProfile::fromSaved(saved(),2),s));
    put(b,5,0x7fffffff,4);assert(!VescCodec::decodeValues(b.data(),54,p,s));
    b=values();put(b,27,0xffff,2);assert(!VescCodec::decodeValues(b.data(),54,p,s));
    b=values();put(b,21,1001,2);assert(!VescCodec::decodeValues(b.data(),54,p,s));
    b=values();put(b,5,static_cast<uint32_t>(-214748364),4);
    assert(VescCodec::decodeValues(b.data(),54,p,s) && s.motor_mA==-2147483640);
''')

    def test_startup_fw_first_poll_timeout_no_motion_unaccepted(self):
        self.check(r'''
    Port p;VescLink l(p,0);l.setDuty(100);l.setBrake(1500);l.tick(0);
    assert(p.tx==wire({0}));p.tx.clear();l.tick(99);l.tick(100);assert(p.tx.empty());
    l.tick(150);assert(l.counters().query_timeouts==1 && p.tx==wire({0}));
    p.tx.clear();feed(p,l,wire({0,42,19}),151);l.tick(250);
    assert(p.tx==wire({4}));feed(p,l,wire(values()),251);
    assert(!l.sample(251).valid && l.sample(251).unsupported);
    l.setDuty(100);l.setBrake(1500);p.tx.clear();l.tick(252);assert(p.tx.empty());
''')

    def test_independent_freshness_fault_unsupported_profile_and_reset(self):
        self.check(r'''
    Port a,b;VescLink left(a,0),right(b,1);ready(a,left);ready(b,right);
    auto s=left.sample(601);assert(s.valid && s.source_age_ms==500);
    s=left.sample(602);assert(!s.valid && s.stale && !s.unsupported && !s.fault);
    left.tick(300);feed(a,left,wire(values()),301);
    assert(left.sample(650).valid && !right.sample(650).valid);
    assert(right.sample(100000).source_age_ms==65535);
    auto bad=values();bad[53]=9;left.tick(400);feed(a,left,wire(bad),401);
    s=left.sample(401);assert(s.valid && s.fault==9 && !s.stale && !s.unsupported);
    a.tx.clear();left.setDuty(100);left.tick(402);assert(a.tx.empty());
    left.setBrake(1500);left.tick(403);assert(a.tx==wire({7,0,0,5,0xdc}));
    feed(a,left,wire({0,42,20}),404);
    s=left.sample(404);assert(!s.valid && s.unsupported && !s.profile_match && s.sample_ms==0);
    auto cfg=saved();cfg.wheel[0].fw_minor=20;
    left.setProfile(VescProfile::fromSaved(cfg,0));
    assert(!left.sample(405).valid); // profile change resets firmware observation too
    feed(a,left,wire({0,42,20}),406);left.tick(506);feed(a,left,wire(values()),507);
    assert(left.sample(507).valid);
    cfg.acceptance=0;left.setProfile(VescProfile::fromSaved(cfg,0));
    assert(!left.sample(508).valid && left.sample(508).sample_ms==0);
''')

    def test_crc_truncation_invalid_fields_never_freshen(self):
        self.check(r'''
    Port p;VescLink l(p,0);ready(p,l);auto old=l.sample(101);
    l.tick(200);auto b=wire(values());b[b.size()-2]^=1;feed(p,l,b,201);
    assert(l.counters().crc_errors>0 && l.sample(201).sample_ms==old.sample_ms);
    auto v=values();put(v,21,1001,2);feed(p,l,wire(v),202);
    assert(l.counters().invalid_values==1 && l.sample(202).sample_ms==101);
    feed(p,l,wire(Bytes{4,0,0}),203);assert(l.sample(203).sample_ms==101);
    b=wire(values());b.back()=0;feed(p,l,b,204);
    assert(l.counters().terminator_errors>0 && l.sample(204).sample_ms==101);
    assert(!l.sample(602).valid);
''')

    def test_short_long_256_oversize_overlap_absolute_expiry_and_rollover(self):
        self.check(r'''
    Port p;VescLink l(p,0);ready(p,l);l.tick(200);
    auto v=values();v.resize(256,0);feed(p,l,wire(v,true),201);
    assert(l.sample(201).sample_ms==201);
    auto old=l.sample(201);feed(p,l,Bytes{3,1,1},202);
    assert(l.counters().oversized_frames>0);
    feed(p,l,wire({99}),203);assert(l.counters().unsupported_packets>0);
    l.tick(300);
    // Bad outer candidate masks a full FW frame until absolute expiry.
    feed(p,l,Bytes{2,100,0x55},301);feed(p,l,wire({0,42,19},true),320);
    l.tick(351);assert(l.counters().frame_timeouts>0);
    feed(p,l,wire(values(),true),352);assert(l.sample(352).sample_ms==352);
    l.tick(400);auto f=wire(values());feed(p,l,Bytes(f.begin(),f.begin()+10),401);
    for(int t=410;t<451;t+=10) feed(p,l,Bytes{0},t);
    l.tick(451);assert(l.sample(451).sample_ms==352); // trickle cannot extend deadline
    feed(p,l,wire(values()),452);assert(l.sample(452).sample_ms==452);
    Port q;VescLink r(q,0);ready(q,r,UINT32_MAX-200);
    assert(r.sample(400).valid && !r.sample(401).valid);
''')

    def test_control_priority_coalescing_partial_writes_and_brake_supersession(self):
        self.check(r'''
    Port p;VescLink l(p,0);ready(p,l);
    p.capacity=0;l.setDuty(100);l.tick(200);l.setDuty(200);l.setBrake(1500);
    p.capacity=100;l.tick(201);assert(p.tx==wire({7,0,0,5,0xdc}));
    p.tx.clear();l.setDuty(100);l.setDuty(-350);l.tick(202);
    assert(p.tx==wire({5,0xff,0xff,0x77,0x48}));
    p.tx.clear();p.per_call=2;l.setDuty(300);l.tick(203);assert(p.tx.size()==2);
    // A partially emitted old duty must be completed as an INVALID frame,
    // then latest brake; never complete its old nonzero actuator packet.
    l.setBrake(1500);
    for(int t=204;t<230;++t) l.tick(t);
    assert(p.tx.size()>=20);Bytes first(p.tx.begin(),p.tx.begin()+10);
    assert(first!=wire({5,0,0,0x75,0x30}) && first.back()==3);
    Bytes brake=wire({7,0,0,5,0xdc});
    assert(!memcmp(p.tx.data()+10,brake.data(),10));
    // Partial query is never interleaved with an actuator.
    Port q;VescLink r(q,0);ready(q,r);q.per_call=1;r.tick(200);r.setBrake(1500);
    for(int t=201;t<230;++t) r.tick(t);
    Bytes query=wire({4});assert(!memcmp(q.tx.data(),query.data(),6));
    assert(!memcmp(q.tx.data()+6,brake.data(),10));
''')

    def test_overlap_recovers_after_bad_crc_and_preserves_candidate_deadline(self):
        self.check(r'''
    Port p;VescLink l(p,0);ready(p,l);l.tick(200);
    // The damaged packet's payload includes a plausible nested long header.
    Bytes bad(20,0x55);bad[0]=99;bad[1]=3;bad[2]=0;bad[3]=200;
    auto corrupt=wire(bad);corrupt[corrupt.size()-2]^=1;
    auto good=wire(values());corrupt.insert(corrupt.end(),good.begin(),good.end());
    feed(p,l,corrupt,201);
    assert(l.sample(201).sample_ms==201 && l.counters().crc_errors>0);
    // Expiry recovery must keep original arrival times of retained headers.
    l.tick(300);feed(p,l,Bytes{2,200,0x55},301);
    feed(p,l,Bytes(good.begin(),good.begin()+10),320);
    l.tick(351);feed(p,l,Bytes(good.begin()+10,good.end()),370);
    assert(l.sample(370).sample_ms==201); // inner candidate also reached 50ms
    feed(p,l,good,371);assert(l.sample(371).sample_ms==371);
''')

    def test_all_partial_offsets_brake_cancels_old_duty_and_profile_gates(self):
        self.check(r'''
    for(size_t offset=1;offset<10;++offset) {
        Port p;VescLink l(p,0);ready(p,l);p.per_call=offset;
        l.setDuty(-350);l.tick(102);assert(p.tx.size()==offset);
        l.setBrake(1500);p.per_call=100;l.tick(103);l.tick(104);
        assert(p.tx.size()==20);
        Bytes first(p.tx.begin(),p.tx.begin()+10);
        assert(first!=wire({5,0xff,0xff,0x77,0x48}));
        assert(first.back()!=3 || crc(Bytes(first.begin()+2,first.begin()+7))!=
            ((uint16_t(first[7])<<8)|first[8]));
        auto brake=wire({7,0,0,5,0xdc});assert(!memcmp(p.tx.data()+10,brake.data(),10));
    }
    Port p;VescLink l(p,0);auto cfg=saved();
    cfg.acceptance &= ~(1u<<kAcceptTimeoutBrake);
    l.setProfile(VescProfile::fromSaved(cfg,0));l.tick(0);
    feed(p,l,wire({0,42,19}),1);l.tick(100);feed(p,l,wire(values()),101);
    assert(l.sample(101).valid); // telemetry acceptance != actuator acceptance
    p.tx.clear();l.setBrake(1500);l.tick(102);assert(p.tx.empty());
    cfg=saved();l.setProfile(VescProfile::fromSaved(cfg,0));
    assert(l.sample(103).sample_ms==0 && !l.sample(103).valid);
''')

    def test_repeated_identical_live_demand_completes_partial_frame(self):
        self.check(r'''
    for(bool brake : {false,true}) {
        Port p;VescLink l(p,0);ready(p,l);p.per_call=1;
        for(int t=102;t<112;++t) {
            if(brake) l.setBrake(1500);else l.setDuty(-350);
            l.tick(t);
        }
        auto expected=brake?wire({7,0,0,5,0xdc}):wire({5,0xff,0xff,0x77,0x48});
        assert(p.tx==expected && l.counters().aborted_commands==0);
    }
''')

    def test_brake_is_not_cancelled_by_later_duty_before_completion(self):
        self.check(r'''
    Port p;VescLink l(p,0);ready(p,l);
    l.setBrake(1500);l.setDuty(350);l.tick(102);
    auto brake=wire({7,0,0,5,0xdc});assert(p.tx==brake);
    p.tx.clear();p.per_call=1;l.setBrake(1500);l.tick(103);
    for(int t=104;t<113;++t) { l.setDuty(350);l.tick(t); }
    assert(p.tx==brake);
    p.tx.clear();p.per_call=100;l.setDuty(350);l.tick(113);
    assert(p.tx==wire({5,0,0,0x88,0xb8})); // new request after brake completion
''')

    def test_one_raw_capture_works_without_approved_profile(self):
        self.check(r'''
    Port p;VescLink l(p,1);l.requestCapture();l.tick(0);
    auto b=wire({0,42,19});feed(p,l,b,1);
    VescCapture c{};assert(l.takeCapture(c));
    assert(c.wheel==1 && c.length==b.size() && !memcmp(c.bytes,b.data(),c.length));
    assert(!l.takeCapture(c));feed(p,l,wire(values()),2);assert(!l.takeCapture(c));
    l.requestCapture();b=wire(values(),true);feed(p,l,b,3);
    assert(l.takeCapture(c) && c.bytes[0]==3 && c.length==b.size());
    l.requestCapture(4);feed(p,l,wire({0,42,19}),4);assert(!l.takeCapture(c));
    feed(p,l,wire(values()),5);assert(l.takeCapture(c) && c.bytes[2]==4);
''')

    def test_paired_sensor_validity_and_stalled_query_cannot_renew_sample(self):
        self.check(r'''
    Port a,b;VescLink left(a,0),right(b,1);ready(a,left);ready(b,right);
    auto s=vescMeasurements(left.sample(101),right.sample(101));
    assert(s.voltage_valid && s.temperature_valid && s.voltage_cV==1280);
    auto ls=left.sample(101),rs=right.sample(101);
    ls.pack_cV=1200;rs.mosfet_dC=320;
    s=vescMeasurements(ls,rs);assert(s.voltage_cV==1200 && s.hottest_mosfet_dC==320);
    rs.valid_fields &= ~0x10; s=vescMeasurements(ls,rs);
    assert(s.voltage_valid && !s.temperature_valid);
    rs=right.sample(602);s=vescMeasurements(ls,rs);
    assert(!s.voltage_valid && !s.temperature_valid);
    left.tick(200);inject(a,wire(values()));left.tick(350);
    assert(left.counters().query_timeouts==1 && left.sample(350).sample_ms==101);
    // Pending nonzero demand expires, even if no UART room until later.
    a.tx.clear();a.capacity=0;left.setDuty(300);left.tick(351);
    a.capacity=100;left.tick(380);
    assert(a.tx.empty()); // no delayed command, still one outstanding query
''')

    def test_values_reply_cannot_freshen_before_new_query_is_fully_written(self):
        self.check(r'''
    Port p;VescLink l(p,0);ready(p,l);
    p.capacity=0;l.tick(200);feed(p,l,wire(values()),201);
    assert(l.sample(201).sample_ms==101);
    p.capacity=100;l.tick(202);feed(p,l,wire(values()),203);
    assert(l.sample(203).sample_ms==203);
''')

    def test_main_target_serial_pins_and_capture_diagnostic_only(self):
        source = (BODY / "main.cpp").read_text()
        adapter = (BODY / "body/HardwareAdapters.h").read_text()
        self.assertIn("g_left_vesc(Serial1", source)
        self.assertIn("g_right_vesc(Serial2", source)
        self.assertIn("g_left.tick(now_ms)", source)
        self.assertIn("g_right.tick(now_ms)", source)
        self.assertIn("kLeftVescRx", adapter)
        self.assertIn("kRightVescTx", adapter)
        self.assertIn(".requestCapture(", source)
        self.assertNotIn(".setDuty(", source)
        self.assertNotIn(".setBrake(", source)


if __name__ == "__main__":
    unittest.main()
