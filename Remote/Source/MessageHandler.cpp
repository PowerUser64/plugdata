#include "MessageHandler.h"
#include "../ipc/boost/interprocess/ipc/message_queue.hpp"

#ifndef PD_REMOTE
#include "PdRemoteBinaryData.h"
#endif


MessageHandler::MessageHandler(String instanceID) : ID(instanceID)
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

MessageHandler::~MessageHandler() {
    child.kill();
}

void MessageHandler::initialise() {
    
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
        
        //if(!child.waitForProcessToFinish(1000)) {
            child.kill();
        //}
    }
    
    auto binaryLocation = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("PlugData").getChildFile("PdRemote");
    
#if JUCE_DEBUG
    auto forceUnpack = firstRun;
    firstRun = false;
#else
    auto forceUnpack = false;
#endif
    
    if(!binaryLocation.exists() || forceUnpack) {
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


void MessageHandler::sendLevelMeterStatus(float l, float r, float midiin, float midiout)
{
    /*
     boost::interprocess::mapped_region region(*levelmeter_memory, boost::interprocess::read_write);
     
     auto* address = static_cast<float*>(region.get_address());
     
     address[0] = l;
     address[1] = r;
     address[2] = midiin;
     address[3] = midiout; */
}

std::tuple<float, float, bool, bool> MessageHandler::receiveLevelMeterStatus()
{
    
    boost::interprocess::mapped_region region(*levelmeter_memory, boost::interprocess::read_only);
    
    auto* address = static_cast<float*>(region.get_address());
    
    return {address[0], address[1], static_cast<bool>(address[2]), static_cast<bool>(address[3])};
}

void MessageHandler::sendMessage(MemoryBlock block)
{
    send_queue->try_send(block.getData(), block.getSize(), 1);
}

bool MessageHandler::receiveMessages(MemoryBlock& message) {
    void* buffer = malloc(bufsize);
    size_t size;
    unsigned int priority;
    bool status = receive_queue->try_receive(buffer, bufsize, size, priority);
    
    if(status) message = MemoryBlock(buffer, size);
    free(buffer);
    
    return status;
}

