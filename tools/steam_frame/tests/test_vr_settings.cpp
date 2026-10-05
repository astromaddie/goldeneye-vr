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
    int curved, steady, hz, even, stairs, motionThrow, lefty, swap, stats, nolean, gunSize, unlock, watch, cutscenes;
    float fov, dist, snap, vignette, sway, throwStrength, throwPitch, gaze;
    float renderScale;
    int msaa;
    int cinemaDepth;
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
    VrWatchStyle = v.watch;
    VrRenderScale = v.renderScale;
    VrMsaa = v.msaa;
    VrCutsceneMode = v.cutscenes;
    VrCinemaDepth = v.cinemaDepth;
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
    EXPECT(VrWatchStyle == v.watch, "WatchStyle");
    EXPECT(near(VrRenderScale, v.renderScale), "RenderScale");
    EXPECT(VrMsaa == v.msaa, "EyeMsaa");
    EXPECT(VrCutsceneMode == v.cutscenes, "CutsceneMode");
    EXPECT(VrCinemaDepth == v.cinemaDepth, "CinemaDepth");
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
    CHECK(VrWatchStyle == 1, "WatchStyle: the Frame's default is the Q-Branch terminal");
    CHECK(near(VrRenderScale, GEVR_FRAME_RENDER_SCALE) && VrMsaa == GEVR_FRAME_MSAA, "RenderScale, EyeMsaa: the Frame's defaults");
    CHECK(VrCutsceneMode == VR_CUTSCENES_CINEMA_3D, "CutsceneMode: the Frame's default is the 3D cinema");
    CHECK(VrCinemaDepth == 100, "CinemaDepth: 100% (64 mm) by default");
    CHECK(VrRefreshRate == 0, "DisplayHz: the Frame's default is Auto (0: SteamVR's per-app rate)");
    CHECK(VrRenderScale == 0.0f, "RenderScale: the Frame's default is Auto (0)");
    CHECK(near(vrSettingsEyeScale(0.0f, 1728) * 1728.0f, 2160.0f, 0.5f), "Auto on the runtime's 1728: 1.25, a 2160 eye");
    CHECK(near(vrSettingsEyeScale(0.0f, 2160), 1.0f), "Auto with SteamVR's per-app 2160: 1.0");
    CHECK(near(vrSettingsEyeScale(0.0f, 1000), 2.0f) && near(vrSettingsEyeScale(0.0f, 8000), 0.5f), "Auto clamped to 0.5..2");
    CHECK(near(vrSettingsEyeScale(1.5f, 2160), 1.5f), "a set scale multiplies the recommendation");

    // Two full sets of values, every one different from the other and from the defaults, at the
    // precision the file keeps. Smooth turning and a vignette/sway of 0 are covered by set B.
    const Values a = { 1, 1, 120, 0, 0, 0, 1, 1, 1, 1, 2, 1, 0, 0,
                       85.0f, 3.25f, 45.0f, 0.70f, 0.35f, 1.75f, -12.5f, 0.85f,
                       1.25f, 4, 150,
                       0x0000000180004004ULL,
                       { 1.25f, -0.5f, -10.75f }, { { 1, 2, 3, 4.5f, 5.5f, 6.5f }, { -1, -2, -3, -4.5f, 0, 90 } },
                       7, 120 };
    const Values b = { 0, 0, 72, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1,
                       40.0f, 1.5f, 0.0f, 0.0f, 0.0f, 0.5f, 20.0f, 0.0f,
                       0.75f, 2, 60,
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
             "DisplayVersion=2\nDisplayHz=90.0\nLeftHandedMode=1.0\nAimSteadying=1.0\nEvenMovement=0.0\nScreenCurved=1.0\nWatchStyle=0.0\nCutsceneMode=0.0\n"
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
    CHECK(VrWatchStyle == 0, "WatchStyle=0.0");
    CHECK(VrCutsceneMode == VR_CUTSCENES_FLAT, "CutsceneMode=0.0");
    writeIni("[VR]\nCinemaDepth=250\n");
    vrSettingsLoad();
    CHECK(VrCinemaDepth == 150, "CinemaDepth=250 clamped to 150");
    writeIni("[VR]\nCinemaDepth=75.0\n");
    vrSettingsLoad();
    CHECK(VrCinemaDepth == 75, "CinemaDepth=75.0 (a decimal)");
    writeIni("[VR]\nCinemaDepth=10\n");
    vrSettingsLoad();
    CHECK(VrCinemaDepth == 50, "CinemaDepth=10 clamped to 50");
    CHECK(near(VrGunOffX, 2.0f) && near(VrGunOffY, 1.0f) && near(VrGunOffZ, -11.0f), "GunOffX/Y/Z as integers");

    // RenderScale and EyeMsaa: hand-written spellings, out-of-range values clamped, and the early
    // reader the VR session uses before the first frame (vrSettingsReadDisplay) agreeing.
    writeIni("[VR]\nDisplayVersion=2\nRenderScale=1\nEyeMsaa=4.0\n");
    vrSettingsLoad();
    CHECK(near(VrRenderScale, 1.0f) && VrMsaa == 4, "RenderScale=1, EyeMsaa=4.0");
    writeIni("[VR]\nDisplayVersion=2\nRenderScale=3.5\nEyeMsaa=8\n");
    vrSettingsLoad();
    CHECK(near(VrRenderScale, RENDER_SCALE_MAX) && VrMsaa == 4, "RenderScale=3.5 -> 1.5, EyeEyeMsaa=8 -> 4");
    writeIni("[VR]\nDisplayVersion=2\nRenderScale=0.2\nEyeMsaa=3\n");
    vrSettingsLoad();
    CHECK(near(VrRenderScale, RENDER_SCALE_MIN) && VrMsaa == 2, "RenderScale=0.2 -> 0.75, EyeMsaa=3 -> 2");
    writeIni("[VR]\nDisplayVersion=2\nRenderScale=0\nEyeMsaa=0\n");
    vrSettingsLoad();
    CHECK(VrRenderScale == 0.0f && VrMsaa == 1, "RenderScale=0 -> Auto, EyeMsaa=0 -> 1");
    // "Msaa", the key's name in a test build of 2026-10-04 that saved 1 into the player's file,
    // is not read: the default stands.
    VrMsaa = GEVR_FRAME_MSAA;
    writeIni("[VR]\nMsaa=1\n");
    vrSettingsLoad();
    vrSettingsReadDisplay();
    CHECK(VrMsaa == GEVR_FRAME_MSAA, "a stale Msaa=1 is ignored");
    VrRenderScale = 1.25f;
    VrMsaa = 2;
    vrSettingsSave();
    VrRenderScale = 1.0f;
    VrMsaa = 1;
    vrSettingsReadDisplay();
    CHECK(near(VrRenderScale, 1.25f) && VrMsaa == 2, "vrSettingsReadDisplay reads what vrSettingsSave wrote");

    // The display defaults of 2026-10-05: a file from before (no DisplayVersion) holding the old
    // defaults takes the new ones once; other values stay; a file with DisplayVersion=2 is as written.
    writeIni("[VR]\nDisplayHz=90\nRenderScale=1.00\nEyeMsaa=2\n");
    vrSettingsLoad();
    CHECK(VrRefreshRate == 0 && near(VrRenderScale, GEVR_FRAME_RENDER_SCALE), "old defaults (90 Hz, 1.00) -> Auto, Auto");
    VrRenderScale = 1.0f;
    vrSettingsReadDisplay();
    CHECK(near(VrRenderScale, GEVR_FRAME_RENDER_SCALE), "the session's early read migrates the scale too");
    writeIni("[VR]\nDisplayHz=72\nRenderScale=1.50\n");
    vrSettingsLoad();
    CHECK(VrRefreshRate == 72 && near(VrRenderScale, 1.5f), "a player's own 72 Hz and 1.5 stay");
    writeIni("[VR]\nDisplayHz=120\nRenderScale=0.75\n");
    vrSettingsLoad();
    CHECK(VrRefreshRate == 120 && near(VrRenderScale, 0.75f), "a player's own 120 Hz and 0.75 stay");
    VrRefreshRate = 90;
    VrRenderScale = 1.0f;
    vrSettingsSave();   // written with DisplayVersion=2: chosen after the change, kept
    VrRefreshRate = 0;
    VrRenderScale = 0.0f;
    vrSettingsLoad();
    CHECK(VrRefreshRate == 90 && near(VrRenderScale, 1.0f), "90 Hz and 1.00 picked after the change stay (DisplayVersion=2)");
    VrRenderScale = 0.0f;
    vrSettingsReadDisplay();
    CHECK(near(VrRenderScale, 1.0f), "and the early read agrees");
    VrRefreshRate = 0;
    vrSettingsSave();
    VrRefreshRate = 90;
    vrSettingsLoad();
    CHECK(VrRefreshRate == 0, "Auto (0) round trip");
    // DisplayHz values: Auto, the three rates, anything else to the nearest
    writeIni("[VR]\nDisplayVersion=2\nDisplayHz=144\n");
    vrSettingsLoad();
    CHECK(VrRefreshRate == 120, "DisplayHz=144 -> 120");
    writeIni("[VR]\nDisplayVersion=2\nDisplayHz=60\n");
    vrSettingsLoad();
    CHECK(VrRefreshRate == 72, "DisplayHz=60 -> 72");
    writeIni("[VR]\nDisplayVersion=2\nDisplayHz=-1\n");
    vrSettingsLoad();
    CHECK(VrRefreshRate == 0, "DisplayHz=-1 -> Auto");

    // Text stays text, and unknown keys are ignored.
    writeIni("[VR]\nActiveTexturePack=2024\nNoSuchKey=3\nNoSuchFloat=1.5\n");
    vrSettingsLoad();
    CHECK(strcmp(g_ActiveExtTexPack, "2024") == 0, "a pack named with digits loads as its name");

    unlink(VR_INI_PATH);
    unlink("vr_debug.txt");   // vr_log.c's host file (the haptics table dump)
    rmdir(dir);
    if (failures) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("vr_settings: %d checks passed\n", checks);
    return 0;
}
