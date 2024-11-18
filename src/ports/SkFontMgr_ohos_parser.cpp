/*
 * Copyright 2024 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkFontArguments.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/core/SkTypes.h"
#include "include/private/base/SkFixed.h"
#include "include/private/base/SkTArray.h"
#include "include/private/base/SkTDArray.h"
#include "rapidjson/document.h"
#include "src/core/SkFontDescriptor.h"
#include "src/core/SkFontScanner.h"
#include "src/core/SkTHash.h"
#include "src/ports/SkFontMgr_ohos_parser.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

using namespace skia_private;

#define SK_FONTMGR_OHOS_PARSER_PREFIX "[SkFontMgr ohos Parser] "

#define SYSTEM_FONT_CONFIG_FILE "/system/etc/fontconfig.json"


static char* readFile(const char* file)
{
    FILE* fp = fopen(file, "r");
    if (fp == nullptr) {
        return nullptr;
    }
    fseek(fp, 0L, SEEK_END);
    int size = ftell(fp) + 1;
    rewind(fp);
    void* data = malloc(size);
    if (data == nullptr) {
        fclose(fp);
        return nullptr;
    }
    memset(data, 0, size);
    (void) fread(data, size, 1, fp);
    fclose(fp);
    return (char*)data;
}

static int parseAlias(const rapidjson::Value& node, skia_private::TArray<AliasInfo>& aliasSet)
{
    if (node.MemberBegin() == node.MemberEnd()) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'generic[alias]' is empty");
    }
    rapidjson::Value::ConstMemberIterator members = node.MemberBegin();
    const char* key = members[0].name.GetString();
    if (!node[key].IsInt()) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "generic[alias][%s] should be an integer", key);
    }
    AliasInfo info = {SkString(key), node[key].GetInt()};
    aliasSet.emplace_back(std::move(info));
    return 0;
}

static int parseAdjust(const rapidjson::Value& node, skia_private::TArray<AdjustInfo>& adjustSet)
{
    const char* tags[] = {"weight", "to"};
    int values[2]; // value[0] - to save 'weight', value[1] - to save 'to'
    for (unsigned int i = 0; i < sizeof(tags) / sizeof(char*); i++) {
        const char* key = tags[i];
        if (!node.HasMember(key)) {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "missing key '%s'", key);
            return -1;
        } else if (!node[key].IsInt()) {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an integer", key);
            return -1;
        } else {
            values[i] = node[key].GetInt();
        }
    }
    AdjustInfo info = {values[0], values[1]};
    adjustSet.emplace_back(info);
    return 0;
}

void SkFontMgr_ohos_Parser::GetSystemFontConfig(FontConfig& fontConfig) {
    char* data = readFile(SYSTEM_FONT_CONFIG_FILE);
    if (data == nullptr) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' could not be opened", SYSTEM_FONT_CONFIG_FILE);
        return;
    }
    SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "Parsing '%s' with content:", SYSTEM_FONT_CONFIG_FILE);
    SkDebugf("%s", data);
    rapidjson::Document document;
    if (document.Parse(data).HasParseError()) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' format is not supported", SYSTEM_FONT_CONFIG_FILE);
        return;
    }
    free((void*)data);
    data = nullptr;
    if (!document.IsObject()) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an json object", SYSTEM_FONT_CONFIG_FILE);
        return;
    }
    auto root = document.GetObject();
    // "fontdir" - optional, the data type should be string
    const char* key = "fontdir";
    if (root.HasMember(key)) {
        if (root[key].IsArray()) {
           auto node = root[key].GetArray();
            for (unsigned int i = 0; i < node.Size(); i++) {
                if (node[i].IsString()) {
                    const char* dir = node[i].GetString();
                    fontConfig.fontdirSet.emplace_back(SkString(dir));
                } else {
                    SkString text;
                    text.appendf("fontdir#%d", i + 1);
                    SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be a string", text.c_str());
                    return;
                }
            }
        } else {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an array", key);
            return;
        }
    }
    THashMap<SkString, TArray<AliasInfo>> aliasMap;
    THashMap<SkString, TArray<AdjustInfo>> adjustMap;

    // "generic" - necessary, the data type should be array
    const char* genericKey = "generic";
    if (!root.HasMember(genericKey)) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "missing key '%s'", genericKey);
        return;
    } else if (!root[key].IsArray()) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an array", genericKey);
    }
    auto genericArray = root[genericKey].GetArray();
    for (unsigned int index = 0; index < genericArray.Size(); index++) {
        auto genericNode = genericArray[index].GetObject();
        // "family" - necessary, the data type should be String
        const char* key = "family";
        if (!genericNode.HasMember(key)) {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "missing key '%s'", key);
            return;
        } else if (!genericNode[key].IsString()) {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be a string", key);
            return;
        }
        SkString familyName = SkString(genericNode[key].GetString());
        // "alias" - necessary, the data type should be Array
        if (!genericNode.HasMember("alias")) {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "missing key 'alias'");
            return;
        }
        // "adjust", "variation" - optional
        const char* tags[] = {"alias", "adjust"};
        TArray<AliasInfo> aliasSet;
        TArray<AdjustInfo> adjustSet;
        for (unsigned int i = 0; i < sizeof(tags) / sizeof(char*); i++) {
            key = tags[i];
            if (!genericNode.HasMember(key)) {
                continue;
            }
            if (genericNode[key].IsArray()) {
                if (!strcmp(key, "alias")) {
                    auto aliasArray = genericNode[key].GetArray();
                    for (unsigned int j = 0; j < aliasArray.Size(); j++) {
                        if (aliasArray[j].IsObject()) {
                            parseAlias(aliasArray[j], aliasSet);
                        } else {
                            SkString text;
                            text.appendf("%s#%d", key, j + 1);
                            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an object", text.c_str());
                        }
                    }
                } else if (!strcmp(key, "adjust")) {
                    auto adjustArray = genericNode[key].GetArray();
                    for (unsigned int j = 0; j < adjustArray.Size(); j++) {
                        if (adjustArray[j].IsObject()) {
                            parseAdjust(adjustArray[j], adjustSet);
                        } else {
                            SkString text;
                            text.appendf("%s#%d", key, j + 1);
                            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an object", text.c_str());
                        }
                    }
                }
            } else {
                SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an array", key);
            }
        }
        if (aliasSet.size()) {
            aliasMap.set(SkString(familyName), aliasSet);
        }
        if (adjustSet.size()) {
            adjustMap.set(SkString(familyName), adjustSet);
        }
        FontGeneric generic = {familyName, aliasSet, adjustSet};
        fontConfig.genericSet.emplace_back(generic);
    }

    fontConfig.aliasMap = aliasMap;
    fontConfig.adjustMap = adjustMap;

    // "fallback" - necessary, the data type should be array
    const char* fallbackKey = "fallback";
    if (!root.HasMember(fallbackKey)) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "missing key '%s'", fallbackKey);
        return;
    } else if (!root[fallbackKey].IsArray()) {
        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an array", fallbackKey);
        return;
    }
    auto fallbackArray = root[fallbackKey].GetArray();
    for (unsigned int index = 0; index < fallbackArray.Size(); index++) {
        auto fallbackNode = fallbackArray[index].GetObject();
        rapidjson::Value::ConstMemberIterator members = fallbackNode.MemberBegin();
        const char* forKey = members[0].name.GetString();
        if (!fallbackNode[forKey].IsArray()) {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "fallback items should be an array");
            return;
        }
        TArray<FallbackInfo> fallbackSet;
        if (fallbackNode[forKey].IsArray()) {
            auto fallabckSubArray = fallbackNode[forKey].GetArray();
            for (unsigned int j = 0; j < fallabckSubArray.Size(); j++) {
                if (!fallabckSubArray[j].IsObject()) {
                    SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "fallback item should be an object");
                    continue;
                }
                auto fallbackSubNode = fallabckSubArray[j].GetObject();
                rapidjson::Value::ConstMemberIterator subMembers = fallbackSubNode.MemberBegin();
                for (unsigned int k = 0; k < fallbackSubNode.MemberCount(); k++) {

                    const char* langKey = subMembers[k].name.GetString();
                    SkString lang = SkString(langKey);
                    if (fallbackSubNode[langKey].IsString()) {
                        SkString familyName = SkString(fallbackSubNode[langKey].GetString());
                        fallbackSet.emplace_back(FallbackInfo{lang, familyName});
                        fontConfig.fallbackNames.add(familyName);
                    } else {
                        SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be a string", langKey);
                    }
                }
            }
        } else {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "'%s' should be an array", forKey);
        }
        FontFallback fallback = {SkString(forKey), fallbackSet};
        fontConfig.fallbackSet.emplace_back(fallback);
    }
};


/*! To scan the system font dirs
 * \param dirs the font dirs to scan font files
 * \param files the font files to be scanned
 */
