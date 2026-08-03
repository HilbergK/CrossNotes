#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {

struct UiFontSize {
  int fontId;
  uint8_t pointSize;
};

constexpr UiFontSize kUiFontSizes[] = {
    {SMALL_FONT_ID, 8},
    {UI_10_FONT_ID, 10},
    {UI_12_FONT_ID, 12},
};

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  registry_.discover();
  registryLoaded_ = true;

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.sdFontResolverCtx = this;

  // If user has a saved SD font selection, load it
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      if (manager_.loadFamily(*family, renderer, SETTINGS.getSdFontTargetPointSize(), SETTINGS.fontSize)) {
        loadedFontSizeStep_ = SETTINGS.fontSize;
        setupUiFallbacks(renderer);
        LOG_DBG("SDFS", "Loaded SD card font family: %s", SETTINGS.sdFontFamilyName);
      } else {
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", SETTINGS.sdFontFamilyName);
        SETTINGS.sdFontFamilyName[0] = '\0';
        SETTINGS.saveToFile();
      }
    } else {
      LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", SETTINGS.sdFontFamilyName);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
    }
  }

  LOG_DBG("SDFS", "SD font system ready (%d families discovered)", registry_.getFamilyCount());
  releaseRegistry();
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.load(std::memory_order_acquire);

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  const uint8_t targetPointSize = SETTINGS.getSdFontTargetPointSize();
  const uint8_t sizeStep = SETTINGS.fontSize;

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
      loadedFontSizeStep_ = UINT8_MAX;
    }
    return;
  }

  if (!registryWasDirty && currentFamily == wantedFamily && loadedFontSizeStep_ == SETTINGS.fontSize) {
    return;
  }

  ensureRegistry();

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
      return;
    }
    const auto* wantedFile = family->selectFile(targetPointSize, sizeStep);
    uint8_t wantedPt = wantedFile ? wantedFile->pointSize : 0;
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (target %u step %u)%s", wantedFamily, manager_.currentPointSize(),
            wantedPt, targetPointSize, sizeStep, registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, targetPointSize, sizeStep)) {
      loadedFontSizeStep_ = SETTINGS.fontSize;
      setupUiFallbacks(renderer);
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.saveToFile();
  }
}

void SdCardFontSystem::releaseLoadedFont(GfxRenderer& renderer) {
  if (manager_.currentFamilyName().empty()) return;

  const std::string familyName = manager_.currentFamilyName();
  (void)familyName;
  manager_.unloadAll(renderer);
  loadedFontSizeStep_ = UINT8_MAX;
  LOG_DBG("SDFS", "Released SD card font before low-memory operation: %s", familyName.c_str());
}

void SdCardFontSystem::ensureRegistry() {
  const bool dirty = registryDirty_.exchange(false, std::memory_order_acq_rel);
  if (registryLoaded_ && !dirty) return;
  if (dirty) LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
  registry_.discover();
  registryLoaded_ = true;
}

void SdCardFontSystem::releaseRegistry() {
  if (!registryLoaded_) return;
  LOG_DBG("SDFS", "Releasing SD font catalog (%d families)", registry_.getFamilyCount());
  registry_.clear();
  registryLoaded_ = false;
}

void SdCardFontSystem::releaseForNetwork(GfxRenderer& renderer) {
  releaseLoadedFont(renderer);

  releaseRegistry();
  registryDirty_.store(true, std::memory_order_release);
}

void SdCardFontSystem::setupUiFallbacks(GfxRenderer& renderer) {
  const std::string& familyName = manager_.currentFamilyName();
  if (familyName.empty()) return;

  const auto* family = registry_.findFamily(familyName);
  if (!family) return;

  const auto readerIt = renderer.getFontMap().find(manager_.getFontId(familyName));
  if (readerIt == renderer.getFontMap().end()) return;

  static constexpr uint32_t kCjkProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00};
  bool hasCjk = false;
  for (const uint32_t cp : kCjkProbes) {
    if (readerIt->second.hasCodepoint(cp)) {
      hasCjk = true;
      break;
    }
  }
  if (!hasCjk) {
    LOG_DBG("SDFS", "%s has no CJK coverage - skipping UI fallback sizes", familyName.c_str());
    return;
  }

  for (const auto& ui : kUiFontSizes) {
    const int sdFontId = manager_.loadFamilyExtraSize(*family, renderer, ui.pointSize);
    if (sdFontId != 0) {
      renderer.setFallbackFont(ui.fontId, sdFontId);
    } else {
      LOG_DBG("SDFS", "No %u pt SD glyphs for UI fallback in %s", ui.pointSize, familyName.c_str());
    }
  }
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*fontSizeEnum*/) const {
  // The manager loads exactly one size (closest to SETTINGS.fontSize), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}

bool SdCardFontSystem::changeReaderFontSize(const bool larger) {
  refreshIfDirty();

  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      const auto sizes = family->availableSizes();
      if (sizes.size() > 1) {
        uint8_t current = SETTINGS.fontSize < sizes.size() ? SETTINGS.fontSize : static_cast<uint8_t>(sizes.size() - 1);
        if (larger) {
          current = static_cast<uint8_t>((current + 1) % sizes.size());
        } else {
          current = current == 0 ? static_cast<uint8_t>(sizes.size() - 1) : static_cast<uint8_t>(current - 1);
        }
        SETTINGS.fontSize = current;
        return true;
      }
    }
  }

  return SETTINGS.changeReaderFontSize(larger);
}
