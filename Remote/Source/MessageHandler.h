#pragma once

#include <JuceHeader.h>
#include "../ipc/boost/interprocess/interprocess_fwd.hpp"

class MessageHandler
{
    static inline constexpr int bufsize = 1024 * 20;
public:
    
    enum MessageType
    {
        tGlobal,
        tObject,
        tPatch
    };
    
    MessageHandler(String instanceID);
    
    ~MessageHandler();
    
    void initialise();
    
    
    void sendLevelMeterStatus(float l, float r, float midiin, float midiout);
    
    std::tuple<float, float, bool, bool> receiveLevelMeterStatus();
    
    void sendMessage(MemoryBlock block);
    
    bool receiveMessages(MemoryBlock& message);
    
    const String ID;
    
    ChildProcess child;
    
    std::unique_ptr<boost::interprocess::message_queue> send_queue;
    std::unique_ptr<boost::interprocess::message_queue> receive_queue;
    
    std::unique_ptr<boost::interprocess::shared_memory_object> levelmeter_memory;
    
#ifndef PD_REMOTE
    struct Ping  : public Thread, private AsyncUpdater
    {
        MessageHandler& messageHandler;
        
        Ping(MessageHandler& m, int timeout = 5000)  : Thread ("IPC ping"), messageHandler(m), timeoutMs (timeout)
        {
            
        }
        
        ~Ping(){
            stopThread(-1);
        }
        
        void startPinging()
        {
            pingReceived();
            startThread (4);
        }
        
        void pingReceived() noexcept
        {
            countdown = timeoutMs / 1000 + 1;
        }
        
        void triggerConnectionLostMessage()
        {
            triggerAsyncUpdate();
        }
        
        void sendPingMessage() {
            MemoryOutputStream message;
            message.writeInt(MessageHandler::tGlobal);
            message.writeString("Ping");
            messageHandler.sendMessage(message.getMemoryBlock());
        }
        
        void pingFailed() {
            messageHandler.initialise();
            pingReceived();
        }
        
        int timeoutMs;
        
        using AsyncUpdater::cancelPendingUpdate;
        
    private:
        Atomic<int> countdown;
        
        void handleAsyncUpdate() override   { pingFailed(); }
        
        void run() override
        {
            while (! threadShouldExit())
            {
                sendPingMessage();
                
                if (--countdown <= 0)
                {
                    triggerConnectionLostMessage();
                    break;
                }
                
                wait (1000);
            }
        }
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ping)
    };
    
    
    Ping ping;
#endif
    
#if DEBUG
    bool firstRun = true;
#endif
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MessageHandler)
};
