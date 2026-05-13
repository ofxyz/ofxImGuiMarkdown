#include "ofxImGuiMarkdown.h"
#include "ofUtils.h"     // ofLaunchBrowser
#include "ofGraphics.h"  // ofEnableArbTex, ofDisableArbTex, ofGetUsingArbTex
#include "ofFileUtils.h" // ofFilePath
#include <algorithm>     // std::transform
#include <cctype>        // std::tolower
#include <cstring>       // strlen

// ---------------------------------------------------------------------------
// ofxMarkdownRenderer
// ---------------------------------------------------------------------------

void ofxMarkdownRenderer::render(const std::string& markdown) {
    render(markdown.c_str(), markdown.c_str() + markdown.size());
}

void ofxMarkdownRenderer::render(const char* str, const char* str_end) {
    if (!str || str == str_end) return;
    if (!str_end) str_end = str + std::strlen(str);
    m_tableId = 0;
    m_codeBlockIndex = 0;
    m_listIndentStack.clear();
    print(str, str_end);
}

ImFont* ofxMarkdownRenderer::get_font() const {
    if (m_hlevel == 1) return headingFont;
    if (m_hlevel >= 2) return h2Font ? h2Font : headingFont;
    if (m_is_table_header && boldFont) return boldFont;
    if (m_is_strong && boldFont) return boldFont;
    if (m_is_em    && italicFont) return italicFont;
    if (m_is_code  && monoFont)  return monoFont; // inline code spans
    return regularFont; // nullptr → ImGui default
}

ImVec4 ofxMarkdownRenderer::get_color() const {
    if (!m_href.empty()) {
        return ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered];
    }
    return ImGui::GetStyle().Colors[ImGuiCol_Text];
}

void ofxMarkdownRenderer::open_url() const {
    if (!m_href.empty()) {
        ofLaunchBrowser(m_href);
    }
}

bool ofxMarkdownRenderer::get_image(image_info& nfo) const {
    if (m_href.empty()) return false;

    auto it = m_imageCache.find(m_href);
    if (it == m_imageCache.end()) {
        CachedImage entry;

        // Detect SVG by file extension (case-insensitive).
        std::string ext = ofFilePath::getFileExt(m_href);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == "svg") {
            // Load the SVG document.
            auto svgDoc = std::make_shared<ofxSvg>();
            if (svgDoc->load(m_href)) {
                float svgW = svgDoc->getWidth();
                float svgH = svgDoc->getHeight();
                if (svgW < 1.0f) svgW = 256.0f; // fallback for SVGs without explicit size
                if (svgH < 1.0f) svgH = 256.0f;

                // Render the SVG into an ofFbo at svgScale× resolution.
                // ofFbo with GL_TEXTURE_2D gives normalised 0–1 UVs for ImGui.
                auto fbo = std::make_shared<ofFbo>();
                ofFboSettings fboSettings;
                fboSettings.width         = std::round(svgW * svgScale);
                fboSettings.height        = std::round(svgH * svgScale);
                fboSettings.internalformat = GL_RGBA;
                fboSettings.textureTarget  = GL_TEXTURE_2D;
                fbo->allocate(fboSettings);

                fbo->begin();
                ofClear(0, 0, 0, 0);
                ofPushMatrix();
                ofScale(svgScale, svgScale);
                svgDoc->draw();
                ofPopMatrix();
                fbo->end();

                entry.fbo = fbo;
            } else {
                entry.failed = true;
            }

        } else {
            // Raster image (PNG, JPG, GIF, …).
            // Temporarily disable ARB so the texture uses normalised 0–1 UVs.
            const bool wasArb = ofGetUsingArbTex();
            ofDisableArbTex();

            auto img = std::make_shared<ofImage>();
            if (img->load(m_href)) {
                entry.raster = img;
            } else {
                entry.failed = true;
            }

            if (wasArb) ofEnableArbTex();
        }

        m_imageCache[m_href] = std::move(entry);
        it = m_imageCache.find(m_href);
    }

    if (it->second.failed) return false;

    const ofTexture* tex = it->second.texture();
    if (!tex || !tex->isAllocated()) return false;

    // SVG FBOs are rendered at svgScale× — display at the logical SVG size.
    float w = tex->getWidth()  / (it->second.fbo ? svgScale : 1.0f);
    float h = tex->getHeight() / (it->second.fbo ? svgScale : 1.0f);

    // Scale down to the display limit, preserving aspect ratio.
    const float limit = (maxImageWidth > 0.0f) ? maxImageWidth
                      : ImGui::GetContentRegionAvail().x;
    if (w > limit) {
        h = h * (limit / w);
        w = limit;
    }

    nfo.texture_id = (ImTextureID)(uintptr_t)tex->getTextureData().textureID;
    nfo.size       = { w, h };
    nfo.uv0        = { 0.0f, 0.0f };
    nfo.uv1        = { 1.0f, 1.0f };
    nfo.col_tint   = { 1.0f, 1.0f, 1.0f, 1.0f };
    nfo.col_border = { 0.0f, 0.0f, 0.0f, 0.0f };
    return true;
}

