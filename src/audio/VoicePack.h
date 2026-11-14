#pragma once
#include <FS.h>

// All the voice clips in ONE file, /voice.pak, instead of one file per clip. The filesystem stores every file in whole 4 kB
// blocks, so 95 clips of about 6 kB each used 800 kB of the 1 MB for 593 kB of sound; in one file they use 600 kB, and the 200 kB
// saved is room for flight logs.
//
// The file is "PVP1" followed by records: 20 bytes of clip name (padded with zeros), a 4 byte little-endian size, then that many
// bytes of clip (the same "IMA1" ADPCM data the clips always had). Records are only ever added at the end, so a pack that was
// only half written can be told (the last record is short) and is simply started again.
//
// A vario that still has the old /v/<name>.adp files packs them itself at the first start with this firmware (migrate()):
// each clip is copied into the pack and its old file deleted, one at a time, so it never needs room for both.
namespace voicepack {

// Reads the index of /voice.pak (a few kilobytes of RAM). True when the pack holds clips.
bool begin();

// Opens the pack with `f` at the first byte of the named clip. False when the clip is not in the pack.
bool open(const char* name, File& f);

// Packs old /v/*.adp files into the pack, deleting each after it is stored. Does nothing when there are none.
void migrate();

}  // namespace voicepack
