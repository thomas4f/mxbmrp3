// ============================================================================
// tests/unit/test_settings_serde.cpp
// The settings layer's enum<->string converters (core/settings_serde.h).
//
// WHY THIS MATTERS MORE THAN IT LOOKS. These 25 pairs are the entire on-disk
// representation of every enum setting the plugin has. They are hand-written
// twin switch statements — one mapping enum->text for the save, one mapping
// text->enum for the load — and nothing but agreement between those two tables
// makes a setting survive a restart. A typo on one side is invisible: the value
// saves fine, then silently reads back as the default on next launch, and the
// user just sees their setting "not sticking". No compiler or existing test
// catches that, because both halves are individually well-formed.
//
// So the property under test is ROUND-TRIP: for every value of every enum,
// stringTo(toString(v)) == v. That is one assertion per enum value, and it fails
// the moment the two tables disagree by so much as a case difference.
//
// The second property is the DEFAULT FALLBACK. The INI is hand-editable — an
// explicitly supported workflow (CLAUDE.md) — so every stringTo* takes a caller-
// supplied default and must return exactly that for input it doesn't recognise,
// including empty, whitespace, wrong-case and near-miss spellings. A converter
// that fell through to a hardcoded value instead of the caller's default would
// quietly override the caller's intent.
//
// This file compiles the REAL header, not a copy — see tests/unit/shim/ for the
// handful of Win32 typedefs that lets it do so with a plain g++.
// ============================================================================
#include "doctest.h"

#include "core/settings_serde.h"

#include <string>

using namespace Settings;

namespace {

// Round-trip every listed value through both tables. `label` names the converter
// in the failure message, since the values themselves print as integers.
template <typename E, typename ToStr, typename ToEnum>
void checkRoundTrip(const char* label, ToStr toStr, ToEnum toEnum,
                    std::initializer_list<E> values) {
    for (E v : values) {
        const char* text = toStr(v);
        INFO(label << ": value " << static_cast<long long>(v) << " -> \"" << text << '"');
        REQUIRE(text != nullptr);
        CHECK(std::string(text).length() > 0);
        CHECK(toEnum(text, v) == v);          // default == v would mask nothing here…
        // …so re-check against a DIFFERENT default: if the text->enum table were
        // missing this entry it would fall through to the default and, with the
        // default set to v, still appear to pass. Picking a sentinel that differs
        // from v closes that hole.
        const E sentinel = *values.begin();
        if (!(sentinel == v)) CHECK(toEnum(text, sentinel) == v);
    }
}

// Every stringTo* must return the caller's default for input it doesn't know.
template <typename E, typename ToEnum>
void checkUnknownFallsBackToDefault(const char* label, ToEnum toEnum, E a, E b) {
    for (const char* junk : {"", " ", "\t", "off", "Off", "OFFF", "OF", "nonsense",
                             "0", "-1", "ALWAYS_NOT_A_MODE", "ÅÄÖ"}) {
        INFO(label << ": junk input \"" << junk << '"');
        // Asserted against TWO different defaults: a converter that ignored the
        // parameter and returned its own hardcoded fallback would satisfy one.
        CHECK(toEnum(junk, a) == a);
        CHECK(toEnum(junk, b) == b);
    }
}

}  // namespace

// --------------------------------------------------------------- round-trip --

