// THPVideoDecode, natively.
//
// THP is the SDK's movie format: baseline JPEG frames, 4:2:0, decoded straight
// into GX I8 textures that the game combines with a YUV-to-RGB TEV pass. Run as
// translated PowerPC, the SDK's decoder spends every frame emulating paired
// singles through a Huffman decoder and an IDCT - which is why a menu page with
// animated buttons (each one a THP movie) was the slowest screen in the game
// while static pages were not.
//
// This follows the decompiled SDK decoder (THPDec.c) wherever the format departs
// from ordinary JPEG, because a stock JPEG decoder gets each of these wrong:
//
//  - The entropy-coded data has no byte stuffing. The SDK reads it as whole
//    big-endian words; 0xFF bytes are data, never escapes.
//  - A restart interval carries no RST markers in the stream: the decoder just
//    realigns to the next byte and resets the DC predictors.
//  - The output is not rows of pixels but GX I8 tiles (8x4 texels, 32 bytes):
//    pixel (x, y) of a plane W wide lives at
//        (y / 4) * (W * 4) + (x / 8) * 32 + (y % 4) * 8 + (x % 8)
//    which is what the SDK's `slwi xPos, 2` / `slwi wid, 2` store addressing and
//    its 0x2000-byte-per-strip copies (512 wide) amount to.
//
// The IDCT is libjpeg's float AAN IDCT (jidctflt.c): the SDK uses the same
// algorithm with the same constants, AAN-scaled quantisation tables and a
// 1024-bias / 8 output step, so the pixels agree with the original's.
#include "hle_stubs.h"
#include "memory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__SWITCH__)
void SwitchBootLogExternal(const char* text) noexcept;
#endif

