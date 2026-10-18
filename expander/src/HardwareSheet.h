#pragma once

#include "ExpanderEngine.h"

#include <juce_graphics/juce_graphics.h>

namespace tonwerkexpander
{
/**
 * The patch drawn as the knob positions of the hardware module it is modelled on (`hardware::panelFor`): one voice
 * module beside the two-voice controller, each knob turned to its clock position, the switches set, and below what
 * does not carry over. Made for the export button and the tests; light like the hardware's panel, for printing.
 */
juce::Image drawHardwareSheet(const Settings& settings, const juce::String& name);
} // namespace tonwerkexpander
