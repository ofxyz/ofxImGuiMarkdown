#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxImGuiTextEdit.h"
#include "ofxImGuiMarkdown.h"

class ofApp : public ofBaseApp {
public:
    void setup();
    void draw();

    ofxImGui::Gui       gui;
    TextEditor          editor;
    ofxMarkdownRenderer renderer;
};
