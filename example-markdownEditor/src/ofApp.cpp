#include "ofApp.h"

static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static const char* kDefaultMarkdown = R"md(# ofxImGuiMarkdown

A live markdown editor and preview built with [ofxImGuiTextEdit](https://github.com/ofxyz/ofxImGuiTextEdit) 
and [imgui_md](https://github.com/mekhontsev/imgui_md).

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
Short headers like "ID" no longer collapse their column.

ID | Name            | Type    | Description
--:|:----------------|:--------|:-----------------------------
1  | `regularFont`   | ImFont* | Body and default text
2  | `boldFont`      | ImFont* | **Bold** and table headers
3  | `italicFont`    | ImFont* | *Italic* text
4  | `headingFont`   | ImFont* | H1 headings
5  | `h2Font`        | ImFont* | H2+ headings (falls back to headingFont)

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

Images are loaded relative to the running executable's `bin/data/` folder
(or via absolute path). ARB textures are disabled so UVs are always
normalised 0–1, as required by ImGui.

![openFrameworks logo](of-logo.svg)

Failed loads are silently skipped — no placeholder is shown.

---

## Escapes

Backslash escapes: \*not italic\*, \[not a link\].

*Edit this text on the left to see the preview update in real time!*
)md";

// ---------------------------------------------------------------------------

void ofApp::setup() {
    ofSetWindowTitle("ofxImGuiMarkdown — live editor");
    ofBackground(30, 30, 30);

    // Disable ARB textures globally so all textures (including those loaded
    // by the markdown renderer) use normalised 0–1 UVs, which ImGui requires.
    ofDisableArbTex();

    gui.setup();

    // --- Optional custom fonts --------------------------------------------------
    // Assign fonts to renderer after gui.setup() so the ImGui atlas is active.
    // Comment these lines in and point them at fonts in your bin/data folder:
    //
    //   renderer.headingFont = gui.addFont(ofToDataPath("fonts/Bold.ttf"),    22.0f);
    //   renderer.boldFont    = gui.addFont(ofToDataPath("fonts/Bold.ttf"),    15.0f);
    //   renderer.italicFont  = gui.addFont(ofToDataPath("fonts/Italic.ttf"),  15.0f);
    //   renderer.regularFont = gui.addFont(ofToDataPath("fonts/Regular.ttf"), 15.0f);
    //
    // Without custom fonts every style uses ImGui's built-in default font,
    // which is still fully functional.
    // ---------------------------------------------------------------------------

    editor.SetPalette(TextEditor::PaletteId::Dark);
    editor.SetText(kDefaultMarkdown);
}

void ofApp::draw() {
    gui.begin();

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, 0));
    ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar(2);

    const float totalW = ImGui::GetContentRegionAvail().x;
    const float totalH = ImGui::GetContentRegionAvail().y;
    const float editorW  = std::floor(totalW * 0.5f);
    const float previewW = totalW - editorW;
    const float wheel = ImGui::GetIO().MouseWheel;
    bool editorHovered = false;
    bool previewHovered = false;

    // ----- Left panel: source editor -----
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    const ImVec2 editorMin = ImGui::GetCursorScreenPos();
    const ImVec2 editorMax = ImVec2(editorMin.x + editorW, editorMin.y + totalH);
    if (ImGui::BeginChild("##editor", ImVec2(editorW, totalH), false)) {
        editor.Render("##te");
        editorHovered = ImGui::IsMouseHoveringRect(editorMin, editorMax, true);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine();

    // Vertical divider
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 divTop = ImGui::GetCursorScreenPos();
    dl->AddLine(divTop, ImVec2(divTop.x, divTop.y + totalH),
                ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 1.0f);

    // ----- Right panel: markdown preview -----
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
    const ImVec2 previewMin = ImGui::GetCursorScreenPos();
    const ImVec2 previewMax = ImVec2(previewMin.x + previewW - 1.0f, previewMin.y + totalH);
    if (ImGui::BeginChild("##preview", ImVec2(previewW - 1.0f, totalH), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const std::string text = editor.GetText();
        renderer.render(text);

        previewHovered = ImGui::IsMouseHoveringRect(previewMin, previewMax, true);

        // Keep the source and preview roughly aligned while scrolling.  The
        // two views do not have identical layout, so ratio sync is more robust
        // than trying to match exact pixel/line positions.
        if (wheel != 0.0f) {
            if (editorHovered) {
                const int lineCount = std::max(1, editor.GetLineCount() - 1);
                const float ratio = clamp01(static_cast<float>(editor.GetFirstVisibleLine()) /
                                            static_cast<float>(lineCount));
                ImGui::SetScrollY(ratio * ImGui::GetScrollMaxY());
            } else if (previewHovered) {
                const float maxY = ImGui::GetScrollMaxY();
                if (maxY > 0.0f) {
                    const float ratio = clamp01(ImGui::GetScrollY() / maxY);
                    const int targetLine = static_cast<int>(
                        std::round(ratio * static_cast<float>(std::max(0, editor.GetLineCount() - 1))));
                    editor.SetViewAtLine(targetLine, TextEditor::SetViewAtLineMode::FirstVisibleLine);
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    gui.end();
}
