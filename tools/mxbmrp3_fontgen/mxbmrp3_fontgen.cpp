// ============================================================================
// tools/mxbmrp3_fontgen/mxbmrp3_fontgen.cpp
// A portable generator for PiBoSo bitmap fonts (.fnt) — a drop-in, improved
// replacement for PiBoSo's Windows-only fontgen.exe. Rasterizes a TrueType/
// OpenType font with stb_truetype and writes the exact .fnt binary the games
// (MX Bikes / GP Bikes / KRP / WRS) load, so the output is usable both in-game
// and by this plugin's companion-window software renderer.
//
// Why "better":
//   * Cross-platform (Linux/macOS/Windows) — no Wine, no 32-bit binary.
//   * You set the cell height in PIXELS directly (predictable), instead of
//     fontgen's opaque auto-fit where `scale` behaves as an inverse area knob.
//   * A vertical-centering control (`center` / `voffset`) fixes fonts whose
//     glyphs bake off-centre in the cell (e.g. Tiny5 sits ~10% low), which the
//     stock fontgen cannot express — it has no offset/baseline key at all.
//   * Correct CP1252 high-range mapping (bytes 0x80-0x9F -> the right glyphs).
//
// Config format is a superset of fontgen.cfg (same keys work); see README.md.
//
//   mxbmrp3_fontgen <config.cfg> <out.fnt>
//
// .fnt layout (reverse-engineered, confirmed against PiBoSo's fontgen 1.02):
//   [0]     "FNT\0"
//   [4]     name, null-terminated (buffer to 264)
//   [264]   int32 cell/line height (px)
//   [268]   256 glyph records x 40 bytes, indexed by codepoint 0..255:
//           int32[10] = { valid, xoffset, width, rightBearing,
//                         atlasX0, atlasX1, atlasY0, atlasY1, 0, 0 }
//           advance = xoffset + width + rightBearing
//   [10508] int32 0
//   [10512] int32 bitmap width
//   [10516] int32 bitmap height
//   [10520] int32 payload byte count (bytes from [10524] to EOF)
//   [10524] int32 compression type (2 = raw DEFLATE)
//   [10528] int32 0
//   [10532] raw-DEFLATE stream -> width*height 8-bit grayscale atlas
// ============================================================================
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../mxbmrp3/vendor/stb/stb_truetype.h"
#include "../../mxbmrp3/vendor/miniz/miniz.h"

