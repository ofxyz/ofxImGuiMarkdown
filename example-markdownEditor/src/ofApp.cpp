#include "ofApp.h"
#include "ImFonts.h"
#include "imgui_internal.h"
#include <algorithm>

static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static const char* kDefaultMarkdown = R"md(# ofxImGuiMarkdown

A live markdown editor and preview built with [ofxImGuiTextEdit](https://github.com/ofxyz/ofxImGuiTextEdit) and [imgui_md](https://github.com/mekhontsev/imgui_md).

---

## Block quote

> This is a block quote. It can span multiple lines and contain 
> **bold**, *italic*, and `inline code` formatting inside.

---

## Text formatting

Normal text, **bold text**, *italic text*, ~~strikethrough~~.

Mixed: **bold and *nested italic* together**.

Underline via HTML: <u>underlined text</u>.

---

## Lists

### Unordered

* Item one
* Item two
  * Nested item A
  * Nested item B
* Item three

### Ordered

1. First step
2. Second step
   1. Sub-step 2a
   2. Sub-step 2b
3. Third step

---

## Links

Visit [openFrameworks](https://openframeworks.cc) or [Dear ImGui](https://github.com/ocornut/imgui) — 
clicking opens the system browser via `ofLaunchBrowser`.

---

## Table

Columns share available width equally and are resizable by dragging the dividers.
Scroll the preview panel vertically for content below.

ID | Name              | Type    | Description
--:|:------------------|:--------|:-----------------------------
1  | `regularFont`     | ImFont* | Body and default text
2  | `boldFont`        | ImFont* | **Bold** and table headers
3  | `italicFont`      | ImFont* | *Italic* text
4  | `boldItalicFont`  | ImFont* | ***Bold + italic*** (nested emphasis)
5  | `headingFont`     | ImFont* | H1 headings
6  | `h2Font`          | ImFont* | H2 only (H3–H6 use `headingScale` × bold/regular)

---

## Code

Inline code: `ofxRenderMarkdown(text)`.

Fenced code block:

```cpp
ofxMarkdownRenderer renderer;
renderer.headingFont = gui.addFont("Bold.ttf", 22.0f);
renderer.boldFont    = gui.addFont("Bold.ttf", 15.0f);
renderer.render(markdownString);
```

---

## HTML extras

A manual line break:<br>
This line follows the break.

<br>

A manual separator:

<hr>

<div class="note">
Custom div classes can be styled by subclassing ofxMarkdownRenderer 
and overriding `html_div()`.
</div>

---

## Images

Paths resolve from the app working directory (`bin/`) and `data/`. The preview uses **ofImage**
(FreeImage) for raster files — PNG, JPEG, GIF, TIFF, BMP, PSD, EXR, HDR, and the rest of the
[formats openFrameworks supports](https://openframeworks.cc/documentation/graphics/ofImage/) — plus
**SVG vector preview** via ofxSvg (rasterised at 2× for sharp display):

![openFrameworks logo (SVG)](of-logo.svg)

ARB textures are disabled globally so UVs stay normalised 0–1 for ImGui. Failed loads are skipped
with no placeholder.

---

## Wiki links

Obsidian / GitHub-style wiki links: [[README|README]] (loads `README.md` from `data/` when clicked in this example),
[[https://openframeworks.cc|openFrameworks]] (opens in the browser), and same-document anchors like [[#Code]].

---

## Escapes

Backslash escapes: \*not italic\*, \[not a link\].

*Edit the source to see the preview update in real time. Drag the Source /
Preview tabs to rearrange the layout.*
)md";

static constexpr const char* kSourceWindowTitle = "Source###ofxImGuiMarkdownSource";
static constexpr const char* kPreviewWindowTitle = "Preview###ofxImGuiMarkdownPreview";
static constexpr const char* kPreviewContentChild = "##previewContent";

static ImGuiWindow* findPreviewScrollWindow() {
    char childName[128];
    snprintf(childName, sizeof(childName), "%s/%s", kPreviewWindowTitle, kPreviewContentChild);
    return ImGui::FindWindowByName(childName);
}

static void splitWikiTarget(const std::string& target,
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

// ---------------------------------------------------------------------------

void ofApp::setup() {
    ofSetWindowTitle("ofxImGuiMarkdown — live editor");
    ofBackground(30, 30, 30);

    // Disable ARB textures globally so all textures (including those loaded
    // by the markdown renderer) use normalised 0–1 UVs, which ImGui requires.
    ofDisableArbTex();

    ImGuiConfigFlags imguiFlags = ImGuiConfigFlags_DockingEnable;
#ifndef TARGET_OPENGLES
    imguiFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
    gui.setup(nullptr, false, imguiFlags, true);

    if (ImFont* uiFont = ImFonts::LoadDefaultFonts(ImGui::GetIO().Fonts, 15.0f))
        gui.setDefaultFont(uiFont);

    const ImFonts::JetBrainsMonoFonts jbm =
        ImFonts::LoadJetBrainsMono(ImGui::GetIO().Fonts, 15.0f);
    renderer.regularFont = jbm.regular;
    renderer.boldFont       = jbm.bold;
    renderer.italicFont     = jbm.italic;
    renderer.boldItalicFont = jbm.boldItalic;
    renderer.monoFont    = jbm.regular;
    renderer.headingFont = ImFonts::LoadJetBrainsMonoFont(
        ImGui::GetIO().Fonts, 22.0f, ImFonts::JetBrainsMonoVariant::Bold);
    renderer.h2Font = ImFonts::LoadJetBrainsMonoFont(
        ImGui::GetIO().Fonts, 18.0f, ImFonts::JetBrainsMonoVariant::Bold);
    editorFont = jbm.regular;
    gui.rebuildFontsTexture();

    editor.SetPalette(TextEditor::PaletteId::Dark);
    editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Markdown);
    editor.SetSoftWrapEnabled(true);
    editor.SetWrapLanguage(renderer.wrapLanguage);
    editor.SetText(kDefaultMarkdown);

    renderer.wikiLinkBasePath = ofToDataPath("", true);
    renderer.onWikiLinkClicked = [this](const std::string& target) {
        std::string page;
        std::string anchor;
        splitWikiTarget(target, page, anchor);

        if (!page.empty() && (page.find("://") != std::string::npos ||
                              page.compare(0, 4, "www.") == 0))
        {
            std::string url = page;
            if (url.compare(0, 4, "www.") == 0)
                url = "https://" + url;
            if (!anchor.empty())
                url += "#" + anchor;
            ofLaunchBrowser(url);
            return;
        }

        if (page.empty()) {
            ofLogNotice("ofxImGuiMarkdown") << "Same-document anchor: #" << anchor;
            return;
        }

        const std::string path = renderer.resolveWikiPagePath(target);
        if (!path.empty() && ofFile::doesFileExist(path)) {
            editor.SetText(ofBufferFromFile(path).getText());
            return;
        }

        ofLogWarning("ofxImGuiMarkdown") << "Wiki page not found: " << target;
    };
}

void ofApp::drawDockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking
                                 | ImGuiWindowFlags_NoTitleBar
                                 | ImGuiWindowFlags_NoCollapse
                                 | ImGuiWindowFlags_NoResize
                                 | ImGuiWindowFlags_NoMove
                                 | ImGuiWindowFlags_NoBringToFrontOnFocus
                                 | ImGuiWindowFlags_NoNavFocus
                                 | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("DockSpace###ofxImGuiMarkdownDockspace", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("ofxImGuiMarkdownDockspace");
    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    if (!defaultDockLayoutBuilt) {
        buildDefaultDockLayout(dockspaceId);
        defaultDockLayoutBuilt = true;
    }

    ImGui::End();
}

void ofApp::buildDefaultDockLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(
        dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID left  = 0;
    ImGuiID right = 0;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.50f, &left, &right);

    ImGui::DockBuilderDockWindow(kSourceWindowTitle, left);
    ImGui::DockBuilderDockWindow(kPreviewWindowTitle, right);
    ImGui::DockBuilderFinish(dockspaceId);
}

void ofApp::syncScrollFromEditor(float wheel) {
    if (wheel == 0.0f || !editorHovered)
        return;

    const int lineCount = std::max(1, editor.GetLineCount() - 1);
    const float ratio = clamp01(static_cast<float>(editor.GetFirstVisibleLine()) /
                                static_cast<float>(lineCount));

    if (ImGuiWindow* preview = findPreviewScrollWindow())
        preview->Scroll.y = ratio * preview->ScrollMax.y;
}

void ofApp::syncScrollFromPreview(float wheel) {
    if (wheel == 0.0f || !previewHovered)
        return;

    ImGuiWindow* preview = findPreviewScrollWindow();
    if (!preview || preview->ScrollMax.y <= 0.0f)
        return;

    const float ratio = clamp01(preview->Scroll.y / preview->ScrollMax.y);
    const int targetLine = static_cast<int>(
        std::round(ratio * static_cast<float>(std::max(0, editor.GetLineCount() - 1))));
    editor.SetViewAtLine(targetLine, TextEditor::SetViewAtLineMode::FirstVisibleLine);
}

static void applyPreviewStyleDefault(ofxMarkdownRenderer& r) {
    r.contentPadding        = { 10.0f, 6.0f };
    r.listItemSpacing       = 1.0f;
    r.verticalBlockGapLines = 0.5f;
    r.codeBlockBg           = { 0.11f, 0.11f, 0.14f, 1.00f };
    r.quoteBarColor         = { 0.40f, 0.45f, 0.90f, 1.00f };
    r.quoteTextColor        = { 0.72f, 0.74f, 0.82f, 1.00f };
}

static void applyPreviewStyleCompact(ofxMarkdownRenderer& r) {
    r.contentPadding        = { 6.0f, 4.0f };
    r.listItemSpacing       = 0.0f;
    r.verticalBlockGapLines = 0.35f;
    r.codeBlockBg           = { 0.09f, 0.09f, 0.12f, 1.00f };
    r.quoteBarColor         = { 0.35f, 0.40f, 0.78f, 1.00f };
    r.quoteTextColor        = { 0.68f, 0.70f, 0.78f, 1.00f };
}

static void applyPreviewStyleComfortable(ofxMarkdownRenderer& r) {
    r.contentPadding        = { 14.0f, 8.0f };
    r.listItemSpacing       = 3.0f;
    r.verticalBlockGapLines = 0.65f;
    r.codeBlockBg           = { 0.13f, 0.13f, 0.16f, 1.00f };
    r.quoteBarColor         = { 0.45f, 0.50f, 0.95f, 1.00f };
    r.quoteTextColor        = { 0.78f, 0.80f, 0.88f, 1.00f };
}

void ofApp::drawPreviewStyleMenu() {
    if (!ImGui::BeginMenu("Style"))
        return;

    if (ImGui::BeginMenu("Preset")) {
        if (ImGui::MenuItem("Default")) {
            applyPreviewStyleDefault(renderer);
            previewItemSpacingY = 4.0f;
        }
        if (ImGui::MenuItem("Compact")) {
            applyPreviewStyleCompact(renderer);
            previewItemSpacingY = 3.0f;
        }
        if (ImGui::MenuItem("Comfortable")) {
            applyPreviewStyleComfortable(renderer);
            previewItemSpacingY = 6.0f;
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Reset to default")) {
        applyPreviewStyleDefault(renderer);
        previewItemSpacingY = 4.0f;
    }

    ImGui::Separator();

    ImGui::TextUnformatted("Layout");
    ImGui::SliderFloat("Left padding", &renderer.contentPadding.x, 0.0f, 24.0f, "%.0f px");
    ImGui::SliderFloat("Right gutter", &renderer.contentPadding.y, 0.0f, 16.0f, "%.0f px");
    ImGui::SliderFloat("List spacing", &renderer.listItemSpacing, 0.0f, 8.0f, "%.0f px");
    ImGui::SliderFloat("Block gap", &renderer.verticalBlockGapLines, 0.2f, 1.0f,
                       "%.2f × line");

    ImGui::Separator();

    ImGui::TextUnformatted("ImGui spacing");
    ImGui::SliderFloat("Item spacing Y", &previewItemSpacingY, 2.0f, 12.0f, "%.0f px");

    ImGui::Separator();

    ImGui::TextUnformatted("Colors");
    ImGui::ColorEdit3("Quote bar", &renderer.quoteBarColor.x,
                       ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit3("Quote text", &renderer.quoteTextColor.x,
                       ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit3("Code block bg", &renderer.codeBlockBg.x,
                       ImGuiColorEditFlags_NoInputs);

    ImGui::EndMenu();
}

void ofApp::drawSourceWindow() {
    ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags sourceFlags = ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin(kSourceWindowTitle, nullptr, sourceFlags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        editorHovered = false;
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            bool softWrap = editor.IsSoftWrapEnabled();
            if (ImGui::MenuItem("Soft Wrap", nullptr, softWrap))
                editor.SetSoftWrapEnabled(!softWrap);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    editorHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    if (editorFont)
        ImGui::PushFont(editorFont);
    editor.Render("##te", focused, avail, false);
    if (editorFont)
        ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void ofApp::drawPreviewWindow() {
    ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags previewFlags = ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin(kPreviewWindowTitle, nullptr, previewFlags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        previewHovered = false;
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            drawPreviewStyleMenu();
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    previewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(style.ItemSpacing.x, previewItemSpacingY));

    ImGui::BeginChild(kPreviewContentChild, ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    renderer.render(editor.GetText());
    ImGui::EndChild();

    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleVar();
}

void ofApp::draw() {
    gui.begin();

    drawDockspace();
    drawSourceWindow();
    drawPreviewWindow();

    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
        if (editorHovered)
            syncScrollFromEditor(wheel);
        else if (previewHovered)
            syncScrollFromPreview(wheel);
    }

    gui.end();
    gui.draw();
}
