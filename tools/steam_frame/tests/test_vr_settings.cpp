/*
 * Host test of goldeneye-vr.ini (port/vr/vr_settings.cpp, the production save and load, with the
 * production defaults and haptics table). Run: tools/steam_frame/tests/run_tests.sh
 *
 * Every key the in-game VR settings (and the launcher pages they share) can change goes through
 * save -> load unchanged; the loader sniffs integers from floats by the decimal point, and a key
 * written the other way was silently dropped (the EvenMovement bug of 2026-10-04), so hand-written
 * spellings of each kind are loaded too.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "vr_settings.h"
#include "vr_screen.h"
#include "vr_haptics.h"
#include <openxr/openxr.h>

extern "C" void vrSettingsSave(void);
extern "C" void vrSettingsLoad(void);

// What the game links from files that cannot be built on the host (vr_input.cpp, vr_openxr.cpp,
// the engine): the settings themselves, defined as there, and no-op engine hooks.
int VrLeftHandedMode = 0;
int VrSwapJoysticks = 0;
int VrAimSteady = 2;
int VrShowStats = 0;
unsigned long long VrCheatMask = 0;
int VrGunSizeCheat = 0;
int VrUnlockAll = 0;
int VrGunFitArmed = 0;
float VrScreenDistance = 2.5f;
float VrScreenFov = 60.0f;
int VrScreenCurved = 0;
float VrScreenHeight = 0.0f;
float XrFov = 0.0f;
float VrStereoCrosshair = 1.0f;
float VrHudDistance = 1.0f;
bool VrWeaponRecoil = true;
int VrPauseHub = 0;
char g_ActiveExtTexPack[256] = "";
static float s_rumble = 0.5f;
extern "C" {
float inputRumbleGetStrength(int) { return s_rumble; }
void inputRumbleSetStrength(int, int strength) { s_rumble = (float)strength; }
void extTexSetPack(const char *) {}
void videoSetExternalTextures(bool) {}
void set_mTrack2Vol(unsigned short) {}
void musicTrack1ApplySeqpVol(unsigned short) {}
void musicTrack3ApplySeqpVol(unsigned short) {}
void gevrSndApplySfxVolume(unsigned short) {}
bool get_button_state(int, const char *) { return false; }
XrResult trigger_haptic_vibration_c(int, float, float) { return XR_SUCCESS; }
XrResult trigger_haptic_vibration_freq_c(int, float, float, float) { return XR_SUCCESS; }
int vr_haptics_ready(void) { return 0; }
}

static int failures, checks;
#define CHECK(cond, what) do { checks++; if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", what, __LINE__); failures++; } } while (0)
static bool near(float a, float b, float eps = 0.006f) { return std::fabs(a - b) < eps; }

struct Values {
    int curved, steady, hz, even, stairs, motionThrow, lefty, swap, stats, nolean, gunSize, unlock;
    float fov, dist, snap, vignette, sway, throwStrength, throwPitch, gaze;
    unsigned long long cheats;
    float gunOff[3], grip[2][6];
    int hapticI, hapticMs;
};

static void put(const Values &v)
{
    VrScreenCurved = v.curved;
    VrScreenFov = v.fov;
    VrScreenDistance = v.dist;
    VrAimSteady = v.steady;
    VrRefreshRate = v.hz;
    VrUseSnapTurn = v.snap;
    VrComfortVignette = v.vignette;
    VrEvenMovement = v.even;
    VrSmoothStairs = v.stairs;
    VrSwayStrength = v.sway;
    VrMotionThrowing = v.motionThrow != 0;
    VrMotionThrowStrength = v.throwStrength;
    VrMotionThrowPitch = v.throwPitch;
    VrMotionThrowGazeAssist = v.gaze;
    VrLeftHandedMode = v.lefty;
    VrSwapJoysticks = v.swap;
    VrShowStats = v.stats;
    VrAimNoLean = v.nolean;
    VrCheatMask = v.cheats;
    VrGunSizeCheat = v.gunSize;
    VrUnlockAll = v.unlock;
    VrGunOffX = v.gunOff[0];
    VrGunOffY = v.gunOff[1];
    VrGunOffZ = v.gunOff[2];
    memcpy(VrGripTrim, v.grip, sizeof(VrGripTrim));
    for (int i = 0; i < vrHapticsGetCount(); i++) {
        vrHapticsGetProfileByIndex(i)->intensity = (v.hapticI + i) % 11;
        vrHapticsGetProfileByIndex(i)->durationMs = v.hapticMs + 10 * i;
    }
}

static void expect(const Values &v, const char *when)
{
    char what[160];
#define EXPECT(cond, key) do { snprintf(what, sizeof(what), "%s: %s", when, key); CHECK(cond, what); } while (0)
    EXPECT(VrScreenCurved == v.curved, "ScreenCurved");
    EXPECT(near(VrScreenFov, v.fov, 0.06f), "ScreenFov");
    EXPECT(near(VrScreenDistance, v.dist), "ScreenDistance");
    EXPECT(VrAimSteady == v.steady, "AimSteadying");
    EXPECT(VrRefreshRate == v.hz, "DisplayHz");
    EXPECT(near(VrUseSnapTurn, v.snap, 0.06f), "SnapTurn");
    EXPECT(near(VrComfortVignette, v.vignette), "ComfortVignette");
    EXPECT(VrEvenMovement == v.even, "EvenMovement");
    EXPECT(VrSmoothStairs == v.stairs, "SmoothStairs");
    EXPECT(near(VrSwayStrength, v.sway), "SwayStrength");
    EXPECT((VrMotionThrowing ? 1 : 0) == v.motionThrow, "MotionThrowing");
    EXPECT(near(VrMotionThrowStrength, v.throwStrength), "MotionThrowStrength");
    EXPECT(near(VrMotionThrowPitch, v.throwPitch, 0.06f), "MotionThrowPitch");
    EXPECT(near(VrMotionThrowGazeAssist, v.gaze), "MotionThrowGazeAssist");
    EXPECT(VrLeftHandedMode == v.lefty, "LeftHandedMode");
    EXPECT(VrSwapJoysticks == v.swap, "SwapJoysticks");
    EXPECT(VrShowStats == v.stats, "ShowStats");
    EXPECT(VrAimNoLean == v.nolean, "AimNoLean");
    EXPECT(VrCheatMask == v.cheats, "Cheats");
    EXPECT(VrGunSizeCheat == v.gunSize, "GunSizeCheat");
    EXPECT(VrUnlockAll == v.unlock, "UnlockAll");
    EXPECT(near(VrGunOffX, v.gunOff[0], 0.001f) && near(VrGunOffY, v.gunOff[1], 0.001f)
           && near(VrGunOffZ, v.gunOff[2], 0.001f), "GunOffX/Y/Z (gun fit)");
    bool grip = true;
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < 6; i++) grip = grip && near(VrGripTrim[c][i], v.grip[c][i], 0.06f);
    EXPECT(grip, "GripPistol/GripRifle (gun fit, two hands)");
    bool haptics = true;
    for (int i = 0; i < vrHapticsGetCount(); i++) {
        haptics = haptics && vrHapticsGetProfileByIndex(i)->intensity == (v.hapticI + i) % 11
                  && vrHapticsGetProfileByIndex(i)->durationMs == v.hapticMs + 10 * i;
    }
    EXPECT(haptics, "haptics (every profile's intensity and duration)");
#undef EXPECT
}

static void writeIni(const char *text)
{
    FILE *f = fopen(VR_INI_PATH, "w");
    fputs(text, f);
    fclose(f);
}

int main()
{
    char dir[] = "/tmp/gevr_settings_test_XXXXXX";
    if (!mkdtemp(dir) || chdir(dir) != 0) {
        perror("temp dir");
        return 1;
    }
    vrSettingsLoad();   // no file yet: the defaults are the settings, and saving is allowed

    // Two full sets of values, every one different from the other and from the defaults, at the
    // precision the file keeps. Smooth turning and a vignette/sway of 0 are covered by set B.
    const Values a = { 1, 1, 120, 0, 0, 0, 1, 1, 1, 1, 2, 1,
                       85.0f, 3.25f, 45.0f, 0.70f, 0.35f, 1.75f, -12.5f, 0.85f,
                       0x0000000180004004ULL,
                       { 1.25f, -0.5f, -10.75f }, { { 1, 2, 3, 4.5f, 5.5f, 6.5f }, { -1, -2, -3, -4.5f, 0, 90 } },
                       7, 120 };
    const Values b = { 0, 0, 72, 1, 1, 1, 0, 0, 0, 0, 0, 0,
                       40.0f, 1.5f, 0.0f, 0.0f, 0.0f, 0.5f, 20.0f, 0.0f,
                       0ULL,
                       { 3.0f, 2.0f, -14.0f }, { { 0, 0, 0, 0, 0, 0 }, { 0.5f, 0.5f, 0.5f, 10, 20, 30 } },
                       2, 40 };

    put(a);
    vrSettingsSave();
    put(b);
    vrSettingsLoad();
    expect(a, "round trip A");

    put(b);
    vrSettingsSave();
    put(a);
    vrSettingsLoad();
    expect(b, "round trip B");

    // Hand-written spellings: float keys as integers, integer keys with a decimal point.
    put(b);
    writeIni("[VR]\n"
             "SnapTurn=45\nComfortVignette=1\nScreenDistance=3\nScreenFov=70\nSwayStrength=1\n"
             "MotionThrowStrength=2\nMotionThrowPitch=-5\nMotionThrowGazeAssist=1\n"
             "DisplayHz=90.0\nLeftHandedMode=1.0\nAimSteadying=1.0\nEvenMovement=0.0\nScreenCurved=1.0\n"
             "GunOffX=2\nGunOffY=1\nGunOffZ=-11\n");
    vrSettingsLoad();
    CHECK(near(VrUseSnapTurn, 45.0f), "SnapTurn=45 (an integer for a float key)");
    CHECK(near(VrComfortVignette, 1.0f), "ComfortVignette=1");
    CHECK(near(VrScreenDistance, 3.0f), "ScreenDistance=3");
    CHECK(near(VrScreenFov, 70.0f), "ScreenFov=70");
    CHECK(near(VrSwayStrength, 1.0f), "SwayStrength=1");
    CHECK(near(VrMotionThrowStrength, 2.0f), "MotionThrowStrength=2");
    CHECK(near(VrMotionThrowPitch, -5.0f), "MotionThrowPitch=-5");
    CHECK(near(VrMotionThrowGazeAssist, 1.0f), "MotionThrowGazeAssist=1");
    CHECK(VrRefreshRate == 90, "DisplayHz=90.0 (a decimal for an integer key)");
    CHECK(VrLeftHandedMode == 1, "LeftHandedMode=1.0");
    CHECK(VrAimSteady == 1, "AimSteadying=1.0");
    CHECK(VrEvenMovement == 0, "EvenMovement=0.0");
    CHECK(VrScreenCurved == 1, "ScreenCurved=1.0");
    CHECK(near(VrGunOffX, 2.0f) && near(VrGunOffY, 1.0f) && near(VrGunOffZ, -11.0f), "GunOffX/Y/Z as integers");

    // Text stays text, and unknown keys are ignored.
    writeIni("[VR]\nActiveTexturePack=2024\nNoSuchKey=3\nNoSuchFloat=1.5\n");
    vrSettingsLoad();
    CHECK(strcmp(g_ActiveExtTexPack, "2024") == 0, "a pack named with digits loads as its name");

    unlink(VR_INI_PATH);
    rmdir(dir);
    if (failures) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("vr_settings: %d checks passed\n", checks);
    return 0;
}
