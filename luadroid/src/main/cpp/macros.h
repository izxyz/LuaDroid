//
// Created by Karven on 2018/8/24.
//

#ifndef LUADROID_MACROS_H
#define LUADROID_MACROS_H

#include <android/log.h>
#ifdef __cplusplus
// Use standard C++ library instead of custom stdcpp
#include <functional>
#include <initializer_list>
#include <memory>  // for std::unique_ptr
#include "MString.h"  // Keep custom MString
#endif

#define likely(x)    __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect((x),0)
#ifdef NDEBUG
#define LOGE(msg, ...)
#else
#define LOGE(msg, ...) __android_log_print(ANDROID_LOG_ERROR,"xiyuezhen",msg,##__VA_ARGS__)
#endif

#endif //LUADROID_MACROS_H
