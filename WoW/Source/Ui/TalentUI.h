#ifndef WOW_SOURCE_UI_TALENTUI_H
#define WOW_SOURCE_UI_TALENTUI_H

#include <cstdint>

// The talent panel's engine half.
//
// The panel itself is Interface\AddOns\Manastorm_Speedflying\Talents.lua: it lays out the circular tree (a core, three
// trees of five rings, three big talents and their linked talents) from a table this file publishes and reads
// back a set of globals every frame.
//
// The server module owns the tree -- the gates, the refusals, the point tally and the ranks are all
// authoritative there and this file only RENDERS them. That is deliberate: predicting the gates client-side
// would let the panel show a talent as buyable that the server then refuses, and the reference implementation
// does not predict either.
namespace TalentUI
{
    // Publishes the talent table once, and keeps the panel's globals in sync with the last server tally.
    void Update(uint32_t timeMs);

    // The three counters of a "T:<points>,<spent>,<unspent>,<n>,<ranks>,..." body. Called by SpeedFlying once it
    // has parsed one.
    void OnTally(uint32_t points, uint32_t spent, uint32_t unspent);

    // Every confirmed rank: `table` is the body's rank string, one character per talent ("0"-"9", then "a"-"z"),
    // and `count` its declared length, which must equal this build's table size or the ranks are dropped.
    void OnRanks(const char* table, int count);

    // The confirmed rank of one talent by its wire id (0 when this build does not know the id).
    int RankOf(int id);

    // The partial-point progress bar, from a bar body "P:<permille>,<yards>".
    void OnPartial(uint32_t permille, uint32_t needYards);

    // A refusal reason from the server, shown as the panel's status line.
    void OnStatus(const char* reason);
}

#endif
