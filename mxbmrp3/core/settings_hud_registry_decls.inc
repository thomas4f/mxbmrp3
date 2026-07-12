// ============================================================================
// core/settings_hud_registry_decls.inc
// Private static member declarations for SettingsManager: the per-HUD
// capture/apply serializers. #included inside the SettingsManager class body
// (settings_manager.h). Definitions and the registry table are in
// settings_hud_registry.cpp. Kept in a separate .inc so the long, mechanical
// list doesn't clutter the class definition. Game-gated entries are #if'd to
// match the registry table and the HUD registrations.
// ============================================================================
    static void cap_StandingsHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_StandingsHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_MapHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_MapHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_RadarHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_RadarHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_PitboardHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_PitboardHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
#if GAME_HAS_RECORDS_PROVIDER
    static void cap_RecordsHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_RecordsHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
#endif
    static void cap_LapLogHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_LapLogHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_SessionChartsHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_SessionChartsHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
#if GAME_HAS_FMX
    static void cap_FmxHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_FmxHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
#endif
    static void cap_StatsHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_StatsHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_EventLogHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_EventLogHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
#if GAME_HAS_STEAM_FRIENDS
    static void cap_FriendsHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_FriendsHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
#endif
    static void cap_IdealLapHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_IdealLapHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_TelemetryHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_TelemetryHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_PerformanceHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_PerformanceHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_LapWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_LapWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_PositionWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_PositionWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_TimeWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_TimeWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_ClockWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_ClockWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_SessionHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_SessionHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_SpeedWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_SpeedWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_GearWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_GearWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_SpeedoWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_SpeedoWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_TachoWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_TachoWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_TimingHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_TimingHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_GapBarHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_GapBarHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_BarsWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_BarsWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_VersionWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_VersionWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_NoticesHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_NoticesHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_FuelWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_FuelWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_GamepadWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_GamepadWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_LeanWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_LeanWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_GForceWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_GForceWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_CompassWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_CompassWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
#if GAME_HAS_TYRE_TEMP
    static void cap_TyreTempWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_TyreTempWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
#endif
#if GAME_HAS_ECU
    static void cap_EcuWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_EcuWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
#endif
    static void cap_SettingsButtonWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_SettingsButtonWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_PointerWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_PointerWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_RumbleHud(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_RumbleHud(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_BenchmarkWidget(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_BenchmarkWidget(HudManager& hudManager, const ProfileCache& cache, const char* name);
    static void cap_Global(const HudManager& hudManager, ProfileCache& cache, const char* name);
    static void app_Global(HudManager& hudManager, const ProfileCache& cache, const char* name);
