// ============================================================================
// handlers/opening_line.h
// THE OPENING LINE: the first split line a rider crosses after the gate drops.
// It decides the Holeshot row (first rider through it), Charger's starting
// position (where the player sat coming out of it) and the holeshot_you cue.
//
// A SPLIT, NOT THE HOLESHOT MARKER. A track creator can place a holeshot
// marker, and the API declares a RaceHoleshot callback for it, but the game
// never sends it - measured on four tracks with the marker set and crossed -
// and a rider's trackPos is a 2D projection onto the centreline that reads the
// wrong section under a bridge and the wrong straight from a start chute, so
// detecting the marker from geometry was tried and taken out again. The split
// events are the game's own: it knows when a line is physically crossed. So
// the row reads the first RaceSplit after the gate, WHICHEVER index that is -
// the splits are numbered from start/finish and a grid need not sit behind it
// (MXB Test Track's first line off the gate is split 2). Start/finish fires
// no split on the opening lap, so a grid laid just behind it (MXB Club) settles
// the row at split 1 - and detecting S/F from trackPos would inherit the chute
// problem above, as the live-gap timing points do harmlessly. If PiBoSo starts
// sending RaceHoleshot, the export in mxb_api.cpp is where it plugs in.
// ============================================================================
#pragma once

namespace Handlers {

// One rider crossed a split (splitIndex as the game numbers it, 0 = split 1).
// Safe to call for every crossing: each of the three things it feeds keeps its
// own arm and ignores the call once that arm is down.
void claimOpeningLine(int raceNum, int splitIndex);

}  // namespace Handlers
