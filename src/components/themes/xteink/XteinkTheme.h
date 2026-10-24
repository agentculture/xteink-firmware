#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// xteink fork theme: "bookplate". Paper-white, 1-bit only (no dithered
// fills on the boot, home or menu surfaces, so nothing ghosts between FAST
// refreshes), square corners, serif accents from the built-in NotoSerif
// reader fonts, ruled catalogue rows and a bookmark-ribbon selection marker.
// Library and other FreeInkUI screens pick the shape up from the metrics
// below through uiThemeTokens(). On-device checks: docs/test-x3.md.
namespace XteinkMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 10,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 84,
                                 .verticalSpacing = 14,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 // Generous page margins, like a book's outer margin.
                                 .contentSidePadding = 28,
                                 .listRowHeight = 44,
                                 .listWithSubtitleRowHeight = 64,
                                 .listRowGap = 0,
                                 .listRowRadius = 0,
                                 .listInset = 20,
                                 .listSidePadding = 14,
                                 .listSelectionStyle = 3,  // triangle marker: rows stay paper-white
                                 .listScrollWidth = 3,
                                 .listScrollSide = 0,
                                 .listTitleBold = false,
                                 .headerSidePadding = 24,
                                 .headerUnderlineSize = 2,
                                 .headerTitleAlign = 0,  // left
                                 .headerBatterySide = 0,
                                 // Centered clock: the home band's left corner carries the wordmark.
                                 .headerClockCentered = true,
                                 .menuRowHeight = 52,
                                 .menuSpacing = 0,  // ruled rows touch, like an index card
                                 .tabSpacing = 8,
                                 .tabBarHeight = 48,
                                 // Square black tab (listRowRadius = 0), a card-catalogue divider.
                                 .tabPillFullSlot = true,
                                 .scrollBarWidth = 3,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 56,
                                 // Same thumb height as Lyra so cached cover thumbs are reused
                                 // when switching from the stock default (no SD regeneration).
                                 .homeCoverHeight = 226,
                                 .homeCoverTileHeight = 250,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 20,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyHeight = 56,
                                 .keyboardKeySpacing = 0,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -7,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupTopOffsetRatio = 0.165f,
                                 .popupMarginX = 18,
                                 .popupMarginY = 14,
                                 .popupFrameThickness = 3,
                                 .popupCornerRadius = 0,
                                 .popupTextBold = false,
                                 .popupTextInverted = false,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = true,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 6,
                                 .optionPopupInnerPadding = 20,
                                 .optionPopupSelectionVPadding = 10,
                                 .optionPopupDialogSideMargin = 20,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0,
                                 .controlRadius = 0,
                                 .sheetRadius = 0,
                                 .capsuleRadius = 0};
}  // namespace XteinkMetrics

class XteinkTheme : public BaseTheme {
 public:
  bool drawBootScreen(GfxRenderer& renderer) const override;
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;

 private:
  // Width of the last cover drawn on Home. The tile is restored from the
  // stored framebuffer region on later renders without re-reading the BMP
  // header, so the text column needs the width remembered (Lyra keeps the
  // same value in a file-static).
  mutable int coverWidth = 0;
};
