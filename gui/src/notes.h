#ifndef NOTES_H
#define NOTES_H

#include <vector>

// Frequencies of notes on the high E string (E4) of a guitar in standard tuning
// Index represents the fret number (0 = open string, 1 = first fret, etc.)
const std::vector<double> highEStringNotes = {
    329.63,  // E4  - Open string (fret 0)
    349.23,  // F4  - fret 1
    369.99,  // F#4 - fret 2
    392.00,  // G4  - fret 3
    415.30,  // G#4 - fret 4
    440.00,  // A4  - fret 5
    466.16,  // A#4 - fret 6
    493.88,  // B4  - fret 7
    523.25,  // C5  - fret 8
    554.37,  // C#5 - fret 9
    587.33,  // D5  - fret 10
    622.25,  // D#5 - fret 11
    659.25,  // E5  - fret 12
    698.46,  // F5  - fret 13
    739.99,  // F#5 - fret 14
    783.99,  // G5  - fret 15
    830.61,  // G#5 - fret 16
    880.00,  // A5  - fret 17
    932.33,  // A#5 - fret 18
    987.77,  // B5  - fret 19
    1046.50, // C6  - fret 20
    1108.73, // C#6 - fret 21
    1174.66, // D6  - fret 22
    1244.51, // D#6 - fret 23
    1318.51  // E6  - fret 24
};

#endif // NOTES_H
