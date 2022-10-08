#include "PureData.h"
#include <x_libpd_multi.h>

#include "../ipc/boost/interprocess/ipc/message_queue.hpp"
#include "../ipc/boost/interprocess/sync/named_semaphore.hpp"

extern "C" {

#include <s_libpd_inter.h>

struct PureData::internal {

    static void multi_bang(PureData* ptr, char const* recv)
    {
        ptr->processMessage({ String("bang"), String(recv) });
    }

    static void multi_float(PureData* ptr, char const* recv, float f)
    {
        ptr->processMessage({ String("float"), String(recv), std::vector<Atom>(1, { f }) });
    }

    static void multi_symbol(PureData* ptr, char const* recv, char const* sym)
    {
        ptr->processMessage({ String("symbol"), String(recv), std::vector<Atom>(1, String(sym)) });
    }

    static void multi_list(PureData* ptr, char const* recv, int argc, t_atom* argv)
    {
        Message mess { String("list"), String(recv), std::vector<Atom>(argc) };
        for (int i = 0; i < argc; ++i) {
            if (argv[i].a_type == A_FLOAT)
                mess.list[i] = Atom(atom_getfloat(argv + i));
            else if (argv[i].a_type == A_SYMBOL)
                mess.list[i] = Atom(String(atom_getsymbol(argv + i)->s_name));
        }

        ptr->processMessage(mess);
    }

    static void multi_message(PureData* ptr, char const* recv, char const* msg, int argc, t_atom* argv)
    {
        Message mess { msg, String(recv), std::vector<Atom>(argc) };
        for (int i = 0; i < argc; ++i) {
            if (argv[i].a_type == A_FLOAT)
                mess.list[i] = Atom(atom_getfloat(argv + i));
            else if (argv[i].a_type == A_SYMBOL)
                mess.list[i] = Atom(String(atom_getsymbol(argv + i)->s_name));
        }
        ptr->processMessage(std::move(mess));
    }

    static void multi_noteon(PureData* ptr, int channel, int pitch, int velocity)
    {
        ptr->processMidiEvent({ midievent::NOTEON, channel, pitch, velocity });
    }

    static void multi_controlchange(PureData* ptr, int channel, int controller, int value)
    {
        ptr->processMidiEvent({ midievent::CONTROLCHANGE, channel, controller, value });
    }

    static void multi_programchange(PureData* ptr, int channel, int value)
    {
        ptr->processMidiEvent({ midievent::PROGRAMCHANGE, channel, value, 0 });
    }

    static void multi_pitchbend(PureData* ptr, int channel, int value)
    {
        ptr->processMidiEvent({ midievent::PITCHBEND, channel, value, 0 });
    }

    static void multi_aftertouch(PureData* ptr, int channel, int value)
    {
        ptr->processMidiEvent({ midievent::AFTERTOUCH, channel, value, 0 });
    }

    static void multi_polyaftertouch(PureData* ptr, int channel, int pitch, int value)
    {
        ptr->processMidiEvent({ midievent::POLYAFTERTOUCH, channel, pitch, value });
    }

    static void multi_midibyte(PureData* ptr, int port, int byte)
    {
        ptr->processMidiEvent({ midievent::MIDIBYTE, port, byte, 0 });
    }

    static void multi_print(PureData* ptr, char const* s)
    {
        ptr->processPrint(s);
    }
};
}

