#pragma once

#include <JuceHeader.h>
#include "../ipc/boost/interprocess/interprocess_fwd.hpp"

struct InterprocessSemaphores;
struct AudioExchanger
{
    bool ipcMode;
    
    AudioExchanger(String ID, bool isPlugin);
    
    ~AudioExchanger();
    
    void waitForOutput();
    bool waitForInput(int timeout);
    
    void notifyOutput();
    void notifyInput();
    
    void sendAudioBuffer(dsp::AudioBlock<float> buffer);
    
    void receiveAudioBuffer(AudioBuffer<float>& buffer);
    
    dsp::AudioBlock<float> receiveAudioBuffer();
    
    WaitableEvent audioProcessSemaphore;
    WaitableEvent audioDoneSemaphore;
    AudioBuffer<float> sharedBuffer;

    std::unique_ptr<boost::interprocess::shared_memory_object> ipcSharedBuffer;
    std::unique_ptr<boost::interprocess::mapped_region> ipcMappedRegion;
    
    std::unique_ptr<boost::interprocess::shared_memory_object> syncSharedBuffer;
    std::unique_ptr<boost::interprocess::mapped_region> syncMappedRegion;
    
    InterprocessSemaphores* ipcSemaphores;

};