namespace {

// SDK error codes, kept so a caller that checks them behaves the same.
constexpr int32_t kThpOk = 0;
constexpr int32_t kThpBadSyntax = 3;
constexpr int32_t kThpBadPrecision = 10;
constexpr int32_t kThpUnsupportedMarker = 11;
constexpr int32_t kThpBadComponents = 12;
constexpr int32_t kThpMissingHuffman = 15;
constexpr int32_t kThpBadSampling = 19;
constexpr int32_t kThpNoInput = 25;
constexpr int32_t kThpNoWork = 26;
constexpr int32_t kThpNoOutput = 27;

// Zig-zag position -> natural (row-major) position.
constexpr uint8_t kNaturalOrder[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

// cos(k*pi/16) * sqrt(2), with 1.0 for k = 0: the AAN scale factors.
constexpr double kAanScale[8] = {
    1.0, 1.387039845, 1.306562965, 1.175875602,
    1.0, 0.785694958, 0.541196100, 0.275899379,
};

struct HuffmanTable {
    bool valid = false;
    int32_t maxCode[18] = {};   // largest code of each length, -1 if none
    int32_t valPtr[17] = {};    // index into vals of the first code of each length
    int32_t minCode[17] = {};
    uint8_t vals[256] = {};
};

struct Component {
    uint8_t quant = 0;
    uint8_t dcTable = 0;
    uint8_t acTable = 0;
    int32_t predDC = 0;
};

class Decoder {
public:
    Decoder(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}

    int32_t Decode(uint8_t* y, uint8_t* u, uint8_t* v);
    uint16_t Width() const { return width_; }
    uint16_t Height() const { return height_; }
    int32_t ParseHeaders();

private:
    uint8_t Byte() { return p_ < end_ ? *p_++ : 0; }
    uint16_t Word() { const uint16_t hi = Byte(); return static_cast<uint16_t>(hi << 8 | Byte()); }

    int32_t ReadFrameHeader();
    int32_t ReadScanHeader();
    void ReadQuantTables();
    void ReadHuffmanTables();

    // Bit reader: no byte stuffing (see the top of the file).
    uint32_t Bit() {
        if (bitCount_ == 0) {
            bitBuffer_ = Byte();
            bitCount_ = 8;
        }
        --bitCount_;
        return (bitBuffer_ >> bitCount_) & 1u;
    }
    int32_t Bits(int n) {
        int32_t value = 0;
        for (int i = 0; i < n; ++i) value = (value << 1) | static_cast<int32_t>(Bit());
        return value;
    }
    void AlignToByte() { bitCount_ = 0; }

    int32_t DecodeHuffman(const HuffmanTable& table);
    static int32_t Extend(int32_t value, int bits) {
        return value < (1 << (bits - 1)) ? value - (1 << bits) + 1 : value;
    }
    void DecodeBlock(Component& comp, float out[64]);
    void Idct(const float coef[64], const float quant[64], uint8_t* plane, uint32_t planeWidth,
              uint32_t x, uint32_t y) const;

    const uint8_t* p_;
    const uint8_t* end_;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    uint16_t restartInterval_ = 0;
    Component comp_[3];
    float quant_[4][64] = {};
    HuffmanTable huff_[8];      // index (id << 1) | class, as the SDK lays them out
    uint32_t bitBuffer_ = 0;
    int bitCount_ = 0;
};

int32_t Decoder::ReadFrameHeader() {
    Word();                                  // length
    if (Byte() != 8) return kThpBadPrecision;
    height_ = Word();
    width_ = Word();
    if (Byte() != 3) return kThpBadComponents;
    for (int i = 0; i < 3; ++i) {
        Byte();                              // component id
        const uint8_t sampling = Byte();
        // 4:2:0 only: Y 2x2, U and V 1x1 - the only layout the SDK accepts.
        if ((i == 0 && sampling != 0x22) || (i > 0 && sampling != 0x11)) return kThpBadSampling;
        comp_[i].quant = static_cast<uint8_t>(Byte() & 3);
    }
    return kThpOk;
}

int32_t Decoder::ReadScanHeader() {
    Word();                                  // length
    if (Byte() != 3) return kThpBadComponents;
    for (int i = 0; i < 3; ++i) {
        Byte();                              // component id
        const uint8_t tables = Byte();
        comp_[i].dcTable = static_cast<uint8_t>((tables >> 4) & 3);
        comp_[i].acTable = static_cast<uint8_t>(tables & 3);
        if (!huff_[comp_[i].dcTable << 1].valid || !huff_[(comp_[i].acTable << 1) | 1].valid) {
            return kThpMissingHuffman;
        }
        comp_[i].predDC = 0;
    }
    Byte(); Byte(); Byte();                  // spectral selection / approximation: baseline
    return kThpOk;
}

void Decoder::ReadQuantTables() {
    int32_t length = Word() - 2;
    while (length > 0 && p_ < end_) {
        const uint8_t id = static_cast<uint8_t>(Byte() & 3);
        uint8_t natural[64];
        for (int i = 0; i < 64; ++i) natural[kNaturalOrder[i]] = Byte();
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                quant_[id][row * 8 + col] = static_cast<float>(
                    natural[row * 8 + col] * kAanScale[row] * kAanScale[col]);
            }
        }
        length -= 65;
    }
}

void Decoder::ReadHuffmanTables() {
    int32_t length = Word() - 2;
    while (length > 0 && p_ < end_) {
        const uint8_t spec = Byte();
        HuffmanTable& table = huff_[((spec & 3) << 1) | ((spec >> 4) & 1)];
        uint8_t counts[17] = {};
        int total = 0;
        for (int len = 1; len <= 16; ++len) {
            counts[len] = Byte();
            total += counts[len];
        }
        total = std::min(total, 256);
        for (int i = 0; i < total; ++i) table.vals[i] = Byte();

        // Canonical code assignment (JPEG Annex C / F.2.2.3).
        int32_t code = 0;
        int32_t index = 0;
        for (int len = 1; len <= 16; ++len) {
            table.valPtr[len] = index;
            table.minCode[len] = code;
            code += counts[len];
            index += counts[len];
            table.maxCode[len] = counts[len] ? code - 1 : -1;
            code <<= 1;
        }
        table.maxCode[17] = 0x7FFFFFFF;
        table.valid = true;
        length -= 17 + total;
    }
}