//==============================================================================
PureData::PureData(String ID) : messageHandler(ID), midiOutput(deviceManager.getDefaultMidiOutput()), audioExchanger(ID, ID.contains("plugin"))
{
    channelPointers.reserve(32);

    // Set up midi buffers
    midiBufferIn.ensureSize(2048);
    midiBufferOut.ensureSize(2048);
    midiBufferTemp.ensureSize(2048);
    midiBufferCopy.ensureSize(2048);
    
    libpd_multi_init();
    libpd_set_verbose(0);
    
    //patches.add(new Patch(messageHandler, libpd_openfile("instantosc.pd", "/Users/timschoen")));
    
    synchronise();
    
    if(!ID.contains("plugin")) {
        // Some platforms require permissions to open input channels so request that here
        if(juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
           && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
        {
            juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                               [&] (bool granted) { setAudioChannels (granted ? numInputChannels : 0, numOutputChannels); });
        }
        else
        {
            // Specify the number of input and output channels that we want to open
            setAudioChannels (numInputChannels, numOutputChannels);
        }
    }
    
    m_midi_receiver = libpd_multi_midi_new(this, reinterpret_cast<t_libpd_multi_noteonhook>(internal::multi_noteon), reinterpret_cast<t_libpd_multi_controlchangehook>(internal::multi_controlchange), reinterpret_cast<t_libpd_multi_programchangehook>(internal::multi_programchange),
        reinterpret_cast<t_libpd_multi_pitchbendhook>(internal::multi_pitchbend), reinterpret_cast<t_libpd_multi_aftertouchhook>(internal::multi_aftertouch), reinterpret_cast<t_libpd_multi_polyaftertouchhook>(internal::multi_polyaftertouch),
        reinterpret_cast<t_libpd_multi_midibytehook>(internal::multi_midibyte));
    m_print_receiver = libpd_multi_print_new(this, reinterpret_cast<t_libpd_multi_printhook>(internal::multi_print));

    m_message_receiver = libpd_multi_receiver_new(this, "pd", reinterpret_cast<t_libpd_multi_banghook>(internal::multi_bang), reinterpret_cast<t_libpd_multi_floathook>(internal::multi_float), reinterpret_cast<t_libpd_multi_symbolhook>(internal::multi_symbol),
        reinterpret_cast<t_libpd_multi_listhook>(internal::multi_list), reinterpret_cast<t_libpd_multi_messagehook>(internal::multi_message));

    m_parameter_receiver = libpd_multi_receiver_new(this, "param", reinterpret_cast<t_libpd_multi_banghook>(internal::multi_bang), reinterpret_cast<t_libpd_multi_floathook>(internal::multi_float), reinterpret_cast<t_libpd_multi_symbolhook>(internal::multi_symbol),
        reinterpret_cast<t_libpd_multi_listhook>(internal::multi_list), reinterpret_cast<t_libpd_multi_messagehook>(internal::multi_message));

    m_parameter_change_receiver = libpd_multi_receiver_new(this, "param_change", reinterpret_cast<t_libpd_multi_banghook>(internal::multi_bang), reinterpret_cast<t_libpd_multi_floathook>(internal::multi_float), reinterpret_cast<t_libpd_multi_symbolhook>(internal::multi_symbol),
        reinterpret_cast<t_libpd_multi_listhook>(internal::multi_list), reinterpret_cast<t_libpd_multi_messagehook>(internal::multi_message));

    m_atoms = malloc(sizeof(t_atom) * 512);

    // Register callback when pd's gui changes
    // Needs to be done on pd's thread
    auto gui_trigger = [](void* instance, void* target) {
        auto* pd = static_cast<t_pd*>(target);

        // redraw scalar
        if (pd && !strcmp((*pd)->c_name->s_name, "scalar")) {
            //static_cast<PureData*>(instance)->receiveGuiUpdate(2);
        } else {
            //static_cast<PureData*>(instance)->receiveGuiUpdate(1);
        }
    };

    auto panel_trigger = [](void* instance, int open, char const* snd, char const* location) {
        
        // TODO: open panel
        //static_cast<PureData*>(instance)->createPanel(open, snd, location);
        
    };

    auto parameter_trigger = [](void* instance) {
        // TODO: parameter changed
        //static_cast<PureData*>(instance)->receiveGuiUpdate(3);
    };

    auto synchronise_trigger = [](void* instance, void* cnv) {};

    register_gui_triggers(static_cast<t_pdinstance*>(m_instance), this, gui_trigger, panel_trigger, synchronise_trigger, parameter_trigger);
    
    sendPing();
    
}

