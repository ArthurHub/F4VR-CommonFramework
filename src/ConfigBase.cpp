#include "ConfigBase.h"

#include <array>
#include <charconv>
#include <nlohmann/json.hpp>

#include "common/MatrixUtils.h"
#include "devbench/DevBench.h"
#include "f4vr/WandActivationSphere.h"
#include "render/PrimitiveDraw.h"
#include "vrcf/InputBindingParser.h"
#include "vrcf/VRControllersHaptic.h"

using json = nlohmann::json;

namespace
{
    DebugAdjustTarget parseDebugAdjustTarget(const std::string_view value)
    {
        if (value == "transform")
            return DebugAdjustTarget::Transform;
        if (value == "handpose")
            return DebugAdjustTarget::HandPose;
        if (value == "flag1")
            return DebugAdjustTarget::FlowFlag1;
        if (value == "flag2")
            return DebugAdjustTarget::FlowFlag2;
        if (value == "flag3")
            return DebugAdjustTarget::FlowFlag3;
        if (value == "flag123")
            return DebugAdjustTarget::FlowFlag123;
        if (value == "haptictest")
            return DebugAdjustTarget::HapticTest;
        return DebugAdjustTarget::None;
    }

    /**
     * Load the given json object with offset data into an offset map.
     */
    void loadOffsetJsonToMap(const json& json, std::unordered_map<std::string, RE::NiTransform>& offsetsMap)
    {
        try {
            for (auto& [key, value] : json.items()) {
                RE::NiTransform data;
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 4; j++) {
                        data.rotate[i][j] = value["rotation"][i * 4 + j].get<float>();
                    }
                }
                data.translate.x = value["x"].get<float>();
                data.translate.y = value["y"].get<float>();
                data.translate.z = value["z"].get<float>();
                data.scale = value["scale"].get<float>();
                offsetsMap[key] = data;
            }
        } catch (std::exception& ex) {
            std::throw_with_nested(std::runtime_error(fmt::format("Failed to load offset json:\n\t{}", ex.what())));
        }
    }

    /**
     * Parse "r,g,b" or "r,g,b,a", each channel a whole number in 0..255, into `color`'s 0..1 channels. Without the
     * alpha, `color`'s own alpha stays. Anything else returns false and leaves `color` untouched.
     */
    bool parseColor255(const std::string& text, std::array<float, 4>& color)
    {
        const auto tokens = f4cf::common::splitTrimmed(text, ',');
        if (tokens.size() != 3 && tokens.size() != 4) {
            return false;
        }

        auto parsed = color;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            int channel = -1;
            const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), channel);
            if (error != std::errc{} || end != token.data() + token.size() || channel < 0 || channel > 255) {
                return false;
            }
            parsed[i] = static_cast<float>(channel) / 255.0f;
        }
        color = parsed;
        return true;
    }

    /**
     * Read how an activation sphere's visual looks from its INI section: a named preset (sSphereStyle) replaces
     * `fallback`'s look (everything but its size), then each per-value key that is present overrides that one value —
     * the mesh (sSphereNif), the texture (sSphereTexture; "none" keeps the texture the mesh itself names), the color
     * (sSphereColor as "r,g,b" or "r,g,b,a" in 0..255 — without the alpha the preset's opacity stays), the brightness
     * (fSphereGlow, 0..1), the middle-to-edge opacity fade (sSphereFalloff as "center,rim"), and the drawn size
     * relative to the zone (fSphereScale, > 0). An empty value keeps what the preset set; a malformed or out-of-range
     * one is logged and ignored.
     */
    f4cf::f4vr::SphereStyle readSphereStyle(const CSimpleIniA& ini, const char* section, const f4cf::f4vr::SphereStyle& fallback)
    {
        auto style = fallback;

        const std::string presetName = ini.GetValue(section, "sSphereStyle", "");
        if (!presetName.empty()) {
            if (const auto preset = f4cf::f4vr::findSphereStylePreset(presetName)) {
                style = *preset;
                style.scale = fallback.scale;
            } else {
                logger::warn("Config: unknown sphere style '{}.sSphereStyle' = '{}'. Using default.", section, presetName);
            }
        }

        if (const std::string nif = ini.GetValue(section, "sSphereNif", ""); !nif.empty()) {
            style.nif = nif;
        }
        if (const std::string texture = ini.GetValue(section, "sSphereTexture", ""); !texture.empty()) {
            style.texture = f4cf::common::normalizeConfigToken(texture) == "none" ? "" : texture;
        }
        if (const char* rawColor = ini.GetValue(section, "sSphereColor", nullptr); rawColor && *rawColor) {
            if (!parseColor255(rawColor, style.color)) {
                logger::warn("Config: malformed sphere color for '{}.sSphereColor' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Ignored.", section, rawColor);
            }
        }
        if (const char* rawGlow = ini.GetValue(section, "fSphereGlow", nullptr); rawGlow && *rawGlow) {
            const auto glow = static_cast<float>(ini.GetDoubleValue(section, "fSphereGlow", -1.0));
            if (glow >= 0.0f && glow <= 1.0f) {
                style.glow = glow;
            } else {
                logger::warn("Config: sphere glow '{}.fSphereGlow' = '{}' is out of range (expected 0..1). Ignored.", section, rawGlow);
            }
        }
        if (const char* rawFalloff = ini.GetValue(section, "sSphereFalloff", nullptr); rawFalloff && *rawFalloff) {
            float center, rim;
            if (std::sscanf(rawFalloff, " %f , %f", &center, &rim) == 2) {
                style.centerOpacity = center;
                style.rimOpacity = rim;
            } else {
                logger::warn("Config: malformed sphere falloff for '{}.sSphereFalloff' = '{}' (expected 'center,rim' opacities in 0..1). Ignored.", section, rawFalloff);
            }
        }
        if (const char* rawScale = ini.GetValue(section, "fSphereScale", nullptr); rawScale && *rawScale) {
            const auto scale = static_cast<float>(ini.GetDoubleValue(section, "fSphereScale", -1.0));
            if (scale > 0.0f) {
                style.scale = scale;
            } else {
                logger::warn("Config: sphere scale '{}.fSphereScale' = '{}' is out of range (expected > 0). Ignored.", section, rawScale);
            }
        }
        return style;
    }

    /**
     * Read how an activation sphere's icon looks from its INI section, each key that is present overriding that one
     * value of `fallback`: the image (sIcon, a .dds path; empty keeps the fallback's), the tint (sIconColor as "r,g,b"
     * or "r,g,b,a" in 0..255 — without the alpha the fallback's opacity stays), and the size of its longer side in
     * game units (fIconSize, > 0). A malformed or out-of-range value is logged and ignored.
     */
    f4cf::f4vr::ActivationIconStyle readIconStyle(const CSimpleIniA& ini, const char* section, const f4cf::f4vr::ActivationIconStyle& fallback)
    {
        auto style = fallback;

        if (const std::string texture = ini.GetValue(section, "sIcon", ""); !texture.empty()) {
            style.texture = texture;
        }
        if (const char* rawColor = ini.GetValue(section, "sIconColor", nullptr); rawColor && *rawColor) {
            if (!parseColor255(rawColor, style.color)) {
                logger::warn("Config: malformed icon color for '{}.sIconColor' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Ignored.", section, rawColor);
            }
        }
        if (const char* rawSize = ini.GetValue(section, "fIconSize", nullptr); rawSize && *rawSize) {
            const auto size = static_cast<float>(ini.GetDoubleValue(section, "fIconSize", -1.0));
            if (size > 0.0f) {
                style.size = size;
            } else {
                logger::warn("Config: icon size '{}.fIconSize' = '{}' is out of range (expected > 0). Ignored.", section, rawSize);
            }
        }
        return style;
    }
}

