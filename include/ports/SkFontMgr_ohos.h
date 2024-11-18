/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkFontMgr_ohos_DEFINED
#define SkFontMgr_ohos_DEFINED

#include "include/core/SkRefCnt.h"

class SkFontMgr;
class SkFontScanner;

SK_API sk_sp<SkFontMgr> SkFontMgr_New_OHOS(const char* fontconfig);

#endif  // SkFontMgr_ohos_DEFINED