void ofxMarkdownRenderer::clearImageCache() {
    m_imageCache.clear();
}

void ofxMarkdownRenderer::soft_break() {
    // Intentionally empty: soft breaks keep inline content on the same line.
    // Override with ImGui::NewLine() if hard-wrap-on-source-newline is desired.
}

// ---------------------------------------------------------------------------
// Headings — synthetic font scaling when custom heading fonts are not loaded
// ---------------------------------------------------------------------------

void ofxMarkdownRenderer::BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e) {
    const unsigned lv = std::min(d->level, 6u);

    // Decide whether to use window-font scaling for this heading level.
    // We scale when there is no custom font that get_font() would return.
    ImFont* customFont = (lv == 1) ? headingFont
                       : (lv == 2) ? (h2Font ? h2Font : headingFont)
                       : nullptr;
    const float scale = headingScale[lv];
    const bool  doScale = !customFont && scale > 1.001f;

    if (e) {
        m_activeHeadScale = doScale ? scale : 1.0f;
        if (doScale) ImGui::SetWindowFontScale(scale);
    }

    imgui_md::BLOCK_H(d, e); // sets m_hlevel, calls set_font(), draws separator

    if (!e) {
        if (m_activeHeadScale > 1.001f) ImGui::SetWindowFontScale(1.0f);
        m_activeHeadScale = 1.0f;
    }
}

// ---------------------------------------------------------------------------
// Fenced code blocks — read-only ofxImGuiTextEdit with language highlighting
// ---------------------------------------------------------------------------

static std::string attributeToString(const MD_ATTRIBUTE& attr) {
    if (!attr.text || attr.size == 0) return "";
    return std::string(attr.text, attr.text + attr.size);
}

TextEditor::LanguageDefinitionId
ofxMarkdownRenderer::languageFromFence(const std::string& lang) {
    std::string s = lang;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (s == "c++" || s == "cpp" || s == "cc" || s == "cxx" || s == "h" || s == "hpp")
        return TextEditor::LanguageDefinitionId::Cpp;
    if (s == "c")
        return TextEditor::LanguageDefinitionId::C;
    if (s == "csharp" || s == "cs")
        return TextEditor::LanguageDefinitionId::Cs;
    if (s == "py" || s == "python")
        return TextEditor::LanguageDefinitionId::Python;
    if (s == "lua")
        return TextEditor::LanguageDefinitionId::Lua;
    if (s == "json")
        return TextEditor::LanguageDefinitionId::Json;
    if (s == "sql")
        return TextEditor::LanguageDefinitionId::Sql;
    if (s == "angelscript" || s == "as")
        return TextEditor::LanguageDefinitionId::AngelScript;
    if (s == "glsl" || s == "vert" || s == "frag")
        return TextEditor::LanguageDefinitionId::Glsl;
    if (s == "hlsl")
        return TextEditor::LanguageDefinitionId::Hlsl;
    return TextEditor::LanguageDefinitionId::None;
}

