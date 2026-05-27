#include "ofxImGuiMarkdown.h"
#include "ofUtils.h"     // ofLaunchBrowser
#include "ofGraphics.h"  // ofEnableArbTex, ofDisableArbTex, ofGetUsingArbTex
#include "ofFileUtils.h" // ofFilePath, ofFile, ofBufferFromFile
#include "imgui_internal.h"
#include <algorithm>     // std::transform
#include <cctype>        // std::tolower
#include <cstring>       // strlen

namespace {
float verticalScrollbarGutterWidth() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window || (window->Flags & ImGuiWindowFlags_NoScrollbar))
        return 0.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    if (window->Flags & ImGuiWindowFlags_AlwaysVerticalScrollbar)
        return style.ScrollbarSize;
    if (window->ScrollMax.y > 0.0f)
        return style.ScrollbarSize;
    return 0.0f;
}

void blockVerticalGap(float lineMultiple) {
    const float gap = ImGui::GetTextLineHeight() * lineMultiple;
    if (gap > 0.0f)
        ImGui::Dummy(ImVec2(1.0f, gap));
}

// Filled disc bullet — no ImFont glyph queries (ImGui font APIs vary by version).
void drawUnorderedListBullet(float& outMarkerW) {
    const float spacing  = ImGui::GetStyle().ItemSpacing.x;
    const float lineH    = ImGui::GetTextLineHeight();
    const float fontSize = ImGui::GetFontSize();
    const float r        = fontSize * 0.125f;
    const float pad      = fontSize * 0.15f;
    const ImVec2 pos     = ImGui::GetCursorScreenPos();

    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(pos.x + pad + r, pos.y + lineH * 0.5f),
        r,
        ImGui::GetColorU32(ImGuiCol_Text));

    outMarkerW = pad + r * 2.0f + spacing * 2.0f;
    ImGui::Indent(outMarkerW);
}
} // namespace

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
    m_pendingSpaceAfterInline = false;
    m_renderInlineAfter       = false;

    if (regularFont)
        ImGui::PushFont(regularFont);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const bool clipToViewport = window && window->WorkRect.GetWidth() > 1.0f;
    if (clipToViewport) {
        ImVec2 clipMin = window->WorkRect.Min;
        ImVec2 clipMax = { layoutRightEdgeX(), window->WorkRect.Max.y };
        ImGui::PushClipRect(clipMin, clipMax, true);
    }

    if (contentPadding.x > 0.0f)
        ImGui::Indent(contentPadding.x);

    print(str, str_end);

    if (m_renderInlineAfter) {
        ImGui::NewLine();
        m_renderInlineAfter = false;
    }
    ImGui::Dummy(ImVec2(1.0f, 1.0f));

    if (contentPadding.x > 0.0f)
        ImGui::Unindent(contentPadding.x);

    if (clipToViewport)
        ImGui::PopClipRect();

    if (regularFont)
        ImGui::PopFont();
}

float ofxMarkdownRenderer::layoutRightEdgeX() const {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window)
        return ImGui::GetCursorScreenPos().x + std::max(ImGui::GetContentRegionAvail().x, 1.0f);

    // Use the tighter of layout and draw clip rects, then reserve the scrollbar
    // overlay (ImGui draws the scrollbar on top of the inner rect, not beside it).
    float edge = window->InnerClipRect.Max.x;
    if (window->WorkRect.GetWidth() > 1.0f)
        edge = std::min(edge, window->WorkRect.Max.x);
    edge -= contentPadding.y;
    edge -= verticalScrollbarGutterWidth();
    return edge;
}

float ofxMarkdownRenderer::wrapWidth() const {
    if (m_tableActive)
        return std::max(ImGui::GetContentRegionAvail().x - contentPadding.y, 1.0f);

    return std::max(layoutRightEdgeX() - ImGui::GetCursorScreenPos().x, 1.0f);
}

void ofxMarkdownRenderer::finishInlineLayout() {
    // Clear inline continuation only — block handlers call NewLine() themselves.
    // Calling NewLine() here stacked with heading scale and caused huge gaps.
    m_renderInlineAfter = false;
}

