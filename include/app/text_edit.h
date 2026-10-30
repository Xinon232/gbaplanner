#pragma once
// Typing a title, notes or a to-do with the gbawriter letters
// (include/app/writer_core.h), drawn in SuperFW like gbavocab's entry editor.
// Start+A: done (the text goes to `done`), Start+B: back without a change.
// Notes are multi-line (Start pressed and let go alone: new line).
#include "app/screen.h"

namespace app {

struct TextEditRequest {
    const char* heading = "";   // small title: "Title", "Notes", "New to-do"
    int max_chars = 60;         // UTF-8 characters (a line break counts 1)
    bool multiline = false;
    bool allow_empty = false;   // notes may be empty; titles not
    // Called on Start+A with the text; returns nullptr when it was taken (the
    // editor closes) or a short message to show (the editor stays open).
    const char* (*done)(void* context, const char* text) = nullptr;
    void* context = nullptr;
};

// Opens the editor (screens::push) with `initial` text.
void open_text_edit(const TextEditRequest& request, const char* initial);
// Character count of a UTF-8 text.
int utf8_chars(const char* s);

}
