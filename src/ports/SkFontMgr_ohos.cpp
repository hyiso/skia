/*
 * Copyright 2024 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/core/SkTypes.h"
#include "include/ports/SkFontMgr_ohos.h"
#include "include/private/base/SkFixed.h"
#include "include/private/base/SkTArray.h"
#include "include/private/base/SkTDArray.h"
#include "include/private/base/SkTemplates.h"
#include "src/base/SkTSearch.h"
#include "src/core/SkFontDescriptor.h"
#include "src/core/SkFontScanner.h"
#include "src/core/SkOSFile.h"
#include "src/core/SkTypefaceCache.h"
#include "src/ports/SkFontMgr_ohos_parser.h"
#include "src/ports/SkTypeface_FreeType.h"

#include <algorithm>
#include <limits>

using namespace skia_private;

class SkData;

namespace {

class SkTypeface_ohos : public SkTypeface_FreeType {
public:
    SkTypeface_ohos(const SkString& pathName,
                    int index,
                    const SkFixed* axes, int axesCount,
                    const SkFontStyle& style,
                    bool isFixedPitch,
                    const SkString& familyName)
        : INHERITED(style, isFixedPitch)
        , fFamilyName(familyName)
        , fPathName(pathName)
        , fIndex(index)
        , fAxes(axes, axesCount)
        { }

    void onGetFontDescriptor(SkFontDescriptor* desc, bool* serialize) const override {
        SkASSERT(desc);
        SkASSERT(serialize);
        desc->setFamilyName(fFamilyName.c_str());
        desc->setStyle(this->fontStyle());
        desc->setFactoryId(SkTypeface_FreeType::FactoryId);
        *serialize = false;
    }

    std::unique_ptr<SkStreamAsset> makeStream() const {
        return SkStream::MakeFromFile(fPathName.c_str());
    }
    std::unique_ptr<SkStreamAsset> onOpenStream(int* ttcIndex) const override {
        *ttcIndex = fIndex;
        return this->makeStream();
    }
    std::unique_ptr<SkFontData> onMakeFontData() const override {
        return std::make_unique<SkFontData>(
                this->makeStream(), fIndex, 0, fAxes.begin(), fAxes.size(), nullptr, 0);
    }

    sk_sp<SkTypeface> onMakeClone(const SkFontArguments& args) const override {
        SkFontStyle style = this->fontStyle();
        std::unique_ptr<SkFontData> data = this->cloneFontData(args, &style);
        if (!data) {
            return nullptr;
        }
        return sk_make_sp<SkTypeface_ohos>(fPathName,
                                           fIndex,
                                           data->getAxis(),
                                           data->getAxisCount(),
                                           style,
                                           this->isFixedPitch(),
                                           fFamilyName);
    }
protected:
    void onGetFamilyName(SkString* familyName) const override {
        *familyName = fFamilyName;
    }

    SkString fFamilyName;

private:
    using INHERITED = SkTypeface_FreeType;
    const SkString fPathName;
    const int fIndex;
    const STArray<4, SkFixed, true> fAxes;
};

template <typename D, typename S> sk_sp<D> sk_sp_static_cast(sk_sp<S>&& s) {
    return sk_sp<D>(static_cast<D*>(s.release()));
}

class SkFontStyleSet_ohos : public SkFontStyleSet {
public:
    explicit SkFontStyleSet_ohos(TArray<sk_sp<SkTypeface_ohos>>& typefaces)
        : fStyles(typefaces) {
    }

    int count() override {
        return fStyles.size();
    }
    void getStyle(int index, SkFontStyle* style, SkString* name) override {
        if (index < 0 || fStyles.size() <= index) {
            return;
        }
        if (style) {
            *style = fStyles[index]->fontStyle();
        }
        if (name) {
            name->reset();
        }
    }
    sk_sp<SkTypeface> createTypeface(int index) override {
        if (index < 0 || fStyles.size() <= index) {
            return nullptr;
        }
        return fStyles[index];
    }

    sk_sp<SkTypeface_ohos> matchAStyle(const SkFontStyle& pattern) {
        return sk_sp_static_cast<SkTypeface_ohos>(this->matchStyleCSS3(pattern));
    }

    sk_sp<SkTypeface> matchStyle(const SkFontStyle& pattern) override {
        return this->matchAStyle(pattern);
    }

private:
    TArray<sk_sp<SkTypeface_ohos>> fStyles;

    using INHERITED = SkFontStyleSet;
};

struct NameToFamily {
    SkString name;
    SkFontStyleSet_ohos* styleSet;
};

struct FallbackNameToFamily {
    SkString name;
    SkString lang;
    SkString fallbackFor;
    SkFontStyleSet_ohos* styleSet;
};

/*!
 * \brief To implement the SkFontMgr for ohos platform
 */