void ofxMarkdownRenderer::SPAN_A(const MD_SPAN_A_DETAIL* d, bool e) {
    imgui_md::SPAN_A(d, e);
    if (!e && !m_is_image) {
        m_pendingSpaceAfterInline = true;
        m_renderInlineAfter       = true;
    }
}

void ofxMarkdownRenderer::SPAN_IMG(const MD_SPAN_IMG_DETAIL* d, bool e) {
    if (e) {
        finishInlineLayout();
        blockVerticalGap(verticalBlockGapLines);
    }
    imgui_md::SPAN_IMG(d, e);
    if (!e)
        blockVerticalGap(verticalBlockGapLines);
}

void ofxMarkdownRenderer::SPAN_WIKILINK(const MD_SPAN_WIKILINK_DETAIL* d, bool e) {
    imgui_md::SPAN_WIKILINK(d, e);
    if (!e && !m_is_image) {
        m_pendingSpaceAfterInline = true;
        m_renderInlineAfter       = true;
    }
}

void ofxMarkdownRenderer::SPAN_EM(bool e) {
    if (italicFont || boldItalicFont) {
        ImFont* f = (m_is_strong && boldItalicFont) ? boldItalicFont : italicFont;
        m_is_em = e;
        if (f) {
            if (e) {
                if (m_renderInlineAfter) {
                    ImGui::SameLine(0.0f, 0.0f);
                    m_renderInlineAfter = false;
                }
                ImGui::PushFont(f);
            } else {
                ImGui::PopFont();
            }
        }
    } else {
        imgui_md::SPAN_EM(e);
    }
}

void ofxMarkdownRenderer::SPAN_STRONG(bool e) {
    if (boldFont || boldItalicFont) {
        ImFont* f = (m_is_em && boldItalicFont) ? boldItalicFont : boldFont;
        m_is_strong = e;
        if (f) {
            if (e) {
                if (m_renderInlineAfter) {
                    ImGui::SameLine(0.0f, 0.0f);
                    m_renderInlineAfter = false;
                }
                ImGui::PushFont(f);
            } else {
                ImGui::PopFont();
            }
        }
    } else {
        imgui_md::SPAN_STRONG(e);
    }
}

ImFont* ofxMarkdownRenderer::get_font() const {
    if (m_hlevel == 1) return headingFont;
    if (m_hlevel == 2) return h2Font;
    if (m_hlevel >= 3 && m_hlevel <= 6)
        return boldFont ? boldFont : regularFont; // H3–H6 size from headingScale in BLOCK_H
    if (m_is_table_header && boldFont) return boldFont;
    if (m_is_strong && m_is_em && boldItalicFont) return boldItalicFont;
    if (m_is_em && italicFont) return italicFont;
    if (m_is_strong && boldFont) return boldFont;
    if (m_is_code  && monoFont)  return monoFont; // inline code spans
    return regularFont; // nullptr → ImGui default
}

ImVec4 ofxMarkdownRenderer::get_color() const {
    if (!m_href.empty()) {
        return ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered];
    }
    return ImGui::GetStyle().Colors[ImGuiCol_Text];
}

namespace
{
    bool looksLikeUrl(const std::string& href)
    {
        if (href.find("://") != std::string::npos)
            return true;
        if (href.size() >= 4 && href.compare(0, 4, "www.") == 0)
            return true;
        return false;
    }

    void splitWikiTarget(const std::string& target,
                         std::string& page,
                         std::string& anchor)
    {
        page   = target;
        anchor.clear();
        const size_t hash = page.find('#');
        if (hash != std::string::npos) {
            anchor = page.substr(hash + 1);
            page.resize(hash);
        }
    }

    bool isRemoteImageSource(const std::string& href)
    {
        return href.find("://") != std::string::npos;
    }

    bool isSvgPath(const std::string& path)
    {
        std::string ext = ofFilePath::getFileExt(path);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == "svg";
    }

    // Resolve local paths: cwd (bin/), then data folder via ofToDataPath().
    // URLs are returned unchanged.
    std::string resolveImagePath(const std::string& href)
    {
        if (href.empty() || isRemoteImageSource(href))
            return href;

        if (ofFile::doesFileExist(href))
            return href;

        const std::string inData = ofToDataPath(href, true);
        if (inData != href && ofFile::doesFileExist(inData))
            return inData;

        const std::string absolute = ofToDataPath(href, false);
        if (ofFile::doesFileExist(absolute))
            return absolute;

        return href;
    }

