// nw4r::lyt::Pane::CalculateMtx, natively.
//
// Every menu screen recomputes each layout pane's matrix every frame: scale,
// three axis rotations and a translation, then chained onto the parent's. As
// translated PowerPC it was 8% of an animated menu's frame (another 2% in the
// PSMTXRotRad it calls three times per pane).
//
// Logic follows the ogws decompilation (doldecomp/ogws, CC0: lyt_pane.cpp). The layout does NOT -
// it is read off Mario Kart Wii's own code, because the nw4r it links (2008)
// differs from Wii Sports' (2006): four bytes were added after the matrices,
// so mAlpha/mGlbAlpha/mFlag sit at 0xB8/0xB9/0xBB here, not 0xB4/0xB5/0xB7.
// Binding by code signature only lands this on a game whose code reads the
// same offsets.
//
// Layout is UI-only: nothing replays it, so ordinary float math replaces the
// paired-single sequence (and the MSL sinf/cosf behind PSMTXRotRad) without
// any determinism concern. Gameplay math must not be ported this way.
#include "hle_stubs.h"
#include "abi_bridge.h"
#include "memory.h"

#include <cmath>
#include <cstdint>

namespace {

namespace pane {
constexpr uint32_t kParent = 0x0C;
constexpr uint32_t kChildSentinel = 0x14;  // mChildList's own node: first child's link at +0
constexpr uint32_t kNode = 0x04;           // a pane's link within its parent's list
constexpr uint32_t kTranslate = 0x2C;
constexpr uint32_t kRotate = 0x38;         // degrees
constexpr uint32_t kScale = 0x44;
constexpr uint32_t kMtx = 0x54;
constexpr uint32_t kGlbMtx = 0x84;
constexpr uint32_t kAlpha = 0xB8;
constexpr uint32_t kGlbAlpha = 0xB9;
constexpr uint32_t kFlag = 0xBB;
constexpr uint8_t kVisible = 0x01;
constexpr uint8_t kInfluencedAlpha = 0x02;
constexpr uint8_t kLocationAdjust = 0x04;
constexpr uint32_t kVtableCalculateMtx = 0x10;
}  // namespace pane

namespace drawinfo {
constexpr uint32_t kViewMtx = 0x04;
constexpr uint32_t kLocationAdjustScale = 0x44;
constexpr uint32_t kGlobalAlpha = 0x4C;
constexpr uint32_t kFlags = 0x50;
constexpr uint8_t kMultipleViewMtx = 0x80;
constexpr uint8_t kInfluencedAlpha = 0x40;
constexpr uint8_t kLocationAdjust = 0x20;
constexpr uint8_t kInvisiblePaneCalculateMtx = 0x10;
}  // namespace drawinfo

// The guest's return address inside the translated original, for callees that
// look at LR.
constexpr uint32_t kChildCallReturn = 0x800791A4u;

struct Mtx34 {
    float m[3][4];
};

Mtx34 ReadMtx(uint32_t address) {
    Mtx34 out;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
            out.m[row][col] = Memory::ReadFloat32(address + static_cast<uint32_t>((row * 4 + col) * 4));
    return out;
}

void WriteMtx(uint32_t address, const Mtx34& mtx) {
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
            Memory::WriteFloat32(address + static_cast<uint32_t>((row * 4 + col) * 4), mtx.m[row][col]);
}

// PSMTXConcat: a * b for 3x4 affine matrices; the result may alias neither.
Mtx34 Concat(const Mtx34& a, const Mtx34& b) {
    Mtx34 out;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            out.m[row][col] = a.m[row][0] * b.m[0][col] + a.m[row][1] * b.m[1][col] +
                              a.m[row][2] * b.m[2][col] + (col == 3 ? a.m[row][3] : 0.0f);
        }
    }
    return out;
}

// PSMTXRotRad about 'x', 'y' or 'z'.
Mtx34 Rotation(char axis, float radians) {
    const float s = std::sin(radians);
    const float c = std::cos(radians);
    Mtx34 r{};
    switch (axis) {
    case 'x':
        r.m[0][0] = 1; r.m[1][1] = c; r.m[1][2] = -s; r.m[2][1] = s; r.m[2][2] = c;
        break;
    case 'y':
        r.m[0][0] = c; r.m[0][2] = s; r.m[1][1] = 1; r.m[2][0] = -s; r.m[2][2] = c;
        break;
    default:
        r.m[0][0] = c; r.m[0][1] = -s; r.m[1][0] = s; r.m[1][1] = c; r.m[2][2] = 1;
        break;
    }
    return r;
}

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

}  // namespace

