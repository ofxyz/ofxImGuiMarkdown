# ofxImGuiMarkdown

Markdown preview widget for [Dear ImGui](https://github.com/ocornut/imgui) inside openFrameworks, built on top of [imgui_md](https://github.com/mekhontsev/imgui_md) and [MD4C](https://github.com/mity/md4c).

![preview](example-markdownEditor/preview.png)

## Supported Markdown Features

- Wrapped text
- Headers (H1–H6)
- **Bold**, *italic*, ~~strikethrough~~, underline
- Ordered and unordered lists (including sub-lists)
- Links (opens via `ofLaunchBrowser`)
- Images (stub — override `get_image()` to add support)
- Horizontal rules
- Tables (with borders and header highlight)
- Code spans
- HTML elements: `<br>`, `<hr>`, `<u>`, `</u>`, `<div class="...">`, `</div>`
- Backslash escapes

## Dependencies

- `ofxImGui`

## Usage

```cpp
#include "ofxImGuiMarkdown.h"

// --- In your ofApp class ---
ofxImGui::Gui gui;
ofxMarkdownRenderer renderer;

// --- setup() ---
gui.setup();

// Optional: set custom fonts (must be done before the first rendered frame).
// If left as nullptr, ImGui's default font is used for all text.
// renderer.headingFont = gui.addFont(ofToDataPath("fonts/MyBold.ttf"), 22.0f);
// renderer.boldFont    = gui.addFont(ofToDataPath("fonts/MyBold.ttf"), 15.0f);
// renderer.italicFont  = gui.addFont(ofToDataPath("fonts/MyItalic.ttf"), 15.0f);
// renderer.regularFont = gui.addFont(ofToDataPath("fonts/MyRegular.ttf"), 15.0f);

// --- draw() ---
gui.begin();
if (ImGui::Begin("Markdown Preview")) {
    renderer.render(myMarkdownString);
}
ImGui::End();
gui.end();
```

### Convenience Free Function

For simple cases without custom fonts, use the free function which keeps a shared static renderer:

```cpp
gui.begin();
if (ImGui::Begin("Preview")) {
    ofxRenderMarkdown(markdownText);
}
ImGui::End();
gui.end();
```

### Image Support

Images are supported out of the box. Paths in markdown `![alt](path)` are
resolved relative to the process working directory (i.e. the project's
`bin/` folder, so `bin/data/my-image.png` is referenced as `data/my-image.png`).

Images are loaded on first encounter and cached for the lifetime of the
renderer. Call `renderer.clearImageCache()` to force a reload.

**Important:** ARB textures must be disabled before images are loaded so that
the texture coordinates are normalised to 0–1 (as ImGui requires). The
simplest way is to call `ofDisableArbTex()` once in `setup()` before
`gui.setup()`:

```cpp
void ofApp::setup() {
    ofDisableArbTex(); // must come before gui.setup() and any texture loads
    gui.setup();
    // ...
}
```

To cap the display width of all inline images:

```cpp
renderer.maxImageWidth = 400.0f; // pixels; 0 = auto-fit to window width
```

To customise image loading (e.g. load from a URL or apply a tint), subclass
`ofxMarkdownRenderer` and override `get_image()`:

```cpp
struct MyRenderer : public ofxMarkdownRenderer {
    bool get_image(image_info& nfo) const override {
        // m_href contains the image path/URL from the markdown source
        nfo.texture_id = (ImTextureID)(uintptr_t)myTexture.getTextureData().textureID;
        nfo.size       = { 200, 150 };
        nfo.uv0        = { 0.0f, 0.0f };
        nfo.uv1        = { 1.0f, 1.0f };
        nfo.col_tint   = { 1.0f, 1.0f, 1.0f, 1.0f };
        nfo.col_border = { 0.0f, 0.0f, 0.0f, 0.0f };
        return true;
    }
};
```

## Example

`example-markdownEditor` — a live editor/preview split panel using `ofxImGuiTextEdit` on the left and the markdown preview on the right. Edit markdown source and see the rendered output update in real time.

## Vendored Libraries

| Library | Source | License |
|---------|--------|---------|
| [imgui_md](https://github.com/mekhontsev/imgui_md) | `libs/imgui_md/src/` | MIT |
| [MD4C](https://github.com/mity/md4c) | `libs/md4c/src/` | MIT |
