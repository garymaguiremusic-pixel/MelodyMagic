#include <catch2/catch_test_macros.hpp>

#include "mm/Formats.h"
#include "mm/Library.h"
#include "mm/ValueTreeCodec.h"

#include <cstdlib>
#include <cctype>
#include <filesystem>

using namespace mm;
namespace fs = std::filesystem;

namespace
{
    /** Optional test data: MM_NORA_DIR = the original preset folder (raw .fas/.qas/.has),
        MM_LIBRARY_DIR = a folder holding the nora_*_library.json files. */
    std::string envDir (const char* name)
    {
        const char* v = std::getenv (name);
        return v != nullptr ? std::string (v) : std::string();
    }
}

TEST_CASE ("Compressed ints and every var type round-trip", "[codec]")
{
    using vt::Var;
    vt::Tree t ("ROOT");
    t.set ("i0", Var::ofInt (0))
     .set ("i1", Var::ofInt (-1))
     .set ("big", Var::ofInt (123456789))
     .set ("neg", Var::ofInt (-300))
     .set ("t", Var::ofBool (true))
     .set ("f", Var::ofBool (false))
     .set ("d", Var::ofDouble (0.25))
     .set ("s", Var::ofString ("Héllo"))
     .set ("l", Var::ofInt64 (-5000000000LL));
    Var arr;
    arr.type = Var::Type::Array;
    arr.array = { Var::ofInt (1), Var::ofString ("x") };
    t.set ("a", arr);
    Var bin;
    bin.type = Var::Type::Binary;
    bin.s = std::string ("\x00\x01\xff", 3);
    t.set ("b", bin);
    t.set ("v", Var());
    t.add (vt::Tree ("CHILD")).set ("x", Var::ofInt (7));

    const auto bytes = vt::write ({ t, vt::Tree ("SECOND") });
    const auto back = vt::read (bytes);
    REQUIRE (back.size() == 2);
    CHECK (back[0] == t);
    CHECK (back[1].type == "SECOND");
    CHECK (vt::write (back) == bytes);
}

TEST_CASE ("Bytes match juce::ValueTree::writeToStream", "[codec]")
{
    vt::Tree t ("A");
    t.set ("n", vt::Var::ofInt (5));
    const auto bytes = vt::write ({ t });
    // "A\0" cint(1) "n\0" cint(5) 01 05000000 cint(0)
    const std::vector<std::uint8_t> expected { 'A', 0, 1, 1, 'n', 0, 1, 5, 1, 5, 0, 0, 0, 0 };
    CHECK (bytes == expected);
}

TEST_CASE ("Malformed data throws", "[codec]")
{
    const std::vector<std::uint8_t> bad { 'A', 0, 1, 1, 'n', 0, 1, 50, 1 };
    CHECK_THROWS_AS (vt::read (bad), vt::ParseError);
    const std::vector<std::uint8_t> unterminated { 'A', 'B' };
    CHECK_THROWS_AS (vt::read (unterminated), vt::ParseError);
}

TEST_CASE ("Phrase model round-trips through the .fas format", "[codec][fas]")
{
    Phrase p;
    p.name = "Test";
    p.autoSize = false;
    p.totalSize = 768;
    p.setLayout (5, 4);
    p.notes = { { 50, 0, 100, 0, 24 }, { 63, 3, 80, 48, 96 }, { 41, -2, 1, 100, 400 } };

    const auto trees = phraseToTrees (p);
    CHECK (fileTypeOf (trees) == "Secuencia Frases");
    const auto back = phraseFromTrees (vt::read (vt::write (trees)));
    CHECK (back == p);
    CHECK (back.lengthTicks() == 768);
    CHECK (back.lanesShown() == 5);
    CHECK (back.rowsPerLane() == 4);
}

TEST_CASE ("Automatic phrase length is the end of the last bar with a note end", "[fas]")
{
    Phrase p;
    p.notes = { { 50, 0, 100, 0, 24 } };
    CHECK (p.lengthTicks() == 384);
    p.notes.push_back ({ 50, 0, 100, 380, 400 });
    CHECK (p.lengthTicks() == 768);
    p.notes = { { 50, 0, 100, 0, 384 } };
    CHECK (p.lengthTicks() == 384);
    p.notes.clear();
    CHECK (p.lengthTicks() == 384);
}

