# SPDX-License-Identifier: GPL-3.0-only
# Link on Deck tests, on a Linux computer with clang (no deck, no Docker):
#   make -C tests          both
#   make -C tests link     flow-beat.so (host build) + a second Link peer: tempo, beat, bar,
#                          start/stop, jog nudges, block steps, "this is 1", lead change (~15 s)
#   make -C tests bars     the bar lookup from a rekordbox PQTZ grid, with ASan + UBSan
#   make -C tests rows     the Mod Menu rows against the core's rules (key, label <= 31 bytes, count)
#   make -C tests audio    Link Audio (0.5.2): flow-beat.so's four deck taps + a LinkAudio peer
#                          standing in for Ableton 12.4: channels, samples, format, beat time, sample rate
#                          (taps at 48000, at 44100, and 44100 with the rate forced to 48000; ~45 s)
#   make -C tests audio-tsan   the same, with ThreadSanitizer
#   make -C tests lights   0.6.0: Lights from this deck: Art-Net bytes, patch, the show (beats, breakdown,
#                          drop hit, fader, idle, no strobe/laser/haze) and end to end over UDP 6454 (~20 s);
#                          lights-tsan the same under TSan
#   make -C tests panel    0.6.2: the FLOW Lights panel: touch, tick, paint, vjpad / FVC1 / FVS1 over UDP, and the
#                          deck show's panel switches (~5 s); panel-tsan the same under TSan
#   make -C tests key      0.6.5: the deck key in track/state packets (Key Sync), with ASan + UBSan
#   make -C tests fx       0.5.3: Stem FX / Drum Roll HOLD / loop state from LED frames and azstem's
#                          export to "fx" packets, with ASan + UBSan (~12 s); fx-tsan the same under TSan
AZ_SDK_INCLUDE ?= ../../include
LINK   := ../third_party/link
INC    := -I$(AZ_SDK_INCLUDE) -I../third_party/link-gcc7 -I$(LINK)/include -I$(LINK)/modules/asio-standalone/asio/include
# Link Audio's headers trip -Wextra: the test peers include them as system headers
SYSINC := -I$(AZ_SDK_INCLUDE) -isystem ../third_party/link-gcc7 -isystem $(LINK)/include -isystem $(LINK)/modules/asio-standalone/asio/include
CC     := clang
CXX    := clang++
B      := build

.PHONY: all key link bars rows audio audio-tsan fx fx-tsan lights lights-tsan panel panel-tsan clean
all: rows bars key fx lights panel link audio

$(B):
	mkdir -p $(B)

$(B)/flow-beat.so: ../flow-beat.c ../mixer.h ../link.cpp ../link.h | $(B)
	$(CC) -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror $(INC) -c -o $(B)/flow-beat.o ../flow-beat.c
	$(CXX) -std=c++17 -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror -Wno-multichar $(INC) -c -o $(B)/link.o ../link.cpp
	$(CXX) -shared -o $@ $(B)/flow-beat.o $(B)/link.o -lm -lpthread

$(B)/test_link: test_link.cpp | $(B)
	$(CXX) -std=c++17 -O2 -g -DNDEBUG -DLINK_PLATFORM_LINUX=1 -Wall -Wextra -Werror -Wno-multichar $(INC) -o $@ $< -ldl -lpthread

link: $(B)/flow-beat.so $(B)/test_link
	$(B)/test_link $(B)/flow-beat.so

$(B)/test_link_audio: test_link_audio.cpp | $(B)
	$(CXX) -std=c++17 -O2 -g -DNDEBUG -DLINK_PLATFORM_LINUX=1 -Wall -Wextra -Werror -Wno-multichar $(SYSINC) -o $@ $< -ldl -lpthread

audio: $(B)/flow-beat.so $(B)/test_link_audio
	$(B)/test_link_audio $(B)/flow-beat.so 48000
	$(B)/test_link_audio $(B)/flow-beat.so 44100
	$(B)/test_link_audio $(B)/flow-beat.so 44100 48000

$(B)/tsan:
	mkdir -p $(B)/tsan

audio-tsan: test_link_audio.cpp ../flow-beat.c ../mixer.h ../link.cpp ../link.h | $(B)/tsan
	$(CC) -fsanitize=thread -O1 -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror $(INC) -c -o $(B)/tsan/flow-beat.o ../flow-beat.c
	$(CXX) -fsanitize=thread -std=c++17 -O1 -g -fPIC -fvisibility=hidden -Wall -Wextra -Werror -Wno-multichar $(INC) -c -o $(B)/tsan/link.o ../link.cpp
	$(CXX) -fsanitize=thread -shared -o $(B)/tsan/flow-beat.so $(B)/tsan/flow-beat.o $(B)/tsan/link.o -lm -lpthread
	$(CXX) -fsanitize=thread -std=c++17 -O1 -g -DNDEBUG -DLINK_PLATFORM_LINUX=1 -Wall -Wextra -Werror -Wno-multichar $(SYSINC) -o $(B)/tsan/test_link_audio test_link_audio.cpp -ldl -lpthread
	$(B)/tsan/test_link_audio $(B)/tsan/flow-beat.so 44100

rows: test_rows.c ../flow-beat.c ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror $(INC) -o $(B)/test_rows test_rows.c -lm -lpthread
	$(B)/test_rows

bars: test_bars.c ../flow-beat.c ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror $(INC) -o $(B)/test_bars test_bars.c -lm -lpthread
	$(B)/test_bars

key: test_key.c ../flow-beat.c ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror $(INC) -o $(B)/test_key test_key.c -lm -lpthread
	$(B)/test_key

fx: test_fx.c ../flow-beat.c ../fx.h ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror $(INC) -rdynamic -o $(B)/test_fx test_fx.c -lm -lpthread -ldl
	$(B)/test_fx

fx-tsan: test_fx.c ../flow-beat.c ../fx.h ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=thread -Wall -Wextra -Werror $(INC) -rdynamic -o $(B)/test_fx_tsan test_fx.c -lm -lpthread -ldl
	$(B)/test_fx_tsan

lights: test_lights.c ../flow-beat.c ../lights.h ../fx.h ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror $(INC) -rdynamic -o $(B)/test_lights test_lights.c -lm -lpthread -ldl
	$(B)/test_lights

lights-tsan: test_lights.c ../flow-beat.c ../lights.h ../fx.h ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=thread -Wall -Wextra -Werror $(INC) -rdynamic -o $(B)/test_lights_tsan test_lights.c -lm -lpthread -ldl
	$(B)/test_lights_tsan

panel: test_panel.c ../flow-beat.c ../panel.h ../lights.h ../fx.h ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror $(INC) -rdynamic -o $(B)/test_panel test_panel.c -lm -lpthread -ldl
	$(B)/test_panel

panel-tsan: test_panel.c ../flow-beat.c ../panel.h ../lights.h ../fx.h ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=thread -Wall -Wextra -Werror $(INC) -rdynamic -o $(B)/test_panel_tsan test_panel.c -lm -lpthread -ldl
	$(B)/test_panel_tsan

clean:
	rm -rf $(B)
