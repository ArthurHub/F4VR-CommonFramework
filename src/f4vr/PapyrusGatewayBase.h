#pragma once

namespace f4cf::f4vr
{
    /**
     * A way to execute Papyrus functions from C++ code.
     * Extend to create a custom gateway for your mod.
     * Only one instance of this class can be used.
     *
     * Requires 2 papyrus scripts in the plugging. One with a native "RegisterPapyrusGatewayScript" function that is called by
     * the second script to register itself for the gateway access. The second script contains the actual functions that can be called from C++ code.
     *
     */
    class PapyrusGatewayBase
    {
    public:
        explicit PapyrusGatewayBase(std::string registerScriptClassName)
            : _registerScriptClassName(std::move(registerScriptClassName))
        {
            if (_instance && _instance != this) {
                throw std::exception("Papyrus Gateway is already initialized, only single instance can be used!");
            }
            _instance = this;

            // Register a native papyrus function that is called from a papyrus script to register for gateway access.
            // I couldn't find a way to get the script object handle so this is a workaround for the papyrus script to send its handle.
            const auto vm = RE::GameVM::GetSingleton()->GetVM();
            vm->BindNativeMethod(_registerScriptClassName, "RegisterPapyrusGatewayScript", onRegisterGatewayScript);
        }

        virtual ~PapyrusGatewayBase()
        {
            _instance = nullptr;
        }

    protected:
        /**
         * On receiving a papyrus script registration call, we store the script handle and name for later to call scripts in it.
         */
        static void onRegisterGatewayScript(std::monostate, const RE::BSTSmartPointer<RE::BSScript::Object> scriptObj)
        {
            const auto typeInfo = scriptObj ? scriptObj->GetTypeInfo() : nullptr;
            if (typeInfo && _instance) {
                const std::string scriptName = typeInfo->GetName();
                const std::uint64_t scriptHandle = scriptObj->GetHandle();
                logger::info("Register for Papyrus gateway by Script:'{}' Handle:({})", scriptName, scriptHandle);
                _instance->_scriptHandle = scriptHandle;
                _instance->_scriptName = scriptName;
            } else {
                logger::error("Papyrus Gateway instance is not set or scriptObj is null");
            }
        }

        /**
         * Execute the given papyrus function on the registered script object with the given arguments, which must match its
         * Papyrus declaration (bool, int, float, string, arrays as std::vector). The call is queued, not waited on.
         */
        template <class... Args>
        void executePapyrusScript(const char* functionName, Args... args) const
        {
            if (_scriptHandle == 0) {
                logger::error("No registered gateway script handle found, Papyrus script missing?");
                return;
            }

            logger::debug("Calling papyrus function '{}' on script '{}'", functionName, _scriptName);
            const auto vm = RE::GameVM::GetSingleton()->GetVM();
            if (!vm->DispatchMethodCall(_scriptHandle, _scriptName.c_str(), functionName, nullptr, args...)) {
                logger::error("Failed to call papyrus function '{}' on script '{}' ({})", functionName, _scriptName, _scriptHandle);
            }
        }

        std::string _registerScriptClassName;

        // the handle to the script object to call its papyrus functions
        std::uint64_t _scriptHandle = 0;

        // the script name (object type) as received from registration call
        std::string _scriptName;

        // workaround as papyrus registration requires global functions.
        inline static PapyrusGatewayBase* _instance = nullptr;
    };
}
