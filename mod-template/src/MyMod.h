#pragma once

#include "Config.h"
#include "ModBase.h"

namespace my_mod
{
    class MyMod : public ModBase
    {
    public:
        MyMod()
            : ModBase(Settings(Version::PROJECT, Version::NAME, &g_config, 32, true))
        {}

    protected:
        virtual void onModLoaded(const F4SE::LoadInterface* f4SE) override;
        virtual void onGameLoaded() override;
        virtual void onGameSessionLoaded() override;
        virtual void onFrameUpdate() override;

    private:
    };

    // The ONE global to rule them ALL
    inline MyMod g_myMod;
}