PureData::~PureData()
{
    // This shuts down the audio device and clears the audio source.
    shutdownAudio();
}

//==============================================================================
void PureData::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    const auto blksize = static_cast<size_t>(libpd_blocksize());
    const auto numIn = static_cast<size_t>(numInputChannels);
    const auto nouts = static_cast<size_t>(numOutputChannels);
    audioBufferIn.resize(numInputChannels * blksize, 0);
    audioBufferOut.resize(numOutputChannels * blksize, 0);

    libpd_init_audio(numInputChannels, numOutputChannels, sampleRate);
    libpd_start_message(1); // one entry in list
    libpd_add_float(1.0f);
    libpd_finish_message("pd", "dsp");
    
    midiInputCollector.reset(sampleRate);
}

void PureData::processInternal(bool without_dsp)
{
    // clear midi out
    midiByteIndex = 0;
    midiByteBuffer[0] = 0;
    midiByteBuffer[1] = 0;
    midiByteBuffer[2] = 0;
    midiBufferOut.clear();

    // Dequeue messages
    //sendMessagesFromQueue();
    //sendPlayhead();
    sendMidiBuffer();
    //sendParameters();
    
    if(without_dsp) {
        libpd_process_nodsp();
        return;
    }

    
    // Process audio
    /*
    FloatVectorOperations::copy(audioBufferIn.data() + ( * 64), audioBufferOut.data() + (2 * 64), (minOut - 2) * 64); */
    libpd_process_raw(audioBufferIn.data(), audioBufferOut.data());
    
    float gain = volume * static_cast<float>(dspState);
    
    FloatVectorOperations::multiply(audioBufferOut.data(), gain, numOutputChannels * 64);
}

void PureData::sendNoteOn(int const channel, int const pitch, int const velocity) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_noteon(channel - 1, pitch, velocity);
}

void PureData::sendControlChange(int const channel, int const controller, int const value) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_controlchange(channel - 1, controller, value);
}

void PureData::sendProgramChange(int const channel, int const value) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_programchange(channel - 1, value);
}

void PureData::sendPitchBend(int const channel, int const value) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_pitchbend(channel - 1, value);
}

void PureData::sendAfterTouch(int const channel, int const value) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_aftertouch(channel - 1, value);
}

void PureData::sendPolyAfterTouch(int const channel, int const pitch, int const value) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_polyaftertouch(channel - 1, pitch, value);
}

void PureData::sendSysEx(int const port, int const byte) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_sysex(port, byte);
}

void PureData::sendSysRealTime(int const port, int const byte) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_sysrealtime(port, byte);
}

void PureData::sendMidiByte(int const port, int const byte) const
{
    libpd_set_instance(static_cast<t_pdinstance*>(m_instance));
    libpd_midibyte(port, byte);
}

void PureData::sendMidiBuffer()
{
    for (const auto& event : midiBufferIn)
    {
        auto const message = event.getMessage();
        if (message.isNoteOn())
        {
            sendNoteOn(message.getChannel(), message.getNoteNumber(), message.getVelocity());
        }
        else if (message.isNoteOff())
        {
            sendNoteOn(message.getChannel(), message.getNoteNumber(), 0);
        }
        else if (message.isController())
        {
            sendControlChange(message.getChannel(), message.getControllerNumber(), message.getControllerValue());
        }
        else if (message.isPitchWheel())
        {
            sendPitchBend(message.getChannel(), message.getPitchWheelValue() - 8192);
        }
        else if (message.isChannelPressure())
        {
            sendAfterTouch(message.getChannel(), message.getChannelPressureValue());
        }
        else if (message.isAftertouch())
        {
            sendPolyAfterTouch(message.getChannel(), message.getNoteNumber(), message.getAfterTouchValue());
        }
        else if (message.isProgramChange())
        {
            sendProgramChange(message.getChannel(), message.getProgramChangeNumber());
        }
        else if (message.isSysEx())
        {
            for (int i = 0; i < message.getSysExDataSize(); ++i)
            {
                sendSysEx(0, static_cast<int>(message.getSysExData()[i]));
            }
        }
        else if (message.isMidiClock() || message.isMidiStart() || message.isMidiStop() || message.isMidiContinue() || message.isActiveSense() || (message.getRawDataSize() == 1 && message.getRawData()[0] == 0xff))
        {
            for (int i = 0; i < message.getRawDataSize(); ++i)
            {
                sendSysRealTime(0, static_cast<int>(message.getRawData()[i]));
            }
        }

        for (int i = 0; i < message.getRawDataSize(); i++)
        {
            sendMidiByte(0, static_cast<int>(message.getRawData()[i]));
        }
    }
    midiBufferIn.clear();
}