namespace f4cf
{
    /**
     * Load the config from the INI file.
     */
    void ConfigBase::load()
    {
        logger::info("Load ini config...");
        common::createDirDeep(_iniFilePath);
        loadIniConfig();
    }

    /**
     * Save the current in-memory config values to the INI file.
     */
    void ConfigBase::save()
    {
        saveIniConfig();
    }

    /**
     * Re-read all values from the on-disk INI file. Skips version migration and watcher setup
     * so it's safe to call repeatedly at runtime (e.g. from the DebugAdjuster reload action).
     */
    void ConfigBase::reload()
    {
        loadIniConfigValues();
        devbench::emit("config.reloaded", [&] {
            return nlohmann::json{ { "file", fs::path(_iniFilePath).filename().string() }, { "trigger", "reload" } };
        });
    }

    /**
     * Load the embedded ini with default values and not the on-disk file.
     */
    void ConfigBase::loadEmbeddedDefaultOnly()
    {
        std::lock_guard lock(_loadMutex);

        CSimpleIniA ini;
        const SI_Error rc = ini.LoadData(common::getEmbededResourceAsString(_module, _iniDefaultConfigEmbeddedResourceId));
        if (rc < 0) {
            logger::warn("Failed to load INI config file! Error:", rc);
            throw std::runtime_error("Failed to load INI config file! Error: " + std::to_string(rc));
        }

        loadDebugSection(ini);
        loadIniConfigInternal(ini);
        _valuesReloaded = true;
    }

    /**
     * Subscribe to be told when the config values were loaded again: after a change of the INI on disk, a
     * session override set or cleared, or reload().
     * The callback runs on the game thread, at the start of the next frame and before the mod's
     * onFrameUpdate(), once for all the loads since the frame before. So it can call into the engine, and a
     * value that is set many times a second while it is previewed is one call a frame.
     * Key used to identify the subscription, for unsubscribe, and prevent duplicates.
     * Call it on the game thread, as unsubscribeFromIniChangedEvent.
     */
    void ConfigBase::subscribeForIniChangedEvent(const std::string& key, const std::function<void(const std::string&)>& callback)
    {
        if (_onIniConfigChangedSubscribers.contains(key)) {
            throw std::runtime_error("ConfigBase::subscribeForIniChangedEvent: Key '" + key + "' is already subscribed!");
        }
        _onIniConfigChangedSubscribers.emplace(key, callback);
    }

    /**
     * Remove ini change subscription for the given key.
     */
    void ConfigBase::unsubscribeFromIniChangedEvent(const std::string& key)
    {
        _onIniConfigChangedSubscribers.erase(key);
    }

    /**
     * Call the subscribers if the config values were loaded again since the last call.
     * ModBase calls it at the start of every frame, on the game thread, so a mod has nothing to call. A mod
     * with a frame update of its own calls it there.
     * A load runs on the file watcher's thread, or on the thread of whoever sets an override, and at any
     * point of a frame. It only marks that it ran, and the subscribers are called from here.
     */
    void ConfigBase::notifySubscribersOfReload()
    {
        if (!_valuesReloaded.exchange(false)) {
            return;
        }

        // a copy, so a subscriber can unsubscribe from inside its call
        const auto subscribers = _onIniConfigChangedSubscribers;
        for (const auto& [key, subscriber] : subscribers) {
            logger::debug("Notify INI config change subscriber '{}'", key);
            subscriber(key);
        }
    }

    /**
     * Check if debug data dump is requested for the given name.
     * If matched, the name will be removed from the list to prevent multiple dumps.
     * Also saved into INI to prevent reloading the same dump name on next config reload.
     * Support specifying multiple names by any separator as only the matched sub-string is removed.
     */
    bool ConfigBase::checkDebugDumpDataOnceFor(const char* name)
    {
        const auto idx = debug.dumpDataOnceNames.find(name);
        if (idx == std::string::npos) {
            return false;
        }
        debug.dumpDataOnceNames = debug.dumpDataOnceNames.erase(idx, strlen(name));
        // write to INI for auto-reload not to re-enable it
        saveIniConfigValue(INI_SECTION_DEBUG, "sDumpDataOnceNames", debug.dumpDataOnceNames.c_str());

        logger::info("---- Debug Dump Data check passed for '{}' ----", name);
        return true;
    }