    bool loadRasterImage(const std::string& path, std::shared_ptr<ofImage>& out)
    {
        const bool wasArb = ofGetUsingArbTex();
        ofDisableArbTex();

        auto img = std::make_shared<ofImage>();
        const bool ok = img->load(path);

        if (wasArb)
            ofEnableArbTex();

        if (ok && img->getTexture().isAllocated()) {
            out = std::move(img);
            return true;
        }
        return false;
    }

    bool loadSvgImage(const std::string& path,
                      std::shared_ptr<ofFbo>& out,
                      float svgScale)
    {
        auto svgDoc = std::make_shared<ofxSvg>();
        if (!svgDoc->load(path))
            return false;

        float svgW = svgDoc->getWidth();
        float svgH = svgDoc->getHeight();
        if (svgW < 1.0f) svgW = 256.0f;
        if (svgH < 1.0f) svgH = 256.0f;

        auto fbo = std::make_shared<ofFbo>();
        ofFboSettings fboSettings;
        fboSettings.width          = std::round(svgW * svgScale);
        fboSettings.height         = std::round(svgH * svgScale);
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

        out = std::move(fbo);
        return true;
    }
}

std::string ofxMarkdownRenderer::resolveWikiPagePath(const std::string& target,
                                                     std::string* outAnchor) const
{
    std::string page;
    std::string anchor;
    splitWikiTarget(target, page, anchor);

    if (outAnchor)
        *outAnchor = anchor;

    if (page.empty() || looksLikeUrl(page))
        return page;

    std::string path = page;
    const bool hasSep = page.find('/') != std::string::npos
                     || page.find('\\') != std::string::npos;
    if (!wikiLinkBasePath.empty() && !hasSep && !ofFilePath::isAbsolute(page))
        path = ofFilePath::join(wikiLinkBasePath, page);

    std::string ext = ofFilePath::getFileExt(path);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext.empty()) {
        if (ofFile::doesFileExist(path + ".md"))
            path += ".md";
        else if (ofFile::doesFileExist(path + ".markdown"))
            path += ".markdown";
    }

    return path;
}

void ofxMarkdownRenderer::open_url() const {
    if (m_href.empty())
        return;

    if (m_is_wikilink) {
        if (onWikiLinkClicked) {
            onWikiLinkClicked(m_href);
            return;
        }

        std::string anchor;
        const std::string pagePath = resolveWikiPagePath(m_href, &anchor);

        if (!pagePath.empty() && looksLikeUrl(pagePath)) {
            std::string url = pagePath;
            if (!anchor.empty())
                url += "#" + anchor;
            ofLaunchBrowser(url);
            return;
        }

        if (pagePath.empty() && !anchor.empty()) {
            ofLogVerbose("ofxMarkdownRenderer")
                << "Same-document wiki anchor (set onWikiLinkClicked to handle): #"
                << anchor;
            return;
        }

        if (!pagePath.empty() && ofFile::doesFileExist(pagePath)) {
            ofLogVerbose("ofxMarkdownRenderer")
                << "Wiki page exists at " << pagePath
                << " (set onWikiLinkClicked to load it)";
            return;
        }

        ofLogWarning("ofxMarkdownRenderer") << "Wiki page not found: " << m_href;
        return;
    }

    ofLaunchBrowser(m_href);
}

