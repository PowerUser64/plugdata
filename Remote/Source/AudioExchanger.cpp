#include "AudioExchanger.h"
#include "../ipc/boost/interprocess/sync/interprocess_semaphore.hpp"
#include "../ipc/boost/interprocess/shared_memory_object.hpp"
#include "../ipc/boost/interprocess/mapped_region.hpp"


struct InterprocessSemaphores
{
    InterprocessSemaphores(String& ID) :
    ipcAudioProcessSemaphore(0),
    ipcAudioDoneSemaphore(0)
    {
    }
 
    boost::interprocess::interprocess_semaphore ipcAudioProcessSemaphore;
    boost::interprocess::interprocess_semaphore ipcAudioDoneSemaphore;
};


AudioExchanger::AudioExchanger(String ID, bool isPlugin) : ipcMode(isPlugin)
{

    if(true) {
        
        ipcSharedBuffer = std::make_unique<boost::interprocess::shared_memory_object>(boost::interprocess::open_or_create, (ID + "_audiobuffer").toRawUTF8(), boost::interprocess::read_write);
        
        ipcSharedBuffer->truncate(64 * 8192 * sizeof(float));
        
        
        syncSharedBuffer = std::make_unique<boost::interprocess::shared_memory_object>(boost::interprocess::open_or_create, (ID + "_sync").toRawUTF8(), boost::interprocess::read_write);
        
        syncSharedBuffer->truncate(sizeof(InterprocessSemaphores));
        
        syncMappedRegion = std::make_unique<boost::interprocess::mapped_region>(*syncSharedBuffer, boost::interprocess::read_write);
        
#if !PD_REMOTE
        void* addr = syncMappedRegion->get_address();
        ipcSemaphores = new (addr) InterprocessSemaphores(ID);
#else
        ipcSemaphores = static_cast<InterprocessSemaphores*>(syncMappedRegion->get_address());
#endif

        ipcMappedRegion = std::make_unique<boost::interprocess::mapped_region>(*ipcSharedBuffer, boost::interprocess::read_write);
        
    }

}

AudioExchanger::~AudioExchanger() = default;

void AudioExchanger::waitForOutput() {
    if(ipcMode) ipcSemaphores->ipcAudioDoneSemaphore.wait();
    else audioDoneSemaphore.wait();
}
bool AudioExchanger::waitForInput(int timeout) {
    if(ipcMode)  {
        const std::chrono::time_point<std::chrono::system_clock> now =
                std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
        
        return ipcSemaphores->ipcAudioProcessSemaphore.timed_wait(now);
    }
    else {
        return audioProcessSemaphore.wait(timeout);
    }
    
}

void AudioExchanger::notifyOutput() {
    
    if(ipcMode) ipcSemaphores->ipcAudioDoneSemaphore.post();
    else audioDoneSemaphore.signal();
}
void AudioExchanger::notifyInput() {
    if(ipcMode) ipcSemaphores->ipcAudioProcessSemaphore.post();
    else audioProcessSemaphore.signal();
}

void AudioExchanger::sendAudioBuffer(dsp::AudioBlock<float> buffer)
{
    if(ipcMode) {
        auto* address = static_cast<float*>(ipcMappedRegion->get_address());

        address[0] = buffer.getNumChannels();
        address[1] = buffer.getNumSamples();
        
        address += 2;
        
        for(int ch = 0; ch < buffer.getNumChannels(); ch++) {
            auto* channelPtr = buffer.getChannelPointer(ch);
            std::copy(channelPtr, channelPtr + buffer.getNumSamples(), address);
            address += buffer.getNumSamples();
        }
    }
    else {
        sharedBuffer.setSize(buffer.getNumChannels(), buffer.getNumSamples());
        buffer.copyTo(sharedBuffer);
    }
}

void AudioExchanger::receiveAudioBuffer(AudioBuffer<float>& buffer)
{
    jassert(ipcMode);

    auto* address = static_cast<float*>(ipcMappedRegion->get_address());
    int i = 0;
    
    buffer.setSize(address[0], address[1]);
    address += 2;

    for(int ch = 0; ch < buffer.getNumChannels(); ch++) {
        auto* channelPtr = buffer.getWritePointer(ch);
        std::copy(address, address + buffer.getNumSamples(), channelPtr);
        address += buffer.getNumSamples();

    }
}

dsp::AudioBlock<float> AudioExchanger::receiveAudioBuffer()
{
    jassert(!ipcMode);

    return dsp::AudioBlock<float>(sharedBuffer);
}