namespace {

// CP1252 high range (0x80-0x9F) -> Unicode. Bytes outside this window are
// identity (Latin-1 == Unicode for 0x00-0x7F and 0xA0-0xFF).
const int kCp1252High[32] = {
    0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
    0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178,
};
int byteToUnicode(int b, bool cp1252) {
    if (cp1252 && b >= 0x80 && b <= 0x9F) return kCp1252High[b - 0x80];
    return b;
}

struct Config {
    std::string name = "Font";
    std::string filename;                 // source .ttf/.otf
    int charStart = 32, charEnd = 255;
    int spacing = 4;
    int bitmapX = 512, bitmapY = 512;
    bool cp1252 = true;
    int cellHeight = 0;                   // px; 0 = auto-fit into the bitmap
    double voffset = 0.0;                 // px, + = shift glyphs down
    bool center = false;                  // auto-centre ink vertically in the cell
    double monoAdvance = 0.0;             // >0: size so digit-advance/cellH == this
    bool normalize = false;               // apply the standard normalisation defaults
    bool centerSet = false;               // whether `center` was set explicitly
};

// The plugin's reference: the normalised cell height (px) and RobotoMono's digit
// advance / cell ratio (0.489 == the in-game monospace column). Normalising a
// font to these makes it render numbers at a consistent size/width in-game.
//
// The cell height is the atlas resolution, NOT the on-screen size: the renderer
// scales by `size * screenH / cellH`, so a taller cell renders IDENTICALLY on
// screen but from more atlas pixels — i.e. crisper when the HUD draws text
// larger than the cell (high-DPI, scaled-up widgets like the speedo). 135px (3x
// the historical 45) keeps even the largest widgets sharp at 4K; the atlas
// auto-grows to hold it (≤2048²). Width-normalisation is a ratio, so on-screen
// layout is unchanged by this — only sharpness improves.
constexpr int   kRefCellHeight = 135;
constexpr double kRefMonoAdvance = 0.489;

// Apply normalisation defaults to any field the user didn't set explicitly.
void applyNormalizeDefaults(Config& c) {
    if (c.cellHeight <= 0) c.cellHeight = kRefCellHeight;
    if (c.monoAdvance <= 0.0) c.monoAdvance = kRefMonoAdvance;
    if (!c.centerSet) c.center = true;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool parseConfig(const std::string& path, Config& c) {
    std::ifstream in(path);
    if (!in) { fprintf(stderr, "mxbmrp3_fontgen: cannot open config '%s'\n", path.c_str()); return false; }
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(t.substr(0, eq)), v = trim(t.substr(eq + 1));
        std::string kl; for (char ch : k) kl += char(std::tolower((unsigned char)ch));
        if (kl == "name") c.name = v;
        else if (kl == "filename") c.filename = v;
        else if (kl == "char_start") c.charStart = std::stoi(v);
        else if (kl == "char_end") c.charEnd = std::stoi(v);
        else if (kl == "spacing") c.spacing = std::stoi(v);
        else if (kl == "bitmap_x") c.bitmapX = std::stoi(v);
        else if (kl == "bitmap_y") c.bitmapY = std::stoi(v);
        else if (kl == "code_page") c.cp1252 = (std::stoi(v) == 1252);
        else if (kl == "cell_height") c.cellHeight = std::stoi(v);
        else if (kl == "voffset") c.voffset = std::stod(v);
        else if (kl == "center") { c.center = (v == "1" || v == "true" || v == "yes"); c.centerSet = true; }
        else if (kl == "mono_advance") c.monoAdvance = std::stod(v);
        else if (kl == "normalize" || kl == "normalise") c.normalize = (v == "1" || v == "true" || v == "yes");
        // `scale` (fontgen's opaque area knob) is intentionally ignored — use cell_height.
    }
    if (c.filename.empty()) { fprintf(stderr, "mxbmrp3_fontgen: config has no 'filename'\n"); return false; }
    if (c.charStart < 0) c.charStart = 0;
    if (c.charEnd > 255) c.charEnd = 255;
    return true;
}

struct Glyph {
    bool valid = false;
    int xoff = 0, width = 0, adv = 0;     // layout metrics (px)
    int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;   // atlas rect
};

void putI32(std::vector<uint8_t>& b, size_t off, int32_t v) { std::memcpy(&b[off], &v, 4); }

}  // namespace