    /**
     * Consume the one-shot "sAddItemsOnceNames" flag.
     * Returns the current value and clears it (in memory + INI) so the bulk item-add runs exactly once,
     * and is not re-triggered by the file-watcher reloading the same value.
     */
    std::string ConfigBase::consumeDebugAddItemsOnce()
    {
        if (debug.addItemsOnceNames.empty()) {
            return {};
        }
        auto spec = debug.addItemsOnceNames;
        debug.addItemsOnceNames.clear();
        saveIniConfigValue(INI_SECTION_DEBUG, "sAddItemsOnceNames", "");
        return spec;
    }

    /**
     * Load all INI config values.
     * If INI file doesn't exist it will be created from the default embedded resource.
     * If the found INI version is not the latest it will run update to the latest using embedded resource.
     */
    void ConfigBase::loadIniConfig()
    {
        // create .ini if it doesn't exist
        common::createFileFromResourceIfNotExists(_iniFilePath, _module, _iniDefaultConfigEmbeddedResourceId, true);

        loadIniConfigValues();

        const auto iniConfigLatestVersion = loadEmbeddedResourceIniConfigVersion();
        if (_iniConfigVersion < iniConfigLatestVersion) {
            logger::info("Updating INI config version {} -> {}", _iniConfigVersion, iniConfigLatestVersion);
            updateIniConfigToLatestVersion(_iniConfigVersion, iniConfigLatestVersion);

            // reload the config after update
            loadIniConfigValues();
        }

        startIniConfigFileWatch();
    }

    /**
     * Get the latest version of the embedded INI config resource to know if update migration is required.
     */
    int ConfigBase::loadEmbeddedResourceIniConfigVersion() const
    {
        const auto embeddedIniStr = common::getEmbededResourceAsString(_module, _iniDefaultConfigEmbeddedResourceId);

        CSimpleIniA ini;
        const SI_Error rc = ini.LoadData(embeddedIniStr);
        if (rc < 0) {
            logger::warn("Failed to load INI config file! Error:", rc);
            throw std::runtime_error("Failed to load INI config file! Error: " + std::to_string(rc));
        }

        return ini.GetLongValue(INI_SECTION_DEBUG, "iVersion", 0);
    }

    void ConfigBase::loadDebugSection(const CSimpleIniA& ini)
    {
        _iniConfigVersion = ini.GetLongValue(INI_SECTION_DEBUG, "iVersion", 0);
        _logLevel = ini.GetLongValue(INI_SECTION_DEBUG, "iLogLevel", 2);
        _logPattern = ini.GetValue(INI_SECTION_DEBUG, "sLogPattern", "%H:%M:%S.%e %L: %v");
        debug.flowFlag1 = static_cast<float>(ini.GetDoubleValue(INI_SECTION_DEBUG, "fFlowFlag1", 0));
        debug.flowFlag2 = static_cast<float>(ini.GetDoubleValue(INI_SECTION_DEBUG, "fFlowFlag2", 0));
        debug.flowFlag3 = static_cast<float>(ini.GetDoubleValue(INI_SECTION_DEBUG, "fFlowFlag3", 0));
        debug.flowText1 = ini.GetValue(INI_SECTION_DEBUG, "sFlowText1", "");
        debug.flowText2 = ini.GetValue(INI_SECTION_DEBUG, "sFlowText2", "");
        debug.transform = getTransformValue(ini, INI_SECTION_DEBUG, "tTransform", common::MatrixUtils::getTransform(0, 0, 0, 0, 0, 0));
        debug.handPose = getHandPoseValue(ini, INI_SECTION_DEBUG, "hHandPose", {});
        // sAdjustTarget is either a fixed keyword (transform/handpose/flag1...) or, when it
        // contains "::", a "Section::Key" reference to any INI field tuned live via the field mode.
        const std::string adjustTarget = ini.GetValue(INI_SECTION_DEBUG, "sAdjustTarget", "none");
        if (adjustTarget.find("::") != std::string::npos) {
            debug.adjustTarget = DebugAdjustTarget::Field;
            debug.adjustField = adjustTarget;
        } else {
            debug.adjustTarget = parseDebugAdjustTarget(adjustTarget);
            debug.adjustField.clear();
        }
        debug.dumpDataOnceNames = ini.GetValue(INI_SECTION_DEBUG, "sDumpDataOnceNames", "");
        debug.addItemsOnceNames = ini.GetValue(INI_SECTION_DEBUG, "sAddItemsOnceNames", "");
        debug.drawEnabled = ini.GetBoolValue(INI_SECTION_DEBUG, "bDebugDrawEnabled", true);
        debug.drawDisabledChannels = ini.GetValue(INI_SECTION_DEBUG, "sDebugDrawDisabledChannels", "");
        debug.drawToggleBinding = ini.GetValue(INI_SECTION_DEBUG, "sDebugDrawToggleBinding", "");
        debug.drawHudPlacement = ini.GetValue(INI_SECTION_DEBUG, "sDebugDrawHudPlacement", "center");
        debug.sceneDepthStrategy = ini.GetValue(INI_SECTION_DEBUG, "sSceneDepthStrategy", "auto");
        debug.sceneDepthDiagnostics = ini.GetBoolValue(INI_SECTION_DEBUG, "bSceneDepthDiagnostics", false);
        debug.vruiShowFingerTip = ini.GetBoolValue(INI_SECTION_DEBUG, "bVRUIShowFingerTip", false);
        debug.vruiDevLayout = ini.GetBoolValue(INI_SECTION_DEBUG, "bVRUIDevLayout", false);
    }

