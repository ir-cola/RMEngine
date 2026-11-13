// The calls into a script DLL, each inside __try / __except (structured exception handling): an access
// violation, a division by zero or a C++ exception thrown out of a script is caught here, and the
// caller stops that script only. Nothing in these functions needs unwinding (__try cannot share a
// function with C++ objects that have destructors - C2712), so they only pass pointers along.

#include "rm_engine/game_production/runtime/Scripts.h"

#include "platform/Win.h"

#include <malloc.h>  // _resetstkoflw

namespace rm::game_production {

namespace {

// After a stack overflow is caught, the stack has no guard page any more: the next one would end the
// program at once. _resetstkoflw puts it back - outside the __except block, where the stack is unwound.
unsigned long Recovered(unsigned long code) {
    if (code == EXCEPTION_STACK_OVERFLOW) _resetstkoflw();
    return code;
}

unsigned long CallHook(const RmScriptDesc& script, int hook, void* self, RmEntityId entity, float dt, RmEntityId other) {
    __try {
        switch (hook) {
            case RM_HOOK_HIT:
                if (script.onHit) script.onHit(self, entity, other);
                break;
            case RM_HOOK_TRIGGER_ENTER:
                if (script.onTriggerEnter) script.onTriggerEnter(self, entity, other);
                break;
            case RM_HOOK_TRIGGER_EXIT:
                if (script.onTriggerExit) script.onTriggerExit(self, entity, other);
                break;
            case RM_HOOK_CLICK:
                if (script.onClick) script.onClick(self, entity);
                break;
            case 0:
                if (script.beginPlay) script.beginPlay(self, entity);
                break;
            case 1:
                if (script.tick) script.tick(self, entity, dt);
                break;
            case 3:
                if (script.onReloaded) script.onReloaded(self, entity);
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

unsigned long CallInit(RmModuleInitFn init, const RmApi* api, uint32_t abi, const RmModule** out) {
    __try {
        *out = init(api, abi);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = nullptr;
        return GetExceptionCode();
    }
}

}  // namespace

unsigned long CallScriptHook(const RmScriptDesc& script, int hook, void* self, RmEntityId entity, float dt, RmEntityId other) {
    return Recovered(CallHook(script, hook, self, entity, dt, other));
}

unsigned long CallModuleInit(RmModuleInitFn init, const RmApi* api, uint32_t abi, const RmModule** out) {
    return Recovered(CallInit(init, api, abi, out));
}

}  // namespace rm::game_production