class SkFontMgr_OHOS : public SkFontMgr {
public:
    explicit SkFontMgr_OHOS() {
        FontConfig fontConfig;
        SkFontMgr_ohos_Parser::GetSystemFontConfig(fontConfig);
        skia_private::TArray<SkString> files;
        SkFontMgr_ohos_Parser::GetSystemFontFiles(fontConfig.fontdirSet, files);

        THashMap<SkString, TArray<sk_sp<SkTypeface_ohos>>> genericTypefacesMap;
        THashMap<SkString, TArray<sk_sp<SkTypeface_ohos>>> fallbackTypefacesMap;
        for (int i = 0; i < files.size(); i++) {
            SkString filename = files[i];
            std::unique_ptr<SkStreamAsset> stream = SkStream::MakeFromFile(filename.c_str());
            if (!stream) {
                // SkDebugf("---- failed to open <%s>\n", filename.c_str());
                continue;
            }

            int numFaces;
            if (!scanner.scanFile(stream.get(), &numFaces)) {
                // SkDebugf("---- failed to open <%s> as a font\n", filename.c_str());
                continue;
            }

            for (int faceIndex = 0; faceIndex < numFaces; faceIndex++) {
                int numInstances;
                if (!scanner.scanFace(stream.get(), faceIndex, &numInstances)) {
                    // SkDebugf("---- failed to open <%s> as a font\n", filename.c_str());
                    continue;
                }
                for (int instanceIndex = 0; instanceIndex <= numInstances; ++instanceIndex) {
                    bool isFixedPitch;
                    SkString realname;
                    SkFontStyle style = SkFontStyle(); // avoid uninitialized warning
                    SkFontScanner::AxisDefinitions axisDefs;
                    if (!scanner.scanInstance(stream.get(),
                                               faceIndex,
                                               instanceIndex,
                                               &realname,
                                               &style,
                                               &isFixedPitch,
                                               &axisDefs)) {
                        // SkDebugf("---- failed to open <%s> <%d> as a font\n",
                        //          filename.c_str(), faceIndex);
                        continue;
                    }
                    if (fontConfig.aliasMap.find(realname) == nullptr &&
                        fontConfig.fallbackNames.find(realname) == nullptr) {
                        continue;
                    }
                    // for adjustMap - update weight
                    if (fontConfig.adjustMap.find(realname) != nullptr) {
                        TArray<AdjustInfo>& adjustSet = fontConfig.adjustMap[realname];
                        for (int k = 0; k < adjustSet.size(); k++) {
                            if (style.weight() == adjustSet[k].weight) {
                                style = SkFontStyle(adjustSet[k].to, style.width(), style.slant());
                                break;
                            }
                        }
                    }
                    int index =  (instanceIndex << 16) + faceIndex;
                    if (fontConfig.aliasMap.find(realname) != nullptr) {
                        TArray<sk_sp<SkTypeface_ohos>>& typefaceSet = genericTypefacesMap[realname];
                        typefaceSet.push_back(sk_make_sp<SkTypeface_ohos>(
                                filename, index, nullptr, 0,
                                style, isFixedPitch, realname));
                    } else {
                        TArray<sk_sp<SkTypeface_ohos>>& typefaceSet = fallbackTypefacesMap[realname];
                        typefaceSet.push_back(sk_make_sp<SkTypeface_ohos>(
                                filename, index, nullptr, 0,
                                style, isFixedPitch, realname));
                    }
                }
            }
        }
        for (int i = 0; i < fontConfig.genericSet.size(); i++) {
            const FontGeneric& generic = fontConfig.genericSet[i];
            if (genericTypefacesMap.find(generic.family) == nullptr) {
                SkDebugf("SkFontMgr_OHOS:: Family %s has no font file\n", generic.family.c_str());
                continue;
            }
            TArray<sk_sp<SkTypeface_ohos>>& typefaces = genericTypefacesMap[generic.family];
            if (typefaces.size() == 0) {
                SkDebugf("SkFontMgr_OHOS:: Family %s has no typeface\n", generic.family.c_str());
                continue;
            }

            for (int j = 0; j < generic.aliasSet.size(); ++j) {
                if (generic.aliasSet[j].weight == 0) {
                    SkDebugf("SkFontMgr_OHOS:: Family %s alias %s has %d typeface\n",
                             generic.family.c_str(), generic.aliasSet[j].name.c_str(), typefaces.size());
                    sk_sp<SkFontStyleSet_ohos> newSet = sk_make_sp<SkFontStyleSet_ohos>(typefaces);
                    fNameToFamilyMap.emplace_back(NameToFamily{generic.aliasSet[j].name, newSet.get()});
                    fStyleSets.emplace_back(std::move(newSet));
                    continue;
                } else {
                    TArray<sk_sp<SkTypeface_ohos>> subTypefaces;
                    for (int k = 0; k < typefaces.size(); k++) {
                        if (typefaces[k]->fontStyle().weight() == generic.aliasSet[j].weight) {
                            subTypefaces.push_back(typefaces[k]);
                        }
                    }
                    if (subTypefaces.size() == 0) {
                        SkDebugf("SkFontMgr_OHOS:: Family %s alias %s: %d has no typeface\n",
                                 generic.family.c_str(), generic.aliasSet[j].name.c_str(),
                                 generic.aliasSet[j].weight);
                        continue;
                    }
                    SkDebugf("SkFontMgr_OHOS:: Family %s alias %s has %d typeface\n",
                             generic.family.c_str(), generic.aliasSet[j].name.c_str(), subTypefaces.size());
                    sk_sp<SkFontStyleSet_ohos> newSet = sk_make_sp<SkFontStyleSet_ohos>(subTypefaces);
                    fNameToFamilyMap.emplace_back(NameToFamily{generic.aliasSet[j].name, newSet.get()});
                    fStyleSets.emplace_back(std::move(newSet));
                }
            }
        }
        for (int i = 0; i < fontConfig.fallbackSet.size(); i++) {
            const FontFallback& fallback = fontConfig.fallbackSet[i];
            for (int j = 0; j < fallback.fallbackSet.size(); ++j) {
                const FallbackInfo& info = fallback.fallbackSet[j];
                if (fallbackTypefacesMap.find(info.familyName) == nullptr) {
                    SkDebugf("SkFontMgr_OHOS:: Fallback Family %s font not found\n", info.familyName.c_str());
                    continue;
                }
                TArray<sk_sp<SkTypeface_ohos>>& typefaces = fallbackTypefacesMap[info.familyName];
                if (typefaces.size() == 0) {
                    SkDebugf("SkFontMgr_OHOS:: Fallback Family %s has no typeface\n", info.familyName.c_str());
                    continue;
                }
                SkDebugf("SkFontMgr_OHOS:: Fallback Family %s has %d typeface\n",
                        info.familyName.c_str(), typefaces.size());
                sk_sp<SkFontStyleSet_ohos> newSet = sk_make_sp<SkFontStyleSet_ohos>(typefaces);
                fFallbackNameToFamilyMap.emplace_back(FallbackNameToFamily{info.familyName, info.lang, fallback.fallbackFor, newSet.get()});
                fStyleSets.emplace_back(std::move(newSet));
            }
        }
    }
protected:
    int onCountFamilies() const override {
        return fNameToFamilyMap.size();
    }
    void onGetFamilyName(int index, SkString* familyName) const override {
        if (index < 0 || fNameToFamilyMap.size() <= index) {
            familyName->reset();
            return;
        }
        familyName->set(fNameToFamilyMap[index].name);
    }
    sk_sp<SkFontStyleSet> onCreateStyleSet(int index) const override {
        if (index < 0 || fNameToFamilyMap.size() <= index) {
            return nullptr;
        }
        return sk_ref_sp(fNameToFamilyMap[index].styleSet);
    }

