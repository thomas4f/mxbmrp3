// ============================================================================
// tests/unit/test_hud_sw_assets.cpp
// Malformed-asset robustness for the companion renderer's two binary parsers,
// `Renderer::fnt()` and `Renderer::tex()` (core/hud_sw_renderer.cpp).
//
// WHY THIS IS A TRUST BOUNDARY. Both read files out of the asset directories,
// which AssetManager discovers by scanning — so the bytes are USER-SUPPLIED,
// not shipped. A .fnt carries file-declared atlas dimensions and a raw DEFLATE
// stream; a .tga carries file-declared dimensions and an RLE stream whose
// packet counts drive the write loop. Both parsers are hardened for exactly
// that (dimension caps, a compression-type check, a decompressed-size check,
// bounds on every RLE read) — and until this file, NOTHING pinned any of it:
// test_hud_sw_renderer.cpp feeds only the real shipped font, so every guard
// here could be refactored away with the suite still green.
//
// The parsers draw a line these tests follow rather than flatten, because the
// distinction IS the contract:
//   * A malformed HEADER is rejected outright, and the frame must come back
//     untouched — a parser that half-accepts and renders garbage is the failure
//     worth catching, and "renders nothing" is a much stronger assertion than
//     "did not crash".
//   * A sound header with a short or overlong PAYLOAD is accepted and decodes
//     what is there. That is deliberate: a half-copied .tga should cost you half
//     an icon, not a black screen. Those cases assert memory safety instead, and
//     their teeth are the `unit-asan` gate — this suite also runs under
//     ASan/UBSan, where running off the end of the RLE data is a hard failure.
// ============================================================================
#include "doctest.h"

#include "core/hud_sw_renderer.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Layout constants, mirrored from the parser under test (hud_sw_renderer.cpp
// documents the format at the top). Duplicated on purpose: a test that derived
// them from the implementation would follow it into a mistake.
constexpr int kFntTableOff = 268;
constexpr int kFntRecStride = 40;
constexpr int kFntBitmapHdr = kFntTableOff + 256 * kFntRecStride;   // 10508
constexpr int kFntMinSize = kFntBitmapHdr + 24;

// One temp asset root per test, removed on scope exit. Mirrors the real layout
// (`<root>/fonts/<name>.fnt`, `<root>/icons/<name>.tga`) since the parser builds
// those paths itself.
class AssetDir {
public:
    AssetDir() {
        static int counter = 0;
        m_root = std::filesystem::temp_directory_path() /
                 ("mxbsw_assets_" + std::to_string(++counter));
        std::filesystem::remove_all(m_root);
        std::filesystem::create_directories(m_root / "fonts");
        std::filesystem::create_directories(m_root / "icons");
    }
    ~AssetDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);   // best effort; a temp dir
    }
    AssetDir(const AssetDir&) = delete;
    AssetDir& operator=(const AssetDir&) = delete;

    std::string root() const { return m_root.string(); }

    void write(const std::string& rel, const std::vector<uint8_t>& bytes) const {
        std::ofstream f(m_root / rel, std::ios::binary);
        REQUIRE(f.good());
        if (!bytes.empty())
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    }

private:
    std::filesystem::path m_root;
};

void put32(std::vector<uint8_t>& v, size_t off, int32_t value) {
    REQUIRE(off + 4 <= v.size());
    std::memcpy(&v[off], &value, 4);
}

// A .fnt that is structurally valid up to the atlas header; the caller corrupts
// whatever it is testing. cellH > 0 and compression type 2 (raw DEFLATE) are
// what the parser requires, so a case that changes nothing else is ACCEPTED
// only if its DEFLATE stream also decodes — which no case here provides.
std::vector<uint8_t> fntSkeleton(int32_t aw, int32_t ah, int32_t ctype = 2,
                                 size_t payload = 64) {
    std::vector<uint8_t> d(size_t(kFntMinSize) + payload, 0);
    std::memcpy(d.data(), "FNT\0", 4);
    put32(d, 264, 135);                       // cell height
    put32(d, kFntBitmapHdr + 4, aw);
    put32(d, kFntBitmapHdr + 8, ah);
    put32(d, kFntBitmapHdr + 16, ctype);
    return d;
}

// An uncompressed 24-bit TGA (imgType 2) or RLE one (imgType 10) header.
std::vector<uint8_t> tgaHeader(int w, int h, int imgType, int bpp) {
    std::vector<uint8_t> d(18, 0);
    d[2] = uint8_t(imgType);
    d[12] = uint8_t(w & 0xff); d[13] = uint8_t((w >> 8) & 0xff);
    d[14] = uint8_t(h & 0xff); d[15] = uint8_t((h >> 8) & 0xff);
    d[16] = uint8_t(bpp);
    d[17] = 0x20;                             // top-origin: skips the flip pass
    return d;
}

