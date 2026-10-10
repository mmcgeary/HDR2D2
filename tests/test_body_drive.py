"""Task 5 synthetic fixtures; no hardware settings or acceptance are inferred."""
import unittest

from cpp_test_support import run_cpp
from test_body_vesc import BODY, SHARED, PRELUDE as VESC_PRELUDE, SOURCES as VESC_SOURCES

SOURCES = VESC_SOURCES + [BODY / "body/DriveController.cpp", BODY / "body/IbusInput.cpp"]
PRELUDE = VESC_PRELUDE + r'''
#include "body/DriveController.h"
#include "body/IbusInput.h"
struct Fixture {
    CommissioningProfile profile;
    IbusInput radio;
    DriveController drive;
    VescSample left{}, right{};
    Fixture() : profile(saved()) {
        assert(setField(profile,kFieldSlew,0,500)==FieldResult::Ok);
        for(int w=0;w<2;++w)
            assert(setField(profile,kFieldReversalDwell,w,100)==FieldResult::Ok);
        acceptInjected();
        deliverWheelSamples(0,0,0);
    }
    void acceptInjected() { profile.acceptance=0xff0; } // Synthetic external acceptance.
    void deliverWheelSamples(uint32_t t,int32_t l,int32_t r) {
        VescSample* samples[2]={&left,&right};
        const int32_t speeds[2]={l,r};
        for(int w=0;w<2;++w) {
            Bytes packet=values();put(packet,23,uint32_t(speeds[w]),4);
            *samples[w]=VescSample{};
            assert(VescCodec::decodeValues(packet.data(),packet.size(),
                VescProfile::fromSaved(profile,w),*samples[w]));
            samples[w]->valid=true;samples[w]->profile_match=true;
            samples[w]->sample_ms=t;samples[w]->wheel=w;
            samples[w]->fw_major=42;samples[w]->fw_minor=19;
        }
    }
    void rc(uint32_t t,uint16_t throttle=1500,uint16_t steer=1500,
            uint16_t enable=2000,uint16_t rate=2000,uint16_t dome=1500) {
        Bytes b(32,0);b[0]=32;b[1]=0x40;
        for(int c=0;c<14;++c) {
            uint16_t v=c==kThrottle?throttle:c==kSteering?steer:
                c==kFeetEnable?enable:c==kDutyRate?rate:c==kManualDome?dome:1500;
            b[2+2*c]=v;b[3+2*c]=v>>8;
        }
        uint16_t sum=0xffff;for(int i=0;i<30;++i)sum-=b[i];
        b[30]=sum;b[31]=sum>>8;radio.feed(b.data(),b.size(),t);
    }
    void update(uint32_t t) { drive.update(radio.snapshot(t),left,right,profile,t); }
    void tickDrive(uint32_t t,uint16_t throttle=1500,uint16_t steer=1500,
                   uint16_t enable=2000,uint16_t rate=2000) {
        rc(t,throttle,steer,enable,rate);update(t);
    }
    // Ends at t, with new RC and measured low-speed samples each 20ms.
    void armDrive(uint32_t t) {
        deliverWheelSamples(t-520,0,0);tickDrive(t-520,1500,1500,1000);
        for(uint32_t dt=500;;dt-=20) {
            deliverWheelSamples(t-dt,0,0);tickDrive(t-dt);
            if(!dt)break;
        }
        assert(drive.driveState()==r2link::DriveState::Armed);
    }
    void run(uint32_t begin,uint32_t end,uint16_t th,uint16_t st=1500) {
        for(uint32_t t=begin;t<=end;t+=20) {
            deliverWheelSamples(t,0,0);tickDrive(t,th,st);
        }
    }
    void brake() {
        assert(drive.commands().left.mode==WheelMode::Brake);
        assert(drive.commands().right.mode==WheelMode::Brake);
        assert(drive.commands().left.brake_mA==1500);
        assert(drive.commands().right.brake_mA==1500);
    }
};
'''