// render_text calls this to accumulate fenced-block text across multiple MD4C
// TEXT events.  The actual editor rendering is deferred to flushCodeBlock()
// which is called from BLOCK_CODE leave, guaranteeing exactly one render per block.
static void accumulateCodeText(std::string& pending, const char* str, const char* str_end) {
    pending.append(str, str_end);
}

void ofxMarkdownRenderer::flushCodeBlock() {
    const int index = m_activeCodeBlock;
    if (index >= static_cast<int>(m_codeBlocks.size()))
        m_codeBlocks.resize(index + 1);

    CodeBlockCache& cache = m_codeBlocks[index];
    if (!cache.editor) {
        cache.editor.reset(new TextEditor());
        cache.editor->SetPalette(TextEditor::PaletteId::Dark);
        cache.editor->SetReadOnlyEnabled(true);
        cache.editor->SetShowWhitespacesEnabled(false);
        cache.editor->SetShowLineNumbersEnabled(true);
    }

    // Strip a single trailing newline that MD4C always appends to code blocks
    // but that TextEditor would show as a spurious empty last line.
    std::string text = m_pendingCodeText;
    if (!text.empty() && text.back() == '\n') text.pop_back();
    if (!text.empty() && text.back() == '\r') text.pop_back();

    const TextEditor::LanguageDefinitionId language = languageFromFence(m_codeLanguage);
    if (cache.text != text) {
        cache.text = text;
        cache.editor->SetText(cache.text);
    }
    if (cache.language != language) {
        cache.language = language;
        cache.editor->SetLanguageDefinition(cache.language);
    }

    int lineCount = 1;
    for (char c : text)
        if (c == '\n') ++lineCount;

    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    const float height = std::min(codeBlockMaxHeight,
                                  std::max(lineH * 3.0f, lineH * lineCount + 12.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, codeBlockBg);
    char id[32];
    snprintf(id, sizeof(id), "##mdcode%d", index);
    cache.editor->Render(id, false, ImVec2(ImGui::GetContentRegionAvail().x, height), true);
    ImGui::PopStyleColor();
}

void ofxMarkdownRenderer::BLOCK_CODE(const MD_BLOCK_CODE_DETAIL* d, bool e) {
    imgui_md::BLOCK_CODE(d, e); // sets m_is_code / m_is_table_body flags in base

    if (e) {
        // Assign this block its stable cache index (incremented per render() frame).
        m_activeCodeBlock = m_codeBlockIndex++;
        m_pendingCodeText.clear();

        m_inCodeBlock = true;
        m_codeLanguage = attributeToString(d->lang);
        if (m_codeLanguage.empty())
            m_codeLanguage = attributeToString(d->info);

        // MD4C's info string may include extra metadata after the language tag.
        const size_t firstSpace = m_codeLanguage.find_first_of(" \t\r\n");
        if (firstSpace != std::string::npos)
            m_codeLanguage.resize(firstSpace);

        ImGui::NewLine();
        ImGui::Spacing();
        ImGui::Indent(8.0f);
        if (!useTextEditorForCodeBlocks && monoFont) ImGui::PushFont(monoFont);

    } else {
        if (useTextEditorForCodeBlocks) {
            // All TEXT events for this block have been accumulated — render once.
            flushCodeBlock();
        }
        if (!useTextEditorForCodeBlocks && monoFont) ImGui::PopFont();
        ImGui::Unindent(8.0f);
        ImGui::Spacing();
        ImGui::NewLine();
        m_inCodeBlock = false;
        m_codeLanguage.clear();
        m_pendingCodeText.clear();
    }
}

// ---------------------------------------------------------------------------
// Block quotes — indented text with a coloured left bar
// ---------------------------------------------------------------------------

void ofxMarkdownRenderer::BLOCK_QUOTE(bool e) {
    if (e) {
        ImGui::NewLine();
        // Record top of the quote for the left bar (drawn on leave).
        m_quoteBarStart   = ImGui::GetCursorScreenPos();
        m_quoteBarStart.x -= 4.0f;
        ImGui::Indent(16.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, quoteTextColor);
    } else {
        ImGui::PopStyleColor();

        const ImVec2 endPos = ImGui::GetCursorScreenPos();
        // Vertical bar — drawn after text so it sits on top (no channel split
        // needed since it's in the left margin, outside text bounds).
        ImGui::GetWindowDrawList()->AddRectFilled(
            m_quoteBarStart,
            { m_quoteBarStart.x + 3.0f, endPos.y },
            ImGui::ColorConvertFloat4ToU32(quoteBarColor));

        ImGui::Unindent(16.0f);
        ImGui::NewLine();
    }
}

// ---------------------------------------------------------------------------
// List items — fully custom so we control indent width and marker placement.
//
// The root cause of the "1 I.tem" visual artifact in imgui_md's original
// BLOCK_LI is that it calls ImGui::SameLine() then ImGui::Indent(), and
// Indent() immediately overwrites CursorPos.x — undoing the SameLine.
// We fix this by:
//   1. Applying Indent(markerWidth) so future wrapped lines start at the
//      content column.
//   2. Then calling SetCursorPosY to move back to the marker's row (Indent
//      leaves CursorPos.y at the next-line position after the marker text).
//   3. And SetCursorPosX to content column — so the very first render_text
//      call also starts at the right column, on the same line as the marker.
// ---------------------------------------------------------------------------

void ofxMarkdownRenderer::BLOCK_LI(const MD_BLOCK_LI_DETAIL* d, bool e) {
    if (m_list_stack.empty()) return;

    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    if (e) {
        if (listItemSpacing > 0.0f)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + listItemSpacing);
        ImGui::NewLine();

        list_info& nfo = m_list_stack.back();
        float markerY   = ImGui::GetCursorPosY();

        // --- render the marker ---
        if (nfo.is_ol) {
            char marker[16];
            snprintf(marker, sizeof(marker), "%d%c", nfo.cur_ol++, nfo.delim);
            ImGui::TextUnformatted(marker);
            float markerW = ImGui::CalcTextSize(marker).x + spacing * 2.0f;
            ImGui::Indent(markerW);
            m_listIndentStack.push_back(markerW);
        } else {
            // Use a bullet character for '*', otherwise the raw delimiter.
            // Render as TextUnformatted (not ImGui::Bullet()) so we can
            // measure the width and use the same Indent+SetCursorPosY pattern
            // as ordered lists — Bullet() leaves an implicit SameLine that
            // Indent() would cancel, reproducing the same visual artifact.
            // Use ASCII characters only — the default ImGui fonts do not
            // include the Unicode bullet (U+2022) so it would show as '?'.
            char ulMarker[4] = {};
            ulMarker[0] = (nfo.delim == '*') ? '*' : (char)nfo.delim;
            ImGui::TextUnformatted(ulMarker);
            float markerW = ImGui::CalcTextSize(ulMarker).x + spacing * 2.0f;
            ImGui::Indent(markerW);
            m_listIndentStack.push_back(markerW);
        }

        // Indent() moved CursorPos to (indentedX, nextLineY).
        // Move cursor back to the marker's line at the new content column.
        ImGui::SetCursorPosY(markerY);
        // CursorPos.x is already at the indented content column after Indent().
    } else {
        if (!m_listIndentStack.empty()) {
            ImGui::Unindent(m_listIndentStack.back());
            m_listIndentStack.pop_back();
        }
        // If we've closed the outermost list, add a blank line.
        if (m_list_stack.size() == 1)
            ImGui::NewLine();
    }
}

