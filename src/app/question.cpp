// Questions (include/app/question.h).
#include "app/question.h"

#include "app/data.h"
#include "app/ui.h"

namespace app {

namespace {
class QuestionScreen final : public Screen {
public:
    Question q;
    int sel = 0, count = 2;
    void update(const Input& in) override
    {
        using namespace keys;
        int answer = -1;
        if(in.hit(B)) answer = count - 1;
        if(in.hit(UP)) { sel = (sel + count - 1) % count; redraw(); }
        if(in.hit(DOWN)) { sel = (sel + 1) % count; redraw(); }
        if(in.hit(A)) answer = sel;
        if(answer < 0) return;
        screens::pop();
        if(q.done) q.done(q.context, answer);
    }
    void draw(Canvas& c) override
    {
        c.clear();
        ui::title(c, q.title);
        // Lines wrap at spaces (28 UNSCII characters per row).
        constexpr int CHARS = (ui::TEXT_RIGHT - ui::TEXT_X) / 8;
        int y = ui::ROW_Y + 8;
        for(const char* line : q.lines) {
            if(!line) continue;
            const char* p = line;
            while(*p) {
                int n = 0, cut = 0;
                while(p[n] && n < CHARS) { if(p[n] == ' ') cut = n; ++n; }
                if(!p[n] || !cut) cut = n;
                char row[CHARS + 1];
                for(int k = 0; k < cut; ++k) row[k] = p[k];
                row[cut] = 0;
                c.text16(ui::TEXT_X, y, row);
                y += 16;
                p += cut;
                while(*p == ' ') ++p;
            }
        }
        const int first = 9 - count - 1;
        for(int i = 0; i < count; ++i) ui::row(c, first + i, q.answers[i], i == sel);
    }
};
QuestionScreen screen;
}

void ask(const Question& q)
{
    screen.q = q;
    screen.count = q.answers[2] ? 3 : 2;
    screen.sel = q.selected < screen.count ? q.selected : 0;
    screens::push(screen);
}

}
