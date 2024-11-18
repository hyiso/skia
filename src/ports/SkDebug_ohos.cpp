/*
 * Copyright 2024 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/base/SkDebug.h"
#include "include/private/base/SkFeatures.h"

#if defined(SK_BUILD_FOR_OHOS)

#include <hilog/log.h>
#include <stdio.h>

static const size_t kBufferSize = 2048;

void SkDebugf(const char format[], ...) {
    char buffer[kBufferSize + 1];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(buffer, kBufferSize, format, args);
    (void)OH_LOG_Print(LOG_APP, LOG_DEBUG, 0, "Skia", "%{public}s", buffer);
    va_end(args);
}

#endif  // defined(SK_BUILD_FOR_OHOS)
