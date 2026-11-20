// スクリプト DLL への呼び出し。どれも __try / __except (構造化例外処理) の中: アクセス違反、
// ゼロ除算、スクリプトから投げられた C++ の例外は、ここで受け止め、呼んだ側がそのスクリプト
// だけを止める。これらの関数には巻き戻しが要るものは無い (__try は、デストラクタを持つ C++
// オブジェクトと同じ関数に置けない - C2712) ので、ポインターを渡すだけにしている。

#include "rm_engine/game_production/runtime/Scripts.h"

#include "platform/Win.h"

#include <malloc.h>  // _resetstkoflw

namespace rm::game_production {

namespace {

// スタックオーバーフローを受け止めたあとは、スタックにガードページが無くなる: 次の 1 回で
// プログラムがすぐに終わってしまう。_resetstkoflw がそれを戻す - スタックが巻き戻された、
// __except ブロックの外で。
unsigned long Recovered(unsigned long code) {
    if (code == EXCEPTION_STACK_OVERFLOW) _resetstkoflw();
    return code;
}

unsigned long CallHook(const RmScriptDesc& script, int hook, void* self, RmEntityId entity, float dt, RmEntityId other, const RmEvent* event) {
    __try {
        switch (hook) {
            case RM_HOOK_EVENT:
                if (script.onEvent && event) script.onEvent(self, entity, event);
                break;
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

unsigned long CallResume(int (*resume)(void*, RmWait*), void* frame, RmWait* next, int* alive) {
    __try {
        *alive = resume(frame, next);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *alive = 0;
        return GetExceptionCode();
    }
}

unsigned long CallDestroy(void (*destroy)(void*), void* frame) {
    __try {
        destroy(frame);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

unsigned long CallConstruct(void (*construct)(void*), void* p) {
    __try {
        construct(p);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

unsigned long CallDestruct(void (*destruct)(void*), void* p) {
    __try {
        destruct(p);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

unsigned long CallCopy(void (*copy)(void*, const void*), void* dst, const void* src) {
    __try {
        copy(dst, src);
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

unsigned long CallScriptHook(const RmScriptDesc& script, int hook, void* self, RmEntityId entity, float dt, RmEntityId other,
                             const RmEvent* event) {
    return Recovered(CallHook(script, hook, self, entity, dt, other, event));
}

unsigned long CallCoroutineResume(int (*resume)(void*, RmWait*), void* frame, RmWait* next, int* alive) {
    return Recovered(CallResume(resume, frame, next, alive));
}

unsigned long CallCoroutineDestroy(void (*destroy)(void*), void* frame) { return Recovered(CallDestroy(destroy, frame)); }

unsigned long CallScriptConstruct(void (*construct)(void*), void* p) { return Recovered(CallConstruct(construct, p)); }
unsigned long CallScriptDestruct(void (*destruct)(void*), void* p) { return Recovered(CallDestruct(destruct, p)); }
unsigned long CallScriptCopy(void (*copy)(void*, const void*), void* dst, const void* src) { return Recovered(CallCopy(copy, dst, src)); }

unsigned long CallModuleInit(RmModuleInitFn init, const RmApi* api, uint32_t abi, const RmModule** out) {
    return Recovered(CallInit(init, api, abi, out));
}

}  // namespace rm::game_production