    /**
     * Load all the config values from INI config file, override all existing values in the instance.
     * This code should be safe to run multiple times as changes are loaded from disk.
     * It runs on the file-watch thread for a change on disk and on the caller's thread for an override, so
     * the whole load is under one lock, the read of the file included: the load that read the file last is
     * then the one applied last.
     * `source` is what is loaded, see config::IniFile::Source. From `ChangedFile`, which is how the file watch
     * loads, a file that holds what the mod last loaded or saved is not loaded, and false is returned. From
     * `Kept`, which is how a set override loads, the file is not read.
     */
    bool ConfigBase::loadIniConfigValues(const config::IniFile::Source source)
    {
        std::lock_guard lock(_loadMutex);

        CSimpleIniA ini;
        const auto result = _iniFile.load(_iniFilePath, ini, source);
        if (result == config::IniFile::LoadResult::Unchanged) {
            return false;
        }
        if (result == config::IniFile::LoadResult::Failed) {
            logger::warn("Failed to load INI config file '{}'", _iniFilePath);
            throw std::runtime_error("Failed to load INI config file: " + _iniFilePath);
        }
        if (source == config::IniFile::Source::ChangedFile) {
            logger::info("INI config change detected, reload...");
        }

        _overrides.applyTo(ini);
        applyIniConfig(ini);
        return true;
    }

    /**
     * Apply an already-loaded INI to all in-memory config members (debug section, logger, and
     * the inherited mod values). Shared by loadIniConfigValues and applyIniConfigWithOverride so both
     * the on-disk reload and the in-memory live override run the exact same propagation path.
     */
    void ConfigBase::applyIniConfig(const CSimpleIniA& ini)
    {
        loadDebugSection(ini);

        // set log after loading from config
        logger::setLogLevelAndPattern(_logLevel, _logPattern);

        // let inherited class load all its values
        loadIniConfigInternal(ini);

        // for the subscribers, which are called on the game thread, see notifySubscribersOfReload
        _valuesReloaded = true;
    }

    /**
     * Re-apply the on-disk INI to all config members with one key overridden in-memory only. The
     * file is not modified, so this can run every frame to live-preview a single field without disk
     * I/O. Used by the DebugAdjuster field mode.
     */
    void ConfigBase::applyIniConfigWithOverride(const char* section, const char* key, const char* value)
    {
        std::lock_guard lock(_loadMutex);

        CSimpleIniA ini;
        if (!loadIniFromFile(ini)) {
            return;
        }
        // persistent session overrides first, then the caller's transient one-off override on top
        _overrides.applyTo(ini);
        ini.SetValue(section, key, value);
        applyIniConfig(ini);
    }

    /**
     * Get the current effective value for an arbitrary section/key as a string: an active session
     * override if one is set (see setConfigOverride), otherwise the on-disk INI value, otherwise
     * defaultValue. Returns the raw string form; the caller parses it to the type it expects.
     * Note: when the key is absent from the file the caller's defaultValue is returned, which may
     * differ from the mod's own hard-coded default used when loading the typed member.
     */
    std::string ConfigBase::getConfigValue(const char* section, const char* key, const char* defaultValue) const
    {
        if (const auto value = _overrides.find({ section, key })) {
            return *value;
        }

        CSimpleIniA ini;
        if (!loadIniFromFile(ini)) {
            return defaultValue ? defaultValue : std::string{};
        }
        return ini.GetValue(section, key, defaultValue ? defaultValue : "");
    }

    /**
     * Set an in-memory override for an arbitrary section/key for the rest of this session. The
     * override is re-applied on every config (re)load, so it survives file-watch reloads and any
     * other reload, and is never written to disk. Accepts any IniValue type (bool/int/float/string);
     * a string value is parsed by the type-appropriate getter when the member is loaded, so a string
     * can override any value. Immediately reloads the config so the typed members reflect it.
     * `owner` is the name of who sets it. An owner sets and clears only its own overrides, so several
     * callers override one config and none removes another's: the devbench tool, the mod itself, and
     * another mod through the mod's API, which passes its caller's name. When two owners override the same
     * key, the one that was set last applies, and when that one is cleared the other applies again. The
     * overrides are held by config::Overrides.
     */
    void ConfigBase::setConfigOverride(const std::string& owner, const char* section, const char* key, const config::IniValue& value)
    {
        setConfigOverrides(owner, { config::IniOverride{ section, key, value } });
    }

    /**
     * Set several session overrides of one owner with one reload of the config, see setConfigOverride.
     * The reload parses the file as the mod last loaded or saved it and reads no disk, so a value can be set
     * many times a second while it is previewed. For the same reason only a key that the owner did not
     * override before is logged at the info level and is a devbench event. A new value for a key it already
     * overrides is logged at the debug level.
     */
    void ConfigBase::setConfigOverrides(const std::string& owner, const std::vector<config::IniOverride>& overrides)
    {
        if (overrides.empty()) {
            return;
        }

        // the ones for a key that the owner did not override before this call
        const auto added = _overrides.set(owner, overrides);
        for (const auto& entry : added) {
            logger::info("Config: '{}' set session override \"{}.{} = {}\"", owner, entry.section, entry.key, entry.value);
        }
        if (logger::isDebugEnabled()) {
            for (const auto& entry : overrides) {
                logger::debug("Config: '{}' session override \"{}.{} = {}\"", owner, entry.section, entry.key, entry.value.toString());
            }
        }

        loadIniConfigValues(config::IniFile::Source::Kept);

        for (const auto& entry : added) {
            devbench::emit("config.override", [&] {
                return nlohmann::json{ { "section", entry.section }, { "key", entry.key }, { "value", entry.value }, { "owner", owner } };
            });
        }
    }

