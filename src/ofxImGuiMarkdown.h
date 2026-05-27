#pragma once

#include "imgui_md.h"
#include "ofImage.h"
#include "ofFbo.h"
#include "ofxSvg.h"
#include "ofxImGuiTextEdit.h"
#if defined(__has_include)
  #if __has_include("ofxUnicode.h")
    #include "ofxUnicode.h"
    #define OFX_IMGUI_MARKDOWN_HAS_OFX_UNICODE 1
  #else
    #define OFX_IMGUI_MARKDOWN_HAS_OFX_UNICODE 0
  #endif
#else
  #define OFX_IMGUI_MARKDOWN_HAS_OFX_UNICODE 0
#endif
#include <functional>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ofxMarkdownRenderer — a concrete openFrameworks-aware subclass of imgui_md.
//
// Call render() from inside an ImGui window or child window to render a
// markdown string using the current ImGui draw list.
//
// Font pointers default to nullptr, which means imgui_md falls back to
// ImGui's current default font for all text. Assign custom fonts after
// gui.setup() to get distinct heading and body weights.
//
// With ofxImGuiStyle (header-only, no disk files needed):
//
//   #include "ImFonts.h"
//   ImFont* ui = ImFonts::LoadDefaultFonts(ImGui::GetIO().Fonts, 15.f);
//   gui.setDefaultFont(ui);
//   ImFonts::JetBrainsMonoFonts jbm = ImFonts::LoadJetBrainsMono(ImGui::GetIO().Fonts, 15.f);
//   renderer.regularFont = jbm.regular;
//   renderer.boldFont       = jbm.bold;
//   renderer.italicFont     = jbm.italic;
//   renderer.boldItalicFont = jbm.boldItalic;
//   renderer.monoFont    = jbm.regular;
//   renderer.headingFont = ImFonts::LoadJetBrainsMonoFont(
//       ImGui::GetIO().Fonts, 22.f, ImFonts::JetBrainsMonoVariant::Bold);
//   gui.rebuildFontsTexture();
//
// Or load from disk via gui.addFont():
//
//   renderer.headingFont = gui.addFont(ofToDataPath("fonts/Bold.ttf"), 22.0f);
//   renderer.boldFont    = gui.addFont(ofToDataPath("fonts/Bold.ttf"), 15.0f);
//   renderer.italicFont  = gui.addFont(ofToDataPath("fonts/Italic.ttf"), 15.0f);
//   renderer.regularFont = gui.addFont(ofToDataPath("fonts/Regular.ttf"), 15.0f);

class ofxMarkdownRenderer : public imgui_md {
public:
    // ---------- Fonts ----------------------------------------------------------
    // All optional. When nullptr the ImGui default font is used for that style.
    ImFont* regularFont = nullptr;
    ImFont* boldFont       = nullptr;
    ImFont* italicFont     = nullptr;
    ImFont* boldItalicFont = nullptr; // bold + italic spans (nested or combined)
    ImFont* headingFont = nullptr; // H1
    ImFont* h2Font      = nullptr; // H2 only (H3–H6 use boldFont/regularFont + headingScale)
    ImFont* monoFont    = nullptr; // fenced code blocks

    // ---------- Heading scale --------------------------------------------------
    // When a heading font is NOT set for that level, the default font is scaled
    // by these factors. Index 0 unused; indices 1–6 correspond to H1–H6.
    // Default order: H1 (1.7) > H2 (1.22) > H3 (1.12) > H4 (1.05) > H5/H6 (1.0).
    float headingScale[7] = { 1.0f, 1.7f, 1.22f, 1.12f, 1.05f, 1.0f, 1.0f };

    // ---------- Style colours --------------------------------------------------
    ImVec4 codeBlockBg   = { 0.11f, 0.11f, 0.14f, 1.00f }; // fenced code bg
    ImVec4 quoteBarColor = { 0.40f, 0.45f, 0.90f, 1.00f }; // block-quote left bar
    ImVec4 quoteTextColor= { 0.72f, 0.74f, 0.82f, 1.00f }; // block-quote text