class DriveTests(unittest.TestCase):
    def check(self, code):
        result = run_cpp(PRELUDE + "\nint main(){\n" + code + "\n}\n",
                         extra_sources=SOURCES, include_dirs=[BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_normalized_rates_deadband_and_endpoint_clamping(self):
        self.check(r'''
auto d=mixDrive(2000,2000,350);assert(d.left==350 && d.right==0);
auto p=mixDrive(1500,2000,350);assert(p.left==350 && p.right==-350);
for(int rate:{350,700,1000}) for(int th=900;th<=2100;th+=20)
    for(int st=900;st<=2100;st+=20) {
        auto x=mixDrive(th,st,rate);
        assert(abs(x.left)<=rate && abs(x.right)<=rate);
    }
assert(mixDrive(1460,1540,1000).left==0);
assert(mixDrive(900,1500,1000).left==-1000);
Fixture f;f.armDrive(1000);f.run(1020,4000,2000);
assert(f.drive.commands().left.duty_permille==950);
f.rc(4020,2000,1500,2000,1250);f.deliverWheelSamples(4020,0,0);f.update(4020);
assert(f.drive.commands().left.duty_permille==350);
f.rc(4040,2000,1500,2000,1750);f.deliverWheelSamples(4040,0,0);f.update(4040);
assert(f.drive.commands().left.duty_permille<=700);
''')

    def test_boot_off_on_neutral_boundary_and_deflection(self):
        self.check(r'''
Fixture f;
for(uint32_t t=0;t<1000;t+=20) {
    f.deliverWheelSamples(t,0,0);f.tickDrive(t,2000);
}
f.brake();assert(f.drive.driveState()!=r2link::DriveState::Armed);
f.deliverWheelSamples(1000,0,0);f.tickDrive(1000,1500,1500,1250);
f.deliverWheelSamples(1020,0,0);f.tickDrive(1020,1500,1500,1750);
for(uint32_t t=1040;t<=1500;t+=20) {
    f.deliverWheelSamples(t,0,0);f.tickDrive(t);
}
f.brake();assert(f.drive.driveState()!=r2link::DriveState::Armed);
f.deliverWheelSamples(1520,0,0);f.tickDrive(1520);
assert(f.drive.driveState()==r2link::DriveState::Armed);
f.tickDrive(1540,1500,1500,1500);f.brake();
f.tickDrive(1560);assert(f.drive.driveState()!=r2link::DriveState::Armed);
// Valid continuing failsafe OFF does not arm.
for(uint32_t t=1580;t<2100;t+=20) {
    f.deliverWheelSamples(t,0,0);f.tickDrive(t,1500,1500,1000);
}
f.brake();
''')

    def test_freshness_boundaries_both_required_fields_faults(self):
        self.check(r'''
for(int wheel=0;wheel<2;++wheel) for(int problem=0;problem<6;++problem) {
    Fixture f;f.armDrive(1000);f.run(1020,1100,2000);
    auto& s=wheel?f.right:f.left;
    if(problem==0)s.sample_ms=599;
    if(problem==1)s.valid_fields &= ~0x40;
    if(problem==2)s.valid_fields &= ~0x08;
    if(problem==3)s.valid_fields &= ~0x01;
    if(problem==4)s.fault=1;
    if(problem==5)s.profile_match=false;
    f.tickDrive(1100,2000);f.brake();
    f.deliverWheelSamples(1120,0,0);f.tickDrive(1120);
    assert(f.drive.driveState()!=r2link::DriveState::Armed);
}
Fixture f;f.armDrive(1000);
f.left.sample_ms=500;f.right.sample_ms=500;f.tickDrive(1000);
assert(f.drive.driveState()==r2link::DriveState::Armed);
f.left.sample_ms=499;f.tickDrive(1000);f.brake();
Fixture r;r.armDrive(1000);
// RC age250 is fresh, age251 is not; no gap in control ticks.
for(uint32_t t=1020;t<=1240;t+=20) {r.deliverWheelSamples(t,0,0);r.update(t);}
r.deliverWheelSamples(1250,0,0);r.update(1250);
assert(r.drive.driveState()==r2link::DriveState::Armed);
r.update(1251);r.brake();assert(r.drive.driveState()!=r2link::DriveState::Armed);
''')

    def test_uncommissioned_invalid_profile_and_remote_inhibited(self):
        self.check(r'''
Fixture f;f.profile=CommissioningProfile();f.tickDrive(0);
assert(f.drive.commands().left.mode==WheelMode::Disabled);
assert(f.drive.commands().right.mode==WheelMode::Disabled);
auto cfg=saved();assert(setField(cfg,kFieldSlew,0,500)==FieldResult::Ok);
f.profile=cfg;f.acceptInjected();f.tickDrive(20,1500,1500,1000);f.armDrive(1000);
r2link::DriveRequest request{900,900,150,0};
assert(f.drive.submitRemote(request,1000)==r2link::Result::Inhibited);
request.lease_ms=151;assert(f.drive.submitRemote(request,1000)==r2link::Result::InvalidArgument);
f.profile.allow_remote_drive=1;f.tickDrive(1020,2000);
assert(f.drive.commands().left.mode==WheelMode::Disabled);
''')

    def test_slew_fractional_neutral_stop_and_missed_deadline(self):
        self.check(r'''
Fixture f;assert(setField(f.profile,kFieldSlew,0,1)==FieldResult::Ok);
f.acceptInjected();f.armDrive(1000);f.run(1020,1980,2000);f.brake();
f.run(2000,2020,2000);
assert(f.drive.commands().left.duty_permille==1);
f.tickDrive(2021,1500);f.brake(); // Safety/zero bypass cadence.
Fixture s;s.armDrive(1000);s.run(1020,1100,2000);
assert(s.drive.commands().left.duty_permille==50);
s.drive.stop(1101);s.brake();
s.deliverWheelSamples(1120,0,0);s.tickDrive(1120);
assert(s.drive.stopLatched());
Fixture stall;stall.armDrive(1000);stall.tickDrive(1020,2000);
stall.deliverWheelSamples(1520,0,0);stall.tickDrive(1520,2000);stall.brake();
assert(stall.drive.deadlineMisses()==1);
assert(stall.drive.driveState()!=r2link::DriveState::Armed);
''')

    def test_reversal_measured_dwell_boundary_and_neutral_history(self):
        self.check(r'''
Fixture f;f.armDrive(1000);f.run(1020,1100,2000);
assert(f.drive.reversalState(0)==ReversalState::Tracking);
f.deliverWheelSamples(1120,500,500);f.tickDrive(1120,1000);f.brake();
assert(f.drive.reversalState(0)==ReversalState::Braking);
f.deliverWheelSamples(1140,80,80);f.tickDrive(1140,1000);
assert(f.drive.reversalState(0)==ReversalState::Qualifying);
for(uint32_t t=1160;t<=1220;t+=20)f.tickDrive(t,1000);
f.deliverWheelSamples(1239,80,80);f.tickDrive(1239,1000);f.brake();
assert(f.drive.reversalState(0)==ReversalState::Qualifying);
f.deliverWheelSamples(1240,80,80);f.tickDrive(1240,1000);
assert(f.drive.reversalState(0)==ReversalState::Tracking);
assert(f.drive.intent()==r2link::DriveIntent::Reverse);
f.deliverWheelSamples(1260,80,80);f.tickDrive(1260,1000);
assert(f.drive.commands().left.duty_permille<0 && f.drive.commands().left.duty_permille>=-10);
// Sample timestamp arriving before tick time cannot underflow or qualify dwell immediately.
Fixture async_s;async_s.armDrive(1000);async_s.run(1020,1100,2000);
async_s.deliverWheelSamples(1120,500,500);async_s.tickDrive(1120,1000);async_s.brake();
async_s.deliverWheelSamples(1135,80,80);async_s.tickDrive(1140,1000);
assert(async_s.drive.commands().left.mode==WheelMode::Brake);
assert(async_s.drive.intent()==r2link::DriveIntent::Forward);
for(uint32_t t=1160;t<=1220;t+=20)async_s.tickDrive(t,1000);
async_s.deliverWheelSamples(1234,80,80);async_s.tickDrive(1234,1000);
assert(async_s.drive.commands().left.mode==WheelMode::Brake);
assert(async_s.drive.intent()==r2link::DriveIntent::Forward);
async_s.deliverWheelSamples(1235,80,80);async_s.tickDrive(1235,1000);
assert(async_s.drive.intent()==r2link::DriveIntent::Reverse);
Fixture n;n.armDrive(1000);n.run(1020,1100,2000);
n.deliverWheelSamples(1120,500,500);n.tickDrive(1120);n.brake();
n.tickDrive(1140,1000);n.brake();
assert(n.drive.intent()==r2link::DriveIntent::Forward);
''')

    def test_reversal_noise_high_speed_changed_target_and_missing_samples(self):
        self.check(r'''
Fixture f;f.armDrive(1000);f.run(1020,1100,2000);
f.deliverWheelSamples(1120,80,80);f.tickDrive(1120,1000);
f.deliverWheelSamples(1140,-80,-80);f.tickDrive(1140,1000);
for(uint32_t t=1160;t<=1200;t+=20) f.tickDrive(t,1000);
f.deliverWheelSamples(1220,-80,-80);f.tickDrive(1220,1000);f.brake();
f.deliverWheelSamples(1240,101,101);f.tickDrive(1240,1000);
f.deliverWheelSamples(1260,0,0);f.tickDrive(1260,1000);
for(uint32_t t=1280;t<=1360;t+=20)f.tickDrive(t,1000);
f.brake(); // No new measured sample at dwell completion.
f.deliverWheelSamples(1380,0,0);f.tickDrive(1380,1000);
assert(f.drive.intent()==r2link::DriveIntent::Reverse);
Fixture g;g.armDrive(1000);g.run(1020,1100,2000);
g.deliverWheelSamples(1120,500,500);g.tickDrive(1120,1000);g.brake();
g.tickDrive(1140,2000);assert(g.drive.commands().left.duty_permille<=10);
g.tickDrive(1160,1000);g.brake();
g.left.valid_fields &= ~0x08;g.tickDrive(1180,1000);g.brake();
g.deliverWheelSamples(1200,0,0);g.tickDrive(1200,1000);
assert(g.drive.driveState()!=r2link::DriveState::Armed);
''')

    def test_per_wheel_pivot_direction_and_partial_intents(self):
        self.check(r'''
Fixture f;assert(setField(f.profile,kFieldDirection,1,-1)==FieldResult::Ok);
f.acceptInjected();f.armDrive(1000);f.run(1020,1100,2000);
assert(f.drive.commands().right.duty_permille<0); // Physical motor direction.
f.deliverWheelSamples(1120,500,-500);f.tickDrive(1120,1500,2000);
assert(f.drive.commands().left.mode==WheelMode::Duty);
assert(f.drive.commands().right.mode==WheelMode::Brake);
assert(f.drive.intent()==r2link::DriveIntent::Forward); // Not raw requested pivot.
f.deliverWheelSamples(1140,80,-80);f.tickDrive(1140,1500,2000);
for(uint32_t t=1160;t<=1220;t+=20) f.tickDrive(t,1500,2000);
f.deliverWheelSamples(1240,80,-80);f.tickDrive(1240,1500,2000);
assert(f.drive.intent()==r2link::DriveIntent::Pivot);
f.deliverWheelSamples(1260,80,-80);f.tickDrive(1260,1500,2000);
assert(f.drive.commands().right.duty_permille>0);
''')

    def test_stop_explicit_release_locks_and_safe_rearming(self):
        self.check(r'''
Fixture f;f.armDrive(1000);f.run(1020,1100,2000);f.drive.stop(1101);
uint16_t epoch=f.drive.controlEpoch();
for(uint32_t t=1120;t<=1640;t+=20) {
    f.deliverWheelSamples(t,0,0);f.tickDrive(t,1500,1500,1000);
}
assert(f.drive.stopLatched());f.brake();
assert(f.drive.releaseStop(epoch-1,1640)==r2link::Result::WrongEpoch);
assert(f.drive.releaseStop(epoch,1640)==r2link::Result::Accepted);
f.armDrive(2200);
assert(f.drive.setMotionLocks(4)==r2link::Result::Accepted);f.brake();
assert(f.drive.setMotionLocks(2)==r2link::Result::InvalidArgument);
assert(f.drive.motionLocks()==4);
assert(f.drive.setMotionLocks(0)==r2link::Result::NotReady);
for(uint32_t t=2220;t<=2740;t+=20) {
    f.deliverWheelSamples(t,500,500);f.tickDrive(t,1500,1500,1000);
}
assert(f.drive.setMotionLocks(0)==r2link::Result::Accepted);
// High measured speed prevents resetting prior sign through rearming.
for(uint32_t t=2760;t<=3320;t+=20) {
    f.deliverWheelSamples(t,500,500);f.tickDrive(t);
}
assert(f.drive.driveState()!=r2link::DriveState::Armed);
''')

    def test_wrap_qualification_dwell_and_cadence(self):
        self.check(r'''
Fixture f;f.armDrive(UINT32_MAX-100);
uint32_t start=UINT32_MAX-80;
for(int i=0;i<5;++i) {
    uint32_t t=start+i*20;f.deliverWheelSamples(t,0,0);f.tickDrive(t,2000);
}
f.deliverWheelSamples(19,80,80);f.tickDrive(19,1000);f.brake();
for(uint32_t t=39;t<=99;t+=20)f.tickDrive(t,1000);
f.deliverWheelSamples(119,80,80);f.tickDrive(119,1000);
assert(f.drive.intent()==r2link::DriveIntent::Reverse);
assert(f.drive.deadlineMisses()==0);
''')

    def test_actual_main_uses_paired_output_and_disabled_boot(self):
        main_source = (BODY / "main.cpp").read_text()
        controller_source = (BODY / "body/BodyController.cpp").read_text()
        self.assertIn("g_controller.tick(", main_source)
        self.assertIn("g_controller.updateRc(", main_source)
        self.assertIn("g_profile", main_source)
        self.assertIn("g_body_status = g_controller.status()", main_source)
        self.assertIn("drive_.update(", controller_source)
        self.assertIn("applyWheelCommands(", controller_source)
        self.assertIn("drive_.intent()", controller_source)

    def test_command_revision_schedules_20ms_renewal_and_immediate_brake(self):
        self.check(r'''
Fixture f;f.armDrive(1000);auto seq=f.drive.commandRevision();
for(uint32_t t=1001;t<1020;++t) f.tickDrive(t,2000);
assert(f.drive.commandRevision()==seq);
f.tickDrive(1020,2000);assert(f.drive.commandRevision()!=seq);
seq=f.drive.commandRevision();
f.tickDrive(1021,1500);f.brake();
assert(f.drive.commandRevision()!=seq);
seq=f.drive.commandRevision();
for(uint32_t t=1022;t<1040;++t) f.tickDrive(t);
assert(f.drive.commandRevision()==seq);
f.tickDrive(1040);assert(f.drive.commandRevision()!=seq);
''')

    def test_voltage_cutoffs_and_fault_recovery_require_new_off(self):
        self.check(r'''
for(int volts:{1049,1501}) {
    Fixture f;f.armDrive(1000);f.run(1020,1100,2000);
    f.right.pack_cV=volts;f.tickDrive(1120,2000);f.brake();
    for(uint32_t t=1140;t<=1660;t+=20) {
        f.deliverWheelSamples(t,0,0);f.tickDrive(t);
    }
    assert(f.drive.driveState()!=r2link::DriveState::Armed);
    f.armDrive(2200);
}
''')

    def test_release_stop_uses_rc_age_at_acknowledgement(self):
        self.check(r'''
Fixture f;f.armDrive(1000);f.drive.stop(1000);
for(uint32_t t=1020;t<=1520;t+=20) {
    f.deliverWheelSamples(t,0,0);f.tickDrive(t,1500,1500,1000);
}
for(uint32_t t=1540;t<=1760;t+=20)f.update(t);
f.update(1770); // Latest fresh RC already age250.
assert(f.drive.releaseStop(f.drive.controlEpoch(),1771)==r2link::Result::NotReady);
assert(f.drive.stopLatched());
''')

    def test_accepted_profile_replacement_cannot_flip_powered_motor_direction(self):
        self.check(r'''
Fixture f;f.armDrive(1000);f.run(1020,1100,2000);
assert(setField(f.profile,kFieldDirection,0,-1)==FieldResult::Ok);
f.acceptInjected();f.deliverWheelSamples(1120,500,500);f.tickDrive(1120,2000);
f.brake();assert(f.drive.driveState()!=r2link::DriveState::Armed);
''')

    def test_arming_cannot_hide_gap_with_new_radio_and_telemetry(self):
        self.check(r'''
Fixture f;f.tickDrive(0,1500,1500,1000);f.tickDrive(20);
f.deliverWheelSamples(600,0,0);f.tickDrive(600);
assert(f.drive.driveState()!=r2link::DriveState::Armed);f.brake();
f.armDrive(1200);
''')

    def test_real_vesc_injection_positive_brake_stale_rx_and_disabled_cancellation(self):
        self.check(r'''
Fixture f;f.armDrive(1000);f.run(1020,1100,2000);
Port lp,rp;VescLink l(lp,0),r(rp,1);
// Hardware-independent accepted profile injected explicitly into real links.
l.setProfile(VescProfile::fromSaved(f.profile,0));
r.setProfile(VescProfile::fromSaved(f.profile,1));
l.tick(1000);r.tick(1000);
feed(lp,l,wire({0,42,19}),1001);feed(rp,r,wire({0,42,19}),1001);
l.tick(1100);r.tick(1100);
Bytes pl=values();put(pl,23,0,4);put(pl,27,128,2);
feed(lp,l,wire(pl),1101);feed(rp,r,wire(pl),1101);
assert(l.sample(1101).valid && r.sample(1101).valid);
lp.tx.clear();rp.tx.clear();
applyWheelCommands(f.drive.commands(),l,r);l.tick(1101);r.tick(1101);
assert(count(lp.tx,5)==1 && count(rp.tx,5)==1);
lp.tx.clear();rp.tx.clear();
f.drive.stop(1102);
l.tick(1800);r.tick(1800); // RX stale; approved brake still permitted.
applyWheelCommands(f.drive.commands(),l,r);l.tick(1800);r.tick(1800);
assert(count(lp.tx,7)==1 && count(rp.tx,7)==1);
for(auto packet:frames(lp.tx)) if(packet[0]==7) {
    assert(packet[1]==0 && packet[2]==0 && packet[3]==5 && packet[4]==220);
}
lp.tx.clear();rp.tx.clear();
WheelCommands disabled{};applyWheelCommands(disabled,l,r);l.tick(1801);r.tick(1801);
assert(count(lp.tx,5)==0 && count(lp.tx,7)==0);
// Cancel a queued duty instead of duty-zero or guessed braking.
feed(lp,l,wire(pl),1802); // query reply accepted only if one was emitted
lp.capacity=0;l.setDuty(100);
l.disableControl();lp.capacity=100;l.tick(1802);
assert(count(lp.tx,5)==0);
''')

    def test_real_main_accepted_fixture_arms_and_updates_status_same_loop(self):
        program = PRELUDE + r'''
#include "main.cpp"
HardwareSerial Serial;
HardwareSerialIMXRT Serial1, Serial2, Serial3, Serial4, Serial5, Serial6;
FakeEeprom EEPROM;
uint32_t fake_ms=0,fake_us=0;
int main() {
    Fixture fixture;g_profile=fixture.profile;setup();
    auto step=[&](uint32_t t,uint16_t enable,uint16_t throttle=1500) {
        fake_ms=t;fake_us=t*1000;
        Serial.space=Serial1.space=Serial2.space=Serial6.space=1000;
        fixture.rc(t,throttle,1500,enable);
        g_input=fixture.radio;loop();
    };
    step(480,1000);
    assert(g_drive.commands().left.mode==WheelMode::Brake);
    auto fw=wire({0,42,19});
    Serial1.bytes.insert(Serial1.bytes.end(),fw.begin(),fw.end());
    Serial2.bytes.insert(Serial2.bytes.end(),fw.begin(),fw.end());
    step(481,1000);
    Bytes v=values();put(v,23,0,4);auto packet=wire(v);
    for(uint32_t t=500;t<=1140;t+=20) {
        if(t>=600 && t%100==0) {
            Serial1.bytes.insert(Serial1.bytes.end(),packet.begin(),packet.end());
            Serial2.bytes.insert(Serial2.bytes.end(),packet.begin(),packet.end());
        }
        step(t,t<620?1000:2000,t==1140?2000:1500);
    }
    assert(g_drive.driveState()==r2link::DriveState::Armed);
    assert(g_drive.commands().left.mode==WheelMode::Duty);
    assert(g_body_status.drive_intent==uint8_t(r2link::DriveIntent::Forward));
    step(1141,2000);
    assert(g_drive.commands().left.mode==WheelMode::Brake);
    assert(g_body_status.drive_intent==uint8_t(r2link::DriveIntent::Stationary));
    g_drive.stop(1142);step(1142,2000);
    assert(g_body_status.lock_reasons==1);
    assert(g_body_status.control_epoch==g_drive.controlEpoch());
}
'''
        result = run_cpp(program, extra_sources=SOURCES + [
            BODY / "body/IbusTelemetry.cpp", BODY / "body/LinkBootstrap.cpp",
            BODY / "body/DomePosition.cpp", BODY / "body/DomeController.cpp",
            BODY / "body/DfPlayer.cpp", BODY / "body/BodyController.cpp",
            SHARED / "src/Endpoint.cpp"],
            include_dirs=[BODY.parent.parent / "tests/radio_fakes", BODY, SHARED, BODY.parent / "include"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