extern "C" void LytPaneCalculateMtx_HLE(CpuContext* ctx)
{
    const uint32_t self = ctx->gpr[3];
    const uint32_t info = ctx->gpr[4];

    const uint8_t flag = Memory::Read8(self + pane::kFlag);
    const uint8_t infoFlags = Memory::Read8(info + drawinfo::kFlags);
    if (!(flag & pane::kVisible) && !(infoFlags & drawinfo::kInvisiblePaneCalculateMtx)) {
        return;
    }

    float scaleX = Memory::ReadFloat32(self + pane::kScale);
    float scaleY = Memory::ReadFloat32(self + pane::kScale + 4);
    if ((infoFlags & drawinfo::kLocationAdjust) && (flag & pane::kLocationAdjust)) {
        scaleX *= Memory::ReadFloat32(info + drawinfo::kLocationAdjustScale);
        scaleY *= Memory::ReadFloat32(info + drawinfo::kLocationAdjustScale + 4);
    }

    Mtx34 scale{};
    scale.m[0][0] = scaleX;
    scale.m[1][1] = scaleY;
    scale.m[2][2] = 1.0f;

    const float rotX = Memory::ReadFloat32(self + pane::kRotate) * kDegToRad;
    const float rotY = Memory::ReadFloat32(self + pane::kRotate + 4) * kDegToRad;
    const float rotZ = Memory::ReadFloat32(self + pane::kRotate + 8) * kDegToRad;

    Mtx34 mtx = Concat(Rotation('x', rotX), scale);
    mtx = Concat(Rotation('y', rotY), mtx);
    mtx = Concat(Rotation('z', rotZ), mtx);

    // PSMTXTransApply: translation added on the left.
    mtx.m[0][3] += Memory::ReadFloat32(self + pane::kTranslate);
    mtx.m[1][3] += Memory::ReadFloat32(self + pane::kTranslate + 4);
    mtx.m[2][3] += Memory::ReadFloat32(self + pane::kTranslate + 8);
    WriteMtx(self + pane::kMtx, mtx);

    const uint32_t parent = Memory::Read32(self + pane::kParent);
    if (parent != 0) {
        WriteMtx(self + pane::kGlbMtx, Concat(ReadMtx(parent + pane::kGlbMtx), mtx));
    } else if (infoFlags & drawinfo::kMultipleViewMtx) {
        WriteMtx(self + pane::kGlbMtx, mtx);
    } else {
        WriteMtx(self + pane::kGlbMtx, Concat(ReadMtx(info + drawinfo::kViewMtx), mtx));
    }

    const uint8_t alpha = Memory::Read8(self + pane::kAlpha);
    const float globalAlpha = Memory::ReadFloat32(info + drawinfo::kGlobalAlpha);
    const bool influenced = (infoFlags & drawinfo::kInfluencedAlpha) != 0;
    Memory::Write8(self + pane::kGlbAlpha,
                   (influenced && parent != 0) ? static_cast<uint8_t>(alpha * globalAlpha) : alpha);

    // Children inherit this pane's alpha while they are computed.
    const bool modifyInfo = (flag & pane::kInfluencedAlpha) && alpha != 255;
    if (modifyInfo) {
        Memory::WriteFloat32(info + drawinfo::kGlobalAlpha,
                             (globalAlpha * static_cast<float>(alpha)) * (1.0f / 255.0f));
        Memory::Write8(info + drawinfo::kFlags,
                       static_cast<uint8_t>(Memory::Read8(info + drawinfo::kFlags) |
                                            drawinfo::kInfluencedAlpha));
    }

    // CalculateMtxChild: each child through its own vtable, which for an
    // ordinary pane routes straight back here.
    const uint32_t sentinel = self + pane::kChildSentinel;
    for (uint32_t link = Memory::Read32(sentinel); link != sentinel && link != 0;
         link = Memory::Read32(link)) {
        const uint32_t child = link - pane::kNode;
        const uint32_t method = Memory::Read32(Memory::Read32(child) + pane::kVtableCalculateMtx);
        ctx->gpr[3] = child;
        ctx->gpr[4] = info;
        ctx->lr = kChildCallReturn;
        InvokeIndirectCpu(method, ctx);
    }

    if (modifyInfo) {
        Memory::WriteFloat32(info + drawinfo::kGlobalAlpha, globalAlpha);
        const uint8_t now = Memory::Read8(info + drawinfo::kFlags);
        Memory::Write8(info + drawinfo::kFlags,
                       influenced ? static_cast<uint8_t>(now | drawinfo::kInfluencedAlpha)
                                  : static_cast<uint8_t>(now & ~drawinfo::kInfluencedAlpha));
    }
}

PPC_NATIVE_OVERRIDE_VOID(80078EF0, LytPaneCalculateMtx_HLE, (CpuContext* ctx), (ctx));
