# Link Audio header fix for GCC 7.5

`ableton/link_audio/MainProcessor.hpp` is a copy of the same file from Ableton Link 4.1.1
(`third_party/link`, GPL-2.0-or-later, copyright Ableton AG) with one change in
`receiveAudioBuffer`: a plain loop instead of `std::find_if` with a generic lambda.

GCC 7.5, the compiler in the AZ-MODS SDK container, emits the function's static local
`audioBuffer` twice when a generic lambda refers to it, and the assembler stops with
"symbol ... audioBuffer is already defined". Newer GCC and clang are not affected.

The Makefiles put this folder before `third_party/link/include`, so this copy is used.
When the Link submodule is updated, compare the two files again and delete this folder
if Ableton's version builds with GCC 7.5.