    /**
     * Remove the owner's session override for section/key and reload, so the member has another owner's
     * override of it, or its on-disk value with none. Returns whether the owner had one, and nothing is
     * reloaded when it did not.
     */
    bool ConfigBase::clearConfigOverride(const std::string& owner, const char* section, const char* key)
    {
        return clearConfigOverrides(owner, { config::IniKey{ section, key } }) > 0;
    }

    /**
     * Remove several session overrides of one owner with one reload of the config, see clearConfigOverride.
     * A key the owner does not override is skipped. Returns how many were removed, and nothing is reloaded
     * when none was.
     * The reload reads the file, where the one of a set does not. A clear is not done many times a second,
     * and the file can be newer than what the mod has: when the previewed values were saved to it right
     * before the clear, they apply at once.
     */
    std::size_t ConfigBase::clearConfigOverrides(const std::string& owner, const std::vector<config::IniKey>& keys)
    {
        const auto removed = _overrides.clear(owner, keys);
        if (removed.empty()) {
            return 0;
        }
        for (const auto& entry : removed) {
            logger::info("Config: '{}' cleared session override \"{}.{}\"", owner, entry.section, entry.key);
        }

        loadIniConfigValues();

        for (const auto& entry : removed) {
            devbench::emit("config.override", [&] {
                return nlohmann::json{ { "section", entry.section }, { "key", entry.key }, { "value", nullptr }, { "owner", owner } };
            });
        }
        return removed.size();
    }

    /**
     * Remove every session override of one owner and reload, so its members have another owner's override
     * or their on-disk value. Returns how many were removed, and nothing is reloaded when none was.
     */
    std::size_t ConfigBase::clearAllConfigOverrides(const std::string& owner)
    {
        const auto removed = _overrides.clearAll(owner);
        if (removed > 0) {
            logger::info("Config: '{}' cleared all its {} session overrides", owner, removed);
            loadIniConfigValues();
            devbench::emit("config.override", [&] {
                return nlohmann::json{ { "all", true }, { "value", nullptr }, { "owner", owner } };
            });
        }
        return removed;
    }

    /**
     * Whether a session override is currently set for section/key, by any owner.
     */
    bool ConfigBase::hasConfigOverride(const char* section, const char* key) const
    {
        return _overrides.find({ section, key }).has_value();
    }

    /**
     * The session override that applies for every overridden key, with its value in the INI string form
     * and the owner that set it.
     */
    std::vector<config::AppliedOverride> ConfigBase::getConfigOverrides() const
    {
        return _overrides.applied();
    }

    /**
     * Save the config values into the INI config file.
     * Load the file first to never lose existing values.
     */
    void ConfigBase::saveIniConfig()
    {
        CSimpleIniA ini;
        if (!loadIniFromFile(ini)) {
            return;
        }

        // let inherited class save all its values
        saveIniConfigInternal(ini);

        saveIniToFile(ini);
    }

    /**
     * Load the INI file into the given CSimpleIniA instance.
     */
    bool ConfigBase::loadIniFromFile(CSimpleIniA& ini) const
    {
        const auto rc = ini.LoadFile(_iniFilePath.c_str());
        if (rc < 0) {
            logger::warn("Failed to open INI config for saving with code: {}", rc);
            return false;
        }
        return true;
    }

    /**
     * Save the INI config to the specified ini config file.
     * The file watch does not load it back: it tells the mod's own save by the content that was written.
     */
    void ConfigBase::saveIniToFile(const CSimpleIniA& ini)
    {
        if (_iniFile.save(_iniFilePath, ini)) {
            logger::info("Config: Saving INI config successful");
        } else {
            logger::error("Config: Failed to save .ini");
        }
    }

    // The single-value overloads are thin wrappers over the batch saveIniConfigValues so there is
    // exactly one file load/save code path. The IniValue conversion happens implicitly at the call.
    void ConfigBase::saveIniConfigValue(const char* section, const char* key, const bool value)
    {
        saveIniConfigValues(section, { { key, value } });
    }

    void ConfigBase::saveIniConfigValue(const char* section, const char* key, const int value)
    {
        saveIniConfigValues(section, { { key, value } });
    }

    void ConfigBase::saveIniConfigValue(const char* section, const char* key, const float value)
    {
        saveIniConfigValues(section, { { key, value } });
    }

    void ConfigBase::saveIniConfigValue(const char* section, const char* key, const char* value)
    {
        saveIniConfigValues(section, { { key, value } });
    }

    /**
     * Save one or more key/value pairs in a single file load/save cycle. The single-value
     * saveIniConfigValue overloads all route through here, so this is the one place that does
     * the disk I/O. Values may be of any type IniValue supports (bool/int/float/string).
     */
    void ConfigBase::saveIniConfigValues(const char* section, std::initializer_list<std::pair<const char*, config::IniValue>> values)
    {
        CSimpleIniA ini;
        const SI_Error rc = ini.LoadFile(_iniFilePath.c_str());
        if (rc < 0) {
            logger::warn("Failed to save INI config values with code: {}", rc);
            return;
        }
        for (const auto& [key, value] : values) {
            value.applyTo(ini, section, key);
            logger::info("Config: Saving \"{} = {}\"", key, value.toString());
        }
        if (!_iniFile.save(_iniFilePath, ini)) {
            logger::warn("Failed to save INI config values");
        }
    }

    /**
     * Parse an NiTransform from "x,y,z;heading,roll,attitude;scale" (rotation in degrees).
     * Returns defaultValue if the key is missing or the value is malformed.
     */
    RE::NiTransform ConfigBase::getTransformValue(const CSimpleIniA& ini, const char* section, const char* key, const RE::NiTransform& defaultValue)
    {
        const char* raw = ini.GetValue(section, key, nullptr);
        if (raw == nullptr) {
            return defaultValue;
        }

        float x, y, z, heading, roll, attitude, scale;
        if (std::sscanf(raw, " %f , %f , %f ; %f , %f , %f ; %f", &x, &y, &z, &heading, &roll, &attitude, &scale) != 7) {
            logger::warn("Config: malformed transform value for '{}.{}' = '{}' (expected 'x,y,z;heading,roll,attitude;scale' in degrees). Using default.", section, key, raw);
            return defaultValue;
        }

        RE::NiTransform result;
        result.translate.x = x;
        result.translate.y = y;
        result.translate.z = z;
        result.rotate = common::MatrixUtils::getMatrixFromEulerAnglesDegrees(heading, roll, attitude);
        result.scale = scale;
        return result;
    }

