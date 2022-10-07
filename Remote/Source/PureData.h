#pragma once

#include <JuceHeader.h>

#include <z_libpd.h>
#include <x_libpd_mod_utils.h>
#include <x_libpd_extra_utils.h>
#include <m_imp.h>

#include "MessageHandler.h"
#include "Patch.h"

class Atom {
public:
    // The default constructor.
    inline Atom()
        : type(FLOAT)
        , value(0)
        , symbol()
    {
    }

    // The float constructor.
    inline Atom(float const val)
        : type(FLOAT)
        , value(val)
        , symbol()
    {
    }

    // The string constructor.
    inline Atom(String sym)
        : type(SYMBOL)
        , value(0)
        , symbol(std::move(sym))
    {
    }

    // The c-string constructor.
    inline Atom(char const* sym)
        : type(SYMBOL)
        , value(0)
        , symbol(String::fromUTF8(sym))
    {
    }

    // Check if the atom is a float.
    inline bool isFloat() const
    {
        return type == FLOAT;
    }

    // Check if the atom is a string.
    inline bool isSymbol() const
    {
        return type == SYMBOL;
    }

    // Get the float value.
    inline float getFloat() const
    {
        return value;
    }

    // Get the string.
    inline String const& getSymbol() const
    {
        return symbol;
    }

    // Compare two atoms.
    inline bool operator==(Atom const& other) const
    {
        if (type == SYMBOL) {
            return other.type == SYMBOL && symbol == other.symbol;
        } else {
            return other.type == FLOAT && value == other.value;
        }
    }

private:
    enum Type {
        FLOAT,
        SYMBOL
    };
    Type type = FLOAT;
    float value = 0;
    String symbol;
};

    
class PureData  : public juce::AudioSource
{
    struct Message {
        String selector;
        String destination;
        std::vector<Atom> list;
    };

    struct dmessage {
        void* object;
        String destination;
        String selector;
        std::vector<Atom> list;
    };

    typedef struct midievent {
        enum {
            NOTEON,
            CONTROLCHANGE,
            PROGRAMCHANGE,
            PITCHBEND,
            AFTERTOUCH,
            POLYAFTERTOUCH,
            MIDIBYTE
        } type;
        int midi1;
        int midi2;
        int midi3;
    } midievent;
    
public:
    //==============================================================================
    PureData(String ID);
    ~PureData() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    
    void processInternal(bool without_dsp);
    
    void waitForNextBlock();
    
    Patch* getPatchByID(String ID);
    
    void synchronise();

    void shutdownAudio();
    void setAudioChannels(int numInputChannels, int numOutputChannels, const XmlElement* const xml = nullptr);
    
    void receiveMessages();
    
    void processPrint(const char* s);
    void processMessage(Message mess);
    void processMidiEvent(midievent event);
    void processSend(dmessage mess);
    
    void receiveNoteOn(const int channel, const int pitch, const int velocity);
    void receiveControlChange(const int channel, const int controller, const int value);
    void receiveProgramChange(const int channel, const int value);
    void receivePitchBend(const int channel, const int value);
    void receiveAftertouch(const int channel, const int value);
    void receivePolyAftertouch(const int channel, const int pitch, const int value);
    void receiveMidiByte(const int port, const int byte);
    void receiveDSPState(bool dsp);
    
    void sendMidiBuffer();
    void sendNoteOn(int const channel, int const pitch, int const velocity) const;
    void sendControlChange(int const channel, int const controller, int const value) const;
    void sendProgramChange(int const channel, int const value) const;
    void sendPitchBend(int const channel, int const value) const;
    void sendAfterTouch(int const channel, int const value) const;
    void sendPolyAfterTouch(int const channel, int const pitch, int const value) const;
    void sendSysEx(int const port, int const byte) const;
    void sendSysRealTime(int const port, int const byte) const;
    void sendMidiByte(int const port, int const byte) const;
    
    void processStatusbar(const AudioBuffer<float>& buffer, MidiBuffer& midiIn, MidiBuffer& midiOut);
    
    void sendPing();
    
    bool shouldQuit();
    
private:
    
    void* m_instance = nullptr;
    void* m_patch = nullptr;
    void* m_atoms = nullptr;
    void* m_message_receiver = nullptr;
    void* m_parameter_receiver = nullptr;
    void* m_parameter_change_receiver = nullptr;
    void* m_midi_receiver = nullptr;
    void* m_print_receiver = nullptr;
    
    AudioDeviceManager deviceManager;
    AudioSourcePlayer audioSourcePlayer;
    
    int audioAdvancement = 0;
    std::vector<float> audioBufferIn;
    std::vector<float> audioBufferOut;
    
    std::vector<float*> channelPointers;

    MidiBuffer midiBufferIn;
    MidiBuffer midiBufferOut;
    MidiBuffer midiBufferTemp;
    MidiBuffer midiBufferCopy;
    
    MidiMessageCollector midiInputCollector;
    MidiOutput* midiOutput;

    bool midiByteIsSysex = false;
    uint8 midiByteBuffer[512] = {0};
    size_t midiByteIndex = 0;

    int numInputChannels = 2;
    int numOutputChannels = 2;
    
    float volume = 1.0f;
    
    bool dspState = true;
    bool connectionLost = false;
    
    MessageHandler messageHandler;
    
    OwnedArray<Patch> patches;

    WaitableEvent audioProcessSemaphore;
    WaitableEvent audioDoneSemaphore;
    juce::AudioSourceChannelInfo sharedBuffer;
    
    Time lastMidiIn;
    Time lastMidiOut;
    
    char printConcatBuffer[2048];
    
    uint32 lastPingMs;
    bool connectionEstablished = false;

    struct internal;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PureData)
};
