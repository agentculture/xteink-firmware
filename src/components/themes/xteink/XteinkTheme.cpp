#include "XteinkTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Everything here is drawn with existing primitives and the fonts that are
// already linked in (main.cpp inserts NOTOSERIF_14 unconditionally and the
// 16/18 sizes unless OMIT_FONTS). No bitmaps, no new font data, no dithered
// fills: pure 1-bit so FAST refreshes leave no grey residue.
namespace {
#ifndef OMIT_FONTS
constexpr int kWordmarkFont = NOTOSERIF_18_FONT_ID;
constexpr int kTitleFont = NOTOSERIF_16_FONT_ID;
#else
constexpr int kWordmarkFont = NOTOSERIF_14_FONT_ID;
constexpr int kTitleFont = NOTOSERIF_14_FONT_ID;
#endif
constexpr int kSerifFont = NOTOSERIF_14_FONT_ID;
constexpr const ThemeMetrics& kMetrics = XteinkMetrics::values;

// Open-book mark: two filled pages dipping toward the spine, with white
// "text lines" on the larger sizes. Returns the drawn height.
int drawBookMark(const GfxRenderer& renderer, const int cx, const int top, const int width) {
  const int gap = std::max(2, width / 36);  // half the spine gap
  const int pageW = width / 2 - gap;
  const int dip = width / 10;
  const int pageH = width / 2;

  const int lx[4] = {cx - gap - pageW, cx - gap, cx - gap, cx - gap - pageW};
  const int ly[4] = {top, top + dip, top + dip + pageH, top + pageH};
  const int rx[4] = {cx + gap, cx + gap + pageW, cx + gap + pageW, cx + gap};
  const int ry[4] = {top + dip, top, top + pageH, top + dip + pageH};
  renderer.fillPolygon(lx, ly, 4, true);
  renderer.fillPolygon(rx, ry, 4, true);

  if (width >= 80) {
    // Lines follow the page's top edge (slope dip/pageW).
    const int margin = width / 12;
    const int step = pageH / 5;
    for (int i = 1; i <= 3; i++) {
      const int o = i * step;
      const int rise = dip * margin / pageW;
      renderer.drawLine(cx - gap - pageW + margin, top + o + rise, cx - gap - margin, top + o + dip - rise, 2, false);
      renderer.drawLine(cx + gap + margin, top + o + dip - rise, cx + gap + pageW - margin, top + o + rise, 2, false);
    }
  }
  return pageH + dip;
}

// Bookmark ribbon with a notched tail: the selection marker on Home.
void drawRibbon(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  const int notch = width / 2;
  const int xs[5] = {x, x + width, x + width, x + width / 2, x};
  const int ys[5] = {y, y, y + height, y + height - notch, y + height};
  renderer.fillPolygon(xs, ys, 5, true);
}

void drawCoverSlot(const GfxRenderer& renderer, const Rect r) {
  renderer.fillRect(r.x, r.y, r.width, r.height, false);
  const int markW = std::min(r.width - 24, 72);
  if (markW > 20) drawBookMark(renderer, r.x + r.width / 2, r.y + (r.height - markW * 6 / 10) / 2, markW);
}
}  // namespace