// Renders one string in font 1 / one sprite quad over a known background and
// reports whether ANY pixel changed. A rejected asset must leave the frame
// exactly as it was — the renderer skips the primitive rather than drawing
// something undefined.
struct Canvas {
    hudsw::Image im;
    std::vector<std::string> fonts, sprites;
    std::vector<SPluginQuad_t> quads;
    std::vector<SPluginString_t> strings;

    Canvas() { im.resize(160, 90); }

    bool renderTouchedAnything(const std::string& assetRoot) {
        hudsw::Frame fr{};
        fr.fontNames = &fonts;
        fr.spriteNames = &sprites;
        fr.quads = quads.data();     fr.quadCount = int(quads.size());
        fr.strings = strings.data(); fr.stringCount = int(strings.size());
        fr.assetRoot = assetRoot;
        fr.firstIcon = 1;                     // sprite 1 resolves under icons/
        hudsw::Renderer r;
        r.render(im, fr, 0, 0, 0);            // black background
        for (size_t i = 0; i < im.px.size(); i += 4)
            if (im.px[i] || im.px[i + 1] || im.px[i + 2]) return true;
        return false;
    }

    void addText() {
        fonts = { "broken" };
        SPluginString_t s{};
        std::strcpy(s.m_szString, "888");
        s.m_afPos[0] = 0.1f; s.m_afPos[1] = 0.2f;
        s.m_iFont = 1;
        s.m_fSize = 0.3f;
        s.m_iJustify = 0;
        s.m_ulColor = 0xFFFFFFFF;             // opaque white: any draw is visible
        strings.push_back(s);
    }

    void addSprite() {
        sprites = { "broken" };
        SPluginQuad_t q{};
        q.m_aafPos[0][0] = 0.1f; q.m_aafPos[0][1] = 0.1f;
        q.m_aafPos[1][0] = 0.1f; q.m_aafPos[1][1] = 0.9f;
        q.m_aafPos[2][0] = 0.9f; q.m_aafPos[2][1] = 0.9f;
        q.m_aafPos[3][0] = 0.9f; q.m_aafPos[3][1] = 0.1f;
        q.m_iSprite = 1;
        q.m_ulColor = 0xFFFFFFFF;
        quads.push_back(q);
    }
};

}  // namespace

TEST_CASE("hud_sw_renderer: a corrupt .fnt is rejected, never half-parsed") {
    struct Case { const char* what; std::vector<uint8_t> bytes; };
    std::vector<Case> cases;

    cases.push_back({ "empty file", {} });
    // Shorter than the fixed header the parser indexes into. The size check is
    // the ONLY thing standing between this and 256 out-of-bounds glyph reads.
    cases.push_back({ "truncated mid-glyph-table",
                      std::vector<uint8_t>(size_t(kFntTableOff) + 500, 0x41) });
    {   // Right length, wrong magic — must not be parsed as a font at all.
        std::vector<uint8_t> d = fntSkeleton(256, 256);
        std::memcpy(d.data(), "PNG\0", 4);
        cases.push_back({ "correct size, wrong magic", std::move(d) });
    }
    // The dimension cap. Without it these multiply into a ~10 GB allocation
    // reached from the render loop, which is the bad_alloc the cap exists for.
    cases.push_back({ "absurd atlas dimensions", fntSkeleton(100000, 100000) });
    cases.push_back({ "negative atlas dimensions", fntSkeleton(-4, -4) });
    cases.push_back({ "zero atlas dimensions", fntSkeleton(0, 0) });
    // aw*ah overflows size_t on 32-bit and is enormous on 64-bit; the per-axis
    // cap rejects it before the multiply is ever used.
    cases.push_back({ "INT32_MAX atlas dimensions",
                      fntSkeleton(2147483647, 2147483647) });
    cases.push_back({ "unsupported compression type", fntSkeleton(64, 64, /*ctype=*/0) });
    {   // Header entirely plausible, atlas stream is garbage: the decompressed
        // size check must reject it rather than render a partly-filled atlas.
        std::vector<uint8_t> d = fntSkeleton(64, 64);
        for (size_t i = size_t(kFntMinSize); i < d.size(); ++i)
            d[i] = uint8_t(i * 31 + 7);
        cases.push_back({ "valid header, garbage DEFLATE stream", std::move(d) });
    }
    {   // Declared dims far exceed what the stream can supply. payload=1, NOT 0:
        // with no payload the file is exactly FNT_BITMAP_HDR + 24 bytes, which
        // the parser's `d.size() > ...` guard rejects on size alone — making this
        // a duplicate of the truncation case above instead of reaching the
        // decompressed-size check it is named for. One byte past the header is
        // what puts it on the intended path.
        std::vector<uint8_t> d = fntSkeleton(2048, 2048, 2, /*payload=*/1);
        cases.push_back({ "declared atlas larger than the payload", std::move(d) });
    }

    for (const auto& c : cases) {
        INFO("case: " << c.what);
        AssetDir dir;
        dir.write("fonts/broken.fnt", c.bytes);
        Canvas canvas;
        canvas.addText();
        CHECK_FALSE(canvas.renderTouchedAnything(dir.root()));
    }
}

