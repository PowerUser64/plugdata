#include <JuceHeader.h>
#include "PureData.h"

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

    while(!pd.shouldQuit())
    {
        pd.receiveMessages();
        
        pd.waitForNextBlock();
        
    }

    MessageManager::deleteInstance();
}
