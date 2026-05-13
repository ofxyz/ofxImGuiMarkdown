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
// gui.setup() to get distinct heading and body weights:
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
    ImFont* boldFont    = nullptr;
    ImFont* italicFont  = nullptr;
    ImFont* headingFont = nullptr; // H1
    ImFont* h2Font      = nullptr; // H2+ (falls back to headingFont if null)
    ImFont* monoFont    = nullptr; // fenced code blocks

    // ---------- Heading scale --------------------------------------------------
    // When a heading font is NOT set, the default font is scaled by these
    // factors. Index 0 unused; indices 1–6 correspond to H1–H6.
    float headingScale[7] = { 1.0f, 1.7f, 1.45f, 1.25f, 1.1f, 1.0f, 1.0f };

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

    // ---------- Images ---------------------------------------------------------
    // Maximum display width for inline images (pixels). 0 = fit to window.
    float maxImageWidth = 0.0f;

    // Supersampling factor used when rasterising SVG files into an ofFbo.
    // 1.0 = native SVG size; 2.0 = 2× resolution (crisper on HiDPI).
    float svgScale = 2.0f;

    // ---------- Tables ---------------------------------------------------------
    // Flags passed to ImGui::BeginTable(). SizingStretchSame makes all columns
    // share available width equally regardless of header text length.
    ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders         |
        ImGuiTableFlags_RowBg           |
        ImGuiTableFlags_SizingStretchSame |
        ImGuiTableFlags_Resizable       |
        ImGuiTableFlags_NoSavedSettings;

    // ---------- Lists ----------------------------------------------------------
    // Extra vertical pixels added before each list item.
    float listItemSpacing = 3.0f;

    // ---------- API ------------------------------------------------------------
    // Render markdown inside the current ImGui window / child window.
    void render(const std::string& markdown);
    void render(const char* str, const char* str_end);

    // Release all cached image textures (e.g. on GL context rebuild).
    void clearImageCache();

protected:
    // ----- font / colour / URL overrides -----
    ImFont* get_font()  const override;
    ImVec4  get_color() const override;
    void    open_url()  const override;
    bool    get_image(image_info& nfo) const override;
    void    soft_break() override;

    // ----- text rendering override (word-wrap + hyphenation) -----
    void render_text(const char* str, const char* str_end) override;

    // ----- block overrides -----
    void BLOCK_H    (const MD_BLOCK_H_DETAIL*,    bool e) override;
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
};

// Convenience free function that renders markdown using a shared static
// ofxMarkdownRenderer with no custom fonts. Suitable for quick use-cases.
void ofxRenderMarkdown(const std::string& markdown);
void ofxRenderMarkdown(const char* str, const char* str_end = nullptr);