TEST_CASE("every enum<->string converter round-trips") {
    using SH = StandingsHud;
    using MH = MapHud;
    using RH = RadarHud;

    checkRoundTrip<ColumnMode>("columnMode", columnModeToString, stringToColumnMode,
        { ColumnMode::OFF, ColumnMode::SPLITS, ColumnMode::ALWAYS });

    checkRoundTrip<SH::GapMode>("gapMode", gapModeToString, stringToGapMode,
        { SH::GapMode::OFF, SH::GapMode::PLAYER, SH::GapMode::ADJACENT, SH::GapMode::ALL });

    checkRoundTrip<SH::PosGainMode>("posGainMode", posGainModeToString, stringToPosGainMode,
        { SH::PosGainMode::OFF, SH::PosGainMode::RACE_START,
          SH::PosGainMode::LAST_SF, SH::PosGainMode::LAST_SPLIT });

    checkRoundTrip<SH::GapReferenceMode>("gapReferenceMode",
        gapReferenceModeToString, stringToGapReferenceMode,
        { SH::GapReferenceMode::LEADER, SH::GapReferenceMode::PLAYER,
          SH::GapReferenceMode::ALTERNATING });

    checkRoundTrip<SH::AnimationMode>("animationMode", animationModeToString, stringToAnimationMode,
        { SH::AnimationMode::OFF, SH::AnimationMode::BASIC, SH::AnimationMode::COLORED });

    checkRoundTrip<MH::RiderColorMode>("riderColorMode", riderColorModeToString, stringToRiderColorMode,
        { MH::RiderColorMode::UNIFORM, MH::RiderColorMode::BRAND, MH::RiderColorMode::RELATIVE_POS });

    checkRoundTrip<MH::LabelMode>("labelMode", labelModeToString, stringToLabelMode,
        { MH::LabelMode::NONE, MH::LabelMode::POSITION, MH::LabelMode::RACE_NUM, MH::LabelMode::BOTH });

    checkRoundTrip<MH::LabelAnchor>("labelAnchor", labelAnchorToString, stringToLabelAnchor,
        { MH::LabelAnchor::BELOW, MH::LabelAnchor::ABOVE,
          MH::LabelAnchor::LEFT, MH::LabelAnchor::RIGHT });

    checkRoundTrip<MH::AnchorPoint>("anchorPoint", anchorPointToString, stringToAnchorPoint,
        { MH::AnchorPoint::TOP_LEFT, MH::AnchorPoint::TOP_RIGHT,
          MH::AnchorPoint::BOTTOM_LEFT, MH::AnchorPoint::BOTTOM_RIGHT });

    checkRoundTrip<RH::RiderColorMode>("radarRiderColorMode",
        radarRiderColorModeToString, stringToRadarRiderColorMode,
        { RH::RiderColorMode::UNIFORM, RH::RiderColorMode::BRAND, RH::RiderColorMode::RELATIVE_POS });

    checkRoundTrip<RH::LabelMode>("radarLabelMode", radarLabelModeToString, stringToRadarLabelMode,
        { RH::LabelMode::NONE, RH::LabelMode::POSITION, RH::LabelMode::RACE_NUM, RH::LabelMode::BOTH });

    checkRoundTrip<GapBarHud::RiderColorMode>("gapBarRiderColorMode",
        gapBarRiderColorModeToString, stringToGapBarRiderColorMode,
        { GapBarHud::RiderColorMode::UNIFORM, GapBarHud::RiderColorMode::BRAND,
          GapBarHud::RiderColorMode::RELATIVE_POS });

    checkRoundTrip<RH::ProximityArrowMode>("proximityArrowMode",
        proximityArrowModeToString, stringToProximityArrowMode,
        { RH::ProximityArrowMode::OFF, RH::ProximityArrowMode::EDGE, RH::ProximityArrowMode::CIRCLE });

    checkRoundTrip<RH::ProximityArrowColorMode>("proximityArrowColorMode",
        proximityArrowColorModeToString, stringToProximityArrowColorMode,
        { RH::ProximityArrowColorMode::DISTANCE, RH::ProximityArrowColorMode::POSITION });

    checkRoundTrip<RH::RadarMode>("radarMode", radarModeToString, stringToRadarMode,
        { RH::RadarMode::OFF, RH::RadarMode::ON, RH::RadarMode::AUTO_HIDE });

    checkRoundTrip<RecordsHud::DataProvider>("dataProvider", dataProviderToString, stringToDataProvider,
        { RecordsHud::DataProvider::CBR, RecordsHud::DataProvider::MXB_RANKED });

    checkRoundTrip<SpeedWidget::SpeedUnit>("speedUnit", speedUnitToString, stringToSpeedUnit,
        { SpeedWidget::SpeedUnit::MPH, SpeedWidget::SpeedUnit::KMH });

    checkRoundTrip<CompassWidget::Style>("compassStyle", compassStyleToString, stringToCompassStyle,
        { CompassWidget::Style::Classic, CompassWidget::Style::Modern });

    checkRoundTrip<FuelWidget::FuelUnit>("fuelUnit", fuelUnitToString, stringToFuelUnit,
        { FuelWidget::FuelUnit::LITERS, FuelWidget::FuelUnit::GALLONS });

    checkRoundTrip<TemperatureUnit>("tempUnit", tempUnitToString, stringToTempUnit,
        { TemperatureUnit::CELSIUS, TemperatureUnit::FAHRENHEIT });

    checkRoundTrip<PBScope>("pbScope", pbScopeToString, stringToPBScope,
        { PBScope::BIKE, PBScope::CATEGORY });

    checkRoundTrip<DisplayTarget>("displayTarget", displayTargetToString, stringToDisplayTarget,
        { DisplayTarget::IN_GAME, DisplayTarget::COMPANION, DisplayTarget::BOTH });
}