bool XteinkTheme::drawBootScreen(GfxRenderer& renderer) const {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  // The bookplate: a heavy outer rule and a hairline inner rule.
  const int plateW = std::min(pageWidth - 96, 360);
  const int plateH = std::min(pageHeight - 128, 400);
  const int plateX = (pageWidth - plateW) / 2;
  const int plateY = (pageHeight - plateH) / 2 - 20;
  renderer.drawRect(plateX, plateY, plateW, plateH, 3, true);
  renderer.drawRect(plateX + 9, plateY + 9, plateW - 18, plateH - 18, 1, true);

  constexpr int markW = 112;
  const int markH = markW / 2 + markW / 10;
  const int wordmarkH = renderer.getLineHeight(kWordmarkFont);
  const int taglineH = renderer.getLineHeight(kSerifFont) * 2;
  constexpr int gapMark = 32;
  constexpr int gapRule = 14;
  const int blockH = markH + gapMark + wordmarkH + gapRule + 2 + gapRule + taglineH;
  int y = plateY + std::max(24, (plateH - blockH) / 2);

  drawBookMark(renderer, pageWidth / 2, y, markW);
  y += markH + gapMark;
  renderer.drawCenteredText(kWordmarkFont, y, tr(STR_THEME_XTEINK), true, EpdFontFamily::BOLD);
  y += wordmarkH + gapRule;
  renderer.fillRect(pageWidth / 2 - 24, y, 48, 2, true);
  y += 2 + gapRule;
  UITheme::drawCenteredWrappedText(renderer, Rect{plateX + 28, y, plateW - 56, taglineH}, kSerifFont,
                                   tr(STR_XTEINK_TAGLINE), 2, true, EpdFontFamily::ITALIC,
                                   UITheme::TextVerticalAlignment::TOP);

  renderer.drawCenteredText(SMALL_FONT_ID, plateY + plateH + 24, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
  return true;
}

void XteinkTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                             const bool backButton) const {
  BaseTheme::drawHeader(renderer, rect, title, subtitle, backButton);
  // Home (untitled stack root) carries the wordmark in the band's left
  // corner; the clock is centered (headerClockCentered) so the corner is free.
  if (title != nullptr || backButton) return;
  int top = 0, right = 0, bottom = 0, left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int y = rect.y + std::max(0, (rect.height - renderer.getLineHeight(kSerifFont)) / 2);
  renderer.drawText(kSerifFont, rect.x + left + headerStatusInset(), y, tr(STR_THEME_XTEINK), true,
                    EpdFontFamily::BOLD);
}

void XteinkTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                      const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                      bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  constexpr int inset = 12;  // room for the selection frame around the cover
  constexpr int textGap = 22;
  const int tileX = rect.x + kMetrics.contentSidePadding;
  const int tileW = rect.width - 2 * kMetrics.contentSidePadding;
  const int coverH = kMetrics.homeCoverHeight;
  const int coverX = tileX + inset;
  const int coverY = rect.y + (rect.height - coverH) / 2;
  if (coverWidth == 0) coverWidth = coverH * 3 / 5;

  if (recentBooks.empty()) {
    coverWidth = coverH * 3 / 5;
    drawCoverSlot(renderer, Rect{coverX, coverY, coverWidth, coverH});
    renderer.drawRect(coverX, coverY, coverWidth, coverH, true);
    const int textX = coverX + coverWidth + textGap;
    const int textW = tileX + tileW - inset - textX;
    const int titleH = renderer.getLineHeight(kTitleFont);
    const int y = coverY + (coverH - titleH - renderer.getLineHeight(kSerifFont)) / 2;
    renderer.drawText(kTitleFont, textX, y,
                      renderer.truncatedText(kTitleFont, tr(STR_NO_OPEN_BOOK), textW, EpdFontFamily::BOLD).c_str(),
                      true, EpdFontFamily::BOLD);
    const auto hint = renderer.wrappedText(kSerifFont, tr(STR_START_READING), textW, 2, EpdFontFamily::ITALIC);
    int hy = y + titleH;
    for (const auto& line : hint) {
      renderer.drawText(kSerifFont, textX, hy, line.c_str(), true, EpdFontFamily::ITALIC);
      hy += renderer.getLineHeight(kSerifFont);
    }
    return;
  }

  const RecentBook& book = recentBooks[0];

  // The cover goes into the stored tile region; selection and text are drawn
  // on top afterwards so a later restore always starts from a clean tile.
  if (!bufferRestored) {
    bool hasCover = false;
    if (!book.coverBmpPath.empty()) {
      const std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, coverH);
      HalFile file;
      if (Storage.openFileForRead("HOME", path, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          coverWidth = std::min(bitmap.getWidth(), tileW / 2);
          drawCoverThumbFill(renderer, bitmap, Rect{coverX, coverY, coverWidth, coverH});
          hasCover = true;
        }
        file.close();
      }
    }
    if (!hasCover) {
      coverWidth = coverH * 3 / 5;
      drawCoverSlot(renderer, Rect{coverX, coverY, coverWidth, coverH});
    }
    renderer.drawRect(coverX, coverY, coverWidth, coverH, true);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  if (selectorIndex == 0) {
    renderer.drawRect(tileX, rect.y, tileW, rect.height, 3, true);
  }

  const int textX = coverX + coverWidth + textGap;
  const int textW = tileX + tileW - inset - textX;
  if (textW <= 0) return;

  const auto titleLines = renderer.wrappedText(kTitleFont, book.title.c_str(), textW, 4, EpdFontFamily::BOLD);
  const std::string author =
      book.author.empty() ? std::string{}
                          : renderer.truncatedText(kSerifFont, book.author.c_str(), textW, EpdFontFamily::ITALIC);
  const int titleLineH = renderer.getLineHeight(kTitleFont);
  const int serifH = renderer.getLineHeight(kSerifFont);
  const int uiH = renderer.getLineHeight(UI_10_FONT_ID);
  const int blockH = titleLineH * static_cast<int>(titleLines.size()) + (author.empty() ? 0 : serifH + 4) + 18 + uiH;
  int y = coverY + std::max(0, (coverH - blockH) / 2);

  for (const auto& line : titleLines) {
    renderer.drawText(kTitleFont, textX, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += titleLineH;
  }
  if (!author.empty()) {
    y += 4;
    renderer.drawText(kSerifFont, textX, y, author.c_str(), true, EpdFontFamily::ITALIC);
    y += serifH;
  }
  y += 8;
  renderer.fillRect(textX, y, 32, 2, true);
  y += 10;
  renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_CONTINUE_READING), true);
}

void XteinkTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                 const std::function<std::string(int index)>& buttonLabel,
                                 const std::function<UIIcon(int index)>& /*rowIcon*/) const {
  // A typographic index: ruled rows, no icons (each icon header is a static
  // array copied into every TU that includes it, so skipping them keeps the
  // theme free of extra flash).
  constexpr int ribbonW = 10;
  constexpr int labelIndent = 28;
  const int x = rect.x + kMetrics.contentSidePadding;
  const int w = rect.width - 2 * kMetrics.contentSidePadding;
  const int rowH = kMetrics.menuRowHeight;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);

  if (buttonCount > 0) renderer.drawLine(x, rect.y, x + w - 1, rect.y, true);
  for (int i = 0; i < buttonCount; ++i) {
    const int y = rect.y + i * (rowH + kMetrics.menuSpacing);
    const bool selected = selectedIndex == i;
    renderer.drawLine(x, y + rowH - 1, x + w - 1, y + rowH - 1, true);
    if (selected) drawRibbon(renderer, x + 4, y + 1, ribbonW, rowH - 10);

    const std::string label = buttonLabel(i);
    const auto style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const auto fitted = renderer.truncatedText(UI_12_FONT_ID, label.c_str(), w - labelIndent - 8, style);
    renderer.drawText(UI_12_FONT_ID, x + labelIndent, y + (rowH - lineH) / 2, fitted.c_str(), true, style);
  }
}

void XteinkTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                  const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Open labels over a short bar that points down at each physical button;
  // slot positions match BaseTheme so the hints line up with the keys.
  const int pageHeight = renderer.getScreenHeight();
  constexpr int slotW = 106;
  constexpr int slotH = XteinkMetrics::values.buttonHintsHeight;
  constexpr int barH = 4;
  constexpr int narrowPositions[] = {25, 130, 245, 350};
  constexpr int widePositions[] = {38, 154, 268, 384};
  const int* positions = renderer.getScreenWidth() >= 528 ? widePositions : narrowPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();
  const int top = pageHeight - slotH;

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int x = positions[i];
    // Same grayscale-pass contract as BaseTheme::drawButtonHints.
    renderer.fillRect(x, top, slotW, slotH, grayscale);
    if (grayscale) continue;
    drawHintLabel(renderer, UI_10_FONT_ID, labels[i], x, slotW, top, slotH - barH - 2);
    renderer.fillRect(x + 10, pageHeight - barH, slotW - 20, barH, true);
  }

  renderer.setOrientation(origOrientation);
}