TEST_CASE("hud_sw_renderer: a corrupt .tga is rejected, never half-parsed") {
    struct Case { const char* what; std::vector<uint8_t> bytes; };
    std::vector<Case> cases;

    cases.push_back({ "empty file", {} });
    cases.push_back({ "shorter than the 18-byte header",
                      std::vector<uint8_t>(9, 0xFF) });
    // Dimensions are two file-declared bytes each, so 65535x65535 costs one
    // edit and would be a ~17 GB rgba allocation without the cap.
    cases.push_back({ "dimensions over the cap", tgaHeader(65535, 65535, 2, 32) });
    cases.push_back({ "zero dimensions", tgaHeader(0, 0, 2, 32) });
    cases.push_back({ "unsupported bit depth", tgaHeader(16, 16, 2, 8) });
    cases.push_back({ "unsupported image type (palettised)", tgaHeader(16, 16, 1, 24) });

    for (const auto& c : cases) {
        INFO("case: " << c.what);
        AssetDir dir;
        dir.write("icons/broken.tga", c.bytes);
        Canvas canvas;
        canvas.addSprite();
        CHECK_FALSE(canvas.renderTouchedAnything(dir.root()));
    }
}

TEST_CASE("hud_sw_renderer: a .tga with a sound header but a bad payload decodes in bounds") {
    // These four differ from the cases above in kind, and the distinction is the
    // parser's actual contract rather than a weaker assertion: the HEADER is
    // entirely valid (type, depth, dimensions all in range), so the texture is
    // ACCEPTED and whatever pixel data exists is decoded. Partial content on a
    // truncated file is graceful degradation, not a defect — a user who drops in
    // a half-copied .tga gets half an icon, not a black screen or a fault.
    //
    // So the property under test is memory safety, not the frame: every one of
    // these drives the copy/RLE loops off the end of the supplied data, and the
    // `o + bpx <= d.size()` / `p < px` / `o < d.size()` guards are the only
    // things keeping them inside the buffer. The teeth are ASan/UBSan in the
    // `unit-asan` gate, where an overrun here is a hard failure; running clean
    // natively only proves the loops terminate.
    struct Case { const char* what; std::vector<uint8_t> bytes; };
    std::vector<Case> cases;

    {   // Uncompressed, header claims 64x64, pixel data stops after two rows.
        std::vector<uint8_t> d = tgaHeader(64, 64, 2, 24);
        d.resize(d.size() + 64 * 2 * 3, 0x7F);
        cases.push_back({ "uncompressed, pixel data truncated", std::move(d) });
    }
    {   // RLE run packet claiming 128 pixels with no pixel bytes behind it.
        std::vector<uint8_t> d = tgaHeader(64, 64, 10, 24);
        d.push_back(0xFF);
        cases.push_back({ "RLE run packet with no payload", std::move(d) });
    }
    {   // RLE raw packet claiming 128 pixels with one byte behind it.
        std::vector<uint8_t> d = tgaHeader(64, 64, 10, 32);
        d.push_back(0x7F);
        d.push_back(0x11);
        cases.push_back({ "RLE raw packet with a partial payload", std::move(d) });
    }
    {   // Packets that would write 128x more pixels than w*h — the `p < px` half
        // of the guard, which is what stops the WRITE side running away.
        std::vector<uint8_t> d = tgaHeader(2, 2, 10, 24);
        for (int i = 0; i < 32; ++i) {
            d.push_back(0xFF); d.push_back(1); d.push_back(2); d.push_back(3);
        }
        cases.push_back({ "RLE packets overrunning the declared size", std::move(d) });
    }

    for (const auto& c : cases) {
        INFO("case: " << c.what);
        AssetDir dir;
        dir.write("icons/broken.tga", c.bytes);
        Canvas canvas;
        canvas.addSprite();
        canvas.renderTouchedAnything(dir.root());   // must return, in bounds
        CHECK(true);
    }
}

TEST_CASE("hud_sw_renderer: a missing asset file renders nothing and is not retried into a crash") {
    AssetDir dir;                             // nothing written at all
    Canvas canvas;
    canvas.addText();
    canvas.addSprite();
    CHECK_FALSE(canvas.renderTouchedAnything(dir.root()));
    // Second render exercises the negative-result cache (the parser stores a
    // not-ok entry rather than re-reading), which must stay non-drawing too.
    CHECK_FALSE(canvas.renderTouchedAnything(dir.root()));
}