    // ---------- Code blocks ----------------------------------------------------
    // Fenced code blocks are rendered with a read-only ofxImGuiTextEdit editor
    // so language highlighting matches the source editor.
    bool  useTextEditorForCodeBlocks = true;
    float codeBlockMaxHeight = 320.0f;

    // ---------- Unicode wrapping ------------------------------------------------
    // Used by ofxUnicode/libunibreak when available.
    std::string wrapLanguage = "en";

    // Left/right inset for preview body text (not window padding). Right inset
    // is subtracted from WorkRect-based wrap width and the render clip rect.
    ImVec2 contentPadding = ImVec2(10.f, 6.f);

    // Vertical gap before/after HRs, before headings, and around inline images
    // (multiple of the current text line height).
    float verticalBlockGapLines = 0.5f;

    // ---------- Images ---------------------------------------------------------
    // Inline images use ofImage (FreeImage — all raster formats OF supports) plus
    // ofxSvg for .svg vector preview. Paths resolve via cwd, then ofToDataPath().
    // http(s):// URLs work for raster images (ofImage network load).
    // Maximum display width for inline images (pixels). 0 = fit to window.
    float maxImageWidth = 0.0f;

    // Supersampling factor used when rasterising SVG files into an ofFbo.
    // 1.0 = native SVG size; 2.0 = 2× resolution (crisper on HiDPI).
    float svgScale = 2.0f;

    // ---------- Tables ---------------------------------------------------------
    // SizingStretchSame: columns share the panel width. Do not combine with
    // ScrollX here — ScrollX uses a clipped child window and breaks layout when
    // outer_size is auto (tables vanish). Wide tables: resize columns or wrap.
    ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders         |
        ImGuiTableFlags_RowBg           |
        ImGuiTableFlags_SizingStretchSame |
        ImGuiTableFlags_Resizable       |
        ImGuiTableFlags_NoSavedSettings;

    // ---------- Lists ----------------------------------------------------------
    // Extra vertical pixels added before each list item (after the first).
    float listItemSpacing = 1.0f;

    // ---------- Wiki links -----------------------------------------------------
    // Base directory for resolving bare page names ("Notes" -> "data/Notes.md").
    // Ignored when the target already contains a path separator or looks like a URL.
    std::string wikiLinkBasePath;

    // Invoked when a [[wiki link]] is clicked. Receives the raw MD4C target string
    // (destination, before the optional |label). When empty, URL-like targets open
    // in the system browser; other targets are logged at verbose level.
    std::function<void(const std::string& target)> onWikiLinkClicked;

    // Resolve a wiki target to a local markdown file path (adds .md / .markdown,
    // applies wikiLinkBasePath). Returns empty when the target is URL-only or
    // anchor-only (#section). Optional outAnchor receives text after '#'.
    std::string resolveWikiPagePath(const std::string& target,
                                    std::string* outAnchor = nullptr) const;

    // ---------- API ------------------------------------------------------------
    // Render markdown inside the current ImGui window / child window.
    void render(const std::string& markdown);
    void render(const char* str, const char* str_end);

    // Release all cached image textures (e.g. on GL context rebuild).
    void clearImageCache();

    // Remaining horizontal width for word-wrap (accounts for scrollbar gutter).
    float wrapWidth() const;

protected:
    // ----- font / colour / URL overrides -----
    ImFont* get_font()  const override;
    ImVec4  get_color() const override;
    void    open_url()  const override;
    bool    get_image(image_info& nfo) const override;
    void    soft_break() override;
    int     text(MD_TEXTTYPE type, const char* str, const char* str_end) override;

    // ----- text rendering override (word-wrap + hyphenation) -----
    void render_text(const char* str, const char* str_end) override;

    // ----- span overrides -----
    void SPAN_A(const MD_SPAN_A_DETAIL* d, bool e) override;
    void SPAN_IMG(const MD_SPAN_IMG_DETAIL* d, bool e) override;
    void SPAN_WIKILINK(const MD_SPAN_WIKILINK_DETAIL* d, bool e) override;
    void SPAN_EM(bool e) override;
    void SPAN_STRONG(bool e) override;

