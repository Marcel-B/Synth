#include <juce_core/juce_core.h>
#include <cstdio>
#include <juce_events/juce_events.h>

int main()
{
    // The processor starts a timer and the editor's code needs a message manager, even if no window opens.
    juce::ScopedJuceInitialiser_GUI juce;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult(i)->failures;
    std::printf("%s\n", failures == 0 ? "All tests passed." : "Some tests FAILED.");
    return failures == 0 ? 0 : 1;
}