void SkFontMgr_ohos_Parser::GetSystemFontFiles(TArray<SkString>& dirs, TArray<SkString>& files)
{
    if (dirs.size() == 0) {
        dirs.emplace_back(SkString("/system/fonts/"));
    }
    for (int i = 0; i < dirs.size(); i++) {
        DIR* dir = opendir(dirs[i].c_str());
        if (dir == nullptr) {
            SkDebugf(SK_FONTMGR_OHOS_PARSER_PREFIX "Directory '%s' does not exist", dirs[i].c_str());
            continue;
        }
        struct dirent* node = nullptr;
        while ((node = readdir(dir))) {
            if (node->d_type != DT_REG) {
                continue;
            }
            const char* fname = node->d_name;
            int len = strlen(fname);
            int suffixLen = strlen(".ttf");
            if (len < suffixLen || (strncmp(fname + len - suffixLen, ".ttf", suffixLen) &&
                strncmp(fname + len - suffixLen, ".otf", suffixLen) &&
                strncmp(fname + len - suffixLen, ".ttc", suffixLen) &&
                strncmp(fname + len - suffixLen, ".otc", suffixLen))) {
                continue;
            }
            len += (dirs[i].size() + 2); // 2 more characters for '/' and '\0'
            char fullname[len];
            memset(fullname,  0, len);
            strncpy(fullname, dirs[i].c_str(), len);
            if (dirs[i][dirs[i].size() - 1] != '/') {
                strcat(fullname, "/");
            }
            strcat(fullname, fname);
            files.push_back(SkString(fullname));
        }
        closedir(dir);
    }
    dirs.clear();
}