    // ----- block overrides -----
    void BLOCK_P   (bool e) override;
    void BLOCK_UL  (const MD_BLOCK_UL_DETAIL*, bool e) override;
    void BLOCK_OL  (const MD_BLOCK_OL_DETAIL*, bool e) override;
    void BLOCK_HR  (bool e) override;
    void BLOCK_H   (const MD_BLOCK_H_DETAIL*,    bool e) override;
    void BLOCK_CODE (const MD_BLOCK_CODE_DETAIL*, bool e) override;
    void BLOCK_QUOTE(bool e) override;
    void BLOCK_LI   (const MD_BLOCK_LI_DETAIL*,  bool e) override;

    // ----- table overrides (replace imgui_md's manual cursor tracking) -----
    void BLOCK_TABLE(const MD_BLOCK_TABLE_DETAIL*, bool e) override;
    void BLOCK_THEAD(bool e) override;
    void BLOCK_TBODY(bool e) override;
    void BLOCK_TR   (bool e) override;
    void BLOCK_TH   (const MD_BLOCK_TD_DETAIL*, bool e) override;
    void BLOCK_TD   (const MD_BLOCK_TD_DETAIL*, bool e) override;

private:
    // Right edge of the text column in screen coordinates (clip + scrollbar safe).
    float layoutRightEdgeX() const;

    void finishInlineLayout();

    // Unified cache entry — holds either a raster ofImage or an SVG rendered
    // into an ofFbo. Marked mutable so get_image() (const) can populate it.
    struct CachedImage {
        std::shared_ptr<ofImage> raster;
        std::shared_ptr<ofFbo>   fbo;    // SVG rasterised at svgScale
        bool failed = false;

        const ofTexture* texture() const {
            if (fbo    && fbo->isAllocated())                 return &fbo->getTexture();
            if (raster && raster->getTexture().isAllocated()) return &raster->getTexture();
            return nullptr;
        }
    };
    mutable std::map<std::string, CachedImage> m_imageCache;

    struct CodeBlockCache {
        std::unique_ptr<TextEditor> editor;
        std::string text;
        TextEditor::LanguageDefinitionId language = TextEditor::LanguageDefinitionId::None;
    };

    std::vector<CodeBlockCache> m_codeBlocks;

    // State for block-level decorations.
    ImVec2 m_codeBlockStart  = { 0, 0 };
    ImVec2 m_quoteBarStart   = { 0, 0 };
    float  m_activeHeadScale = 1.0f;
    bool   m_inCodeBlock     = false;
    int    m_codeBlockIndex  = 0;  // resets each render(); used as cache key per block
    int    m_activeCodeBlock = 0;  // index of the block currently being accumulated
    std::string m_codeLanguage;
    std::string m_pendingCodeText; // accumulates text across multiple MD4C TEXT events

    void flushCodeBlock();         // renders the accumulated fenced block; called on leave
    static TextEditor::LanguageDefinitionId languageFromFence(const std::string& lang);

    // Table state (ImGui Table API).
    int  m_tableColCount  = 0;     // column count from MD_BLOCK_TABLE_DETAIL
    bool m_tableInHeader  = false;
    bool m_tableActive    = false; // true only when BeginTable() returned true
    bool m_tableSkipping  = false; // true when BeginTable() returned false
    int  m_tableId        = 0;    // incremented per table for unique BeginTable IDs

    // List rendering state.
    // Each entry stores the indent amount applied on that BLOCK_LI enter,
    // so we can exactly match it on leave regardless of marker width.
    std::vector<float> m_listIndentStack;

    bool m_pendingSpaceAfterInline = false;
    // Set when a text run ends mid-line; the next run (often after PushFont for
    // **bold** / *italic*) must SameLine() or the space before it vanishes.
    bool m_renderInlineAfter = false;
};

// Convenience free function that renders markdown using a shared static
// ofxMarkdownRenderer with no custom fonts. Suitable for quick use-cases.
void ofxRenderMarkdown(const std::string& markdown);
void ofxRenderMarkdown(const char* str, const char* str_end = nullptr);
