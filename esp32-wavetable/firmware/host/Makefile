# Host test of the module's DSP and panel logic, against the plugins' own voices: Tonwerk Wavetable for the module's
# firmware, Tonwerk Physical for the Physical firmware (-DTONWERK_PHYSICAL=1).
#   make test                                         builds and runs both tests (SYNTH defaults to this repo)
#   make examples                                     also writes listening examples to ./hoerproben
SYNTH ?= ../../..
CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
INCLUDES = -I../main -I$(SYNTH)/src -I$(SYNTH)/wavetable/src -I$(SYNTH)/physical/src

host-test: HostTest.cpp ../main/dsp/Tables.cpp ../main/dsp/*.h ../main/app/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ HostTest.cpp ../main/dsp/Tables.cpp $(SYNTH)/wavetable/src/Wavetables.cpp

physical-test: PhysicalTest.cpp ../main/dsp/*.h ../main/app/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ PhysicalTest.cpp

test: host-test physical-test
	./host-test
	./physical-test

examples: host-test physical-test
	mkdir -p hoerproben hoerproben-physical && ./host-test hoerproben && ./physical-test hoerproben-physical

.PHONY: test examples
