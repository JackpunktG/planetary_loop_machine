#include "../../lib/mini_audio/miniaudio.h"
#include "../../lib/arena_memory/arena_memory.h"
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <linux/input.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <errno.h>
#include <termios.h>
#include <assert.h>
#include <ctype.h>
#define MIDI_INTERFACE_IMPLEMENTATION
#include "../../lib/MIDI_interface.h"

#define END_MISSION -99
#define MA_DEBUG_OUTPUT
//#define SAMPLE_FORMAT   ma_format_f32 // 32-bit float format
//#define CHANNEL_COUNT   2
//#define SAMPLE_RATE     44100
//#define BEATS_PER_BAR  4
//#define BARS_PER_LOOP  2     // most electronic music when we talk bpm its 2 beats(on 1 & 3) per bar so to loop every 4 beats its 2 bars
// looping every 8 beats would be 4 bars, 16 beats = 8 bars, etc....

typedef struct Synth Synth;
typedef struct LFO_Module LFO_Module;
/* Sound Controller and Sample */

typedef struct
{
    float* buffer;
    uint32_t length;
    uint32_t cursor;
    short nextSample;
    bool newSample;
    bool oneShot;
    uint16_t index; //index in **samples
    char name[30];
    float volume;
} Sample;

#define MAX_ACTIVE_SAMPLES 20
#define NO_ACTIVE_SAMPLE -25
#define MAX_ACTIVE_ONE_SHOT 5


typedef struct
{
    Sample** activeSamples;
    short activeIndex[MAX_ACTIVE_SAMPLES];
    Sample** oneShotActive;
    uint8_t oneShotCount;
    /* 3 byte hole */
    float bpm;
    Sample** samples;
    uint16_t sampleCount;
    uint8_t activeCount;
    uint8_t beatCount;
    uint32_t loopFrameLength; //4 beat timer for swapping samples or bring in queued samples
    uint32_t globalCursor;
    bool newQueued;
    uint8_t channelCount;
    uint8_t synthCount;
    uint8_t synthMax;
    Synth** synth;
    MIDI_Controller* midiController;
    Arena* arena;
} SoundController;

//Only vaild format is f32 thus far
//MIDI controller can be nulled to not active
SoundController* sound_controller_init(float bpm, const char* loadDirectory, uint8_t beatsPerBar, uint8_t barsPerLoop, uint16_t sampleRate, uint8_t channelCount, ma_format format, uint8_t synthMax, MIDI_Controller* midiController);
void data_callback_f32(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
void sound_controller_destroy(SoundController* sc);
void process_midi_commands(SoundController* sc);
//ran each loop to check status of one shot launched samples and reset their settings
void one_shot_check(SoundController* sc);
//generate for all attached synths
void controller_synth_generate_audio(SoundController* sc);


/* Synth */

typedef enum
{
    LFO_TYPE_PHASE_MODULATION
} LFO_Module_Type;
#define LFO_MODULE_ACTIVE (1 << 0)

typedef struct LFO_Module
{
    double phase;
    double phaseIncrement;
    float intensity; //think like the volume of the LFO effect
    float frequency;
    uint32_t FLAGS;
    LFO_Module_Type type;
    LFO_Module* nextLFO;
} LFO_Module;

typedef enum
{
    SYNTH_ACTIVE            = (1 << 0),
    SYNTH_NOTE_ON           = (1 << 1),
    SYNTH_NOTE_OFF          = (1 << 2),
    SYNTH_ATTACKING         = (1 << 3),
    SYNTH_DECAYING          = (1 << 4),
    SYNTH_WAITING_NOTE_ON   = (1 << 5) // outputting no sound, but the phase and LFO logic is still being updated
} Synth_FLAGS;

#define SYNTH_BUFFER_BEING_READ (1 << 0)
typedef enum
{
    SYNTH_TYPE_BASIC_SINEWAVE
} Synth_Type;

#define VELOCITY_WEIGHTING_NEUTRAL 64
typedef struct Synth
{
    float* buffer;
    uint32_t cursor;
    uint32_t bufferMax;
    double phase;
    double phaseIncrement;
    float volume;
    float frequency;
    float decayTime;
    float decay_rate;
    float attackTime;
    float attack_rate;
    float adjustment_rate; //will be used when attacking or decaying
    uint16_t sampleRate;
    char name[14];
    Synth_Type type;
    uint8_t audio_thread_flags;
    uint8_t velocity; // used for midi input, (0 - 127) At VELOCITY_WEIGHTING_NEUTRAL will be the attackTime set, higher or lower will just accordingly
    uint32_t FLAGS;
    LFO_Module* lfo;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Synth;

//Name can be 12 characters long
Synth* synth_init(SoundController* sc, const char* name, Synth_Type type, uint16_t sampleRate, float frequency, float attackTime, float decayTime, uint32_t FLAGS);
//if you want to generate some sound before starting the callback
void synth_generate_audio(Synth* synth);
void synth_print_out(SoundController* sc);
// best to send in bpm_to_hert(bpm) to the frequency parameter
void LFO_attach(SoundController* sc, Synth* synth, LFO_Module_Type type, float intensity, float frequency, uint32_t FLAGS);

// bpm to hertz converter function
float bpm_to_hz(float bpm);


/* Input Controller and UI */

// ANSI color codes
#define RESET   "\033[0m"
#define BLACK   "\033[30m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
// Bold colors
#define BOLD_BLACK   "\033[1;30m"
#define BOLD_RED     "\033[1;31m"
#define BOLD_GREEN   "\033[1;32m"
#define BOLD_YELLOW  "\033[1;33m"
#define BOLD_BLUE    "\033[1;34m"
#define BOLD_MAGENTA "\033[1;35m"
#define BOLD_CYAN    "\033[1;36m"
#define BOLD_WHITE   "\033[1;37m"

#define MAX_KEY_POLL 7
#define MAX_COMMAND_LENGTH 63

#define MAX_SLIDERS 8

typedef struct
{
    bool heldKeys[256];
    bool keys[256];
    uint8_t keysEventPoll[MAX_KEY_POLL];
    uint8_t pollIndex;
    char command[MAX_COMMAND_LENGTH];
    uint8_t commandIndex;
    int inputFile;
    uint8_t sliderCount;
    /* 3 byte hole */
    struct
    {
        uint8_t channel;
        bool active;
        uint16_t index;
        float targetVolume;
        uint16_t framesLeft;
    } slider[MAX_SLIDERS];
} InputController;

// passing in the pointer to allow stac allocation
int input_controller_init(InputController* ic, uint32_t inputDeviceIndex);
void input_controller_destroy(InputController* ic);

// the three fuctions called in main loop
void poll_keyboard(InputController* ic);
int input_process(InputController* ic, SoundController* s);
void slider_update(InputController* ic, SoundController* sc);