Patch* PureData::getPatchByID(String ID) {
    for(auto* patch : patches) {
        if(patch->getID() == ID) {
            return patch;
        }
    }
    
    return nullptr;
}
/*
void PureData::removeObject(String canvasID, String initialiser) {
    
    auto* cnv = reinterpret_cast<t_canvas*>(canvasID.getLargeIntValue());
    
    
    //libpd_createobj(cnv, t_symbol *s, int argc, t_atom *argv);
} */

void PureData::synchronise() {
    
    for(auto* patch : patches) {
        patch->synchronise();
    }
}

void PureData::processPrint(const char* s)
{
    auto forwardMessage = [this](String s){
        MemoryOutputStream message;
        message.writeInt(MessageHandler::tGlobal);
        message.writeString("Console");
        message.writeString(s);
        messageHandler.sendMessage(message.getMemoryBlock());
    };
    
    static int length = 0;
    printConcatBuffer[length] = '\0';

    int len = (int)strlen(s);
    while (length + len >= 2048) {
        int d = 2048 - 1 - length;
        strncat(printConcatBuffer, s, d);

        // Send concatenated line to PlugData!
        forwardMessage(String::fromUTF8(printConcatBuffer));

        s += d;
        len -= d;
        length = 0;
        printConcatBuffer[0] = '\0';
    }

    strncat(printConcatBuffer, s, len);
    length += len;

    if (length > 0 && printConcatBuffer[length - 1] == '\n') {
        printConcatBuffer[length - 1] = '\0';

        // Send concatenated line to PlugData!

        forwardMessage(String::fromUTF8(printConcatBuffer));
    
        length = 0;
    }
}

void PureData::processMessage(Message mess)
{
    if (mess.destination == "param") {
        int index = mess.list[0].getFloat();
        float value = std::clamp(mess.list[1].getFloat(), 0.0f, 1.0f);
        // TODO: performParameterChange(0, index - 1, value);
    } else if (mess.destination == "param_change") {
        int index = mess.list[0].getFloat();
        int state = mess.list[1].getFloat() != 0;
        // TODO: performParameterChange(1, index - 1, state);
    } else if (mess.selector == "bang") {
        //receiveBang(mess.destination);
    } else if (mess.selector == "float") {
       // receiveFloat(mess.destination, mess.list[0].getFloat());
    } else if (mess.selector == "symbol") {
       // receiveSymbol(mess.destination, mess.list[0].getSymbol());
    } else if (mess.selector == "list") {
       // receiveList(mess.destination, mess.list);
    } else if (mess.selector == "dsp") {
        receiveDSPState(mess.list[0].getFloat());
    } else {
       // receiveMessage(mess.destination, mess.selector, mess.list);
    }
}

void PureData::receiveDSPState(bool dsp)
{
    MemoryOutputStream message;
    
    message.writeInt(MessageHandler::tGlobal);
    message.writeString("DSP");
    message.writeBool(dsp);
    
    messageHandler.sendMessage(message.getMemoryBlock());
}