    /**
     * Parse a color from "r,g,b" or "r,g,b,a", each a whole number in 0..255, where a is the opacity. Without
     * the a, the default's opacity stays.
     * Returns defaultValue if the key is missing or empty, and with a warning if the value is malformed.
     */
    render::Color ConfigBase::getColorValue(const CSimpleIniA& ini, const char* section, const char* key, const render::Color& defaultValue)
    {
        const char* raw = ini.GetValue(section, key, nullptr);
        if (raw == nullptr || *raw == '\0') {
            return defaultValue;
        }

        std::array<float, 4> channels{ defaultValue.r, defaultValue.g, defaultValue.b, defaultValue.a };
        if (!parseColor255(raw, channels)) {
            logger::warn("Config: malformed color value for '{}.{}' = '{}' (expected 'r,g,b' or 'r,g,b,a' in 0..255). Using default.", section, key, raw);
            return defaultValue;
        }
        return { channels[0], channels[1], channels[2], channels[3] };
    }

    /**
     * Parse a 22-float hand pose from the INI value at section/key.
     * Format is strict: 5 ';'-separated groups of 4 ','-separated floats (thumb, index, middle,
     * ring, pinky — each prox,mid,dist,splay), followed by 2 trailing ','-separated floats
     * (palmPitch, palmYaw). Whitespace around separators is allowed.
     * Returns defaultValue (and logs a warning) if the key is missing or the structure doesn't match.
     */
    std::array<float, 22> ConfigBase::getHandPoseValue(const CSimpleIniA& ini, const char* section, const char* key, const std::array<float, 22>& defaultValue)
    {
        const char* raw = ini.GetValue(section, key, nullptr);
        if (raw == nullptr) {
            return defaultValue;
        }

        std::array<float, 22> r{};
        const int parsed = std::sscanf(raw,
            " %f , %f , %f , %f ;" // thumb
            " %f , %f , %f , %f ;" // index
            " %f , %f , %f , %f ;" // middle
            " %f , %f , %f , %f ;" // ring
            " %f , %f , %f , %f ;" // pinky
            " %f , %f", // palmPitch, palmYaw
            &r[0],
            &r[1],
            &r[2],
            &r[3],
            &r[4],
            &r[5],
            &r[6],
            &r[7],
            &r[8],
            &r[9],
            &r[10],
            &r[11],
            &r[12],
            &r[13],
            &r[14],
            &r[15],
            &r[16],
            &r[17],
            &r[18],
            &r[19],
            &r[20],
            &r[21]);
        if (parsed != 22) {
            logger::warn(
                "Config: malformed hand pose value for '{}.{}' = '{}' "
                "(expected 'p,m,d,s;p,m,d,s;p,m,d,s;p,m,d,s;p,m,d,s;pp,py'). Using default.",
                section,
                key,
                raw);
            return defaultValue;
        }
        return r;
    }

    /**
     * Parse a controller input binding from the INI value at section/key.
     * Delegates to vrcf::parseInputBinding (see vrcf/InputBindingParser.h for the grammar).
     * Returns defaultValue (and logs a warning) if the key is missing or the value is malformed.
     */
    vrcf::InputBinding ConfigBase::getInputBindingValue(const CSimpleIniA& ini, const char* section, const char* key, const vrcf::InputBinding& defaultValue)
    {
        const char* raw = ini.GetValue(section, key, nullptr);
        if (raw == nullptr) {
            return defaultValue;
        }

        const auto parsed = vrcf::parseInputBinding(raw);
        if (!parsed) {
            logger::warn("Config: malformed input binding for '{}.{}' = '{}'. Using default.", section, key, raw);
            return defaultValue;
        }
        return *parsed;
    }

    /**
     * Read a whole activation-sphere gesture from one INI section into a f4vr::WandActivationConfig. Each key
     * falls back to the matching field of `defaults`: the zone (tZone), its optional power-armor variant
     * (tZonePA — set only when present, so WandActivationConfig::zoneFor falls back to the regular zone), the
     * two bindings (sPrimaryBinding / sSecondaryBinding — suppress is a token in the binding string, see
     * InputBindingParser), the entry + per-binding activation haptics (sEntryHaptic / sPrimaryHaptic
     * / sSecondaryHaptic — "none"/empty = silent; absent keeps the default), when the sphere
     * visual is drawn (sShowSphere — never / always / wheninside / whenavailable), how it looks and how big it is drawn
     * (sSphereStyle preset + per-value overrides incl. fSphereScale, see readSphereStyle), which way it faces
     * (sSphereOrientation — hmd / body / world), and the same for the icon: when (sShowIcon) and how it looks (sIcon,
     * sIconColor, fIconSize, see readIconStyle).
     */
    f4vr::WandActivationConfig ConfigBase::loadWandActivationConfig(const CSimpleIniA& ini, const char* section, const f4vr::WandActivationConfig& defaults)
    {
        f4vr::WandActivationConfig cfg;
        cfg.zone = getTransformValue(ini, section, "tZone", defaults.zone);
        cfg.zonePA = ini.GetValue(section, "tZonePA", nullptr) ? std::optional{ getTransformValue(ini, section, "tZonePA", cfg.zone) } : defaults.zonePA;
        cfg.primary = getInputBindingValue(ini, section, "sPrimaryBinding", defaults.primary);
        cfg.secondary = getInputBindingValue(ini, section, "sSecondaryBinding", defaults.secondary);

        const char* rawEntry = ini.GetValue(section, "sEntryHaptic", nullptr);
        cfg.entryHaptic = rawEntry ? vrcf::parseHapticPattern(rawEntry) : defaults.entryHaptic;
        const char* rawPrimaryHaptic = ini.GetValue(section, "sPrimaryHaptic", nullptr);
        cfg.primaryHaptic = rawPrimaryHaptic ? vrcf::parseHapticPattern(rawPrimaryHaptic) : defaults.primaryHaptic;
        const char* rawSecondaryHaptic = ini.GetValue(section, "sSecondaryHaptic", nullptr);
        cfg.secondaryHaptic = rawSecondaryHaptic ? vrcf::parseHapticPattern(rawSecondaryHaptic) : defaults.secondaryHaptic;

        cfg.showSphere = f4vr::parseActivationSphereVisibility(ini.GetValue(section, "sShowSphere", ""), defaults.showSphere);

        cfg.sphereStyle = readSphereStyle(ini, section, defaults.sphereStyle);
        cfg.sphereOrientation = f4vr::parseActivationSphereOrientation(ini.GetValue(section, "sSphereOrientation", ""), defaults.sphereOrientation);

        cfg.showIcon = f4vr::parseActivationSphereVisibility(ini.GetValue(section, "sShowIcon", ""), defaults.showIcon);
        cfg.iconStyle = readIconStyle(ini, section, defaults.iconStyle);
        return cfg;
    }

