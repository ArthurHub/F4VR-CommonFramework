#include "UIHandPointing.h"

#include "ModBase.h"

#include <Windows.h>

namespace f4cf::vrui
{
    // FRIK draws and poses the player's hands, and its API is how another mod asks for a hand pose. The API is a
    // table of function pointers, read from the FRIK.dll loaded in the game. Only the entries that point a hand are
    // called, so each table is declared up to the last of them, with the entries in between as placeholders.
    // The tables are declared here under names of their own, and are not FRIK's client headers: a mod copies those
    // into its own source, and one DLL holding them twice would share one API instance between the two copies.
    namespace
    {
        using FRIKUnusedEntry = void (*)();

        enum class FRIKHand : std::uint8_t
        {
            Primary,
            Offhand,
        };

        enum class FRIKHandPoseKind : std::uint8_t
        {
            Pointing = 3,
        };

        /**
         * The start of FRIK's first API table, as it is laid out from its version 4.
         */
        struct FRIKApiTable
        {
            std::uint32_t(__cdecl* getVersion)();
            const char*(__cdecl* getModVersion)();
            FRIKUnusedEntry unused1[9];
            bool(__cdecl* setHandPose)(const char* tag, FRIKHand hand, FRIKHandPoseKind handPose);
            FRIKUnusedEntry unused2[1];
            bool(__cdecl* clearHandPose)(const char* tag, FRIKHand hand);
        };

        /**
         * The start of FRIK's second API table, as it is laid out from its version 2. From that version FRIK only
         * adds entries at the end. A hand pose takes a priority here.
         */
        struct FRIKApiV2Table
        {
            std::uint32_t(__cdecl* getVersion)();
            const char*(__cdecl* getModVersion)();
            FRIKUnusedEntry unused1[9];
            bool(__cdecl* setHandPose)(const char* tag, FRIKHand hand, FRIKHandPoseKind handPose, int priority);
            FRIKUnusedEntry unused2[4];
            bool(__cdecl* clearHandPose)(const char* tag, FRIKHand hand);
        };

        static_assert(offsetof(FRIKApiTable, setHandPose) == 11 * sizeof(FRIKUnusedEntry) && offsetof(FRIKApiTable, clearHandPose) == 13 * sizeof(FRIKUnusedEntry));
        static_assert(offsetof(FRIKApiV2Table, setHandPose) == 11 * sizeof(FRIKUnusedEntry) && offsetof(FRIKApiV2Table, clearHandPose) == 16 * sizeof(FRIKUnusedEntry));

        constexpr std::uint32_t FRIK_API_MIN_VERSION = 4;
        constexpr std::uint32_t FRIK_API_V2_MIN_VERSION = 2;

        // FRIK's priority for a hand pose with no stronger claim is 50. Pointing at the UI goes above it, to win
        // over a pose the mod itself holds on that hand.
        constexpr int POINTING_PRIORITY = 60;

        struct FRIKHandPoseApi
        {
            const FRIKApiV2Table* apiV2 = nullptr;
            const FRIKApiTable* api = nullptr;

            // one per mod, so a mod that releases its pointing leaves the pointing of another mod's UI
            std::string tag;
        };

        /**
         * Find the FRIK API to point a hand through: the second table for its priority, else the first.
         * Both stay empty without FRIK, and with a FRIK from before these versions of the tables.
         */
        FRIKHandPoseApi findFRIKHandPoseApi()
        {
            FRIKHandPoseApi frik;
            frik.tag = g_mod->getName() + "_UI";

            const auto frikDll = GetModuleHandleA("FRIK.dll");
            if (!frikDll) {
                logger::info("FRIK is not installed, the hand is not pointed at the UI");
                return frik;
            }

            const auto getApiV2 = reinterpret_cast<const FRIKApiV2Table*(__cdecl*)()>(GetProcAddress(frikDll, "FRIKAPI_V2_GetApi"));
            const auto getApiV2Size = reinterpret_cast<std::uint32_t(__cdecl*)()>(GetProcAddress(frikDll, "FRIKAPI_V2_GetApiStructSize"));
            if (getApiV2 && getApiV2Size) {
                const auto apiV2 = getApiV2();
                if (apiV2 && apiV2->getVersion() >= FRIK_API_V2_MIN_VERSION && getApiV2Size() >= sizeof(FRIKApiV2Table)) {
                    logger::info("The hand is pointed at the UI through FRIK (v{}) API v2 (v{})", apiV2->getModVersion(), apiV2->getVersion());
                    frik.apiV2 = apiV2;
                    return frik;
                }
            }

            const auto getApi = reinterpret_cast<const FRIKApiTable*(__cdecl*)()>(GetProcAddress(frikDll, "FRIKAPI_GetApi"));
            if (getApi) {
                const auto api = getApi();
                if (api && api->getVersion() >= FRIK_API_MIN_VERSION) {
                    logger::info("The hand is pointed at the UI through FRIK (v{}) API (v{})", api->getModVersion(), api->getVersion());
                    frik.api = api;
                    return frik;
                }
            }

            logger::warn("FRIK is too old to point the hand at the UI: its API is older than v{}", FRIK_API_MIN_VERSION);
            return frik;
        }
    }

    /**
     * Point the given hand, its index finger out, or release it. It is done through FRIK, which is looked for
     * on the first call. Without FRIK, or with one too old, nothing is done.
     */
    void setFRIKHandPointing(const bool primaryHand, const bool toPoint)
    {
        static const FRIKHandPoseApi frik = findFRIKHandPoseApi();

        const auto hand = primaryHand ? FRIKHand::Primary : FRIKHand::Offhand;
        if (frik.apiV2) {
            if (toPoint) {
                frik.apiV2->setHandPose(frik.tag.c_str(), hand, FRIKHandPoseKind::Pointing, POINTING_PRIORITY);
            } else {
                frik.apiV2->clearHandPose(frik.tag.c_str(), hand);
            }
        } else if (frik.api) {
            if (toPoint) {
                frik.api->setHandPose(frik.tag.c_str(), hand, FRIKHandPoseKind::Pointing);
            } else {
                frik.api->clearHandPose(frik.tag.c_str(), hand);
            }
        }
    }
}