void PureData::processMidiEvent(midievent event)
{
    if (event.type == midievent::NOTEON)
        receiveNoteOn(event.midi1 + 1, event.midi2, event.midi3);
    else if (event.type == midievent::CONTROLCHANGE)
        receiveControlChange(event.midi1 + 1, event.midi2, event.midi3);
    else if (event.type == midievent::PROGRAMCHANGE)
        receiveProgramChange(event.midi1 + 1, event.midi2);
    else if (event.type == midievent::PITCHBEND)
        receivePitchBend(event.midi1 + 1, event.midi2);
    else if (event.type == midievent::AFTERTOUCH)
        receiveAftertouch(event.midi1 + 1, event.midi2);
    else if (event.type == midievent::POLYAFTERTOUCH)
        receivePolyAftertouch(event.midi1 + 1, event.midi2, event.midi3);
    else if (event.type == midievent::MIDIBYTE)
        receiveMidiByte(event.midi1, event.midi2);
}

void PureData::receiveNoteOn(const int channel, const int pitch, const int velocity)
{
    if (velocity == 0)
    {
        midiBufferOut.addEvent(MidiMessage::noteOff(channel, pitch, uint8(0)), audioAdvancement);
    }
    else
    {
        midiBufferOut.addEvent(MidiMessage::noteOn(channel, pitch, static_cast<uint8>(velocity)), audioAdvancement);
    }
}

void PureData::receiveControlChange(const int channel, const int controller, const int value)
{
    midiBufferOut.addEvent(MidiMessage::controllerEvent(channel, controller, value), audioAdvancement);
}

void PureData::receiveProgramChange(const int channel, const int value)
{
    midiBufferOut.addEvent(MidiMessage::programChange(channel, value), audioAdvancement);
}

void PureData::receivePitchBend(const int channel, const int value)
{
    midiBufferOut.addEvent(MidiMessage::pitchWheel(channel, value + 8192), audioAdvancement);
}

void PureData::receiveAftertouch(const int channel, const int value)
{
    midiBufferOut.addEvent(MidiMessage::channelPressureChange(channel, value), audioAdvancement);
}

void PureData::receivePolyAftertouch(const int channel, const int pitch, const int value)
{
    midiBufferOut.addEvent(MidiMessage::aftertouchChange(channel, pitch, value), audioAdvancement);
}

void PureData::receiveMidiByte(const int port, const int byte)
{
    if (midiByteIsSysex)
    {
        if (byte == 0xf7)
        {
            midiBufferOut.addEvent(MidiMessage::createSysExMessage(midiByteBuffer, static_cast<int>(midiByteIndex)), audioAdvancement);
            midiByteIndex = 0;
            midiByteIsSysex = false;
        }
        else
        {
            midiByteBuffer[midiByteIndex++] = static_cast<uint8>(byte);
            if (midiByteIndex == 512)
            {
                midiByteIndex = 511;
            }
        }
    }
    else if (midiByteIndex == 0 && byte == 0xf0)
    {
        midiByteIsSysex = true;
    }
    else
    {
        midiByteBuffer[midiByteIndex++] = static_cast<uint8>(byte);
        if (midiByteIndex >= 3)
        {
            midiBufferOut.addEvent(MidiMessage(midiByteBuffer, 3), audioAdvancement);
            midiByteIndex = 0;
        }
    }
}