    sk_sp<SkFontStyleSet> onMatchFamily(const char familyName[]) const override {
        SkDebugf("SkFontMgr_OHOS::onMatchFamily(%s)\n", familyName);
        if (!familyName) {
            return nullptr;
        }
        SkAutoAsciiToLC tolc(familyName);
        for (int i = 0; i < fNameToFamilyMap.size(); ++i) {
            if (fNameToFamilyMap[i].name.equals(tolc.lc())) {
                SkDebugf("SkFontMgr_OHOS::onMatchFamily(%s) found in generic\n", familyName);
                return sk_ref_sp(fNameToFamilyMap[i].styleSet);
            }
        }
        // TODO: eventually we should not need to name fallback families.
        for (int i = 0; i < fFallbackNameToFamilyMap.size(); ++i) {
            if (fFallbackNameToFamilyMap[i].name.equals(tolc.lc())) {
                SkDebugf("SkFontMgr_OHOS::onMatchFamily(%s) found in fallback\n", familyName);
                return sk_ref_sp(fFallbackNameToFamilyMap[i].styleSet);
            }
        }
        return nullptr;
    }

    sk_sp<SkTypeface> onMatchFamilyStyle(const char familyName[],
                                         const SkFontStyle& style) const override {
        SkDebugf("SkFontMgr_OHOS::onMatchFamilyStyle(%s, %d)\n", familyName, style.slant());
        sk_sp<SkFontStyleSet> sset(this->matchFamily(familyName));
        return sset->matchStyle(style);
    }