    /**
     * Read the on-disk transform value for an arbitrary section/key (DebugAdjuster field mode seed).
     */
    RE::NiTransform ConfigBase::readIniTransformValue(const char* section, const char* key, const RE::NiTransform& defaultValue) const
    {
        CSimpleIniA ini;
        if (!loadIniFromFile(ini)) {
            return defaultValue;
        }
        return getTransformValue(ini, section, key, defaultValue);
    }

    /**
     * Read the on-disk 22-float hand pose for an arbitrary section/key (DebugAdjuster field mode seed).
     */
    std::array<float, 22> ConfigBase::readIniHandPoseValue(const char* section, const char* key, const std::array<float, 22>& defaultValue) const
    {
        CSimpleIniA ini;
        if (!loadIniFromFile(ini)) {
            return defaultValue;
        }
        return getHandPoseValue(ini, section, key, defaultValue);
    }

    /**
     * Read the on-disk float value for an arbitrary section/key (DebugAdjuster field mode seed).
     */
    float ConfigBase::readIniFloatValue(const char* section, const char* key, const float defaultValue) const
    {
        CSimpleIniA ini;
        if (!loadIniFromFile(ini)) {
            return defaultValue;
        }
        return static_cast<float>(ini.GetDoubleValue(section, key, defaultValue));
    }

    /**
     * Current .ini file is older. Need to update it by:
     * 1. Overriding the file with the default .ini resource.
     * 2. Saving the current config values read from previous .ini to the new .ini file.
     * This preserves the user changed values, including new values and comments, and remove old values completely.
     * A backup of the previous file is created with the version number for safety.
     */
    void ConfigBase::updateIniConfigToLatestVersion(const int currentVersion, const int latestVersion) const
    {
        CSimpleIniA oldIni;
        SI_Error rc = oldIni.LoadFile(_iniFilePath.c_str());
        if (rc < 0) {
            throw std::runtime_error("Failed to load old .ini file! Error: " + std::to_string(rc));
        }

        // override the file with the default .ini resource.
        const auto tmpIniPath = std::string(_iniFilePath) + ".tmp";
        common::createFileFromResourceIfNotExists(tmpIniPath, _module, _iniDefaultConfigEmbeddedResourceId, true);

        CSimpleIniA newIni;
        rc = newIni.LoadFile(tmpIniPath.c_str());
        if (rc < 0) {
            throw std::runtime_error("Failed to load new .ini file! Error: " + std::to_string(rc));
        }

        // remove temp ini file
        auto res = std::remove(tmpIniPath.c_str());
        if (res != 0) {
            logger::warn("Failed to remove temp INI config with code: {}", res);
        }

        // update all values in the new ini with the old ini values but only if they exist in the new
        std::list<CSimpleIniA::Entry> sectionsList;
        oldIni.GetAllSections(sectionsList);
        for (const auto& section : sectionsList) {
            std::list<CSimpleIniA::Entry> keysList;
            oldIni.GetAllKeys(section.pItem, keysList);
            for (const auto& key : keysList) {
                const auto oldVal = oldIni.GetValue(section.pItem, key.pItem);
                const auto newVal = newIni.GetValue(section.pItem, key.pItem);
                if (newVal != nullptr && std::strcmp(oldVal, newVal) != 0) {
                    logger::info("Migrating {}.{} = {}", section.pItem, key.pItem, oldIni.GetValue(section.pItem, key.pItem));
                    newIni.SetValue(section.pItem, key.pItem, oldIni.GetValue(section.pItem, key.pItem));
                } else {
                    logger::debug("Skipping {}.{} ({})", section.pItem, key.pItem, newVal == nullptr ? "removed" : "unchanged");
                }
            }
        }

        // set the version to latest
        newIni.SetLongValue(INI_SECTION_DEBUG, "iVersion", latestVersion);

        updateIniConfigToLatestVersionCustom(currentVersion, latestVersion, oldIni, newIni);

        // backup the old ini file before overwriting
        auto nameStr = std::string(_iniFilePath);
        nameStr = nameStr.replace(nameStr.length() - 4, 4, "_backup_v" + std::to_string(_iniConfigVersion) + ".ini");
        res = std::rename(_iniFilePath.c_str(), nameStr.c_str());
        if (res != 0) {
            logger::warn("Failed to backup old .ini file to '{}'. Error: {}", nameStr.c_str(), res);
        }

        // save the new ini file
        rc = newIni.SaveFile(_iniFilePath.c_str());
        if (rc < 0) {
            throw std::runtime_error("Failed to save post update .ini file! Error: " + std::to_string(rc));
        }

        logger::info(".ini updated successfully");
    }