void PureData::waitForNextBlock()
{
    bool triggered = audioExchanger.waitForInput(500);
    
    if(!triggered) {
        //processInternal(true);
        //return;
    }
    
    dsp::AudioBlock<float> buffer;
    AudioBuffer<float> temp_buf;
    
    if(audioExchanger.ipcMode) {
        audioExchanger.receiveAudioBuffer(temp_buf);
        buffer = dsp::AudioBlock<float>(temp_buf);
        
        numInputChannels = buffer.getNumChannels();
        numOutputChannels = buffer.getNumChannels();
        prepareToPlay(buffer.getNumSamples(), 44100.0f);
    }
    else {
        buffer = audioExchanger.receiveAudioBuffer();
    }
    
    
    MidiBuffer midiMessages;
    midiInputCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());
    
    ScopedNoDenormals noDenormals;
    const int blockSize = libpd_blocksize();
    const int numSamples = static_cast<int>(buffer.getNumSamples());
    const int adv = audioAdvancement >= 64 ? 0 : audioAdvancement;
    const int numLeft = blockSize - adv;
    const int numIn = numInputChannels;
    const int numOut = numOutputChannels;

    channelPointers.clear();
    for(int ch = 0; ch < std::max(numIn, numOut); ch++) {
        channelPointers.push_back(buffer.getChannelPointer(ch));
    }

    const bool midiConsume = true;
    const bool midiProduce = true;

    auto const maxOuts = std::max(numOut, std::max(numIn, numOut));
    for (int ch = numIn; ch < maxOuts; ch++)
    {
        buffer.getSingleChannelBlock(ch).clear();
    }

    // If the current number of samples in this block
    // is inferior to the number of samples required
    if (numSamples < numLeft)
    {
        // we save the input samples and we output
        // the missing samples of the previous tick.
        for (int j = 0; j < numIn; ++j)
        {
            const int index = j * blockSize + adv;
            FloatVectorOperations::copy(audioBufferIn.data() + index, channelPointers[j], numSamples);
        }
        for (int j = 0; j < numOut; ++j)
        {
            const int index = j * blockSize + adv;
            FloatVectorOperations::copy(channelPointers[j], audioBufferOut.data() + index, numSamples);
        }
        if (midiConsume)
        {
            midiBufferIn.addEvents(midiMessages, 0, numSamples, adv);
        }
        if (midiProduce)
        {
             midiMessages.clear();
            midiMessages.addEvents(midiBufferOut, adv, numSamples, -adv);
        }
        audioAdvancement += numSamples;
    }
    // If the current number of samples in this block
    // is superior to the number of samples required
    else
    {
        // we save the missing input samples, we output
        // the missing samples of the previous tick and
        // we call DSP perform method.
        MidiBuffer const& midiin = midiProduce ? midiBufferTemp : midiMessages;
        if (midiProduce)
        {
            midiBufferTemp.swapWith(midiMessages);
            midiMessages.clear();
        }

        for (int j = 0; j < numIn; ++j)
        {
            const int index = j * blockSize + adv;
            FloatVectorOperations::copy(audioBufferIn.data() + index, channelPointers[j], numLeft);
        }
        for (int j = 0; j < numOut; ++j)
        {
            const int index = j * blockSize + adv;
            FloatVectorOperations::copy(channelPointers[j], audioBufferOut.data() + index, numLeft);
        }
        midiBufferIn.addEvents(midiin, 0, numLeft, adv);
        midiMessages.addEvents(midiBufferOut, adv, numLeft, -adv);
        
        audioAdvancement = 0;
        processInternal(false);

        // If there are other DSP ticks that can be
        // performed, then we do it now.
        int pos = numLeft;
        while ((pos + blockSize) <= numSamples)
        {
            for (int j = 0; j < numIn; ++j)
            {
                const int index = j * blockSize;
                FloatVectorOperations::copy(audioBufferIn.data() + index, channelPointers[j] + pos, blockSize);
            }
            for (int j = 0; j < numOut; ++j)
            {
                const int index = j * blockSize;
                FloatVectorOperations::copy(channelPointers[j] + pos, audioBufferOut.data() + index, blockSize);
            }
            
            midiBufferIn.addEvents(midiin, pos, blockSize, 0);
            midiMessages.addEvents(midiBufferOut, 0, blockSize, pos);
            
            processInternal(false);
            pos += blockSize;
        }

        // If there are samples that can't be
        // processed, then save them for later
        // and outputs the remaining samples
        const int remaining = numSamples - pos;
        if (remaining > 0)
        {
            for (int j = 0; j < numIn; ++j)
            {
                const int index = j * blockSize;
                FloatVectorOperations::copy(audioBufferIn.data() + index, channelPointers[j] + pos, remaining);
            }
            for (int j = 0; j < numOut; ++j)
            {
                const int index = j * blockSize;
                FloatVectorOperations::copy(channelPointers[j] + pos, audioBufferOut.data() + index, remaining);
            }

            midiBufferIn.addEvents(midiin, pos, remaining, 0);
            midiMessages.addEvents(midiBufferOut, 0, remaining, pos);
            
            audioAdvancement = remaining;
        }
    }
    
    processStatusbar(buffer, midiBufferIn, midiBufferOut);
    
    audioExchanger.sendAudioBuffer(buffer);
    audioExchanger.notifyOutput();

}

