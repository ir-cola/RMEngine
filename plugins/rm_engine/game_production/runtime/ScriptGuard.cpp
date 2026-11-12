// The calls into a script DLL, each inside __try / __except (structured exception handling): an access
// violation, a division by zero or a C++ exception thrown out of a script is caught here, and the
// caller stops that script only. Nothing in these functions needs unwinding (__try cannot share a
// function with C++ objects that have destructors - C2712), so they only pass pointers along.

#include "rm_engine/game_production/runtime/Scripts.h"

#include "platform/Win.h"

namespace rm::game_production {

unsigned long CallScriptHook(const RmScriptDesc& script, int hook, void* self, RmEntityId entity, float dt) {
    __try {
        switch (hook) {
            case 0:
                if (script.beginPlay) script.beginPlay(self, entity);
                break;
            case 1:
                if (script.tick) script.tick(self, entity, dt);
                break;
            default:
                if (script.endPlay) script.endPlay(self, entity);
                break;
        }
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

unsigned long CallModuleInit(RmModuleInitFn init, const RmApi* api, uint32_t abi, const RmModule** out) {
    __try {
        *out = init(api, abi);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = nullptr;
        return GetExceptionCode();
    }
}

}  // namespace rm::game_production
