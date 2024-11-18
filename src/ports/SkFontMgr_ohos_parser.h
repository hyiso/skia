/*
 * Copyright 2024 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkFontMgr_ohos_parser_DEFINED
#define SkFontMgr_ohos_parser_DEFINED

#include "include/core/SkFontArguments.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkString.h"
#include "include/core/SkTypes.h"
#include "include/private/base/SkFixed.h"
#include "include/private/base/SkTArray.h"
#include "include/private/base/SkTDArray.h"
#include "src/core/SkFontScanner.h"
#include "src/core/SkTHash.h"
#include "src/ports/SkTypeface_FreeType.h"

#include <climits>
#include <limits>

using namespace skia_private;

struct AdjustInfo {
    int weight; // the real value of the font weight
    int to; // the specified value of weight for a font
};

struct AliasInfo {
    SkString name; // the alias name of generic family
    int weight; // the weight of the font style set. 0 means no specified weight
};

struct FontGeneric {
    SkString family;
    TArray<AliasInfo> aliasSet;
    TArray<AdjustInfo> adjustSet;
};

struct FallbackInfo {
    SkString lang;
    SkString familyName;
};

struct FontFallback {
    SkString fallbackFor;
    TArray<FallbackInfo> fallbackSet;
};


struct FontConfig {
    TArray<SkString> fontdirSet;
    TArray<FontGeneric> genericSet;
    TArray<FontFallback> fallbackSet;
    THashMap<SkString, TArray<AliasInfo>> aliasMap;
    THashMap<SkString, TArray<AdjustInfo>> adjustMap;
    THashSet<SkString> fallbackNames;
};

namespace SkFontMgr_ohos_Parser {

/** Parses system font configuration files and appends result to fontFamilies. */
void GetSystemFontConfig(FontConfig& fontConfig);
void GetSystemFontFiles(TArray<SkString>& dirs, TArray<SkString>& files);

}  // namespace SkFontMgr_ohos_Parser

#endif /* SkFontMgr_android_parser_DEFINED */