TEST_CASE("the uint8_t-coded converters round-trip too") {
    // These three are plain uint8_t rather than enum class, so the compiler gives
    // them no protection at all — a swapped pair of constants is a valid program.
    checkRoundTrip<uint8_t>("pitboardDisplayMode",
        pitboardDisplayModeToString, stringToPitboardDisplayMode,
        { PitboardHud::MODE_ALWAYS, PitboardHud::MODE_PIT, PitboardHud::MODE_SPLITS });

    checkRoundTrip<uint8_t>("pitboardGapCompareMode",
        pitboardGapCompareModeToString, stringToPitboardGapCompareMode,
        { PitboardHud::GAP_AUTO, PitboardHud::GAP_LEADER, PitboardHud::GAP_SESSION_PB,
          PitboardHud::GAP_IDEAL, PitboardHud::GAP_ALLTIME_PB, PitboardHud::GAP_OVERALL,
          PitboardHud::GAP_RECORD });

    checkRoundTrip<uint8_t>("displayMode", displayModeToString, stringToDisplayMode,
        { TelemetryHud::DISPLAY_GRAPHS, TelemetryHud::DISPLAY_VALUES, TelemetryHud::DISPLAY_BOTH });
}

// ------------------------------------------------------- unknown input paths --

TEST_CASE("unknown text falls back to the CALLER's default, not a hardcoded one") {
    using SH = StandingsHud;
    using MH = MapHud;
    using RH = RadarHud;

    checkUnknownFallsBackToDefault<ColumnMode>("columnMode", stringToColumnMode,
        ColumnMode::SPLITS, ColumnMode::ALWAYS);
    checkUnknownFallsBackToDefault<SH::GapMode>("gapMode", stringToGapMode,
        SH::GapMode::PLAYER, SH::GapMode::ADJACENT);
    checkUnknownFallsBackToDefault<SH::PosGainMode>("posGainMode", stringToPosGainMode,
        SH::PosGainMode::RACE_START, SH::PosGainMode::LAST_SF);
    checkUnknownFallsBackToDefault<SH::GapReferenceMode>("gapReferenceMode", stringToGapReferenceMode,
        SH::GapReferenceMode::PLAYER, SH::GapReferenceMode::ALTERNATING);
    checkUnknownFallsBackToDefault<SH::AnimationMode>("animationMode", stringToAnimationMode,
        SH::AnimationMode::BASIC, SH::AnimationMode::COLORED);
    checkUnknownFallsBackToDefault<MH::RiderColorMode>("riderColorMode", stringToRiderColorMode,
        MH::RiderColorMode::BRAND, MH::RiderColorMode::RELATIVE_POS);
    checkUnknownFallsBackToDefault<MH::LabelMode>("labelMode", stringToLabelMode,
        MH::LabelMode::POSITION, MH::LabelMode::BOTH);
    checkUnknownFallsBackToDefault<MH::LabelAnchor>("labelAnchor", stringToLabelAnchor,
        MH::LabelAnchor::ABOVE, MH::LabelAnchor::RIGHT);
    checkUnknownFallsBackToDefault<MH::AnchorPoint>("anchorPoint", stringToAnchorPoint,
        MH::AnchorPoint::TOP_RIGHT, MH::AnchorPoint::BOTTOM_LEFT);
    checkUnknownFallsBackToDefault<RH::RadarMode>("radarMode", stringToRadarMode,
        RH::RadarMode::ON, RH::RadarMode::AUTO_HIDE);
    checkUnknownFallsBackToDefault<RH::ProximityArrowMode>("proximityArrowMode", stringToProximityArrowMode,
        RH::ProximityArrowMode::EDGE, RH::ProximityArrowMode::CIRCLE);
    checkUnknownFallsBackToDefault<RecordsHud::DataProvider>("dataProvider", stringToDataProvider,
        RecordsHud::DataProvider::CBR, RecordsHud::DataProvider::MXB_RANKED);
    checkUnknownFallsBackToDefault<SpeedWidget::SpeedUnit>("speedUnit", stringToSpeedUnit,
        SpeedWidget::SpeedUnit::MPH, SpeedWidget::SpeedUnit::KMH);
    checkUnknownFallsBackToDefault<TemperatureUnit>("tempUnit", stringToTempUnit,
        TemperatureUnit::CELSIUS, TemperatureUnit::FAHRENHEIT);
    checkUnknownFallsBackToDefault<PBScope>("pbScope", stringToPBScope,
        PBScope::BIKE, PBScope::CATEGORY);
    checkUnknownFallsBackToDefault<DisplayTarget>("displayTarget", stringToDisplayTarget,
        DisplayTarget::COMPANION, DisplayTarget::BOTH);
    checkUnknownFallsBackToDefault<uint8_t>("pitboardGapCompareMode", stringToPitboardGapCompareMode,
        PitboardHud::GAP_LEADER, PitboardHud::GAP_IDEAL);

    // The remaining converters, so that ALL 25 have their fallback arm exercised
    // rather than a representative sample. Coverage showed the difference: the
    // unlisted ones were the only unhit lines left in the header.
    checkUnknownFallsBackToDefault<RH::RiderColorMode>("radarRiderColorMode",
        stringToRadarRiderColorMode, RH::RiderColorMode::BRAND, RH::RiderColorMode::RELATIVE_POS);
    checkUnknownFallsBackToDefault<RH::LabelMode>("radarLabelMode", stringToRadarLabelMode,
        RH::LabelMode::POSITION, RH::LabelMode::BOTH);
    checkUnknownFallsBackToDefault<GapBarHud::RiderColorMode>("gapBarRiderColorMode",
        stringToGapBarRiderColorMode, GapBarHud::RiderColorMode::BRAND,
        GapBarHud::RiderColorMode::UNIFORM);
    checkUnknownFallsBackToDefault<RH::ProximityArrowColorMode>("proximityArrowColorMode",
        stringToProximityArrowColorMode, RH::ProximityArrowColorMode::DISTANCE,
        RH::ProximityArrowColorMode::POSITION);
    checkUnknownFallsBackToDefault<uint8_t>("pitboardDisplayMode", stringToPitboardDisplayMode,
        PitboardHud::MODE_PIT, PitboardHud::MODE_SPLITS);
    checkUnknownFallsBackToDefault<uint8_t>("displayMode", stringToDisplayMode,
        TelemetryHud::DISPLAY_GRAPHS, TelemetryHud::DISPLAY_VALUES);
    checkUnknownFallsBackToDefault<CompassWidget::Style>("compassStyle", stringToCompassStyle,
        CompassWidget::Style::Classic, CompassWidget::Style::Modern);
    checkUnknownFallsBackToDefault<FuelWidget::FuelUnit>("fuelUnit", stringToFuelUnit,
        FuelWidget::FuelUnit::LITERS, FuelWidget::FuelUnit::GALLONS);
}

