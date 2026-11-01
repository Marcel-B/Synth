# Host test of the module's DSP and panel logic, against the plugins' own voices: Tonwerk Wavetable for the module's
# firmware, Tonwerk Physical for the Physical firmware (-DTONWERK_PHYSICAL=1), Tonwerk PD for the PD firmware
# (-DTONWERK_PD=1), Tonwerk Granular for the Granular firmware (-DTONWERK_GRANULAR=1), Tonwerk Additiv for the
# Additiv firmware (-DTONWERK_ADDITIVE=1).
#   make test                                         builds and runs all tests (SYNTH defaults to this repo)
#   make examples                                     also writes listening examples to ./hoerproben
#   make grain-sources                                writes ../data/grain-sources.bin, the Granular firmware's sources
SYNTH ?= ../../..
CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
INCLUDES = -I../main -I$(SYNTH)/src -I$(SYNTH)/wavetable/src -I$(SYNTH)/physical/src -I$(SYNTH)/pd/src -I$(SYNTH)/granular/src -I$(SYNTH)/additive/src

host-test: HostTest.cpp ../main/dsp/Tables.cpp ../main/dsp/*.h ../main/app/*.h ../main/hw/UpdateImage.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ HostTest.cpp ../main/dsp/Tables.cpp $(SYNTH)/wavetable/src/Wavetables.cpp

physical-test: PhysicalTest.cpp ../main/dsp/*.h ../main/app/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ PhysicalTest.cpp

pd-test: PdTest.cpp ../main/dsp/*.h ../main/app/*.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ PdTest.cpp

granular-test: GranularTest.cpp ../main/dsp/*.h ../main/app/*.h $(SYNTH)/granular/src/*.h $(SYNTH)/granular/src/GrainSources.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ GranularTest.cpp $(SYNTH)/granular/src/GrainSources.cpp

additive-test: AdditiveTest.cpp ../main/dsp/*.h ../main/app/*.h $(SYNTH)/additive/src/AdditiveEngine.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ AdditiveTest.cpp

grain-sources: granular-test
	mkdir -p ../data
	./granular-test grain-sources ../data/grain-sources.bin

test: host-test physical-test pd-test granular-test additive-test
	./host-test
	./physical-test
	./pd-test
	./granular-test
	./additive-test

examples: host-test physical-test pd-test granular-test additive-test
	mkdir -p hoerproben hoerproben-physical hoerproben-pd hoerproben-granular hoerproben-additiv
	./host-test hoerproben && ./physical-test hoerproben-physical && ./pd-test hoerproben-pd \
		&& ./granular-test hoerproben-granular && ./additive-test hoerproben-additiv

.PHONY: test examples grain-sources
