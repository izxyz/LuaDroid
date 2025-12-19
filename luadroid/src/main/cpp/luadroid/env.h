//
// Created by 西曰祯 on 2025/11/30.
//

#ifndef LUADROID_ENV_H
#define LUADROID_ENV_H

#include "common.h"
#include "tls.h"

inline TJNIEnv *getThreadEnv() {
    static ThreadLocal<TJNIEnv> threadEnvCache;
    TJNIEnv* env = threadEnvCache.get();
    if (likely(env != nullptr)) {
        return env;
    }
    int status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_4);
    if (unlikely(status == JNI_EDETACHED)) {
        status = vm->AttachCurrentThread(reinterpret_cast<JNIEnv **>(&env), nullptr);
    }
    if (likely(status == JNI_OK)) {
        threadEnvCache.rawSet(env);
        return env;
    }
    return nullptr;
}

#endif //LUADROID_ENV_H
