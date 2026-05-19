#include <JuceHeader.h>
#include "MainComponent.h"

namespace te = tracktion_engine;

class PerformanceDAWApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise (const juce::String&) override
    {
        // te::Engine lifetime = app lifetime.
        engine = std::make_unique<te::Engine> (getApplicationName());
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
    }

    void shutdown() override
    {
        mainWindow.reset();
        engine.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, te::Engine& engine)
            : juce::DocumentWindow (name,
                                    juce::Colour (0xff111111),
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent (engine);
            setContentOwned (content, true);
            // Register MainComponent as a key listener on the window so shortcuts
            // fire even when child components (text editors, combos) have focus.
            addKeyListener (content);

            #if JUCE_IOS || JUCE_ANDROID
                setFullScreen (true);
            #else
                setResizable (true, true);
                centreWithSize (1440, 900);
            #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<te::Engine>   engine;
    std::unique_ptr<MainWindow>   mainWindow;
};

START_JUCE_APPLICATION (PerformanceDAWApp)