bool ofxMarkdownRenderer::get_image(image_info& nfo) const {
    if (m_href.empty()) return false;

    auto it = m_imageCache.find(m_href);
    if (it == m_imageCache.end()) {
        CachedImage entry;
        const std::string path = resolveImagePath(m_href);
        bool loaded = false;

        if (isSvgPath(path)) {
            loaded = loadSvgImage(path, entry.fbo, svgScale);
        } else {
            // All raster formats that ofImage / FreeImage can read (PNG, JPEG,
            // GIF, TIFF, BMP, PSD, EXR, HDR, …) plus http(s):// URLs.
            loaded = loadRasterImage(path, entry.raster);
        }

        if (!loaded) {
            entry.failed = true;
            ofLogVerbose("ofxMarkdownRenderer") << "Image not loaded: " << m_href;
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
    const float limit = (maxImageWidth > 0.0f) ? maxImageWidth : wrapWidth();
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
    // Markdown soft line break — stay inline (like a space), do not NewLine().
    m_renderInlineAfter = true;
}

int ofxMarkdownRenderer::text(MD_TEXTTYPE type, const char* str, const char* str_end) {
    switch (type) {
    case MD_TEXT_NORMAL:
        render_text(str, str_end);
        break;
    case MD_TEXT_CODE:
        render_text(str, str_end);
        break;
    case MD_TEXT_NULLCHAR:
        break;
    case MD_TEXT_BR:
        // Single source newlines inside a paragraph are soft breaks in CommonMark.
        // Treat them as inline continuation so "link\nand" stays on one flow.
        if (!m_inCodeBlock && m_hlevel == 0 && m_list_stack.empty())
            soft_break();
        else
            ImGui::NewLine();
        break;
    case MD_TEXT_SOFTBR:
        soft_break();
        break;
    case MD_TEXT_ENTITY:
        if (!render_entity(str, str_end))
            render_text(str, str_end);
        break;
    case MD_TEXT_HTML:
        if (!check_html(str, str_end))
            render_text(str, str_end);
        break;
    case MD_TEXT_LATEXMATH:
        render_text(str, str_end);
        break;
    default:
        break;
    }

    if (m_is_table_header) {
        const float x = ImGui::GetCursorPosX();
        if (x > m_table_last_pos.x)
            m_table_last_pos.x = x;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Headings — synthetic font scaling when custom heading fonts are not loaded
// ---------------------------------------------------------------------------

void ofxMarkdownRenderer::BLOCK_P(bool e) {
    if (!e)
        return;
    if (!m_list_stack.empty())
        return;
    finishInlineLayout();
    ImGui::Spacing();
}

void ofxMarkdownRenderer::BLOCK_UL(const MD_BLOCK_UL_DETAIL* d, bool e) {
    if (e) {
        m_list_stack.push_back(list_info{ 0, d->mark, false });
    } else if (!m_list_stack.empty()) {
        m_list_stack.pop_back();
    }
}

void ofxMarkdownRenderer::BLOCK_OL(const MD_BLOCK_OL_DETAIL* d, bool e) {
    if (e) {
        m_list_stack.push_back(list_info{ d->start, d->mark_delimiter, true });
    } else if (!m_list_stack.empty()) {
        m_list_stack.pop_back();
    }
}

void ofxMarkdownRenderer::BLOCK_HR(bool e) {
    if (e)
        return;
    finishInlineLayout();
    blockVerticalGap(verticalBlockGapLines * 0.9f);
    ImGui::Separator();
    blockVerticalGap(verticalBlockGapLines * 0.9f);
}

void ofxMarkdownRenderer::BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e) {
    const unsigned lv = std::min(d->level, 6u);

    ImFont* levelFont = (lv == 1) ? headingFont
                      : (lv == 2) ? h2Font
                      : nullptr;
    const float scale = headingScale[lv];
    const bool  doScale = !levelFont && scale > 1.001f;

    if (e) {
        finishInlineLayout();
        if (ImGui::GetCursorPosY() > ImGui::GetWindowContentRegionMin().y + 0.5f)
            blockVerticalGap(verticalBlockGapLines);
        m_hlevel          = lv;
        m_activeHeadScale = doScale ? scale : 1.0f;
        set_font(true);
        if (doScale)
            ImGui::SetWindowFontScale(scale);
    } else {
        set_font(false);
        if (m_activeHeadScale > 1.001f)
            ImGui::SetWindowFontScale(1.0f);
        m_activeHeadScale = 1.0f;
        m_hlevel          = 0;
        if (lv <= 2) {
            ImGui::Spacing();
            ImGui::Separator();
        }
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
    if (s == "md" || s == "markdown")
        return TextEditor::LanguageDefinitionId::Markdown;
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
        cache.editor->SetSoftWrapEnabled(false);
    }

    // Strip trailing newlines MD4C appends to fenced blocks (avoid empty lines).
    std::string text = m_pendingCodeText;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();

    const TextEditor::LanguageDefinitionId language = languageFromFence(m_codeLanguage);
    if (cache.text != text) {
        cache.text = text;
        cache.editor->SetText(cache.text);
    }
    if (cache.language != language) {
        cache.language = language;
        cache.editor->SetLanguageDefinition(cache.language);
    }

    ImFont* codeFont = monoFont ? monoFont : regularFont;
    if (codeFont)
        ImGui::PushFont(codeFont);

    const int lineCount = cache.editor->GetLineCount();
    const float lineH = ImGui::GetTextLineHeightWithSpacing() * cache.editor->GetLineSpacing();
    const float pad   = ImGui::GetStyle().FramePadding.y * 2.0f + 2.0f;
    const float height = std::min(codeBlockMaxHeight, lineH * lineCount + pad);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, codeBlockBg);
    char id[32];
    snprintf(id, sizeof(id), "##mdcode%d", index);
    cache.editor->Render(id, false, ImVec2(wrapWidth(), height), false);
    if (codeFont)
        ImGui::PopFont();
    ImGui::PopStyleColor();
}

void ofxMarkdownRenderer::BLOCK_CODE(const MD_BLOCK_CODE_DETAIL* d, bool e) {
    imgui_md::BLOCK_CODE(d, e); // sets m_is_code / m_is_table_body flags in base

    if (e) {
        finishInlineLayout();
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
        finishInlineLayout();
        ImGui::Spacing();
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
        ImGui::Spacing();
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
        // ImGui already advances to the next line for each widget without
        // SameLine(); an extra NewLine() here doubled the gap between items.
        if (listItemSpacing > 0.0f)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + listItemSpacing);

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
            // Draw a bullet disc (ImDrawList) — works with any font; avoids
            // ImGui::Bullet() SameLine/Indent interaction and '?' for U+2022.
            float markerW = 0.0f;
            drawUnorderedListBullet(markerW);
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
        finishInlineLayout();
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

        // Pin table width to the visible column so wide tables do not widen the
        // scroll canvas and break wrapping for text above.
        const float tableWidth = wrapWidth();
        m_tableActive = ImGui::BeginTable(id, m_tableColCount, tableFlags,
                                          ImVec2(tableWidth, 0.0f));
        m_tableSkipping = !m_tableActive;
        if (!m_tableActive)
            return;
        for (int i = 0; i < m_tableColCount; ++i)
            ImGui::TableSetupColumn("");
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

static bool skip_soft_line_break(const char*& str, const char* str_end) {
    if (str >= str_end || !is_explicit_line_break(*str))
        return false;
    if (*str == '\r' && str + 1 < str_end && *(str + 1) == '\n')
        str += 2;
    else
        ++str;
    return true;
}

// When breaking at a whitespace separator, include that character in the
// rendered chunk so it is not dropped by later skip logic.
static const char* advance_break_past_separator(const char* lastBreak) {
    if (lastBreak && is_wrap_space(*lastBreak))
        return lastBreak + 1;
    return lastBreak;
}

// After rendering [str, te), emit any separator spaces before the next chunk.
static void consume_in_run_separator_spaces(const char*& str, const char* str_end) {
    while (str < str_end && is_wrap_space(*str)) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(str, str + 1);
        ++str;
    }
}

static float markdownWrapWidth(const ofxMarkdownRenderer* renderer) {
    return renderer ? renderer->wrapWidth() : std::max(ImGui::GetContentRegionAvail().x, 1.0f);
}

static const char* clampToRemainingWidth(ImFont* font,
                                          float fontSize,
                                          const char* str,
                                          const char* str_end,
                                          const char* te,
                                          float maxWidth) {
    if (te <= str || maxWidth <= 1.0f)
        return te;
    if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str, te).x <= maxWidth)
        return te;

    const char* best = str;
    for (const char* p = str; p < te; ) {
        const char* next = utf8_advance(p, te);
        if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str, next).x > maxWidth)
            break;
        best = next;
        p = next;
    }
    return (best > str) ? best : utf8_advance(str, str_end);
}