// ---------------------------------------------------------------------------
// Tables — ImGui Table API replaces imgui_md's manual cursor tracker
//
// imgui_md measures column widths from header text, so a 2-character header
// produces a 2-character-wide column. The ImGui Table API distributes the
// available width according to the sizing policy (default: stretch equally)
// and supports resizable columns, proper clipping, and alternating row bg.
//
// We do NOT call any imgui_md base-class table methods — the base class uses
// private cursor-tracking state (m_table_col_pos, m_table_row_pos, etc.) that
// would conflict with the ImGui Table API.
// ---------------------------------------------------------------------------

void ofxMarkdownRenderer::BLOCK_TABLE(const MD_BLOCK_TABLE_DETAIL* d, bool e) {
    if (e) {
        m_tableColCount = static_cast<int>(d->col_count);
        m_tableActive   = false;
        m_tableSkipping = false;

        if (m_tableColCount <= 0) {
            m_tableSkipping = true;
            return;
        }

        // Ensure we're on a fresh line before starting the table widget.
        ImGui::NewLine();

        char id[32];
        snprintf(id, sizeof(id), "##mdt%d", m_tableId++);

        // BeginTable returns false when the table is clipped (e.g. scrolled
        // fully out of view).  ALL subsequent table calls must be guarded by
        // m_tableActive — calling EndTable / TableNextRow on a false-begin is UB.
        m_tableActive = ImGui::BeginTable(id, m_tableColCount, tableFlags);
        m_tableSkipping = !m_tableActive;
        if (m_tableActive) {
            for (int i = 0; i < m_tableColCount; ++i)
                ImGui::TableSetupColumn("");
        }
    } else {
        if (m_tableActive) {
            ImGui::EndTable();
            m_tableActive = false;
        }
        m_tableSkipping = false;
        ImGui::NewLine();
    }
}

