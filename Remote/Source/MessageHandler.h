#pragma once

#include <JuceHeader.h>
#include "../ipc/boost/interprocess/ipc/message_queue.hpp"

#ifndef PD_REMOTE
#include "PdRemoteBinaryData.h"
#endif

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
    
    MessageHandler(String instanceID) : ID(instanceID)
#ifndef PD_REMOTE
    , ping(*this)
#endif
    {
        
        if(ID == "test_mode") {
            send_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_or_create, (ID + "_receive").toRawUTF8(), 100, bufsize);
            
            receive_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_or_create, (ID + "_send").toRawUTF8(), 100, bufsize);
            
            return;
        }
        
        initialise();
        
    }
    
    ~MessageHandler() {
        child.kill();
    }
    
    void initialise() {
        
#ifdef PD_REMOTE
        send_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_only, (ID + "_send").toRawUTF8());
        
        receive_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_only, (ID + "_receive").toRawUTF8());
        
        levelmeter_memory = std::make_unique<boost::interprocess::shared_memory_object>(boost::interprocess::open_only, (ID + "_levelmeter").toRawUTF8(), boost::interprocess::read_write);
        
#else
        
        if(child.isRunning()) {
            
            MemoryOutputStream message;
            message.writeInt(MessageHandler::tGlobal);
            message.writeString("Quit");
            sendMessage(message.getMemoryBlock());
            
            if(!child.waitForProcessToFinish(1000)) {
                child.kill();
            }
        }
        
        auto binaryLocation = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("PlugData").getChildFile("PdRemote");
        
#if JUCE_DEBUG
        auto debug = true;
#else
        auto debug = false;
#endif
        
        if(!binaryLocation.exists() || debug) {
            MemoryInputStream zippedBinary(PdRemoteBinaryData::PdRemote_zip, PdRemoteBinaryData::PdRemote_zipSize, false);
            auto file = ZipFile(zippedBinary);
            file.uncompressTo(binaryLocation.getParentDirectory());
            binaryLocation.getChildFile("PdRemote").setExecutePermission(true);
        }
        
        auto path = binaryLocation.getChildFile("PdRemote").getFullPathName();
        
        StringArray args = {path, ID};
        child.start(args);
        
        send_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_or_create, (ID + "_receive").toRawUTF8(), 100, 1024 * 20);
        
        receive_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_or_create, (ID + "_send").toRawUTF8(), 100, 1024 * 20);
        
        levelmeter_memory = std::make_unique<boost::interprocess::shared_memory_object>(boost::interprocess::open_or_create, (ID + "_levelmeter").toRawUTF8(), boost::interprocess::read_write);
        
        levelmeter_memory->truncate(sizeof(float) * 4);
        
        ping.startPinging();
#endif
    }
    
    
    void sendLevelMeterStatus(float l, float r, float midiin, float midiout)
    {
        /*
         boost::interprocess::mapped_region region(*levelmeter_memory, boost::interprocess::read_write);
         
         auto* address = static_cast<float*>(region.get_address());
         
         address[0] = l;
         address[1] = r;
         address[2] = midiin;
         address[3] = midiout; */
    }
    
    std::tuple<float, float, bool, bool> receiveLevelMeterStatus()
    {
        
        boost::interprocess::mapped_region region(*levelmeter_memory, boost::interprocess::read_only);
        
        auto* address = static_cast<float*>(region.get_address());
        
        return {address[0], address[1], static_cast<bool>(address[2]), static_cast<bool>(address[3])};
    }
    
    void sendMessage(MemoryBlock block)
    {
        send_queue->try_send(block.getData(), block.getSize(), 1);
    }
    
    bool receiveMessages(MemoryBlock& message) {
        void* buffer = malloc(bufsize);
        size_t size;
        unsigned int priority;
        bool status = receive_queue->try_receive(buffer, bufsize, size, priority);
        
        if(status) message = MemoryBlock(buffer, size);
        free(buffer);
        
        return status;
    }
    
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
            pingReceived();
        }
        
        ~Ping(){
            stopThread(-1);
        }
        
        void startPinging()
        {
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
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MessageHandler)
};