    static int find_lang(const SkString& lang, const char* bcp47[], int bcp47Count) {
        /*
        * zh-Hans : ('zh' : iso639 code, 'Hans' : iso15924 code)
        */
        if (bcp47 == nullptr || bcp47Count == 0) {
            return -1;
        }
        for (int i = bcp47Count - 1; i >= 0; i--) {
            if (lang.find(bcp47[i]) != -1) {
                return i;
            } else {
                const char* iso15924 = strrchr(bcp47[i], '-');
                if (iso15924 == nullptr) {
                    continue;
                }
                iso15924++;
                int len = iso15924 - 1 - bcp47[i];
                SkString country(bcp47[i], len);
                if (lang.find(iso15924) != -1 ||
                    (strncmp(bcp47[i], "und", strlen("und")) && lang.find(country.c_str()) != -1)) {
                    return i + bcp47Count;
                }
            }
        }
        return -1;
    }

    static sk_sp<SkTypeface> find_family_style_character(
            const SkString& familyName,
            const TArray<FallbackNameToFamily, true>& fallbackNameToFamilyMap,
            const SkFontStyle& style, const char* bcp47[], int bcp47Count,
            SkUnichar character)
    {
        for (int i = 0; i < fallbackNameToFamilyMap.size(); i++) {
            if (familyName != fallbackNameToFamilyMap[i].fallbackFor) {
                continue;
            }
            int index = find_lang(fallbackNameToFamilyMap[i].lang, bcp47, bcp47Count);
            if (index == -1) {
                continue;
            }
            sk_sp<SkTypeface> face(fallbackNameToFamilyMap[i].styleSet->matchStyle(style));
            if (face->unicharToGlyph(character) != 0) {
                return face;
            }
        }
        return nullptr;
    }