void ofxMarkdownRenderer::BLOCK_THEAD(bool e) {
    m_is_table_header = e;
    m_tableInHeader   = e;
}

void ofxMarkdownRenderer::BLOCK_TBODY(bool e) {
    // Do NOT set m_is_table_body — the base render_text() would then try to
    // read m_table_col_pos (which we never populate).  Leaving it false lets
    // render_text() use GetContentRegionAvail().x, which is correct inside an
    // ImGui Table cell.
    (void)e;
}

void ofxMarkdownRenderer::BLOCK_TR(bool e) {
    if (!m_tableActive) return;
    if (e) {
        ImGui::TableNextRow(m_tableInHeader ? ImGuiTableRowFlags_Headers
                                            : ImGuiTableRowFlags_None);
    }
}

void ofxMarkdownRenderer::BLOCK_TH(const MD_BLOCK_TD_DETAIL* d, bool e) {
    if (!m_tableActive) return;
    if (e) {
        ImGui::TableNextColumn();
        if (boldFont) ImGui::PushFont(boldFont);
    } else {
        if (boldFont) ImGui::PopFont();
    }
    (void)d;
}

void ofxMarkdownRenderer::BLOCK_TD(const MD_BLOCK_TD_DETAIL* d, bool e) {
    if (!m_tableActive) return;
    if (e) {
        ImGui::TableNextColumn();

        // Best-effort alignment nudge.  Full pre-measurement would require a
        // two-pass renderer; this heuristic is sufficient for typical content.
        if (d->align == MD_ALIGN_CENTER || d->align == MD_ALIGN_RIGHT) {
            const float avail = ImGui::GetContentRegionAvail().x;
            const float pad   = (d->align == MD_ALIGN_CENTER) ? avail * 0.25f
                                                               : avail * 0.45f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
        }
    }
}

// ---------------------------------------------------------------------------
// render_text override — proper word-wrap and emergency hyphenation
//
// imgui_md's base implementation relies on ImGui's CalcWordWrapPositionA and
// falls back to `++te` (one byte at a time) when no break point fits in the
// remaining line width.  That causes mid-word breaks with no hyphen.
//
// Strategy:
//   1. Scan for our own word boundaries.
//   2. On failure (te == str): emit NewLine() and retry with the full column
//      width.  This moves the word cleanly to the next line.
//   3. If the word is still too wide (wider than the full column itself),
//      break it at the last character that fits alongside a '-' glyph.
//
// Decoration drawing (links, underlines, strikethrough) is replicated here
// because imgui_md::line() was private; it is now a protected static so we
// can call it directly.
// ---------------------------------------------------------------------------

// Advance one UTF-8 codepoint, clamped to [s, end).
static const char* utf8_advance(const char* s, const char* end) {
    if (s >= end) return end;
    unsigned char c = static_cast<unsigned char>(*s);
    int bytes;
    if      (c < 0x80) bytes = 1;
    else if (c < 0xE0) bytes = 2;
    else if (c < 0xF0) bytes = 3;
    else               bytes = 4;
    const char* next = s + bytes;
    return (next <= end) ? next : end;
}

