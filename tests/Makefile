# SPDX-License-Identifier: GPL-3.0-only
# Link on Deck tests, on a Linux computer with clang (no deck, no Docker):
#   make -C tests          both
#   make -C tests link     flow-beat.so (host build) + a second Link peer: tempo, beat, bar,
#                          start/stop, jog nudges, block steps, "this is 1", lead change (~15 s)
#   make -C tests bars     the bar lookup from a rekordbox PQTZ grid, with ASan + UBSan
AZ_SDK_INCLUDE ?= ../../include
LINK   := ../third_party/link
INC    := -I$(AZ_SDK_INCLUDE) -I$(LINK)/include -I$(LINK)/modules/asio-standalone/asio/include
CC     := clang
CXX    := clang++
B      := build

.PHONY: all link bars clean
all: bars link

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

bars: test_bars.c ../flow-beat.c ../mixer.h | $(B)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror $(INC) -o $(B)/test_bars test_bars.c -lm -lpthread
	$(B)/test_bars

clean:
	rm -rf $(B)
