#pragma once
// A question with two or three answers (gbamp3 question rows, as gbavocab's
// "Delete this entry?"): the text on top, the answers as rows below.
// B = the last answer (No / Cancel).
#include "app/screen.h"

namespace app {

struct Question {
    const char* title = "";
    const char* lines[3] = {nullptr, nullptr, nullptr};   // UNSCII lines
    const char* answers[3] = {"Yes", "No", nullptr};
    int selected = 1;                                       // the answer selected first
    // Called with the answer's index (B: the last answer) after the
    // question closed (screens::pop); may push another screen.
    void (*done)(void* context, int answer) = nullptr;
    void* context = nullptr;
};
void ask(const Question& q);

// Draws a busy screen at once (title + text) and shows it; for long card
// work (loading, conversion). Implemented in main.cpp (the display).
void show_busy(const char* title, const char* text);

}