static bool is_wrap_space(char c) {
    return c == ' ' || c == '\t';
}

static bool is_explicit_line_break(char c) {
    return c == '\n' || c == '\r';
}

static const char* find_word_wrap_position(ImFont* font,
                                            float scale,
                                            const char* str,
                                            const char* str_end,
                                            float wrap_width,
                                            const std::string& language) {
    if (wrap_width <= 1.0f)
        return str;

#if OFX_IMGUI_MARKDOWN_HAS_OFX_UNICODE
    // Unicode line breaking from ofxUnicode/libunibreak.  The break result is
    // per codepoint, so walk the original UTF-8 byte range in parallel.
    try {
        std::string text(str, str_end);
        std::u32string text32 = ofxUTF8::toUTF32(text);
        std::vector<ofxLinebreaker::BreakType> breaks =
            ofxLinebreaker::findBreaks(text32, language);

        const char* p = str;
        const char* lastBreak = nullptr;

        for (std::size_t i = 0; i < text32.size() && p < str_end; ++i) {
            if (is_explicit_line_break(*p))
                return p;

            const char* next = utf8_advance(p, str_end);
            const float w = font->CalcTextSizeA(scale, FLT_MAX, 0.0f, str, next).x;

            if (w > wrap_width)
                return lastBreak ? lastBreak : str;

            if (i < breaks.size()) {
                const ofxLinebreaker::BreakType br = breaks[i];
                if (br == ofxLinebreaker::BreakType::MUST_BREAK)
                    return next;
                if (br == ofxLinebreaker::BreakType::ALLOW_BREAK)
                    lastBreak = next;
            }

            p = next;
        }

        return str_end;
    } catch (...) {
        // Fall through to the ASCII-ish fallback below.  We still prefer a
        // usable renderer over failing a frame because of malformed text.
    }
#else
    (void)language;
#endif

    const char* p = str;
    const char* lastBreak = nullptr;

    while (p < str_end) {
        if (is_explicit_line_break(*p))
            return p;

        const char* next = utf8_advance(p, str_end);
        const float w = font->CalcTextSizeA(scale, FLT_MAX, 0.0f, str, next).x;

        if (w > wrap_width)
            return lastBreak ? lastBreak : str;

        if (is_wrap_space(*p) || *p == '-' || *p == '/')
            lastBreak = p;

        p = next;
    }

    return str_end;
}