    /**
     * Load all embedded in resources offsets in the given resource range.
     */
    std::unordered_map<std::string, RE::NiTransform> ConfigBase::loadEmbeddedOffsets(const WORD fromResourceId, const WORD toResourceId)
    {
        std::unordered_map<std::string, RE::NiTransform> offsets;
        for (WORD resourceId = fromResourceId; resourceId <= toResourceId; resourceId++) {
            auto resourceOpt = common::getEmbeddedResourceAsStringIfExists(resourceId);
            if (resourceOpt.has_value()) {
                json json = json::parse(resourceOpt.value());
                loadOffsetJsonToMap(json, offsets);
            }
        }
        return offsets;
    }

    /**
     * Load offset data from given json file path and store it in the given map.
     * Use the entry key in the json file but for everything to work properly the name of the json should match the key.
     */
    void ConfigBase::loadOffsetJsonFile(const std::string& file, std::unordered_map<std::string, RE::NiTransform>& offsetsMap)
    {
        try {
            std::ifstream inF;
            inF.open(file, std::ios::in);
            if (inF.fail()) {
                logger::warn("cannot open {}", file.c_str());
                inF.close();
                return;
            }

            json weaponJson;
            try {
                inF >> weaponJson;
            } catch (json::parse_error& ex) {
                logger::info("cannot open {}: parse error at byte {}", file.c_str(), ex.byte);
                inF.close();
                return;
            }
            inF.close();

            loadOffsetJsonToMap(weaponJson, offsetsMap);
        } catch (std::exception& ex) {
            std::throw_with_nested(std::runtime_error(fmt::format("Failed to load offset json from file '{}':\n\t{}", file.c_str(), ex.what())));
        }
    }

    /**
     * Load all the offsets found in json files in a specific folder.
     */
    std::unordered_map<std::string, RE::NiTransform> ConfigBase::loadOffsetsFromFilesystem(const std::string& path)
    {
        std::unordered_map<std::string, RE::NiTransform> offsets;
        for (const auto& file : std::filesystem::directory_iterator(path)) {
            if (file.exists() && !file.is_directory()) {
                loadOffsetJsonFile(file.path().string(), offsets);
            }
        }
        return offsets;
    }

    /**
     * Save the given offsets transform to a json file using the given name.
     */
    bool ConfigBase::saveOffsetsToJsonFile(const std::string& name, const RE::NiTransform& transform, const std::string& file)
    {
        logger::info("Saving offsets '{}' to '{}'", name.c_str(), file.c_str());
        json offsetJson;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                offsetJson[name]["rotation"].push_back(transform.rotate[i][j]);
            }
        }
        offsetJson[name]["x"] = transform.translate.x;
        offsetJson[name]["y"] = transform.translate.y;
        offsetJson[name]["z"] = transform.translate.z;
        offsetJson[name]["scale"] = transform.scale;

        std::ofstream outF;
        outF.open(file, std::ios::out);
        if (outF.fail()) {
            logger::info("cannot open '{}' for writing", file.c_str());
            return false;
        }
        try {
            outF << std::setw(4) << offsetJson;
            outF.close();
            return true;
        } catch (std::exception& e) {
            outF.close();
            logger::warn("Unable to save json '{}': {}", file.c_str(), e.what());
            return false;
        }
    }

    /**
     * Setup filesystem watch on INI config file to reload config when changes are detected.
     * There can be 3-5 events fired for 1 change, sometimes the last a full second after it, and the mod's
     * own save fires them too. Every event reads the file, and it is loaded only when it holds something else
     * than the mod last loaded or saved, see config::IniFile.
     */
    void ConfigBase::startIniConfigFileWatch()
    {
        if (_iniConfigFileWatch) {
            return;
        }
        // use thread as otherwise there is a deadlock
        std::thread([this]() {
            logger::info("Start file watch in INI config '{}'", _iniFilePath.c_str());
            _iniConfigFileWatch = std::make_unique<filewatch::FileWatch<std::string>>(_iniFilePath, [this](const std::string&, const filewatch::Event changeType) {
                if (changeType != filewatch::Event::modified) {
                    return;
                }

                waitForIniFileWriteToEnd();

                if (!loadIniConfigValues(config::IniFile::Source::ChangedFile)) {
                    logger::debug("Ignore INI config file event, the file is as the mod last loaded or saved it");
                    return;
                }

                // the values are loaded, and the subscribers are called at the start of the next frame
                devbench::emit("config.reloaded", [&] {
                    return nlohmann::json{ { "file", fs::path(_iniFilePath).filename().string() }, { "trigger", "file" } };
                });
            });
        }).detach();
    }

    /**
     * Wait until the INI file was not written for a short time, so it is not read while a program still
     * writes it. Runs on the file-watch thread.
     */
    void ConfigBase::waitForIniFileWriteToEnd() const
    {
        constexpr auto delay = std::chrono::milliseconds(200);
        while (true) {
            std::error_code ec;
            const auto sinceWrite = fs::file_time_type::clock::now() - fs::last_write_time(_iniFilePath, ec);
            // a write time in the future is a clock that was set back, not a write to wait for
            if (ec || sinceWrite >= delay || sinceWrite < std::chrono::milliseconds(0)) {
                return;
            }
            std::this_thread::sleep_for(delay - sinceWrite);
        }
    }

    /**
     * Stop current file watch.
     */
    void ConfigBase::stopIniConfigFileWatch()
    {
        logger::info("Stop current file watch in INI config");
        _iniConfigFileWatch.reset();
    }
}
