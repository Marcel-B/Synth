# Host test of the module's DSP and panel logic, against Tonwerk Wavetable's own voice.
#   make test                                         builds and runs the test (SYNTH defaults to this repo)
#   make examples                                     also writes listening examples to ./hoerproben
SYNTH ?= ../../..
CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
INCLUDES = -I../main -I$(SYNTH)/src -I$(SYNTH)/wavetable/src

host-test: HostTest.cpp ../main/dsp/Tables.cpp ../main/dsp/*.h ../main/app/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ HostTest.cpp ../main/dsp/Tables.cpp $(SYNTH)/wavetable/src/Wavetables.cpp

test: host-test
	./host-test

examples: host-test
	mkdir -p hoerproben && ./host-test hoerproben

.PHONY: test examples