int32_t Decoder::ParseHeaders() {
    for (;;) {
        if (p_ >= end_) return kThpBadSyntax;
        if (Byte() != 0xFF) return kThpBadSyntax;
        while (p_ < end_ && *p_ == 0xFF) ++p_;
        const uint8_t marker = Byte();

        int32_t status = kThpOk;
        switch (marker) {
        case 0xC4: ReadHuffmanTables(); break;
        case 0xC0: status = ReadFrameHeader(); break;
        case 0xDB: ReadQuantTables(); break;
        case 0xDD: Word(); restartInterval_ = Word(); break;
        case 0xD8: break;                                   // SOI
        case 0xDA: return ReadScanHeader();                 // entropy data follows
        default:
            if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
                const uint16_t skip = static_cast<uint16_t>(p_[0] << 8 | p_[1]);
                p_ = std::min(p_ + skip, end_);
                break;
            }
            return kThpUnsupportedMarker;
        }
        if (status != kThpOk) return status;
    }
}

int32_t Decoder::DecodeHuffman(const HuffmanTable& table) {
    int32_t code = static_cast<int32_t>(Bit());
    int len = 1;
    while (len <= 16 && code > table.maxCode[len]) {
        code = (code << 1) | static_cast<int32_t>(Bit());
        ++len;
    }
    if (len > 16) return 0;                                 // corrupt stream: treat as EOB
    const int32_t index = table.valPtr[len] + code - table.minCode[len];
    return (index >= 0 && index < 256) ? table.vals[index] : 0;
}

void Decoder::DecodeBlock(Component& comp, float out[64]) {
    std::fill(out, out + 64, 0.0f);

    const int32_t dcBits = DecodeHuffman(huff_[comp.dcTable << 1]);
    const int32_t diff = dcBits ? Extend(Bits(dcBits), dcBits) : 0;
    comp.predDC += diff;
    out[0] = static_cast<float>(comp.predDC);

    const HuffmanTable& ac = huff_[(comp.acTable << 1) | 1];
    for (int k = 1; k < 64;) {
        const int32_t rs = DecodeHuffman(ac);
        const int run = rs >> 4;
        const int size = rs & 15;
        if (size == 0) {
            if (run != 15) break;                           // EOB
            k += 16;                                        // ZRL
            continue;
        }
        k += run;
        if (k > 63) break;
        out[kNaturalOrder[k]] = static_cast<float>(Extend(Bits(size), size));
        ++k;
    }
}

