#pragma once

// TotK's camera: where the active gameplay camera is, where it looks, and a
// held override for moving it (a freecam).
//
// THE ACTIVE CAMERA is engine::camera::CameraModule's camera entity 0, its
// sead::Camera at +8 - the path engine::camera::GetCameraForwardDir takes. It
// is a sead::LookAtCamera, and TotK's sead keeps it in DOUBLES:
//
//   +0x08  Matrix34f view matrix   (sead::Camera::getWorldPosByMatrix reads it)
//   +0x50  float[3] up             (LookAtCamera::setUp writes it, normalised)
//   +0x60  double[3] position      (LookAtCamera::doUpdateMatrix reads them and
//   +0x78  double[3] look-at        builds the matrix from them)
//
// ONE HOOK DOES BOTH JOBS: sead::LookAtCamera::doUpdateMatrix, which runs for
// every LookAtCamera; only the active one is touched.
//   * BEFORE the original: if a mod holds an override, its position, look-at
//     and (optionally) up are written into the camera, so the matrix the game
//     renders with is built from them. The game's camera controller sets these
//     every frame, so writing them anywhere else would last until the next
//     frame - here they are what the frame is drawn with.
//   * AFTER the original: position, look-at, up and forward are copied into a
//     snapshot. Mods read the copy and never touch the camera.
//
// THE OVERRIDE IS HELD, NOT LATCHED - the input-injection rule. It lasts the
// frames asked for; "forever" means 300 frames after the last call unless it
// is called again, so a mod that stops calling cannot leave the player with a
// camera stuck in the sky. Frames are presented frames (totk.frame), so it
// needs the frame source live.
//
// THE OVERRIDE MOVES THE VIEW, NOT THE GAME'S IDEA OF THE CAMERA. Systems that
// read the camera before its matrix is rebuilt this frame will see where the
// game put it; nothing here tries to hide that.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/build.hpp>
#include <wiixlaunch/totk/graphics/present.hpp>

#include <cstdint>

#if WIIXL_SWITCH
#include <lib.hpp>
#endif

namespace WiiXLaunch::TotK::Camera {

constexpr uint32_t kHoldForever = 0xFFFFFFFFu;
constexpr uint32_t kMaxOverrideFrames = 300;

namespace impl {

struct Snapshot {
    volatile uint32_t present;
    volatile uint32_t updates;
    volatile float pos[3], at[3], up[3], fwd[3];
};
inline Snapshot g_Snap{};

// One override slot, published under a sequence count like input injection.
struct Override {
    float pos[3], at[3], up[3];
    uint32_t setUp;
    uint32_t until;   // presented-frame count it lapses at; 0 = none
};
inline Override g_Ovr{};
inline volatile uint32_t g_OvrSeq = 0;

inline const Build::CameraLayout* g_L = nullptr;
inline uintptr_t g_ModuleCell = 0;
inline bool g_Ready = false;
inline bool g_FirstLogged = false;
inline bool g_OverrideLogged = false;

template <typename T>
inline T At(const void* base, uint32_t off) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + off);
}
template <typename T>
inline T* Ptr(void* base, uint32_t off) {
    return reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off);
}

inline void* ActiveCamera() {
    if (!g_ModuleCell) return nullptr;
    void* const* sInstance = *reinterpret_cast<void* const* const*>(g_ModuleCell);
    void* module = sInstance ? *sInstance : nullptr;
    if (!module) return nullptr;
    void* entity = At<void*>(module, g_L->moduleEntity0);
    return entity ? At<void*>(entity, g_L->entityCamera) : nullptr;
}

inline void Publish(const Override& o) {
    __atomic_fetch_add(&g_OvrSeq, 1u, __ATOMIC_ACQ_REL);
    g_Ovr = o;
    __atomic_fetch_add(&g_OvrSeq, 1u, __ATOMIC_RELEASE);
}

inline bool ReadOverride(Override& out) {
    for (int tries = 0; tries < 4; ++tries) {
        const uint32_t a = __atomic_load_n(&g_OvrSeq, __ATOMIC_ACQUIRE);
        if (a & 1u) continue;
        out = g_Ovr;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&g_OvrSeq, __ATOMIC_RELAXED) == a) return true;
    }
    return false;
}

inline bool OverrideActive(Override& o) {
    return ReadOverride(o) && o.until != 0 && Present::FrameCount() < o.until;
}

inline void ApplyOverride(void* cam) {
    Override o;
    if (!OverrideActive(o)) return;
    const Build::CameraLayout& L = *g_L;
    double* pos = Ptr<double>(cam, L.camPos);
    double* at = Ptr<double>(cam, L.camAt);
    for (int i = 0; i < 3; ++i) {
        pos[i] = o.pos[i];
        at[i] = o.at[i];
    }
    if (o.setUp) {
        float* up = Ptr<float>(cam, L.camUp);
        for (int i = 0; i < 3; ++i) up[i] = o.up[i];
    }
    if (!g_OverrideLogged) {
        g_OverrideLogged = true;
        WIIXL_LOG("totk.camera: override applied to the active camera");
    }
}

