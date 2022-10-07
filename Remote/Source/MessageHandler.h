#pragma once

#include <JuceHeader.h>
#include "../ipc/boost/interprocess/ipc/message_queue.hpp"

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
    
    MessageHandler(String instanceID, bool owner) : ID(instanceID)
    {
        
        if(ID == "test_mode") {
            send_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_or_create, (ID + "_receive").toRawUTF8(), 100, bufsize);
            
            receive_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_or_create, (ID + "_send").toRawUTF8(), 100, bufsize);
            
            return;
        }
        
        if(owner) {
            
            auto path = String("/Users/timschoen/Projecten/PlugData/XCode/PdRemote_artefacts/Debug/PdRemote");
            
            StringArray args = {path, ID};
            child.start(args);
            
            send_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::create_only, (ID + "_receive").toRawUTF8(), 100, 1024 * 20);
            
            receive_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::create_only, (ID + "_send").toRawUTF8(), 100, 1024 * 20);
        }
        else {
            send_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_only, (ID + "_send").toRawUTF8());
            
            receive_queue = std::make_unique<boost::interprocess::message_queue>(boost::interprocess::open_only, (ID + "_receive").toRawUTF8());
        }
        
    }
    
    ~MessageHandler() {
        child.kill();
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
    
    String ID;
    
    ChildProcess child;

    std::unique_ptr<boost::interprocess::message_queue> send_queue;
    std::unique_ptr<boost::interprocess::message_queue> receive_queue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MessageHandler)
};