void ofxMarkdownRenderer::render_text(const char* str, const char* str_end) {
    if (m_tableSkipping) return;

    if (m_inCodeBlock && useTextEditorForCodeBlocks) {
        // Accumulate — the editor is rendered once in BLOCK_CODE leave.
        accumulateCodeText(m_pendingCodeText, str, str_end);
        return;
    }

    const float scale = ImGui::GetIO().FontGlobalScale;
    const ImGuiStyle& s = ImGui::GetStyle();
    ImFont* font = ImGui::GetFont();
    bool is_lf = false;

    while (!m_is_image && str < str_end) {

        const char* te = str_end;

        if (!m_is_table_header) {

            float wl = ImGui::GetContentRegionAvail().x;

            // (Intentionally do not replicate the m_is_table_body branch —
            // our table override keeps m_is_table_body false and lets
            // GetContentRegionAvail() reflect the ImGui Table column width.)

            te = find_word_wrap_position(font, scale, str, str_end, wl, wrapLanguage);

            if (te == str) {
                // ── No break point fits in the remaining line width ──────────

                // Explicit line breaks can also produce te == str. Consume
                // them immediately; otherwise the hyphenation path sees an
                // empty word and the input pointer never advances.
                if (is_explicit_line_break(*str)) {
                    ImGui::NewLine();
                    is_lf = true;
                    ++str;
                    continue;
                }

                // Step 1: try a fresh line.  After NewLine() the cursor moves
                // to the current indentation and GetContentRegionAvail() gives
                // the true column width from that point.
                ImGui::NewLine();
                is_lf = true;
                wl = ImGui::GetContentRegionAvail().x;
                te = find_word_wrap_position(font, scale, str, str_end, wl, wrapLanguage);

                if (te == str) {
                    // ── Word is wider than the whole column — hyphenate ──────

                    // Same guard after the fresh-line retry: keep every loop
                    // path consuming at least one byte.
                    if (is_explicit_line_break(*str)) {
                        ImGui::NewLine();
                        is_lf = true;
                        ++str;
                        continue;
                    }

                    // Find the end of this word (next space or newline).
                    const char* word_end = str;
                    while (word_end < str_end &&
                           !is_wrap_space(*word_end) &&
                           !is_explicit_line_break(*word_end))
                        word_end = utf8_advance(word_end, str_end);

                    const float hyphen_w = font->CalcTextSizeA(
                        scale, FLT_MAX, 0.0f, "-").x;
                    const float budget = wl - hyphen_w;

                    const char* frag = str;
                    if (budget > 0.0f) {
                        // Walk forward until fragment + '-' would exceed budget.
                        while (frag < word_end) {
                            const char* next = utf8_advance(frag, word_end);
                            float w = font->CalcTextSizeA(
                                scale, FLT_MAX, 0.0f, str, next).x;
                            if (w > budget) break;
                            frag = next;
                        }
                    }
                    // Always advance at least one codepoint to avoid an
                    // infinite loop in degenerate (very narrow) columns.
                    if (frag == str)
                        frag = utf8_advance(str, word_end);
                    if (frag == str)
                        frag = utf8_advance(str, str_end);

                    // Render fragment, hyphen, then loop for remainder.
                    ImGui::TextUnformatted(str, frag);
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextUnformatted("-");
                    ImGui::NewLine();
                    is_lf = true;

                    str = frag;
                    while (str < str_end && is_wrap_space(*str)) ++str;
                    continue;
                }
            }
        }

        // Inline code spans: draw a pill-shaped background behind the text
        // using the same colour as fenced code blocks, before drawing the text
        // itself so the glyph pixels render on top.
        if (m_is_code && !m_inCodeBlock) {
            const ImVec2 p   = ImGui::GetCursorScreenPos();
            const float  w   = font->CalcTextSizeA(scale, FLT_MAX, 0.0f, str, te).x;
            const float  h   = ImGui::GetTextLineHeight();
            const float  pad = 3.0f;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x - pad,     p.y - 1.0f),
                ImVec2(p.x + w + pad, p.y + h + 1.0f),
                ImGui::ColorConvertFloat4ToU32(codeBlockBg), 3.0f);
        }

        ImGui::TextUnformatted(str, te);

        // Track only the LAST rendered fragment.  Intermediate NewLine() calls
        // for word-wrap must not keep is_lf=true after we've drawn more text —
        // that would suppress the final SameLine and cause the next render_text
        // call (e.g. a lone '.') to start on the wrong line.
        is_lf = (te > str && *(te - 1) == '\n');

        // Decorations — replicated from imgui_md::line() (now protected static).
        if (!m_href.empty()) {
            ImVec4 c;
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", m_href.c_str());
                c = s.Colors[ImGuiCol_ButtonHovered];
                if (ImGui::IsMouseReleased(0)) open_url();
            } else {
                c = s.Colors[ImGuiCol_Button];
            }
            line(ImColor(c), true);
        }
        if (m_is_underline)     line(ImColor(s.Colors[ImGuiCol_Text]), true);
        if (m_is_strikethrough) line(ImColor(s.Colors[ImGuiCol_Text]), false);

        str = te;
        while (str < str_end && is_wrap_space(*str)) ++str;
    }

    if (!is_lf) ImGui::SameLine(0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// Free-function convenience API
// ---------------------------------------------------------------------------

void ofxRenderMarkdown(const std::string& markdown) {
    ofxRenderMarkdown(markdown.c_str(), markdown.c_str() + markdown.size());
}

void ofxRenderMarkdown(const char* str, const char* str_end) {
    static ofxMarkdownRenderer s_renderer;
    s_renderer.render(str, str_end);
}