// Case-insensitive check for a font-file extension.
static bool hasFontExt(const std::string& p) {
    auto ends = [&](const char* e) {
        size_t n = std::strlen(e);
        if (p.size() < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower((unsigned char)p[p.size() - n + i]) != e[i]) return false;
        return true;
    };
    return ends(".ttf") || ends(".otf");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: mxbmrp3_fontgen <config.cfg> <out.fnt>\n"
            "       mxbmrp3_fontgen <font.ttf> [out.fnt]     # auto-normalise (size/offset), drop-in\n");
        return 2;
    }
    Config cfg;
    std::string outPath;
    if (hasFontExt(argv[1])) {
        // Bare font input: auto-normalise with the standard defaults. Derive the
        // font name and output path from the file name.
        cfg.filename = argv[1];
        cfg.normalize = true;
        std::string base = cfg.filename;
        size_t slash = base.find_last_of("/\\"); if (slash != std::string::npos) base = base.substr(slash + 1);
        size_t dot = base.find_last_of('.'); std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
        cfg.name = stem;
        outPath = argc >= 3 ? argv[2] : stem + ".fnt";
    } else {
        if (argc < 3) { fprintf(stderr, "usage: mxbmrp3_fontgen <config.cfg> <out.fnt>\n"); return 2; }
        if (!parseConfig(argv[1], cfg)) return 1;
        outPath = argv[2];
    }
    if (cfg.normalize) applyNormalizeDefaults(cfg);

    std::ifstream fin(cfg.filename, std::ios::binary);
    if (!fin) { fprintf(stderr, "mxbmrp3_fontgen: cannot open font '%s'\n", cfg.filename.c_str()); return 1; }
    std::vector<uint8_t> ttf((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        fprintf(stderr, "mxbmrp3_fontgen: '%s' is not a valid font\n", cfg.filename.c_str()); return 1;
    }
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);

    // fontgen sizes the cell to the ACTUAL ink bounding box of the character range
    // (topmost ascender ink to bottommost descender ink), not the font's declared
    // line box — which is why its cell height depends on the range. We do the same
    // so advance/cellH matches (the plugin's monospace columns depend on that ratio).
    // Work in font units (scale-independent) for the box, using the glyph outlines.
    int unitTop = INT32_MIN, unitBot = INT32_MAX;   // y-up font units: top = max, bottom = min
    for (int b = cfg.charStart; b <= cfg.charEnd; ++b) {
        int gi = stbtt_FindGlyphIndex(&font, byteToUnicode(b, cfg.cp1252));
        if (gi == 0) continue;
        int x0, y0, x1, y1;
        if (!stbtt_GetGlyphBox(&font, gi, &x0, &y0, &x1, &y1)) continue;
        if (x1 <= x0 || y1 <= y0) continue;   // empty (e.g. space)
        unitTop = std::max(unitTop, y1); unitBot = std::min(unitBot, y0);
    }
    if (unitTop <= unitBot) { unitTop = ascent; unitBot = descent; }   // fallback
    const int inkSpanUnits = unitTop - unitBot;

    // Cell height (px): explicit target, or auto-fit into the bitmap. `scale` is
    // derived so the ink box equals the cell.
    auto scaleFor = [&](int ch) { return float(ch) / inkSpanUnits; };
    auto packFits = [&](int ch) -> bool {
        float scale = scaleFor(ch);
        int px = cfg.spacing, py = cfg.spacing, rowH = ch + cfg.spacing;
        for (int b = cfg.charStart; b <= cfg.charEnd; ++b) {
            int cp = byteToUnicode(b, cfg.cp1252);
            if (stbtt_FindGlyphIndex(&font, cp) == 0) continue;
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&font, cp, scale, scale, &x0, &y0, &x1, &y1);
            int w = x1 - x0;
            if (w <= 0) continue;
            if (px + w + cfg.spacing > cfg.bitmapX) { px = cfg.spacing; py += rowH; }
            if (py + ch + cfg.spacing > cfg.bitmapY) return false;
            px += w + cfg.spacing;
        }
        return true;
    };
    int cellH;
    float scale;
    if (cfg.monoAdvance > 0.0) {
        // Width-normalise: size the font so digit-advance / cellH == mono_advance,
        // with a fixed cell height. Every font then renders its digits at the same
        // width and overall size, so a 3-digit race number fits the plate the same
        // in every font (the in-game monospace column reserves a fixed width). Ink
        // taller than the cell (some letters' ascenders/descenders) is cropped to
        // the cell — digits, which have neither, are unaffected. Reference cell:
        // cell_height if given, else 45 (RobotoMono's).
        long digitUnits = 0; int dn = 0;
        for (int c = '0'; c <= '9'; ++c) {
            int aw, lsb; stbtt_GetCodepointHMetrics(&font, c, &aw, &lsb);
            if (aw > 0) { digitUnits += aw; ++dn; }
        }
        double avgDigit = dn ? double(digitUnits) / dn : double(ascent - descent);
        cellH = cfg.cellHeight > 0 ? cfg.cellHeight : kRefCellHeight;
        scale = float(cellH * cfg.monoAdvance / avgDigit);
        fprintf(stderr, "mxbmrp3_fontgen: width-normalise digit-advance/cell=%.3f  cell=%dpx  scale=%.5f\n",
                cfg.monoAdvance, cellH, scale);
    } else {
        cellH = cfg.cellHeight;
        if (cellH <= 0) {
            cellH = 8;
            for (int ch = cfg.bitmapY; ch >= 8; --ch) { if (packFits(ch)) { cellH = ch; break; } }
            fprintf(stderr, "mxbmrp3_fontgen: auto-fit cell height = %d px\n", cellH);
        } else if (!packFits(cellH)) {
            fprintf(stderr, "mxbmrp3_fontgen: warning: cell_height=%d does not fit %dx%d — glyphs will be clipped\n",
                    cellH, cfg.bitmapX, cfg.bitmapY);
        }
        scale = scaleFor(cellH);
    }
    // Rows from cell top to baseline: the topmost ink sits at cell row 0.
    int baseline = int(std::lround(unitTop * scale));

    // Optional vertical centring: shift so the CAP/DIGIT band (the plate-relevant
    // glyphs) is centred in the cell. Fonts whose cell reserves deep-descender room
    // (e.g. Tiny5) otherwise float their digits high — the user's number-plate case.
    if (cfg.center) {
        long topSum = 0, botSum = 0; int n = 0;
        auto acc = [&](int c) {
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&font, c, scale, scale, &x0, &y0, &x1, &y1);
            if (x1 > x0 && y1 > y0) { topSum += baseline + y0; botSum += baseline + y1; ++n; }
        };
        for (int c = '0'; c <= '9'; ++c) acc(c);
        for (int c = 'A'; c <= 'Z'; ++c) acc(c);
        if (n > 0) {
            double inkCentre = (double(topSum) + double(botSum)) / (2.0 * n);
            cfg.voffset += (cellH / 2.0) - inkCentre;
        }
    }
    baseline += int(std::lround(cfg.voffset));

    // Ensure the glyphs pack into the atlas at the chosen scale; grow it (up to
    // 2048) if not. Needed mainly for width-normalised wide fonts, whose larger
    // scale can overflow a 512² atlas.
    auto fitsAtlas = [&](int bw, int bh) -> bool {
        int px = cfg.spacing, py = cfg.spacing, rh = cellH + cfg.spacing;
        for (int b = cfg.charStart; b <= cfg.charEnd; ++b) {
            int cp = byteToUnicode(b, cfg.cp1252);
            if (stbtt_FindGlyphIndex(&font, cp) == 0) continue;
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&font, cp, scale, scale, &x0, &y0, &x1, &y1);
            int w = x1 - x0;
            if (w <= 0) continue;
            if (px + w + cfg.spacing > bw) { px = cfg.spacing; py += rh; }
            if (py + cellH + cfg.spacing > bh) return false;
            px += w + cfg.spacing;
        }
        return true;
    };
    while (!fitsAtlas(cfg.bitmapX, cfg.bitmapY) && cfg.bitmapX < 2048) {
        cfg.bitmapX *= 2; cfg.bitmapY *= 2;
        fprintf(stderr, "mxbmrp3_fontgen: grew atlas to %dx%d to fit all glyphs\n", cfg.bitmapX, cfg.bitmapY);
    }

    // Rasterize + pack into an 8-bit atlas.
    std::vector<uint8_t> atlas(size_t(cfg.bitmapX) * cfg.bitmapY, 0);
    Glyph glyphs[256];
    int px = cfg.spacing, py = cfg.spacing, rowH = cellH + cfg.spacing, placed = 0;
    for (int b = cfg.charStart; b <= cfg.charEnd; ++b) {
        int cp = byteToUnicode(b, cfg.cp1252);
        int gi = stbtt_FindGlyphIndex(&font, cp);
        int aw, lsb; stbtt_GetCodepointHMetrics(&font, cp, &aw, &lsb);
        int advance = int(aw * scale);   // truncate (matches fontgen; keeps advance <= cell ratio)
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, cp, scale, scale, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;

        Glyph& g = glyphs[b];
        g.valid = true;
        g.adv = advance;
        // Blank glyph (space, or a codepoint the font lacks): metrics only.
        if (gi == 0 || w <= 0 || h <= 0) {
            g.xoff = 0; g.width = 0;
            g.ax0 = px; g.ax1 = px; g.ay0 = py; g.ay1 = py + cellH;
            continue;
        }
        if (px + w + cfg.spacing > cfg.bitmapX) { px = cfg.spacing; py += rowH; }
        int inkTop = py + baseline + y0;      // atlas row of the glyph's top
        // Rasterize to a temp buffer, then copy only the rows that fall inside the
        // cell [py, py+cellH) and columns inside the bitmap — so ink taller than the
        // cell is cropped cleanly (never spilling into a neighbouring glyph).
        std::vector<uint8_t> tmp(size_t(w) * h);
        stbtt_MakeCodepointBitmap(&font, tmp.data(), w, h, w, scale, scale, cp);
        int cellTop = py, cellBot = py + cellH;
        for (int gy = 0; gy < h; ++gy) {
            int ay = inkTop + gy;
            if (ay < cellTop || ay >= cellBot || ay >= cfg.bitmapY) continue;
            for (int gx = 0; gx < w; ++gx) {
                int ax = px + gx;
                if (ax < 0 || ax >= cfg.bitmapX) continue;
                atlas[size_t(ay) * cfg.bitmapX + ax] = tmp[size_t(gy) * w + gx];
            }
        }
        g.xoff = x0;                          // left bearing (matches fontgen; may be signed)
        g.width = w;
        g.ax0 = px; g.ax1 = px + w;
        g.ay0 = py; g.ay1 = py + cellH;
        px += w + cfg.spacing;
        ++placed;
    }

    // Assemble the file. Header region up to the bitmap header is fixed size.
    const size_t BITMAP_HDR = 268 + 256 * 40;   // 10508
    std::vector<uint8_t> out(BITMAP_HDR + 24, 0);
    std::memcpy(&out[0], "FNT\0", 4);
    std::string nm = cfg.name.substr(0, 255);
    std::memcpy(&out[4], nm.c_str(), nm.size());   // rest stays 0 (null-terminated)
    putI32(out, 264, cellH);
    for (int b = 0; b < 256; ++b) {
        const Glyph& g = glyphs[b];
        size_t o = 268 + size_t(b) * 40;
        if (!g.valid) continue;
        int rb = g.adv - g.xoff - g.width;
        putI32(out, o + 0, 1);
        putI32(out, o + 4, g.xoff);
        putI32(out, o + 8, g.width);
        putI32(out, o + 12, rb);
        putI32(out, o + 16, g.ax0);
        putI32(out, o + 20, g.ax1);
        putI32(out, o + 24, g.ay0);
        putI32(out, o + 28, g.ay1);
    }
    putI32(out, BITMAP_HDR + 0, 0);
    putI32(out, BITMAP_HDR + 4, cfg.bitmapX);
    putI32(out, BITMAP_HDR + 8, cfg.bitmapY);
    // compression type 2 = raw DEFLATE.
    putI32(out, BITMAP_HDR + 16, 2);
    putI32(out, BITMAP_HDR + 20, 0);

    // Raw DEFLATE (no zlib header): negative window bits.
    int flags = tdefl_create_comp_flags_from_zip_params(9, -15, MZ_DEFAULT_STRATEGY);
    size_t compLen = 0;
    void* comp = tdefl_compress_mem_to_heap(atlas.data(), atlas.size(), &compLen, flags);
    if (!comp) { fprintf(stderr, "mxbmrp3_fontgen: compression failed\n"); return 1; }
    out.insert(out.end(), (uint8_t*)comp, (uint8_t*)comp + compLen);
    mz_free(comp);
    // Payload byte count = bytes from [10524] (the type field) to EOF, like fontgen.
    putI32(out, BITMAP_HDR + 12, int(out.size() - (BITMAP_HDR + 16)));

    std::ofstream fo(outPath, std::ios::binary);
    if (!fo) { fprintf(stderr, "mxbmrp3_fontgen: cannot write '%s'\n", outPath.c_str()); return 1; }
    fo.write((const char*)out.data(), out.size());
    fprintf(stderr, "mxbmrp3_fontgen: wrote %s  (%d glyphs, cell %dpx, atlas %dx%d, %zu bytes)\n",
            outPath.c_str(), placed, cellH, cfg.bitmapX, cfg.bitmapY, out.size());
    return 0;
}