// libjpeg's jidctflt: float AAN, dequantising as it reads. Output is the SDK's
// (x + 1024) / 8, saturated to u8, stored as GX I8 tiles.
void Decoder::Idct(const float coef[64], const float quant[64], uint8_t* plane,
                   uint32_t planeWidth, uint32_t x0, uint32_t y0) const {
    float ws[64];

    for (int col = 0; col < 8; ++col) {
        const float* in = coef + col;
        const float* q = quant + col;
        if (in[8] == 0 && in[16] == 0 && in[24] == 0 && in[32] == 0 &&
            in[40] == 0 && in[48] == 0 && in[56] == 0) {
            const float dc = in[0] * q[0];
            for (int row = 0; row < 8; ++row) ws[row * 8 + col] = dc;
            continue;
        }

        float tmp0 = in[0] * q[0], tmp1 = in[16] * q[16];
        float tmp2 = in[32] * q[32], tmp3 = in[48] * q[48];
        float tmp10 = tmp0 + tmp2, tmp11 = tmp0 - tmp2;
        float tmp13 = tmp1 + tmp3;
        float tmp12 = (tmp1 - tmp3) * 1.414213562f - tmp13;
        tmp0 = tmp10 + tmp13; tmp3 = tmp10 - tmp13;
        tmp1 = tmp11 + tmp12; tmp2 = tmp11 - tmp12;

        float tmp4 = in[8] * q[8], tmp5 = in[24] * q[24];
        float tmp6 = in[40] * q[40], tmp7 = in[56] * q[56];
        const float z13 = tmp6 + tmp5, z10 = tmp6 - tmp5;
        const float z11 = tmp4 + tmp7, z12 = tmp4 - tmp7;
        tmp7 = z11 + z13;
        tmp11 = (z11 - z13) * 1.414213562f;
        const float z5 = (z10 + z12) * 1.847759065f;
        tmp10 = 1.082392200f * z12 - z5;
        tmp12 = -2.613125930f * z10 + z5;
        tmp6 = tmp12 - tmp7; tmp5 = tmp11 - tmp6; tmp4 = tmp10 + tmp5;

        ws[0 * 8 + col] = tmp0 + tmp7; ws[7 * 8 + col] = tmp0 - tmp7;
        ws[1 * 8 + col] = tmp1 + tmp6; ws[6 * 8 + col] = tmp1 - tmp6;
        ws[2 * 8 + col] = tmp2 + tmp5; ws[5 * 8 + col] = tmp2 - tmp5;
        ws[4 * 8 + col] = tmp3 + tmp4; ws[3 * 8 + col] = tmp3 - tmp4;
    }

    const auto store = [&](uint32_t x, uint32_t y, float value) {
        int32_t v = static_cast<int32_t>((value + 1024.0f) * 0.125f);
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        plane[(y >> 2) * (planeWidth * 4) + (x >> 3) * 32 + (y & 3) * 8 + (x & 7)] =
            static_cast<uint8_t>(v);
    };

    for (int row = 0; row < 8; ++row) {
        const float* w = ws + row * 8;
        float tmp10 = w[0] + w[4], tmp11 = w[0] - w[4];
        float tmp13 = w[2] + w[6];
        float tmp12 = (w[2] - w[6]) * 1.414213562f - tmp13;
        const float tmp0 = tmp10 + tmp13, tmp3 = tmp10 - tmp13;
        const float tmp1 = tmp11 + tmp12, tmp2 = tmp11 - tmp12;

        const float z13 = w[5] + w[3], z10 = w[5] - w[3];
        const float z11 = w[1] + w[7], z12 = w[1] - w[7];
        const float tmp7 = z11 + z13;
        tmp11 = (z11 - z13) * 1.414213562f;
        const float z5 = (z10 + z12) * 1.847759065f;
        tmp10 = 1.082392200f * z12 - z5;
        tmp12 = -2.613125930f * z10 + z5;
        const float tmp6 = tmp12 - tmp7, tmp5 = tmp11 - tmp6, tmp4 = tmp10 + tmp5;

        const uint32_t y = y0 + static_cast<uint32_t>(row);
        store(x0 + 0, y, tmp0 + tmp7); store(x0 + 7, y, tmp0 - tmp7);
        store(x0 + 1, y, tmp1 + tmp6); store(x0 + 6, y, tmp1 - tmp6);
        store(x0 + 2, y, tmp2 + tmp5); store(x0 + 5, y, tmp2 - tmp5);
        store(x0 + 4, y, tmp3 + tmp4); store(x0 + 3, y, tmp3 - tmp4);
    }
}

