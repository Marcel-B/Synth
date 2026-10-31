# Host test of the module's DSP and panel logic, against the plugins' own voices: Tonwerk Wavetable for the module's
# firmware, Tonwerk Physical for the Physical firmware (-DTONWERK_PHYSICAL=1), Tonwerk PD for the PD firmware
# (-DTONWERK_PD=1).
#   make test                                         builds and runs all tests (SYNTH defaults to this repo)
#   make examples                                     also writes listening examples to ./hoerproben
SYNTH ?= ../../..
CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
INCLUDES = -I../main -I$(SYNTH)/src -I$(SYNTH)/wavetable/src -I$(SYNTH)/physical/src -I$(SYNTH)/pd/src

host-test: HostTest.cpp ../main/dsp/Tables.cpp ../main/dsp/*.h ../main/app/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ HostTest.cpp ../main/dsp/Tables.cpp $(SYNTH)/wavetable/src/Wavetables.cpp

physical-test: PhysicalTest.cpp ../main/dsp/*.h ../main/app/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ PhysicalTest.cpp

pd-test: PdTest.cpp ../main/dsp/*.h ../main/app/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ PdTest.cpp

test: host-test physical-test pd-test
	./host-test
	./physical-test
	./pd-test

examples: host-test physical-test pd-test
	mkdir -p hoerproben hoerproben-physical hoerproben-pd
	./host-test hoerproben && ./physical-test hoerproben-physical && ./pd-test hoerproben-pd

.PHONY: test examples
