"""Guided commissioning: baseline fill, digests and profile comparison."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
SHARED = ROOT / "shared/R2BodyLink"
SOURCES = [BODY / "body/ConfigStore.cpp", SHARED / "src/Codec.cpp"]
PRELUDE = r'''
#include <cassert>
#include "body/ConfigStore.h"
using namespace body;
static int32_t get(const CommissioningProfile& p, uint8_t id, uint8_t w) {
    int32_t v = -999; return getField(p, id, w, v) ? v : -999;
}
'''


class BaselineTests(unittest.TestCase):
    def check(self, body):
        result = run_cpp(PRELUDE + "int main() {\n" + body + "\nreturn 0;\n}\n",
                         extra_sources=SOURCES, include_dirs=[BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_baseline_fills_every_unset_field_with_the_spec_values(self):
        self.check(r'''
    CommissioningProfile p;
    const ObservedFirmware fw[2] = {{true, 6, 2}, {true, 6, 2}};
    assert(applyBaseline(p, fw) == 5 + 2 * 13);
    assert(get(p, kFieldServoNeutral, 0) == 1500 && get(p, kFieldServoMin, 0) == 1000);
    assert(get(p, kFieldServoMax, 0) == 2000 && get(p, kFieldAutoSpeed, 0) == 15 && get(p, kFieldSlew, 0) == 500);
    for (uint8_t w = 0; w < 2; ++w) {
        assert(get(p, kFieldFwMajor, w) == 6 && get(p, kFieldFwMinor, w) == 2 && get(p, kFieldLayout, w) == 1);
        assert(get(p, kFieldMotorMa, w) == 12000 && get(p, kFieldBatteryMa, w) == 5000);
        assert(get(p, kFieldRegenMa, w) == 2500 && get(p, kFieldBrakeMa, w) == 3000);
        assert(get(p, kFieldUndervoltage, w) == 1100 && get(p, kFieldOvervoltage, w) == 1480);
        assert(get(p, kFieldTimeoutMs, w) == 150 && get(p, kFieldTimeoutBrakeMa, w) == 3000);
        assert(get(p, kFieldReversalErpm, w) == 300 && get(p, kFieldReversalDwell, w) == 200);
        assert(get(p, kFieldDirection, w) == -999);   // set by the direction test
    }
    assert(get(p, kFieldCwRate, 0) == -999 && get(p, kFieldCcwRate, 0) == -999);
    assert(validateProfile(p));
''')

    def test_baseline_never_overwrites_and_skips_unknown_firmware(self):
        self.check(r'''
    CommissioningProfile p;
    assert(setField(p, kFieldBrakeMa, 1, 4200) == FieldResult::Ok);
    assert(setField(p, kFieldServoNeutral, 0, 1512) == FieldResult::Ok);
    const ObservedFirmware fw[2] = {{false, 0, 0}, {true, 5, 3}};
    applyBaseline(p, fw);
    assert(get(p, kFieldBrakeMa, 1) == 4200 && get(p, kFieldServoNeutral, 0) == 1512);
    assert(get(p, kFieldFwMajor, 0) == -999 && get(p, kFieldFwMinor, 0) == -999);
    assert(get(p, kFieldFwMajor, 1) == 5 && get(p, kFieldFwMinor, 1) == 3);
    assert(applyBaseline(p, fw) == 0);   // idempotent
''')

    def test_field_digest_and_same_profile(self):
        self.check(r'''
    CommissioningProfile a, b;
    const ObservedFirmware fw[2] = {{true, 6, 2}, {true, 6, 2}};
    applyBaseline(a, fw); b = a;
    const uint8_t ids[] = {kFieldBrakeMa, kFieldTimeoutMs};
    assert(fieldDigest(a, 0, ids, 2) == fieldDigest(b, 0, ids, 2) && fieldDigest(a, 0, ids, 2) != 0);
    assert(sameProfile(a, b));
    assert(setField(b, kFieldBrakeMa, 0, 3100) == FieldResult::Ok);
    assert(fieldDigest(a, 0, ids, 2) != fieldDigest(b, 0, ids, 2));
    assert(fieldDigest(a, 1, ids, 2) == fieldDigest(b, 1, ids, 2));   // other wheel untouched
    assert(!sameProfile(a, b));
    b = a; b.acceptance |= 1u << kAcceptVescConfig; assert(!sameProfile(a, b));
''')


if __name__ == "__main__":
    unittest.main()
