#include "FactoryPhrases.h"

#include <initializer_list>

namespace mmp
{
    namespace
    {
        constexpr int S = mm::kPhrasePpq / 4;   // one 1/16 step

        struct N { int number, step, len, vel; int offset = 0; };

        mm::Phrase make (const char* name, int lanes, int rows, int bars, std::initializer_list<N> notes, int stepTicks = S)
        {
            mm::Phrase p;
            p.name = name;
            p.snap = "1/16";
            p.setLayout (lanes, rows);
            p.autoSize = false;
            p.totalSize = bars * mm::kTicksPerBar;
            for (auto& n : notes)
                p.notes.push_back ({ n.number, n.offset, n.vel, n.step * stepTicks, n.step * stepTicks + n.len });
            p.sortNotes();
            return p;
        }

        /** Repeats a sequence of Numbers on every step. */
        mm::Phrase cycle (const char* name, int lanes, int rows, std::initializer_list<int> numbers, int gate, int steps = 16, int stepTicks = S)
        {
            mm::Phrase p;
            p.name = name;
            p.snap = "1/16";
            p.setLayout (lanes, rows);
            p.autoSize = false;
            p.totalSize = steps * stepTicks;
            const std::vector<int> seq (numbers);
            for (int i = 0; i < steps; ++i)
            {
                const int vel = (i % 4 == 0) ? 112 : (i % 2 == 0 ? 96 : 84);
                p.notes.push_back ({ seq[(size_t) i % seq.size()], 0, vel, i * stepTicks, i * stepTicks + gate });
            }
            return p;
        }
    }

    const std::map<std::string, mm::Phrase>& factoryPhrases()
    {
        static const std::map<std::string, mm::Phrase> phrases = []
        {
            std::map<std::string, mm::Phrase> m;
            auto add = [&m] (const char* folder, mm::Phrase p) { m[std::string ("Factory/") + folder + "/" + p.name] = std::move (p); };

            add ("Arps", cycle ("Up 3",          3, 3, { 50, 51, 52 }, 18));
            add ("Arps", cycle ("Down 3",        3, 3, { 52, 51, 50 }, 18));
            add ("Arps", cycle ("Up Down",       3, 3, { 50, 51, 52, 51 }, 18));
            add ("Arps", cycle ("Up 2 Octaves",  3, 3, { 50, 51, 52, 60, 61, 62 }, 16, 24));
            add ("Arps", cycle ("Up 4",          3, 4, { 50, 51, 52, 53 }, 18));
            add ("Arps", cycle ("Pinky Up",      3, 3, { 50, 62, 51, 62, 52, 62 }, 14, 24));
            add ("Arps", cycle ("Converge",      3, 4, { 50, 53, 51, 52 }, 18));
            add ("Arps", cycle ("Triplet Up",    3, 3, { 50, 51, 52 }, 24, 12, 32));

            add ("Bass", make ("Offbeat Bass", 3, 3, 1,
                { { 40, 2, 18, 110 }, { 40, 6, 18, 104 }, { 40, 10, 18, 110 }, { 40, 14, 18, 104 } }));
            add ("Bass", make ("Rolling Bass", 3, 3, 1,
                { { 40, 1, 16, 100 }, { 40, 2, 16, 92 }, { 40, 3, 16, 96 }, { 40, 5, 16, 100 }, { 40, 6, 16, 92 }, { 40, 7, 16, 96 },
                  { 40, 9, 16, 100 }, { 40, 10, 16, 92 }, { 40, 11, 16, 96 }, { 40, 13, 16, 100 }, { 40, 14, 16, 92 }, { 42, 15, 16, 96 } }));
            add ("Bass", make ("Octave Pump", 3, 3, 1,
                { { 40, 0, 20, 112 }, { 50, 2, 12, 90 }, { 40, 4, 20, 112 }, { 50, 6, 12, 90 },
                  { 40, 8, 20, 112 }, { 50, 10, 12, 90 }, { 40, 12, 20, 112 }, { 50, 14, 12, 90 } }));
            add ("Bass", make ("Walking Fifths", 3, 3, 2,
                { { 40, 0, 40, 104 }, { 42, 4, 40, 96 }, { 50, 8, 40, 100 }, { 42, 12, 40, 96 },
                  { 40, 16, 40, 104 }, { 41, 20, 40, 96 }, { 42, 24, 40, 100 }, { 41, 28, 22, 90 }, { 40, 30, 22, 90 } }));

            add ("Stabs", make ("Offbeat Stab", 3, 3, 1,
                { { 50, 2, 14, 104 }, { 51, 2, 14, 104 }, { 52, 2, 14, 104 },
                  { 50, 6, 14, 100 }, { 51, 6, 14, 100 }, { 52, 6, 14, 100 },
                  { 50, 10, 14, 104 }, { 51, 10, 14, 104 }, { 52, 10, 14, 104 },
                  { 50, 14, 14, 100 }, { 51, 14, 14, 100 }, { 52, 14, 14, 100 } }));
            add ("Stabs", make ("Trance Gate", 3, 3, 1,
                { { 50, 0, 18, 110 }, { 51, 0, 18, 110 }, { 52, 0, 18, 110 },
                  { 50, 3, 12, 90 }, { 51, 3, 12, 90 }, { 52, 3, 12, 90 },
                  { 50, 6, 18, 104 }, { 51, 6, 18, 104 }, { 52, 6, 18, 104 },
                  { 50, 10, 12, 92 }, { 51, 10, 12, 92 }, { 52, 10, 12, 92 },
                  { 50, 12, 30, 108 }, { 51, 12, 30, 108 }, { 52, 12, 30, 108 } }));
            add ("Stabs", make ("Chord Hold", 3, 4, 1,
                { { 50, 0, 380, 100 }, { 51, 0, 380, 96 }, { 52, 0, 380, 96 }, { 53, 0, 380, 92 } }));

            add ("Melodic", make ("Scale Run", 3, 3, 1,
                { { 50, 0, 20, 108 }, { 50, 2, 20, 92, 2 }, { 51, 4, 20, 100 }, { 50, 6, 20, 92, 5 },
                  { 52, 8, 20, 104 }, { 50, 10, 20, 92, 9 }, { 60, 12, 20, 108 }, { 52, 14, 20, 92, 2 } }));
            add ("Melodic", make ("Call Response", 3, 3, 2,
                { { 50, 0, 20, 110 }, { 51, 2, 20, 96 }, { 52, 3, 40, 104 }, { 60, 8, 20, 100 }, { 52, 10, 40, 96 },
                  { 52, 16, 20, 104 }, { 51, 18, 20, 96 }, { 50, 19, 40, 100 }, { 42, 24, 20, 96 }, { 50, 26, 60, 104 } }));
            return m;
        }();
        return phrases;
    }

    mm::Phrase defaultPhrase()
    {
        return factoryPhrases().at ("Factory/Arps/Up Down");
    }
}