static const char* find_word_wrap_position(ImFont* font,
                                            float fontSize,
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
            const float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str, next).x;

            if (w > wrap_width)
                return advance_break_past_separator(lastBreak ? lastBreak : str);

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
        const float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str, next).x;

        if (w > wrap_width)
            return advance_break_past_separator(lastBreak ? lastBreak : str);

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

    if (m_renderInlineAfter) {
        ImGui::SameLine(0.0f, 0.0f);
        m_renderInlineAfter = false;
    }

    if (m_pendingSpaceAfterInline && str < str_end) {
        m_pendingSpaceAfterInline = false;
        if (!is_wrap_space(*str) && *str != ')' && *str != '.' && *str != ',' &&
            *str != ';' && *str != ':' && *str != '!' && *str != '?')
        {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted(" ", str + 1);
        }
    }

    while (skip_soft_line_break(str, str_end))
        m_renderInlineAfter = true;
    if (str >= str_end)
        return;

    const float fontSize = ImGui::GetFontSize();
    const ImGuiStyle& s = ImGui::GetStyle();
    ImFont* font = ImGui::GetFont();
    bool is_lf = false;

    while (!m_is_image && str < str_end) {

        const char* te = str_end;

        if (!m_is_table_header) {

            float wl = markdownWrapWidth(this);

            te = find_word_wrap_position(font, fontSize, str, str_end, wl, wrapLanguage);

            if (te == str) {
                // ── No break point fits in the remaining line width ──────────

                // Explicit line breaks can also produce te == str. Consume
                // them immediately; otherwise the hyphenation path sees an
                // empty word and the input pointer never advances.
                if (is_explicit_line_break(*str)) {
                    skip_soft_line_break(str, str_end);
                    m_renderInlineAfter = true;
                    continue;
                }

                // Step 1: try a fresh line.  After NewLine() the cursor moves
                // to the current indentation and GetContentRegionAvail() gives
                // the true column width from that point.
                ImGui::NewLine();
                is_lf = true;
                wl = markdownWrapWidth(this);
                te = find_word_wrap_position(font, fontSize, str, str_end, wl, wrapLanguage);

                if (te == str) {
                    // ── Word is wider than the whole column — hyphenate ──────

                    // Same guard after the fresh-line retry: keep every loop
                    // path consuming at least one byte.
                    if (is_explicit_line_break(*str)) {
                        skip_soft_line_break(str, str_end);
                        m_renderInlineAfter = true;
                        continue;
                    }

                    // Find the end of this word (next space or newline).
                    const char* word_end = str;
                    while (word_end < str_end &&
                           !is_wrap_space(*word_end) &&
                           !is_explicit_line_break(*word_end))
                        word_end = utf8_advance(word_end, str_end);

                    const float hyphen_w = font->CalcTextSizeA(
                        fontSize, FLT_MAX, 0.0f, "-").x;
                    const float budget = wl - hyphen_w;

                    const char* frag = str;
                    if (budget > 0.0f) {
                        while (frag < word_end) {
                            const char* next = utf8_advance(frag, word_end);
                            float w = font->CalcTextSizeA(
                                fontSize, FLT_MAX, 0.0f, str, next).x;
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
                    consume_in_run_separator_spaces(str, str_end);
                    continue;
                }
            }

            // Safety net: find_word_wrap can overshoot when layout width shrinks after
            // earlier SameLine fragments; never draw past the current remaining width.
            wl = markdownWrapWidth(this);
            te = clampToRemainingWidth(font, fontSize, str, str_end, te, wl);
            if (te == str && str < str_end && !is_explicit_line_break(*str)) {
                ImGui::NewLine();
                is_lf = true;
                continue;
            }
        }

        // Inline code spans: draw a pill-shaped background behind the text
        // using the same colour as fenced code blocks, before drawing the text
        // itself so the glyph pixels render on top.
        if (m_is_code && !m_inCodeBlock) {
            const ImVec2 p   = ImGui::GetCursorScreenPos();
            const float  w   = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str, te).x;
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
        consume_in_run_separator_spaces(str, str_end);
        if (str < str_end)
            ImGui::SameLine(0.0f, 0.0f);
    }

    if (!is_lf)
        m_renderInlineAfter = true;
    else
        m_renderInlineAfter = false;
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
