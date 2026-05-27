#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxImGuiTextEdit.h"
#include "ofxImGuiMarkdown.h"

class ofApp : public ofBaseApp {
public:
    void setup();
    void draw();

private:
    void drawDockspace();
    void buildDefaultDockLayout(ImGuiID dockspaceId);
    void drawSourceWindow();
    void drawPreviewWindow();
    void drawPreviewStyleMenu();
    void syncScrollFromEditor(float wheel);
    void syncScrollFromPreview(float wheel);

    ofxImGui::Gui       gui;
    TextEditor          editor;
    ofxMarkdownRenderer renderer;
    ImFont*             editorFont = nullptr;

    bool defaultDockLayoutBuilt = false;
    bool editorHovered          = false;
    bool previewHovered         = false;

    // ImGuiStyle tuning for the preview scroll area (ItemSpacing, etc.).
    float previewItemSpacingY = 4.0f;
};
