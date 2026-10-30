#include "MainComponent.h"
#include <iostream>

namespace Reel { int runSelfTest (te::Engine&, const juce::File&); }

class ReelApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Reel Studio"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        engine = std::make_unique<te::Engine> ("Reel Studio");
        engine->getPluginManager().createBuiltInType<Reel::DistortionPlugin>();
        engine->getPluginManager().createBuiltInType<Reel::VocoderPlugin>();

        auto args = juce::StringArray::fromTokens (commandLine, true);
        if (args.contains ("--selftest"))
        {
            const auto i = args.indexOf ("--selftest");
            auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1].isNotEmpty() ? args[i + 1] : juce::String ("selftest"));
            const int fails = Reel::runSelfTest (*engine, dir);
            setApplicationReturnValue (fails == 0 ? 0 : 1);
            quit();
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        session = std::make_unique<Reel::Session> (*engine);
        auto autosave = Reel::appDataDir().getChildFile ("Autosave.tracktionedit");
        bool opened = false;
        for (auto& a : args)
            if (a.unquoted().endsWith (".tracktionedit") && juce::File (a.unquoted()).existsAsFile())
                opened = session->openProject (juce::File (a.unquoted()));
        if (! opened && autosave.existsAsFile() && session->openProject (autosave))
        {
            auto last = Reel::appDataDir().getChildFile ("LastProject.txt").loadFileAsString().trim();
            if (last.isNotEmpty()) session->projectFile = juce::File (last);
            opened = true;
        }
        if (! opened) session->loadDemo();
        window = std::make_unique<MainWindow> (*session);
    }

    void shutdown() override
    {
        window.reset();
        session.reset();
        engine.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        auto f = juce::File (commandLine.unquoted().trim());
        if (session != nullptr && f.hasFileExtension ("tracktionedit") && f.existsAsFile()) session->openProject (f);
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (Reel::Session& s)
            : DocumentWindow ("Reel Studio", Reel::Col::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new Reel::MainComponent (s), true);
            setResizable (true, true);
            setResizeLimits (900, 620, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            if (auto* c = getContentComponent()) c->grabKeyboardFocus();
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    Reel::ReelLookAndFeel lookAndFeel;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<Reel::Session> session;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (ReelApplication)
