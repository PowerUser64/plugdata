/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
#include "PureData.h"

/*
//==============================================================================
class PdRemoteApplication  : public juce::JUCEApplication
{
public:
    //==============================================================================
    PdRemoteApplication() {}

    const juce::String getApplicationName() override       { return "PdRemote"; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    //==============================================================================
    void initialise (const juce::String& commandLine) override
    {
        // This method is where you should put your application's initialisation code..

        pd.reset(new PureData(commandLine));
    }

    void shutdown() override
    {
        // Add your application's shutdown code here..

        pd = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
    }

private:
    std::unique_ptr<PureData> pd;
};*/



int main(int argc, char *argv[]) { 
    
    //Thread::getCurrentThread()->setPriority(Thread::realtimeAudioPriority);
    
    // Run in test mode
    if(argc < 2)  {
        
        MessageManager::deleteInstance();
        return; // Outcomment to enable test mode
        
        auto pd = PureData("test_mode");

        while(true)
        {
            pd.waitForNextBlock();
        }
    }

    auto pd = PureData(String(argv[1]));

    while(true) 
    {
        pd.receiveMessages();
        
        pd.waitForNextBlock();
        
    }

    MessageManager::deleteInstance();
}