void PureData::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if(audioExchanger.ipcMode)  {
        bufferToFill.buffer->clear();
        return;
    }
    
    audioExchanger.sendAudioBuffer(*bufferToFill.buffer);
    audioExchanger.notifyInput();
    audioExchanger.waitForOutput();
    
    if(audioExchanger.ipcMode) {
        audioExchanger.receiveAudioBuffer(*bufferToFill.buffer);
    }
    else {
        auto block = audioExchanger.receiveAudioBuffer();
        block.copyTo(*bufferToFill.buffer);
    }

}

void PureData::releaseResources()
{
    // This will be called when the audio device stops, or when it is being
    // restarted due to a setting change.

    // For more details, see the help for AudioProcessor::releaseResources()
}

void PureData::setAudioChannels(int numInputChannels, int numOutputChannels, const XmlElement* const xml)
{
    String audioError;

    audioError = deviceManager.initialise (numInputChannels, numOutputChannels, xml, true);

    jassert (audioError.isEmpty());

    deviceManager.addAudioCallback (&audioSourcePlayer);
    audioSourcePlayer.setSource (this);
}

void PureData::shutdownAudio()
{
    audioSourcePlayer.setSource (nullptr);
    deviceManager.removeAudioCallback (&audioSourcePlayer);
}

void PureData::receiveMessages()
{
    MemoryBlock target;
    while(messageHandler.receiveMessages(target)) {
        MemoryInputStream istream(target, false);
        auto type = static_cast<MessageHandler::MessageType>(istream.readInt());
        
        if(type == MessageHandler::tPatch)
        {
            auto ID = istream.readString();
            auto* patch = getPatchByID(ID);
            
            
            MemoryBlock message;
            istream.readIntoMemoryBlock(message);
            
            patch->receiveMessage(message);
        }
        if(type == MessageHandler::tObject)
        {
            auto patchID = istream.readString();
            auto objectID = istream.readString();
            
            MemoryBlock message;
            istream.readIntoMemoryBlock(message);
            
            auto* patch = getPatchByID(patchID);
            
            for(auto& object : patch->objects)
            {
                if(object.getID() == objectID)
                {
                    object.receiveMessage(message);
                }
            }
        }
        if(type == MessageHandler::tGlobal)
        {
            auto selector = istream.readString();
            if(selector == "Ping")
            {
                lastPingMs = Time::getCurrentTime().getMillisecondCounter();
                connectionEstablished = true;
                sendPing();
            }
            else if(selector == "Quit")
            {
                connectionEstablished = true;
                connectionLost = true;
            }
            else if(selector == "OpenPatch")
            {
                auto file = File(istream.readString());

                patches.add(new Patch(messageHandler, libpd_openfile(file.getFileName().toRawUTF8(), file.getParentDirectory().getFullPathName().toRawUTF8())));
                
                synchronise();
            }
            else if(selector == "ClosePatch")
            {
                auto canvasID = istream.readString();
                auto* patch = getPatchByID(canvasID);
                auto* ptr = patch->getPointer();
                
                patches.removeObject(patch);
                libpd_closefile(ptr);
                
                synchronise();
            }
            else if(selector == "AudioStatus")
            {
                numInputChannels = istream.readInt();
                numOutputChannels = istream.readInt();
                
                auto state = XmlDocument(istream.readString()).getDocumentElement();
                
                // this is ridiculous, but it works
                // if we don't run it on another thread, we risk deadlock
                Thread::launch([this, s = state.release()]() mutable {
                    
                    MessageManager::getInstance()->setCurrentThreadAsMessageThread();
                    
                    setAudioChannels(numInputChannels, numOutputChannels, s);
                    
                    delete s;
                    
                    for(auto& midiInput : MidiInput::getAvailableDevices()) {
                        if(deviceManager.isMidiInputDeviceEnabled(midiInput.identifier)) {
                            
                            deviceManager.addMidiInputDeviceCallback(midiInput.identifier, &midiInputCollector);
                        }
                        else {
                            deviceManager.removeMidiInputDeviceCallback(midiInput.identifier, &midiInputCollector);
                        }
                    }
                });
            }
            else if(selector == "Volume")
            {
                volume = istream.readFloat();
            }
            else if(selector == "DSP")
            {
                float state = istream.readBool();
                dspState = state;
                
                t_atom av;
                libpd_set_float(&av, state);
                libpd_message("pd", "dsp", 1, &av);
            }
            else if(selector == "SearchPaths") {
                
                libpd_clear_search_path();
                
                // Start of list
                jassert(istream.readString() == "#");
                
                std::vector<void*> objects;
                
                while(!istream.isExhausted())  {
                    auto path = istream.readString();
                    
                    // End of list
                    if(path == "#") break;
                    
                    libpd_add_to_search_path(path.toRawUTF8());
                }
                
            }
        }
    }
    
    if(connectionEstablished &&  (Time::getCurrentTime().getMillisecondCounter() - lastPingMs) > 8000) {
        connectionLost = true;
    }
}