TEST_CASE("every toString has a working default arm for an out-of-range value") {
    // The `default:` arm of each save-side switch. Unreachable through the enum's
    // own values, so it needs a deliberate cast — and it matters, because a
    // corrupted or newly-added-but-unhandled enumerator must still write text the
    // load side can read rather than falling off the end of the function.
    using SH = StandingsHud;
    using MH = MapHud;
    using RH = RadarHud;
    #define CHECK_DEFAULT_ARM(fn, type) \
        CHECK(std::string(fn(static_cast<type>(99))).length() > 0)

    CHECK_DEFAULT_ARM(posGainModeToString,          SH::PosGainMode);
    CHECK_DEFAULT_ARM(gapReferenceModeToString,     SH::GapReferenceMode);
    CHECK_DEFAULT_ARM(animationModeToString,        SH::AnimationMode);
    CHECK_DEFAULT_ARM(riderColorModeToString,       MH::RiderColorMode);
    CHECK_DEFAULT_ARM(labelModeToString,            MH::LabelMode);
    CHECK_DEFAULT_ARM(labelAnchorToString,          MH::LabelAnchor);
    CHECK_DEFAULT_ARM(anchorPointToString,          MH::AnchorPoint);
    CHECK_DEFAULT_ARM(radarRiderColorModeToString,  RH::RiderColorMode);
    CHECK_DEFAULT_ARM(radarLabelModeToString,       RH::LabelMode);
    CHECK_DEFAULT_ARM(gapBarRiderColorModeToString, GapBarHud::RiderColorMode);
    CHECK_DEFAULT_ARM(proximityArrowModeToString,   RH::ProximityArrowMode);
    CHECK_DEFAULT_ARM(proximityArrowColorModeToString, RH::ProximityArrowColorMode);
    CHECK_DEFAULT_ARM(dataProviderToString,         RecordsHud::DataProvider);
    CHECK_DEFAULT_ARM(speedUnitToString,            SpeedWidget::SpeedUnit);
    CHECK_DEFAULT_ARM(compassStyleToString,         CompassWidget::Style);
    CHECK_DEFAULT_ARM(fuelUnitToString,             FuelWidget::FuelUnit);
    CHECK_DEFAULT_ARM(tempUnitToString,             TemperatureUnit);
    CHECK_DEFAULT_ARM(pbScopeToString,              PBScope);
    #undef CHECK_DEFAULT_ARM
}