int32_t Decoder::Decode(uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane) {
    const uint32_t mcusPerRow = (width_ + 15u) / 16u;
    const uint32_t mcuRows = (height_ + 15u) / 16u;
    const uint32_t chromaWidth = width_ / 2u;
    uint32_t untilRestart = restartInterval_;
    float block[64];

    for (uint32_t mcuY = 0; mcuY < mcuRows; ++mcuY) {
        for (uint32_t mcuX = 0; mcuX < mcusPerRow; ++mcuX) {
            const uint32_t x = mcuX * 16u, y = mcuY * 16u;

            // Same order as the SDK: four Y blocks, then U, then V.
            for (int b = 0; b < 4; ++b) {
                DecodeBlock(comp_[0], block);
                Idct(block, quant_[comp_[0].quant], yPlane, width_,
                     x + static_cast<uint32_t>((b & 1) * 8), y + static_cast<uint32_t>((b >> 1) * 8));
            }
            DecodeBlock(comp_[1], block);
            Idct(block, quant_[comp_[1].quant], uPlane, chromaWidth, x / 2u, y / 2u);
            DecodeBlock(comp_[2], block);
            Idct(block, quant_[comp_[2].quant], vPlane, chromaWidth, x / 2u, y / 2u);

            if (restartInterval_ != 0 && --untilRestart == 0) {
                untilRestart = restartInterval_;
                AlignToByte();
                comp_[0].predDC = comp_[1].predDC = comp_[2].predDC = 0;
            }
        }
    }
    return kThpOk;
}

// The frame's length is not passed in, so find how much guest memory is readable
// from its start, up to a bound no THP frame approaches.
size_t ReadableFrom(uint32_t address) {
    size_t good = 0;
    size_t probe = 64u * 1024u;
    constexpr size_t kLimit = 4u * 1024u * 1024u;
    while (probe <= kLimit && Memory::Contains(address, probe)) {
        good = probe;
        probe *= 2;
    }
    return good;
}

#if defined(__SWITCH__)
std::atomic<uint32_t> g_thpFrames{0};
std::atomic<uint64_t> g_thpMicros{0};
#endif

}  // namespace

extern "C" int32_t THPVideoDecode_HLE(uint32_t file, uint32_t tileY, uint32_t tileU,
                                      uint32_t tileV, uint32_t work) {
    if (file == 0) return kThpNoInput;
    if (tileY == 0 || tileU == 0 || tileV == 0) return kThpNoOutput;
    if (work == 0) return kThpNoWork;

#if defined(__SWITCH__)
    const auto start = std::chrono::steady_clock::now();
#endif

    const size_t readable = ReadableFrom(file);
    if (readable == 0) return kThpBadSyntax;

    Decoder decoder(Memory::GetPointer(file, readable), readable);
    const int32_t header = decoder.ParseHeaders();
    if (header != kThpOk) return header;

    const size_t lumaBytes = static_cast<size_t>(decoder.Width()) * decoder.Height();
    if (lumaBytes == 0 || !Memory::Contains(tileY, lumaBytes) ||
        !Memory::Contains(tileU, lumaBytes / 4) || !Memory::Contains(tileV, lumaBytes / 4)) {
        return kThpNoOutput;
    }

    const int32_t result = decoder.Decode(Memory::GetPointer(tileY, lumaBytes),
                                          Memory::GetPointer(tileU, lumaBytes / 4),
                                          Memory::GetPointer(tileV, lumaBytes / 4));

#if defined(__SWITCH__)
    g_thpMicros.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::steady_clock::now() - start).count()),
                          std::memory_order_relaxed);
    const uint32_t frames = g_thpFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    if (frames == 1 || (frames % 300) == 0) {
        char line[128];
        std::snprintf(line, sizeof(line), "[thp] native decode #%u %ux%u avg=%lluus",
                      frames, decoder.Width(), decoder.Height(),
                      static_cast<unsigned long long>(g_thpMicros.load(std::memory_order_relaxed) / frames));
        SwitchBootLogExternal(line);
    }
#endif
    return result;
}

PPC_NATIVE_OVERRIDE(801B3BAC, THPVideoDecode_HLE, int32_t,
                    (uint32_t file, uint32_t tileY, uint32_t tileU, uint32_t tileV, uint32_t work),
                    (file, tileY, tileU, tileV, work));