inline void Snap(void* cam) {
    const Build::CameraLayout& L = *g_L;
    const double* pos = Ptr<double>(cam, L.camPos);
    const double* at = Ptr<double>(cam, L.camAt);
    const float* up = Ptr<float>(cam, L.camUp);
    const float* m = Ptr<float>(cam, L.camMatrix);   // row-major 3x4
    for (int i = 0; i < 3; ++i) {
        g_Snap.pos[i] = static_cast<float>(pos[i]);
        g_Snap.at[i] = static_cast<float>(at[i]);
        g_Snap.up[i] = up[i];
        g_Snap.fwd[i] = -m[8 + i];   // -row 2: GetCameraForwardDir's arithmetic
    }
    g_Snap.updates = g_Snap.updates + 1;
    g_Snap.present = 1;
    if (!g_FirstLogged) {
        g_FirstLogged = true;
        WIIXL_LOG("totk.camera: active camera seen - pos %d,%d,%d, looking %d,%d,%d (x1000)",
                  static_cast<int>(g_Snap.pos[0]), static_cast<int>(g_Snap.pos[1]),
                  static_cast<int>(g_Snap.pos[2]), static_cast<int>(g_Snap.fwd[0] * 1000.f),
                  static_cast<int>(g_Snap.fwd[1] * 1000.f), static_cast<int>(g_Snap.fwd[2] * 1000.f));
    }
}

#if WIIXL_SWITCH
using UpdateFn = void (*)(void* cam, float* matrixOut);
inline UpdateFn g_OrigUpdate = nullptr;

inline void UpdateHook(void* cam, float* matrixOut) {
    void* active = ActiveCamera();
    const bool mine = cam && cam == active;
    if (mine) ApplyOverride(cam);
    const UpdateFn orig = g_OrigUpdate;
    if (orig) orig(cam, matrixOut);
    if (mine) Snap(cam);
}

inline uintptr_t Checked(const Build::Func& f, const char* name) {
    const uintptr_t a = exl::util::GetMainModuleInfo().m_Total.m_Start + f.offset;
    const uint32_t word = *reinterpret_cast<const uint32_t*>(a);
    if (word != f.insn) {
        WIIXL_LOG("totk.camera: %s at main+0x%X starts 0x%08X, the row says 0x%08X - "
                  "NOT using it", name, f.offset, word, f.insn);
        return 0;
    }
    return a;
}
#endif

} // namespace impl

inline void Init() {
#if WIIXL_SWITCH
    static bool done = false;
    if (done) return;
    done = true;

    const Build::Row* row = Build::Current();
    if (!row) {
        WIIXL_LOG("totk.camera: no address row for this build - the camera layer is OFF");
        return;
    }
    impl::g_L = &row->camera;
    const uintptr_t upd = impl::Checked(row->camera.doUpdateMatrix, "LookAtCamera::doUpdateMatrix");
    if (!upd) {
        WIIXL_LOG("totk.camera: cannot hook the camera update - the camera layer is OFF");
        return;
    }
    impl::g_ModuleCell = exl::util::GetMainModuleInfo().m_Total.m_Start + row->camera.moduleCell;
    impl::g_OrigUpdate = reinterpret_cast<impl::UpdateFn>(exl::hook::Hook(
        reinterpret_cast<void*>(upd), reinterpret_cast<void*>(&impl::UpdateHook), true));
    if (!impl::g_OrigUpdate) {
        WIIXL_LOG("totk.camera: the doUpdateMatrix hook returned no original - BROKEN");
        return;
    }
    impl::g_Ready = true;
    WIIXL_LOG("totk.camera: hooked LookAtCamera::doUpdateMatrix for %s", row->name);
#endif
}

inline bool Supported() { return impl::g_Ready; }
inline bool IsPresent() { return impl::g_Snap.present != 0; }
inline uint32_t UpdateCount() { return impl::g_Snap.updates; }

inline bool GetPosition(float* out3) {
    if (!out3 || !IsPresent()) return false;
    for (int i = 0; i < 3; ++i) out3[i] = impl::g_Snap.pos[i];
    return true;
}
inline bool GetLookAt(float* out3) {
    if (!out3 || !IsPresent()) return false;
    for (int i = 0; i < 3; ++i) out3[i] = impl::g_Snap.at[i];
    return true;
}
inline bool GetUp(float* out3) {
    if (!out3 || !IsPresent()) return false;
    for (int i = 0; i < 3; ++i) out3[i] = impl::g_Snap.up[i];
    return true;
}
inline bool GetForward(float* out3) {
    if (!out3 || !IsPresent()) return false;
    for (int i = 0; i < 3; ++i) out3[i] = impl::g_Snap.fwd[i];
    return true;
}

// Holds the camera at pos looking at at (and with up, if given) for `frames`
// presented frames; kHoldForever = until Release, 300 frames after the last
// call at most. False without a live frame clock.
inline bool Override(const float* pos, const float* at, const float* up, uint32_t frames) {
    if (!impl::g_Ready || !pos || !at || !Present::IsLive()) return false;
    impl::Override o{};
    for (int i = 0; i < 3; ++i) {
        o.pos[i] = pos[i];
        o.at[i] = at[i];
        o.up[i] = up ? up[i] : 0.f;
    }
    o.setUp = up ? 1u : 0u;
    const uint32_t n = frames == 0 ? 1u
                     : frames == kHoldForever ? kMaxOverrideFrames : frames;
    o.until = Present::FrameCount() + n;
    impl::Publish(o);
    return true;
}

inline void Release() { impl::Publish(impl::Override{}); }

inline bool IsOverridden() {
    impl::Override o;
    return impl::OverrideActive(o);
}

} // namespace WiiXLaunch::TotK::Camera