TEST_CASE("matching is exact: no trimming, no case folding") {
    // Documenting the ACTUAL behaviour rather than an aspiration. A hand-edited
    // INI with `gapMode = all` (lowercase) or a stray space silently reverts to
    // the default — worth knowing, and worth failing loudly if someone later adds
    // normalisation on only one of the 25 converters.
    using SH = StandingsHud;
    CHECK(stringToGapMode("ALL", SH::GapMode::OFF) == SH::GapMode::ALL);
    CHECK(stringToGapMode("all", SH::GapMode::OFF) == SH::GapMode::OFF);
    CHECK(stringToGapMode("All", SH::GapMode::OFF) == SH::GapMode::OFF);
    CHECK(stringToGapMode(" ALL", SH::GapMode::OFF) == SH::GapMode::OFF);
    CHECK(stringToGapMode("ALL ", SH::GapMode::OFF) == SH::GapMode::OFF);
}

TEST_CASE("toString never returns null or empty, including for out-of-range input") {
    // The save path writes this straight into the INI, so a null would crash the
    // writer and an empty string would produce a key that reads back as unknown.
    // Every converter has a `default:` arm; these force it with values no enum
    // member has.
    CHECK(std::string(columnModeToString(static_cast<ColumnMode>(99))).length() > 0);
    CHECK(std::string(gapModeToString(static_cast<StandingsHud::GapMode>(99))).length() > 0);
    CHECK(std::string(radarModeToString(static_cast<RadarHud::RadarMode>(99))).length() > 0);
    CHECK(std::string(pitboardDisplayModeToString(99)).length() > 0);
    CHECK(std::string(pitboardGapCompareModeToString(99)).length() > 0);
    CHECK(std::string(displayModeToString(99)).length() > 0);
    CHECK(std::string(displayTargetToString(static_cast<DisplayTarget>(99))).length() > 0);

    // And the fallback text must itself be a value the load side accepts —
    // otherwise a corrupted enum would write text that can never be read back.
    CHECK(stringToColumnMode(columnModeToString(static_cast<ColumnMode>(99)), ColumnMode::ALWAYS)
          == ColumnMode::OFF);
    CHECK(stringToRadarMode(radarModeToString(static_cast<RadarHud::RadarMode>(99)),
                            RadarHud::RadarMode::OFF) == RadarHud::RadarMode::ON);
}