TEST_CASE ("Groove and key map round-trip", "[codec][qas][has]")
{
    Groove g { "Swing", 768, 96, { 0, 23, 48, 71 } };
    CHECK (grooveFromTrees (vt::read (vt::write (grooveToTrees (g)))) == g);

    KeyMap km;
    km.map[9] = { 9, 13, 16 };
    km.map[8] = { -4, 8, 11, 14 };
    const auto trees = keyMapToTrees (km);
    REQUIRE (trees[1].children.size() == 3);
    CHECK (trees[1].children[0].children.size() == 144);
    const auto back = keyMapFromTrees (vt::read (vt::write (trees)));
    CHECK (back.map[9] == km.map[9]);
    CHECK (back.map[8] == km.map[8]);
    CHECK (back.expand (57) == std::vector<int> { 57, 61, 64 });   // A3 -> A, C#+12, E+12 (base 48)
}

TEST_CASE ("Every original .fas / .qas / .has file round-trips byte-identically", "[codec][corpus]")
{
    const auto dir = envDir ("MM_NORA_DIR");
    if (dir.empty() || ! fs::is_directory (dir))
    {
        SUCCEED ("MM_NORA_DIR not set: corpus round-trip skipped");
        return;
    }

    int files = 0, failures = 0;
    for (auto& entry : fs::recursive_directory_iterator (dir))
    {
        if (! entry.is_regular_file())
            continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = (char) std::tolower ((unsigned char) c);
        if (ext != ".fas" && ext != ".qas" && ext != ".has")
            continue;

        const auto bytes = vt::readFileBytes (entry.path().string());
        ++files;
        try
        {
            if (vt::write (vt::read (bytes)) != bytes)
            {
                ++failures;
                UNSCOPED_INFO ("not byte-identical: " << entry.path().string());
            }
        }
        catch (const std::exception& e)
        {
            ++failures;
            UNSCOPED_INFO (entry.path().string() << ": " << e.what());
        }
    }
    INFO (files << " files");
    CHECK (failures == 0);
}

TEST_CASE ("The JSON libraries load completely and the models round-trip", "[library][corpus]")
{
    const auto dir = envDir ("MM_LIBRARY_DIR");
    if (dir.empty() || ! fs::is_directory (dir))
    {
        SUCCEED ("MM_LIBRARY_DIR not set: library test skipped");
        return;
    }

    Library lib;
    std::vector<std::string> errors;
    lib.scanFolder (dir, &errors);
    CHECK (errors.empty());
    CHECK (lib.phrases.size() == 487);
    CHECK (lib.grooves.size() == 312);
    CHECK (lib.keyMaps.size() == 1464);
    CHECK (lib.clips.size() == 1388);

    int notes = 0;
    for (auto& [key, p] : lib.phrases)
    {
        INFO (key);
        notes += (int) p.notes.size();
        REQUIRE (phraseFromTrees (vt::read (vt::write (phraseToTrees (p)))) == p);
        for (auto& n : p.notes)
        {
            REQUIRE (n.number >= 30);
            REQUIRE (n.number <= 79);
            REQUIRE (n.end > n.start);
        }
    }
    CHECK (notes == 9672);

    int gridLines = 0;
    for (auto& [key, g] : lib.grooves)
    {
        INFO (key);
        gridLines += (int) g.grid.size();
        REQUIRE (g.globalPpq == 96);
        REQUIRE (grooveFromTrees (vt::read (vt::write (grooveToTrees (g)))) == g);
    }
    CHECK (gridLines == 6971);

    for (auto& [key, km] : lib.keyMaps)
    {
        INFO (key);
        REQUIRE (keyMapFromTrees (vt::read (vt::write (keyMapToTrees (km)))).map == km.map);
    }

    // ANALYSIS.md 2.3: A Major ChordScale. A -> A, C#+12, E+12; G# -> G#, B, D+12
    const auto it = lib.keyMaps.find ("MidiKeyMaps/Kick101/ChordScales/Major/ChordScale/A_Major.has");
    REQUIRE (it != lib.keyMaps.end());
    CHECK (it->second.map[9] == std::vector<int> { 9, 13, 16 });
    CHECK (it->second.map[8] == std::vector<int> { 8, 11, 14 });
}
