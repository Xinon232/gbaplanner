#pragma once
// Controls topics and Credits pages (Settings), in the gbawriter layout: a
// title and eight lines; "#" lines are grey subheadings.
namespace app::help {

constexpr int LINES = 8;
struct Page {
    const char* title;
    const char* lines[LINES];
};
// Controls topics, then Credits (the last topic).
constexpr int TOPICS = 6;
constexpr int CREDITS = TOPICS - 1;
const char* topic_name(int topic);
int topic_pages(int topic);
const Page& page(int topic, int page);
// The last Credits page opens Secret Settings with A.
bool secret_page(int topic, int page);

}