    sk_sp<SkTypeface> onMatchFamilyStyleCharacter(const char familyName[], const SkFontStyle& style,
                                                  const char* bcp47[], int bcp47Count,
                                                  SkUnichar character) const override {
        SkDebugf("SkFontMgr_OHOS::onMatchFamilyStyleCharacter(%s)\n", familyName);
        // The variant 'elegant' is 'not squashed', 'compact' is 'stays in ascent/descent'.
        // The variant 'default' means 'compact and elegant'.
        // As a result, it is not possible to know the variant context from the font alone.
        // TODO: add 'is_elegant' and 'is_compact' bits to 'style' request.

        SkString familyNameString(familyName);
        for (const SkString& currentFamilyName : { familyNameString, SkString() }) {
            sk_sp<SkTypeface> matchingTypeface =
                find_family_style_character(currentFamilyName, fFallbackNameToFamilyMap,
                                            style, bcp47, bcp47Count, character);
            if (matchingTypeface) {
                return matchingTypeface;
            }
        }
        for (int i = 0; i < fFallbackNameToFamilyMap.size(); i++) {
            SkFontStyleSet_ohos* styleSet = fFallbackNameToFamilyMap[i].styleSet;
            if (styleSet->matchStyle(style)->unicharToGlyph(character) != 0) {
                return styleSet->matchAStyle(style);
            }
        }
        return nullptr;
    }

    sk_sp<SkTypeface> onMakeFromData(sk_sp<SkData> data, int ttcIndex) const override {
        return this->makeFromStream(std::unique_ptr<SkStreamAsset>(new SkMemoryStream(std::move(data))),
                                    ttcIndex);
    }
    sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset> stream,
                                            int ttcIndex) const override {
        return this->makeFromStream(std::move(stream),
                                    SkFontArguments().setCollectionIndex(ttcIndex));
    }
    sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset> stream,
                                           const SkFontArguments& args) const override {
        return SkTypeface_FreeType::MakeFromStream(std::move(stream), args);
    }
    sk_sp<SkTypeface> onMakeFromFile(const char path[], int ttcIndex) const override {
        std::unique_ptr<SkStreamAsset> stream = SkStream::MakeFromFile(path);
        return stream ? this->makeFromStream(std::move(stream), ttcIndex) : nullptr;
    }

    sk_sp<SkTypeface> onLegacyMakeTypeface(const char familyName[], SkFontStyle style) const override {
        SkDebugf("SkFontMgr_OHOS::onLegacyMakeTypeface(%s, %d)\n", familyName, style.weight());
        if (familyName) {
            // On Android, we must return nullptr when we can't find the requested
            // named typeface so that the system/app can provide their own recovery
            // mechanism. On other platforms we'd provide a typeface from the
            // default family instead.
            return sk_sp<SkTypeface>(this->onMatchFamilyStyle(familyName, style));
        }
        return fStyleSets[0]->matchStyle(style);
    }

private:
    SkFontScanner_FreeType scanner; // the scanner to parse a font file
    TArray<sk_sp<SkFontStyleSet_ohos>> fStyleSets;

    TArray<NameToFamily, true> fNameToFamilyMap;
    TArray<FallbackNameToFamily, true> fFallbackNameToFamilyMap;
};

} // namespace

/*! To create SkFontMgr object for ohos platform
 * \param fontconfig the path of system fontconfig.json
 * \return The object of SkFontMgr_OHOS
 */
sk_sp<SkFontMgr> SkFontMgr_New_OHOS(const char* fontconfig)
{
    return sk_make_sp<SkFontMgr_OHOS>();
}
