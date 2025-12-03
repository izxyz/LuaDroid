//
// Created by 西曰祯 on 2025/11/27.
//

#ifndef LUADROID_LOGGER_H
#define LUADROID_LOGGER_H

#include <android/log.h>

#define LOGE(msg, ...) __android_log_print(ANDROID_LOG_ERROR,"xiyuezhen",msg,##__VA_ARGS__)

#endif //LUADROID_LOGGER_H