static bool hasRealEvents(MidiBuffer& buffer)
{
    return std::any_of(buffer.begin(), buffer.end(),
    [](const auto& event){
        return !event.getMessage().isSysEx();
    });
}

void PureData::processStatusbar(const dsp::AudioBlock<float> buffer, MidiBuffer& midiIn, MidiBuffer& midiOut)
{
    auto level = std::vector<float>(2);
    bool midiSent = false;
    bool midiReceived = false;
    

    for (int ch = 0; ch < buffer.getNumChannels(); ch++)
    {
        auto* channelData = buffer.getChannelPointer(ch);
        
        // TODO: this logic for > 2 channels makes no sense!!
        auto localLevel = level[ch & 1];

        for (int n = 0; n < buffer.getNumSamples(); n++)
        {
            float s = std::abs(channelData[n]);

            const float decayFactor = 0.99992f;

            if (s > localLevel)
                localLevel = s;
            else if (localLevel > 0.001f)
                localLevel *= decayFactor;
            else
                localLevel = 0;
        }

        level[ch & 1] = localLevel;
    }

    auto now = Time::getCurrentTime();
    
    auto hasInEvents = hasRealEvents(midiIn);
    auto hasOutEvents = hasRealEvents(midiOut);

    if (!hasInEvents && (now - lastMidiIn).inMilliseconds() > 700)
    {
        midiReceived = false;
    }
    else if (hasInEvents)
    {
        midiReceived = true;
        lastMidiIn = now;
    }

    if (!hasOutEvents && (now - lastMidiOut).inMilliseconds() > 700)
    {
        midiSent = false;
    }
    else if (hasOutEvents)
    {
        midiSent = true;
        lastMidiOut = now;
    }
    
    messageHandler.sendLevelMeterStatus(level[0], level[1], midiReceived, midiSent);
}


void PureData::sendPing()
{
    MemoryOutputStream message;
    message.writeInt(MessageHandler::tGlobal);
    message.writeString("Ping");
    messageHandler.sendMessage(message.getMemoryBlock());
}

bool PureData::shouldQuit() {
    return connectionLost;
}
