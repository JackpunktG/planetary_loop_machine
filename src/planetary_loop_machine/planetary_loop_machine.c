#include "planetary_loop_machine.h"

#include "math.h"


uint32_t calculate_loop_frames(float bpm, uint32_t sample_rate, uint32_t beats_per_bar, uint32_t bars)
{
    float seconds_per_beat = 60.0f / bpm;
    float frames_per_beat = seconds_per_beat * sample_rate;

    return (uint32_t)(frames_per_beat * beats_per_bar * bars);
}

float bpm_to_hz(float bpm)
{
    return bpm / 120;
}

Sample* sample_F32_load(SoundController* soundController, const char* filename, uint16_t index, uint8_t beatsPerBar, uint8_t barsPerLoop, uint16_t sampleRate, uint8_t channelCount)
{
    ma_decoder decoder;
    ma_decoder_config config;
    ma_uint64 total_frame_count;

    config = ma_decoder_config_init(ma_format_f32, channelCount, sampleRate);

    if (ma_decoder_init_file(filename, &config, &decoder) != MA_SUCCESS)
    {
        printf("Failed to load file: %s\n", filename);
        return NULL;
    }

    // Get total length in frames
    ma_result result = ma_decoder_get_length_in_pcm_frames(&decoder, &total_frame_count);
    if (result != MA_SUCCESS)
    {
        printf("Failed to get length of file: %s\n", filename);
        ma_decoder_uninit(&decoder);
        return NULL;
    }

    Sample* sample = arena_alloc(soundController->arena, sizeof(Sample), NULL);
    memset(sample, 0, sizeof(Sample));

    size_t t = 0;
    sample->length = total_frame_count;
    sample->cursor = 0;
    sample->volume = 1.0f;
    sample->nextSample = -1;
    sample->newSample = true;
    sample->oneShot = false;
    sample->index = index;
    if (soundController->loopFrameLength == 0)
        soundController->loopFrameLength = calculate_loop_frames(soundController->bpm, sampleRate, beatsPerBar, barsPerLoop);
    // Allocate buffer
    sample->buffer = arena_alloc(soundController->arena, total_frame_count * channelCount * sizeof(float), &t);
    //printf("arena alloc %zu        \n", t);

    if (sample->buffer == NULL)
    {
        printf("ERROR - Failed to allocate memory\n");
        ma_decoder_uninit(&decoder);
        return NULL;
    }

    ma_uint64 frames_read = 0;
    result = ma_decoder_read_pcm_frames(&decoder, sample->buffer, total_frame_count, &frames_read);

    if (result != MA_SUCCESS || frames_read != total_frame_count)
        printf("WARNING: Only read %llu of %llu frames\n", frames_read, total_frame_count);

    ma_decoder_uninit(&decoder);

    return sample;
}


SoundController* sound_controller_init(float bpm, const char* loadDirectory, uint8_t beatsPerBar, uint8_t barsPerLoop, uint16_t sampleRate, uint8_t channelCount, ma_format format, uint8_t synthMax, MIDI_Controller* midiController)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir(loadDirectory);

    // first counting how many samples
    uint16_t sampleCount = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] != '.')
            sampleCount++;
    }
    rewinddir(dir);

    Arena* arena = arena_init(ARENA_BLOCK_SIZE, 32, false);
    SoundController* sController = arena_alloc(arena, sizeof(SoundController), NULL);
    memset(sController, 0, sizeof(SoundController));
    sController->arena = arena;
    sController->midiController = midiController == NULL ? NULL : midiController;
    sController->sampleCount = sampleCount;
    sController->bpm = bpm;
    sController->activeCount = 0;
    sController->loopFrameLength = 0;
    sController->globalCursor = 0;
    sController->beatCount = 0;
    sController->oneShotCount = 0;
    sController->channelCount = channelCount;
    sController->newQueued = false;
    for(uint32_t i = 0; i < MAX_ACTIVE_SAMPLES; ++i)
        sController->activeIndex[i] = NO_ACTIVE_SAMPLE;
    sController->activeSamples = arena_alloc(arena, sizeof(Sample*) * MAX_ACTIVE_SAMPLES, NULL);
    for(uint32_t i = 0; i < MAX_ACTIVE_SAMPLES; ++i)
        sController->activeSamples[i] = NULL;
    sController->oneShotActive = arena_alloc(arena, sizeof(Sample*) * MAX_ACTIVE_ONE_SHOT, NULL);
    for(uint32_t i = 0; i < MAX_ACTIVE_ONE_SHOT; ++i)
        sController->oneShotActive[i] = NULL;
    sController->samples = arena_alloc(arena, sizeof(Sample*) * sampleCount, NULL);

    uint16_t i = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] != '.')
        {
            char buffer[strlen(entry->d_name) + strlen(loadDirectory) + 1];
            memcpy(buffer, loadDirectory, strlen(loadDirectory));
            memcpy(buffer + strlen(loadDirectory), entry->d_name, strlen(entry->d_name) + 1);
            switch(format)
            {
            case 5:
                sController->samples[i] = sample_F32_load(sController, buffer, i, beatsPerBar, barsPerLoop, sampleRate, channelCount);
                break;
            default:
                assert(false && "given format invalid\n");
            }
            strncpy(sController->samples[i++]->name, entry->d_name, strlen(entry->d_name) -4 < 32 ? strlen(entry->d_name) -4 : 31);
        }
    }

    char formatStr[16];
    switch(format)
    {
    case 5:
        strcpy(formatStr, "32-bit float");
        break;
    default:
        assert(false && "given format invalid\n");
    }

    if (synthMax > 0)
    {
        sController->synth = arena_alloc(arena, sizeof(Synth*) * synthMax, NULL);
        sController->synthMax = synthMax;
        sController->synthCount = 0;
    }
    else
    {
        sController->synth = NULL;
        sController->synthMax = 0;
        sController->synthCount = 0;
    }

    printf(BOLD_CYAN "\nSuccessfully loading of session at %s - Sample rate: %u, Channels: %u, Format: %s, BPM: %0.2f, Beats per loop: %u (frames: %u)\n\n" RESET BOLD_MAGENTA "Memory for %u Synths\n\n"RESET BOLD_YELLOW "Samples:\n" RESET,
           loadDirectory, sampleRate, channelCount, formatStr, sController->bpm, (beatsPerBar * barsPerLoop) /2, sController->loopFrameLength, synthMax);
    for (uint32_t j = 0; j < sController->sampleCount; ++j)
        printf(YELLOW "  %s (%u Sample Count - %u length in sec)\n" RESET, sController->samples[j]->name, sController->samples[j]->length,
               sController->samples[j]->length / sampleRate);
    if (midiController != NULL)
    {
        printf(BOLD_GREEN "\nMidi Interface successfully attached. With Connection to channals:" RESET);
        for(uint8_t i = 0; i < MIDI_MAX_CHANNELS; ++i)
        {
            if (midiController->active_channels & (1<<i))
                printf(BOLD_GREEN " %u" RESET, i +1);
        }
        printf("\n\n");
    }
    closedir(dir);

    return sController;
}



void sound_controller_destroy(SoundController* sc)
{
    if (sc->midiController != NULL)
        midi_controller_destrory(sc->midiController);
    arena_destroy(sc->arena);
}

bool synth_buffer_being_read(Synth* synth);
void synth_frames_read(Synth *synth);
void data_callback_f32(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    //printf("FrameCount: %u\n", frameCount);
    SoundController* s = (SoundController*)pDevice->pUserData;
    if (s->activeCount == 0 && s->oneShotCount == 0 && s->synthCount == 0) return;

    uint8_t count = s->activeCount;
    uint8_t oneShotCount = s->oneShotCount;
    Sample* activeSamples[count + oneShotCount];
    for (uint8_t i = 0; i < count; ++i)
        activeSamples[i] = s->activeSamples[s->activeIndex[i]];
    //One shot
    for (uint8_t i = 0; i < oneShotCount; ++i)
        activeSamples[count++] = s->oneShotActive[i];


    float* pOutputF32 = (float*)pOutput;
    uint32_t pushedFrames = 0;

    uint8_t channelCount = s->channelCount;

    while(pushedFrames < frameCount * channelCount)
    {
        for(uint i = 0; i < count; ++i)
        {
            float volume = activeSamples[i]->volume;
            if (!activeSamples[i]->oneShot)
            {
                if (!s->newQueued)
                    pOutputF32[pushedFrames] += activeSamples[i]->buffer[activeSamples[i]->cursor++] * volume;
                else
                {
                    if (!activeSamples[i]->newSample)
                        pOutputF32[pushedFrames] += activeSamples[i]->buffer[activeSamples[i]->cursor++] * volume;
                    else if (s->globalCursor == 0)
                    {
                        pOutputF32[pushedFrames] += activeSamples[i]->buffer[activeSamples[i]->cursor++] * volume;
                        activeSamples[i]->newSample = false;
                    }
                }

                if(activeSamples[i]->cursor > activeSamples[i]->length)
                    activeSamples[i]->cursor = 0;
                if (activeSamples[i]->nextSample >= 0 && activeSamples[i]->cursor % s->loopFrameLength == 0)// to swap in queued sample of start of the next bar
                {
                    uint16_t index = (uint16_t)activeSamples[i]->nextSample;    // current active sample will have the nextSample set at the index of the queued sample where it appears in s->**samples
                    Sample* swap = s->samples[index];
                    s->activeSamples[swap->nextSample] = swap;      // next sample of the incomping sample is loaded with the channel
                    swap->nextSample = -1;                        // resetting next sample of the incoming sample
                    activeSamples[i] = swap;
                }
            }
            else //One Shot
            {
                if(activeSamples[i]->cursor > activeSamples[i]->length)
                    continue;

                if (!s->newQueued)
                    pOutputF32[pushedFrames] += activeSamples[i]->buffer[activeSamples[i]->cursor++] * volume;
                else
                {
                    if (!activeSamples[i]->newSample)
                        pOutputF32[pushedFrames] += activeSamples[i]->buffer[activeSamples[i]->cursor++] * volume;
                    else if (s->globalCursor == 0)
                    {
                        pOutputF32[pushedFrames] += activeSamples[i]->buffer[activeSamples[i]->cursor++] * volume;
                        activeSamples[i]->newSample = false;
                    }
                }
            }
        }

        ++pushedFrames;

        if (s->globalCursor == 0)
        {
            s->newQueued = false;       //as all the queued samples would be playing due to loop around the bar, we can turn the flag off
            s->beatCount = 1;
            printf("\r    Loop 4/4        ");
            fflush(stdout);
        }
        else if (s->globalCursor % (s->loopFrameLength /4)== 0)
        {
            printf("\r    Loop %u/%u        ", s->beatCount++, 4);
            fflush(stdout);
        }
        else
        {
            printf("\r    Loop %u/%u        ", s->beatCount, 4);
            fflush(stdout);
        }

        //for MIDI_Clock
        if (s->globalCursor % (s->loopFrameLength / (MIDI_TICKS_PER_BAR)) == 0 && s->midiController != NULL)
        {
            midi_command_clock(s->midiController);
            //printf("clock and command count: %u\n", s->midiController->command_count);
        }
        ++s->globalCursor;
        if (s->globalCursor > s->loopFrameLength)
            s->globalCursor = 0;
    }
    // Synth audio pushing
    if (s->synthCount > 0)
    {
        for (uint8_t i = 0; i < s->synthCount; ++i)
        {
            Synth* synth = s->synth[i];
            if(!synth_buffer_being_read(synth))
                continue;
            float volume = synth->volume;
            pushedFrames = 0;

            while(pushedFrames < frameCount * channelCount)
            {
                pOutputF32[pushedFrames++] += synth->buffer[synth->cursor++] * volume;
                if (synth->cursor > synth->bufferMax)
                {
                    synth->cursor = 0;
                    printf("WARNING - synth[%u] cursor wrapped around\n", i);
                }
            }
            synth_frames_read(synth);
        }
    }

    (void)pDevice;
    (void)pOutput;
}


struct termios orig_termios;
//automaticcally called on program exit to restore terminal settings on exit
void disable_raw_mode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode()
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);  // Restore on exit

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // Disable echo and canonical mode
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void command_reset(InputController* ic)
{
    ic->commandIndex = 0;
    for (uint32_t i = 0; i < MAX_COMMAND_LENGTH; ++i)
        ic->command[i] = '\0';
}

int input_controller_init(InputController* ic, uint32_t inputDeviceIndex)
{
    //disabling text stream to terminal
    enable_raw_mode();
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "/dev/input/event%u", inputDeviceIndex);
    printf("%s\n", buffer);
    ic->inputFile = open(buffer, O_RDONLY | O_NONBLOCK);
    if (ic->inputFile == -1)
    {
        perror("Cannot open input device");
        return -1;
    }

    command_reset(ic);
    return 0;
}
void input_controller_destroy(InputController* ic)
{
    close(ic->inputFile);
}

void poll_keyboard(InputController* ic)
{
    struct input_event ev;
    memset (ic->keys, 0, sizeof(ic->keys));

    // Drain all available events
    while (1)
    {
        ssize_t bytes = read(ic->inputFile, &ev, sizeof(ev));

        if (bytes == sizeof(ev))
        {
            // Successfully read an event
            if (ev.type == EV_KEY && ev.code < 256)
            {
                if (ev.value == 1 && !ic->heldKeys[ev.code]) // Key press
                {
                    ic->keys[ev.code] = true;
                    ic->heldKeys[ev.code] = true;
                    if (ic->pollIndex < MAX_KEY_POLL)
                        ic->keysEventPoll[ic->pollIndex++] = ev.code;
                }
                else if (ev.value == 0 && ic->heldKeys[ev.code]) // Key release
                    ic->heldKeys[ev.code] = false;
            }
        }
        else if (bytes == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            else
            {
                perror("read error");
                break;
            }
        }
    }
}


int command_quit(InputController* ic, SoundController* sc)
{
    bool active = false;
    for (uint32_t i = 0; i < MAX_ACTIVE_SAMPLES; ++i)
    {
        if (sc->activeSamples[sc->activeIndex[i]] != NULL)
        {
            printf(BOLD_MAGENTA "\t\tWARNING: Stopping active sample on Channel %u (%s) before quitting\n" RESET, sc->activeIndex[i], sc->activeSamples[sc->activeIndex[i]]->name);
            active = true;
        }
    }
    if (active)
        return 0;
    else
        return END_MISSION;
}

void command_kill_all(SoundController* sc)
{
    if (sc->activeCount == 0)
    {
        for (uint32_t i = 0; i < MAX_ACTIVE_SAMPLES; ++i)
            if (sc->activeSamples[i] != NULL)
                assert(false && "ERROR: active samples count is 0 but active index not cleared");
        printf(MAGENTA "\t\tCurrently no active samples\n" RESET);
        return;
    }

    for (uint32_t i = 0; i < MAX_ACTIVE_SAMPLES; ++i)
    {
        if (sc->activeSamples[i] != NULL)
        {
            printf(CYAN"\t\tKilling active sample on Channel %u (%s)\n" RESET, i, sc->activeSamples[i]->name);
            sc->activeSamples[i] = NULL;
        }

        sc->activeIndex[i] = NO_ACTIVE_SAMPLE;
    }
    printf(BOLD_CYAN "\t\tAll active samples killed\n" RESET);
    sc->activeCount = 0;
}

void active_channel_kill(SoundController* sc, uint8_t channel)
{
    if (sc->activeSamples[channel] == NULL)
    {
        printf(MAGENTA "\t\tWARNING: Channel %u already inactive\n" RESET, channel);
        return;
    }

    bool found = false;
    for (uint16_t i = 0; i < sc->activeCount; i++)
    {
        if (sc->activeIndex[i] == channel)
        {
            found = true;
            printf(BOLD_CYAN "\t\tKilling active sample on Channel %u (%s)\n" RESET, channel, sc->activeSamples[channel]->name);
            sc->activeSamples[channel] = NULL;
            for (uint16_t j = i; j < sc->sampleCount; ++j)
                sc->activeIndex[j] = sc->activeIndex[j+1];
            break;
        }
    }
    assert(found && "ERROR: active sample to kill not found in active index");
    sc->activeIndex[sc->activeCount--] = NO_ACTIVE_SAMPLE;
}

void command_kill(InputController* ic, SoundController* sc)
{
    char buffer[MAX_COMMAND_LENGTH -1];
    strcpy(buffer, ic->command +1);
    if (strlen(buffer) == 0 || strlen(buffer) > 2)
    {
        printf(MAGENTA "\t\tWARNING: Invalid channel\n" RESET);
        return;
    }

    if (strlen(buffer) == 2 && (!isdigit(buffer[0]) || !isdigit(buffer[1]) || buffer[0] == '0' ))
    {
        printf(MAGENTA "\t\tWARNING: Invalid channel\n" RESET);
        return;
    }
    else if (strlen(buffer) == 1 &&!isdigit(buffer[0]))
    {
        printf(MAGENTA "\t\tWARNING: Invalid channel\n" RESET);
        return;
    }

    int channel = atoi(buffer);
    printf("buffer: %s, channel: %d", buffer, channel);
    if (channel > 19 || channel < 0)
    {
        printf(MAGENTA "\t\tWARNING: %d is not a vaild channel\n" RESET, channel);
        return;
    }

    switch (channel)
    {
    case 0:
        active_channel_kill(sc, channel);
        break;
    case 1:
        active_channel_kill(sc, channel);
        break;
    case 2:
        active_channel_kill(sc, channel);
        break;
    case 3:
        active_channel_kill(sc, channel);
        break;
    case 4:
        active_channel_kill(sc, channel);
        break;
    case 5:
        active_channel_kill(sc, channel);
        break;
    case 6:
        active_channel_kill(sc, channel);
        break;
    case 7:
        active_channel_kill(sc, channel);
        break;
    case 8:
        active_channel_kill(sc, channel);
        break;
    case 9:
        active_channel_kill(sc, channel);
        break;
    case 10:
        active_channel_kill(sc, channel);
        break;
    case 11:
        active_channel_kill(sc, channel);
        break;
    case 12:
        active_channel_kill(sc, channel);
        break;
    case 13:
        active_channel_kill(sc, channel);
        break;
    case 14:
        active_channel_kill(sc, channel);
        break;
    case 15:
        active_channel_kill(sc, channel);
        break;
    case 16:
        active_channel_kill(sc, channel);
        break;
    case 17:
        active_channel_kill(sc, channel);
        break;
    case 18:
        active_channel_kill(sc, channel);
        break;
    case 19:
        active_channel_kill(sc, channel);
        break;
    default:
        assert(false && "ERROR: invalid channel in command_kill");
        break;
    }


}

void print_synth_lfo_info(Synth* synth);
void command_list(InputController* ic, SoundController* sc)
{
    if (strcmp(ic->command, "la") == 0)
    {
        for(uint8_t i = 0; i < MAX_ACTIVE_SAMPLES; ++i)
            if (sc->activeSamples[i] != NULL)
                printf(BOLD_GREEN "\t\tChannel: %u - %s, volume: %0.2f\n" RESET, i, sc->activeSamples[i]->name, sc->activeSamples[i]->volume);
        for(uint8_t i = 0; i < sc->oneShotCount; ++i)
            printf(GREEN "\t\tOne Shot: %u - %s, volume: %0.2f\n" RESET, i, sc->activeSamples[i]->name, sc->activeSamples[i]->volume);
    }
    else if (strcmp(ic->command, "ls") == 0)
    {
        for (uint16_t i = 0; i < sc->sampleCount; ++i)
        {
            bool active = false;
            bool oneShot = false;
            uint8_t channel = 0;
            for (uint8_t j = 0; j < MAX_ACTIVE_SAMPLES; ++j)
            {
                if (sc->samples[i] == sc->activeSamples[j])
                {
                    active = true;
                    channel = j;
                    break;
                }
            }
            for (uint8_t j = 0; j < sc->oneShotCount; ++j)
            {
                if (sc->samples[i] == sc->oneShotActive[j])
                {
                    oneShot = true;
                    break;
                }
            }

            if (active)
                printf(BOLD_GREEN "\t\tChannel: %u %s (SampleID %u)\n" RESET, channel, sc->samples[i]->name, i);
            else if (oneShot)
                printf(GREEN "\t\tOne Shot active: %s (SampleID %u)\n" RESET, sc->samples[i]->name, i);
            else
                printf(BOLD_YELLOW "\t\tSampleID: %u - %s\n" RESET, i, sc->samples[i]->name);
        }
    }
    else if (strcmp(ic->command, "li") == 0)
    {
        for (uint16_t i = 0; i < sc->sampleCount; ++i)
        {
            bool active = false;
            bool oneShot = false;
            uint8_t channel = 0;
            for (uint8_t j = 0; j < MAX_ACTIVE_SAMPLES; ++j)
            {
                if (sc->samples[i] == sc->activeSamples[j])
                {
                    active = true;
                    channel = j;
                    break;
                }
            }
            for (uint8_t j = 0; j < sc->oneShotCount; ++j)
            {
                if (sc->samples[i] == sc->oneShotActive[j])
                {
                    oneShot = true;
                    break;
                }
            }

            if (!active && !oneShot)
                printf(BOLD_YELLOW "\t\tSampleID: %u - %s\n" RESET, i, sc->samples[i]->name);
        }
    }
    else if (strcmp(ic->command, "ly_SYNTH_ONLY") == 0)
    {
        if (sc->synthCount > 0)
        {
            for (uint8_t i = 0; i < sc->synthCount; ++i)
            {
                if (sc->synth[i]->FLAGS & SYNTH_ACTIVE)
                    printf(BOLD_GREEN "\t\tSynth: %s channel:%d, Frequency: %0.f2, Volume: %0.2f\n" RESET, sc->synth[i]->name, i +1, sc->synth[i]->frequency, sc->synth[i]->volume);
                else
                    printf(BOLD_YELLOW "\t\tSynth: %s channel:%d, Frequency: %0.f2, Volume: %0.2f\n" RESET, sc->synth[i]->name, i +1, sc->synth[i]->frequency, sc->synth[i]->volume);
            }
        }
        else
            printf(MAGENTA "\t\tNo Synths attached\n" RESET);
    }
    else if (strcmp(ic->command, "ly") == 0)
    {
        if (sc->synthCount > 0)
        {
            for (uint8_t i = 0; i < sc->synthCount; ++i)
            {
                if (sc->synth[i]->FLAGS & SYNTH_ACTIVE)
                {
                    printf(BOLD_GREEN "\t\tSynth: %s channel:%d, Frequency: %0.f2, Volume: %0.2f\n" RESET, sc->synth[i]->name, i +1, sc->synth[i]->frequency, sc->synth[i]->volume);
                    print_synth_lfo_info(sc->synth[i]);
                }
                else
                {
                    printf(BOLD_YELLOW "\t\tSynth: %s channel:%d, Frequency: %0.f2, Volume: %0.2f\n" RESET, sc->synth[i]->name, i +1, sc->synth[i]->frequency, sc->synth[i]->volume);
                    print_synth_lfo_info(sc->synth[i]);
                }
            }
        }
        else
            printf(MAGENTA "\t\tNo Synths attached\n" RESET);
    }
    else
        printf(MAGENTA "\t\tInvaild list command ('la' - active | 'li' - inactive | 'ls' - all samples | 'ly' - Synths)\n" RESET);
}

void parse_sample_to_channel(const char* command, uint16_t* sampleIndex, uint8_t* channel)
{
    char indexStr[] = {'\0', '\0', '\0'};
    char channelStr[] = {'\0', '\0', '\0'};
    uint8_t j = 0;
    for (uint8_t i = 1; i < strlen(command); ++i)
    {
        if (isdigit(command[i]))
        {
            if (j < sizeof(indexStr) -1)
                indexStr[j++] = command[i];
            else
            {
                printf(MAGENTA "\t\tWARNING: Sample index too long. Command: %s\n" RESET, command);
                *sampleIndex = 65535;
                *channel = 255;
                return;
            }
        }
        else if(command[i] == 'c')
        {
            j = 0;
            for (uint8_t k = i +1; k < strlen(command); ++k)
                if(isdigit(command[k]))
                {
                    if (j < sizeof(channelStr) -1)
                        channelStr[j++] = command[k];
                    else
                    {
                        printf(MAGENTA "\t\tWARNING: Channel value too long. Command: %s\n" RESET, command);
                        *sampleIndex = 65535;
                        *channel = 255;
                        return;
                    }
                }
            break;
        }
    }
    if (strlen(indexStr) == 0 || strlen(channelStr) == 0)
    {
        printf(MAGENTA "\t\tWARNING: Parsing of launch samples failed. Command: %s\n" RESET, command);
        *sampleIndex = 65535;
        *channel = 255;
        return;
    }
    *sampleIndex = atoi(indexStr);
    *channel = atoi(channelStr);
}

#define LAUNCH_OPTION_MUTE 'm'
#define LAUNCH_OPTION_FADE 'f'
#define LAUNCH_FADE_IN_TIME 12 // in sec

void lauch_fade_in(InputController* ic, uint16_t index, uint8_t channel, uint8_t time)
{
    if(ic->sliderCount >= MAX_SLIDERS)
    {
        printf(MAGENTA "\t\tWARNING: Maximum number of volume sliders reached. Command: %s\n" RESET, ic->command);
        return;
    }
    ic->slider[ic->sliderCount].channel = channel;
    ic->slider[ic->sliderCount].targetVolume = 0.95f;
    ic->slider[ic->sliderCount].framesLeft = time * 60; // assuming 60 frames per second
    ic->slider[ic->sliderCount].active = false;
    ic->slider[ic->sliderCount].index = index;
    ic->sliderCount++;

}

void command_one_shot(InputController* ic, SoundController* sc)
{
    // o38;
    // o<sample index>;
    if (sc->oneShotCount >= MAX_ACTIVE_ONE_SHOT)
    {
        printf(MAGENTA "\t\tWARNING: Maximum of shot shot samples currently active\n" RESET);
        return;
    }

    for (uint32_t i = 1; i < strlen(ic->command); ++i)
        if (!isdigit(ic->command[i]))
        {
            printf(MAGENTA "\t\tWARNING: Parsing of oneshot command found unvaild sample index. Command: %s\n" RESET, ic->command);
            return;
        }
    char tmp[strlen(ic->command)];
    strcpy(tmp, ic->command +1);
    uint16_t sampleI = atoi(tmp);
    printf("[DEBUG] tmp: %s, sampleI: %u\n", tmp,  sampleI);

    if (sc->sampleCount <=  sampleI)
    {
        printf(MAGENTA "\t\tWARNING: Sample Index out of range %u\n" RESET, sampleI);
        return;
    }

    for (uint8_t i = 0; i < sc->activeCount; ++i)
    {
        if (sc->samples[sampleI] == sc->activeSamples[i])
        {
            printf(MAGENTA "\t\tWARNING: One shot launch aborted - Sample %s already active\n" RESET, sc->samples[sampleI]->name);
            return;
        }
    }

    Sample* sample = sc->samples[sampleI];
    sample->cursor = 0;
    sample->nextSample = -1;
    sample->oneShot = true;
    sample->volume = 1;
    sample->newSample = true;

    sc->oneShotActive[sc->oneShotCount++] = sample;
    sc->newQueued = true;
    printf(BOLD_GREEN "\t\tSample %s engaged for one shot\n" RESET, sample->name);
}

void command_sample_launch(InputController* ic, SoundController* sc)
{
    // l38c2m;
    // l<sample index>c<channel><option>;
    uint16_t sampleI = 0;
    uint8_t channel = 0;
    char option = ic->command[strlen(ic->command) -1];

    parse_sample_to_channel(ic->command, &sampleI, &channel);
    //printf("sample %u, channel %u\n", sampleI, channel);
    if (sampleI >= sc->sampleCount || channel >= MAX_ACTIVE_SAMPLES)
    {
        printf(MAGENTA "\t\tWARNING: Parsing of launch samples failed. Command: %s\n" RESET, ic->command);
        return;
    }

    for (uint8_t i = 0; i < sc->activeCount; ++i)
    {
        if (sc->samples[sampleI] == sc->activeSamples[i])
        {
            printf(MAGENTA "\t\tWARNING: Sample launch aborted - Sample %s already active\n" RESET, sc->samples[sampleI]->name);
            return;
        }
    }

    Sample* sample = sc->samples[sampleI];
    sample->cursor = 0;
    sample->nextSample = -1;
    if (option == LAUNCH_OPTION_MUTE || option == LAUNCH_OPTION_FADE)
        sample->volume = 0;
    if (sc->activeSamples[channel] == NULL)
    {
        sample->newSample = true;
        sc->activeSamples[channel] = sample;
        sc->activeIndex[sc->activeCount++] = channel;
        sc->newQueued = true;
        if (option == LAUNCH_OPTION_FADE)
            lauch_fade_in(ic, sampleI, channel, LAUNCH_FADE_IN_TIME);
        printf(BOLD_GREEN "\t\tSample %s launched into channel %u\n" RESET, sample->name, channel);

    }
    else
    {
        sample->newSample = false;
        sample->nextSample = channel;
        sc->activeSamples[channel]->nextSample = sampleI;
        if (option == LAUNCH_OPTION_FADE)
            lauch_fade_in(ic, sampleI, channel, LAUNCH_FADE_IN_TIME);
        printf(BOLD_GREEN "\t\tSample %s launched will be swapped at next loop into channel %u\n" RESET, sample->name, channel);
    }
}

void command_volume_slider(InputController* ic, SoundController* sc)
{
    //vs0.75c2-3
    float volume;
    uint8_t channel;
    uint8_t time;

    int result = sscanf(ic->command, "vs%fc%hhu-%hhu", &volume, &channel, &time);
    if (result != 3)
    {
        printf(MAGENTA "\t\tWARNING: Parsing of volume slider command failed. Command: %s\n" RESET, ic->command);
        return;
    }

    if (sc->activeSamples[channel] == NULL)
    {
        printf(MAGENTA "\t\tWARNING: Channel %u is not active. Cannot set volume\n" RESET, channel);
        return;
    }
    if (volume < 0.0f || volume > 1.0f)
    {
        printf(MAGENTA "\t\tWARNING: Volume out of range (0.0 - 1.0). Command: %s\n" RESET, ic->command);
        return;
    }
    if (time == 0)
    {
        printf(MAGENTA "\t\tWARNING: Time must be greater than 0. Command: %s\n" RESET, ic->command);
        return;
    }

    if(ic->sliderCount >= MAX_SLIDERS)
    {
        printf(MAGENTA "\t\tWARNING: Maximum number of volume sliders reached. Command: %s\n" RESET, ic->command);
        return;
    }

    ic->slider[ic->sliderCount].channel = channel;
    ic->slider[ic->sliderCount].targetVolume = volume;
    ic->slider[ic->sliderCount].framesLeft = time * 60; // assuming 60 frames per second
    ic->slider[ic->sliderCount].active = true;
    ic->sliderCount++;

    printf(BOLD_GREEN "\t\tVolume slider set to %0.2f on channel %u over %u frames\n" RESET, volume, channel, time * 60);

}

void command_volume(InputController* ic, SoundController* sc)
{
    //v0.75c2

    char volumeStr[] = {'\0', '\0', '\0', '\0', '\0'};
    char channelStr[] = {'\0', '\0', '\0'};
    uint8_t j = 0;
    for (uint8_t i = 1; i < strlen(ic->command); ++i)
    {
        if (isdigit(ic->command[i]) || ic->command[i] == '.')
        {
            if (j < sizeof(volumeStr) -1)
                volumeStr[j++] = ic->command[i];
            else
            {
                printf(MAGENTA "\t\tWARNING: Volume value too long. Command: %s\n" RESET, ic->command);
                return;
            }
        }
        else if(ic->command[i] == 'c')
        {
            j = 0;
            for (uint8_t k = i +1; k < strlen(ic->command); ++k)
                if(isdigit(ic->command[k]))
                {
                    if (j < sizeof(channelStr) -1)
                        channelStr[j++] = ic->command[k];
                    else
                    {
                        printf(MAGENTA "\t\tWARNING: Channel value too long. Command: %s\n" RESET, ic->command);
                        return;
                    }
                }
            break;
        }
    }
    if (strlen(volumeStr) == 0 || strlen(channelStr) == 0)
    {
        printf(MAGENTA "\t\tWARNING: Parsing of volume command failed. Command: %s\n" RESET, ic->command);
        return;
    }
    float volume = atof(volumeStr);
    uint8_t channel = atoi(channelStr);

    if (sc->activeSamples[channel] == NULL)
    {
        printf(MAGENTA "\t\tWARNING: Channel %u is not active. Cannot set volume\n" RESET, channel);
        return;
    }
    if (volume < 0.0f || volume > 1.0f)
    {
        printf(MAGENTA "\t\tWARNING: Volume out of range (0.0 - 1.0). Command: %s\n" RESET, ic->command);
        return;
    }
    sc->activeSamples[channel]->volume = volume;
    printf(BOLD_GREEN "\t\tVolume of channel %u set to %0.2f\n" RESET, channel, volume);
}

int fire_command(InputController* ic, SoundController* sc);
void command_multi(InputController* ic, SoundController* sc)
{
    // mul;sl38c2;vs0.5c2-3;k2;
    if (strncmp(ic->command, "mul;", 4) != 0)
    {
        printf(MAGENTA "\t\tWARNING: Invalid multi command. Command: %s\n" RESET, ic->command);
        return;
    }
    // mul;cmd1;cmd2;cmd3...
    char buffer[strlen(ic->command) -4 +1];
    strcpy(buffer, ic->command +4);
    if (buffer[strlen(buffer) -1] == ';')
        buffer[strlen(buffer) -1] = '\0';
    //printf("[DEBUG] buffer: %s - before:%s\n", buffer, ic->command);

    char* commandStr = strtok(buffer, ";");
    while (commandStr != NULL)
    {
        strncpy(ic->command, commandStr, MAX_COMMAND_LENGTH -1);
        ic->commandIndex = strlen(commandStr);
        printf(BLUE "\t\tMulti Command Fired: %s\n" RESET, ic->command);
        fire_command(ic, sc);
        commandStr = strtok(NULL, ";");
    }

}

void command_synth_volume(InputController* ic, SoundController* sc)
{
    //yv0.3c2
    if ((isdigit(ic->command[2])) || ic->command[2] == '.')
    {
        float volume;
        uint32_t synthIndex;
        sscanf(ic->command, "yv%fc%u", &volume, &synthIndex);

        printf("%f %u\n", volume, synthIndex);
        if (volume >= 0 && volume <= 1)
        {
            if (synthIndex <= sc->synthCount)
            {
                sc->synth[synthIndex -1]->volume = volume;
                printf(BOLD_GREEN "\t\tVolume of Synth: %s set to %0.2f\n" RESET, sc->synth[synthIndex -1]->name, volume);
            }
            else
                printf(MAGENTA "\t\tWARNING: Synth Index out of range. Command: %s\n" RESET, ic->command);
        }
        else
            printf(MAGENTA "\t\tWARNING: Volume out of range (0.0 - 1.0). Command: %s\n" RESET, ic->command);
    }
    else
        printf(MAGENTA "\t\tWARNING: Invaild Synth Command: %s\n" RESET, ic->command);

    return;
}

void command_synth_frequence(InputController* ic, SoundController* sc)
{
    //yf440.4c3
    if (isdigit(ic->command[2]))
    {
        float frequency;
        uint32_t synthIndex;
        sscanf(ic->command, "yf%fc%u", &frequency, &synthIndex);

        printf("%f %u\n", frequency, synthIndex);

        if (frequency >= 30 && frequency <= 20000)
        {
            if (synthIndex <= sc->synthCount)
            {

                sc->synth[synthIndex -1]->frequency = frequency;
                sc->synth[synthIndex -1]->phase = 0;
                printf(BOLD_GREEN "\t\tFrequency of Synth: %s set to %0.2f\n" RESET, sc->synth[synthIndex -1]->name, frequency);
            }
            else
                printf(MAGENTA "\t\tWARNING: Synth Index out of range. Command: %s\n" RESET, ic->command);
        }
        else
            printf(MAGENTA "\t\tWARNING: Frequency out of range (30.0 - 20000.0). Command: %s\n" RESET, ic->command);
    }
    else
        printf(MAGENTA "\t\tWARNING: Invaild Synth Command: %s\n" RESET, ic->command);

    return;
}

int fire_command(InputController* ic, SoundController* sc)
{
    if(ic->commandIndex == 0)
        return 0;

    int result = 0;

    switch(ic->command[0])
    {
    case 'q':
        if (strcmp(ic->command, "quit") == 0)
            result = command_quit(ic, sc);
        break;
    case 'k':
        if (strcmp(ic->command, "killall") == 0)
            command_kill_all(sc);
        else
            command_kill(ic, sc);
        break;
    case 'l':
        if (isdigit(ic->command[1]))
            command_sample_launch(ic, sc);
        else
            command_list(ic, sc);
        break;
    case 'o':
        command_one_shot(ic, sc);
        break;
    case 'm':
        command_multi(ic, sc);
        break;
    case 'v':
        if (ic->command[1] == 's')
            command_volume_slider(ic, sc);
        else
            command_volume(ic, sc);
        break;
    case 'y':
        if (ic->command[1] == 'f')
            command_synth_frequence(ic, sc);
        else if (ic->command[1] == 'v')
            command_synth_volume(ic, sc);
        else
            printf(MAGENTA "\t\tWARNING: Invaild Synth Command: %s\n" RESET, ic->command);
        break;
    }

    command_reset(ic);
    return result;
}

void tab_info(InputController* ic, SoundController* s)
{
    uint32_t commandLength = strlen(ic->command);
    uint8_t commandIndex = ic->commandIndex;
    char command[commandLength +1];
    strcpy(command, ic->command);
    //printf("[DEBUG] command inputted: %s\n", command);


    if (command[0] == 'y')
    {
        strcpy(ic->command, "ly_SYNTH_ONLY");
        ic->commandIndex = 13;
    }
    else if (command[commandLength -1] == 'l' || command[commandLength -1] == 'o')
    {
        strcpy(ic->command, "li");
        ic->commandIndex = 2;
    }
    else
    {
        strcpy(ic->command, "la");
        ic->commandIndex = 2;
    }

    //printf("[DEBUG] new command: %s\n", ic->command);
    fire_command(ic, s);

    ic->commandIndex = commandIndex;
    strcpy(ic->command, command);
}

char get_char_from_linux_key(uint8_t value)
{
    switch(value)
    {
    case KEY_A:
        return 'a';
    case KEY_C:
        return 'c';
    case KEY_Q:
        return 'q';
    case KEY_T:
        return 't';
    case KEY_U:
        return 'u';
    case KEY_I:
        return 'i';
    case KEY_S:
        return 's';
    case KEY_O:
        return 'o';
    case KEY_F:
        return 'f';
    case KEY_P:
        return 'p';
    case KEY_L:
        return 'l';
    case KEY_K:
        return 'k';
    case KEY_V:
        return 'v';
    case KEY_Y:
        return 'y';
    case KEY_M:
        return 'm';
    case KEY_MINUS:
        return '-';
    case KEY_DOT:
        return '.';
    case KEY_SEMICOLON:
        return ';';
    case KEY_1:
        return '1';
    case KEY_D:
        return 'd';
    case KEY_2:
        return '2';
    case KEY_3:
        return '3';
    case KEY_4:
        return '4';
    case KEY_5:
        return '5';
    case KEY_6:
        return '6';
    case KEY_7:
        return '7';
    case KEY_8:
        return '8';
    case KEY_9:
        return '9';
    case KEY_0:
        return '0';

    default:
        return '\0';
    }
}

bool build_command(InputController* ic, uint8_t index)
{
    ic->command[ic->commandIndex] = get_char_from_linux_key(ic->keysEventPoll[index]);
    if(ic->command[ic->commandIndex] != '\0')
    {
        ++ic->commandIndex;
        return true;
    }
    else
        return false;

}

int input_process(InputController* ic, SoundController* s)
{
    if (ic->pollIndex == 0)
        return 0;

    int result = 0;
    for (uint8_t i = 0; i < ic->pollIndex; ++i)
    {
        switch (ic->keysEventPoll[i])
        {
        case KEY_ENTER:
            printf(BLUE "Command Fired: %s\n" RESET, ic->command);
            result = fire_command(ic, s);
            break;
        case KEY_ESC:
            if (ic->commandIndex > 0)
            {
                command_reset(ic);
                printf("Command reset\n");
            }
            break;
        case KEY_TAB:
            if (ic->commandIndex > 0)
                tab_info(ic, s);
            break;
        case KEY_BACKSPACE:
            if (ic->commandIndex > 0)
            {
                ic->command[--ic->commandIndex] = '\0';
                printf("%s\n", ic->command);
            }
            break;
        default:
            if (build_command(ic, i))
                printf("%s\n", ic->command);
            break;


        }


    }
    ic->pollIndex = 0;

    return result;
}

void slider_update(InputController* ic, SoundController* sc)
{
    if (ic->sliderCount == 0)
        return;

    for (uint8_t i = 0; i < ic->sliderCount; ++i)
    {
        uint8_t channel = ic->slider[i].channel;
        if (!ic->slider[i].active)
        {
            // Check if the sample has been swaped it
            if (sc->activeSamples[channel] == sc->samples[ic->slider[i].index])
            {
                printf(BOLD_GREEN "\t\tSlider on channel %u activated after sample swap\n" RESET, channel);
                ic->slider[i].active = true;
            }
            continue;
        }
        else if (ic->slider[i].active && sc->activeSamples[channel] == NULL)
        {
            printf(MAGENTA "\t\tWARNING: Channel %u is not active. Slider reset\n" RESET, channel);
            ic->slider[i] = ic->slider[--ic->sliderCount];
            --i;
            continue;
        }

        float targetVolume = ic->slider[i].targetVolume;
        uint16_t framesLeft = ic->slider[i].framesLeft;

        float currentVolume = sc->activeSamples[channel]->volume;
        float volumeStep = (targetVolume - currentVolume) / framesLeft;

        sc->activeSamples[channel]->volume += volumeStep;
        ic->slider[i].framesLeft--;
        //  printf("\r\t\tChannel %u volume: %0.2f (target: %0.2f)       \n", channel, sc->activeSamples[channel]->volume, targetVolume);

        if (ic->slider[i].framesLeft == 0)
        {
            sc->activeSamples[channel]->volume = targetVolume;
            // Remove this slider
            ic->slider[i] = ic->slider[--ic->sliderCount];
            --i;
        }
    }
}

void one_shot_check(SoundController* sc)
{
    if(sc->oneShotCount == 0)
        return;

    for (uint8_t i = 0; i < sc->oneShotCount; ++i)
    {
        Sample* sample = sc->oneShotActive[i];
        if (sample->cursor > sample->length)
        {
            //swaping with the most recently activated one shot sample
            sc->oneShotActive[i] = sc->oneShotActive[--sc->oneShotCount];
            sample->oneShot = false;
            //printf("[DEBUG] oneshot %u is reset. Active one shots: %u\n", i, sc->oneShotCount);
        }
    }
}

/* Synth implmentation */

#define PI 3.14159265358979323846
#define TWO_PI (2.0 * PI)

void synth_audio_buffer_init(Synth* synth);
Synth* synth_init(SoundController* sc, const char* name, Synth_Type type, uint16_t sampleRate, float frequency, float attackTime, float decayTime, uint32_t FLAGS)
{
    Synth* synth = arena_alloc(sc->arena, sizeof(Synth), NULL);
    memset(synth, 0, sizeof(Synth));
    synth->bufferMax = sampleRate * 2; //for 1 sec of audio buffer as we take 2 channels
    strncpy(synth->name, name, 12);
    synth->sampleRate = sampleRate;
    synth->type = type;
    synth->FLAGS = FLAGS;
    synth->decayTime = decayTime;
    synth->attackTime = attackTime;
    synth->audio_thread_flags = 0;
    synth->cursor = 0;
    synth->frequency = frequency;
    synth->phase = 0.0f;
    synth->volume = 1.0f;
    synth->phaseIncrement = TWO_PI * synth->frequency / sampleRate;
    synth->lfo = NULL;

    synth->buffer = arena_alloc(sc->arena, sizeof(float) * synth->bufferMax, NULL);
    memset(synth->buffer, 0, sizeof(float) * synth->bufferMax);

    //synth_audio_buffer_init(synth);

    assert(sc->synthCount < sc->synthMax && "ERROR synth max exceeded");
    sc->synth[sc->synthCount++] = synth;

    return synth;
}

void LFO_attach(SoundController* sc, Synth* synth, LFO_Module_Type type, float intensity, float frequency, uint32_t FLAGS)
{
    LFO_Module* lfo = arena_alloc(sc->arena, sizeof(LFO_Module), NULL);
    lfo->type = type;
    lfo->phase = 0;
    lfo->intensity = intensity;
    lfo->frequency = frequency;
    lfo->phaseIncrement = TWO_PI * frequency / synth->sampleRate;
    lfo->nextLFO = NULL;
    lfo->FLAGS = FLAGS;


    if (synth->lfo == NULL)
        synth->lfo = lfo;
    else
    {
        uint8_t saftey = 0;
        LFO_Module* module = synth->lfo;
        while(saftey < 255)
        {
            if (module->nextLFO == NULL)
            {
                module->nextLFO = lfo;
                break;
            }
            else
                module = module->nextLFO;

            ++saftey;
        }
        assert(saftey != 255 && "ERROR - Next LFO Module couldn't be initalised");
    }
}

// Called by audio callback before reading buffer true if synth active false is not
bool synth_buffer_being_read(Synth* synth)
{
    pthread_mutex_lock(&synth->mutex);
    if (synth->FLAGS & SYNTH_ACTIVE)
    {
        synth->audio_thread_flags |= SYNTH_BUFFER_BEING_READ;
        pthread_mutex_unlock(&synth->mutex);
        return true;
    }
    else
    {
        pthread_mutex_unlock(&synth->mutex);
        return false;
    }
}

// Called by audio callback after reading buffer
void synth_frames_read(Synth *synth)
{
    pthread_mutex_lock(&synth->mutex);
    synth->audio_thread_flags &= ~SYNTH_BUFFER_BEING_READ;
    pthread_cond_signal(&synth->cond);
    pthread_mutex_unlock(&synth->mutex);
}

void basic_sinewave_synth_audio_generate(Synth* synth)
{
    for (uint32_t i = synth->bufferMax - synth->cursor; i < synth->bufferMax; ++i)
    {
        double toGenPhase = synth->phase; //Saving the phase to be used to generate the sound, to the certin LFO's can maniplate it in different ways

        if (synth->lfo != NULL)
        {
            LFO_Module* lfo = synth->lfo;
            uint8_t saftey = 0;
            while(saftey < 255)
            {
                if (lfo->FLAGS & LFO_MODULE_ACTIVE)
                {
                    switch (lfo->type)
                    {
                    case LFO_TYPE_PHASE_MODULATION:
                        lfo->phase += lfo->phaseIncrement;
                        synth->phase += lfo->phase * lfo->intensity;
                        break;
                    default:
                        assert(false && "ERROR - LFO type couldn't be found");
                    }

                    if(lfo->phase >= TWO_PI)
                        lfo->phase -= TWO_PI;
                }
                if (lfo->nextLFO == NULL)
                    break;
                else
                    lfo = lfo->nextLFO;

                ++saftey;
            }
            assert(saftey != 255 && "WARNING - saftey used to stop lfo loop");
        }

        if (synth->FLAGS & SYNTH_DECAYING)
        {
            synth->buffer[i] = (sin(toGenPhase) * 0.05) * synth->adjustment_rate;
            if (i + 1 < synth->bufferMax)
                synth->buffer[++i] = (sin(toGenPhase) * 0.05) * synth->adjustment_rate;
            else
                printf("WARNING - Odd number of frames generated\n");

            synth->adjustment_rate -= synth->decay_rate;
            if (synth->adjustment_rate < 0)
            {
                synth->FLAGS &= ~SYNTH_DECAYING;
                synth->FLAGS |= SYNTH_WAITING_NOTE_ON;
            }
        }
        else if (synth->FLAGS & SYNTH_WAITING_NOTE_ON)
        {
            synth->buffer[i] = 0;
            if (i + 1 < synth->bufferMax)
                synth->buffer[++i] = 0;
            else
                printf("WARNING - Odd number of frames generated\n");
        }
        else if (synth->FLAGS & SYNTH_ATTACKING)
        {
            synth->buffer[i] = (sin(toGenPhase) * 0.05) * synth->adjustment_rate; // *0.05 to get the sound down in line with other sample
            if (i + 1 < synth->bufferMax)
                synth->buffer[++i] = (sin(toGenPhase) * 0.05) * synth->adjustment_rate; // generating the same sample for both frames
            else
                printf("WARNING - Odd number of frames generated\n");

            synth->adjustment_rate += synth->attack_rate;
            if (synth->adjustment_rate > 1)
            {
                synth->FLAGS &= ~SYNTH_ATTACKING;
            }
        }
        else
        {
            synth->buffer[i] = sin(toGenPhase) * 0.05; // *0.05 to get the sound down in line with other sample
            if (i + 1 < synth->bufferMax)
                synth->buffer[++i] = sin(toGenPhase) * 0.05; // generating the same sample for both frames
            else
                printf("WARNING - Odd number of frames generated\n");
        }
        synth->phase += synth->phaseIncrement;

        //printf("before lfo: %f. phase: %f\n", synth->lfo, synth->phase);
        // Wrap phase to prevent accumulation errors - keep phase in range [0, 2π]
        if (synth->phase >= TWO_PI)
            synth->phase -= TWO_PI;
    }

}

void controller_synth_generate_audio(SoundController* sc)
{
    if (sc->synthCount == 0)
        return;

    for (uint8_t i = 0; i < sc->synthCount; ++i)
        synth_generate_audio(sc->synth[i]);
}

void synth_generate_audio(Synth* synth)
{
    assert(synth != NULL);
    if (!(synth->FLAGS & SYNTH_ACTIVE)) // Only entering synth is currently active
        return;


    pthread_mutex_lock(&synth->mutex);  // Lock before checking

    while (synth->audio_thread_flags & SYNTH_BUFFER_BEING_READ)
        pthread_cond_wait(&synth->cond, &synth->mutex); // Wait if being currently read

    if (synth->FLAGS & SYNTH_NOTE_ON)
    {
        synth->cursor = synth->bufferMax;
        synth->FLAGS &= ~(SYNTH_NOTE_ON | SYNTH_WAITING_NOTE_ON);
        synth->FLAGS |= SYNTH_ATTACKING;
        synth->attack_rate = 1.0f / (synth->attackTime * synth->sampleRate);
        synth->adjustment_rate = 0.0f;
        if (synth->attack_rate <= 0.0f)
            synth->attack_rate = 0.000001f;
        printf("attack rate: %f\n", synth->attack_rate);
        printf("adjustment rate: %f\n", synth->adjustment_rate);
    }
    else
        memmove(synth->buffer, synth->buffer + synth->cursor, sizeof(float) * (synth->bufferMax - synth->cursor));

    if (synth->FLAGS & SYNTH_NOTE_OFF)
    {
        synth->FLAGS &= ~SYNTH_NOTE_OFF;
        synth->FLAGS |= SYNTH_DECAYING;
        synth->decay_rate = 1.0f / (synth->decayTime * synth->sampleRate);
        synth->adjustment_rate = 1.0f;
        if (synth->decay_rate <= 0.0f)
            synth->decay_rate = 0.000001f;
    }

    synth->phaseIncrement = TWO_PI * synth->frequency / synth->sampleRate;
    switch (synth->type)
    {
    case SYNTH_TYPE_BASIC_SINEWAVE:
        basic_sinewave_synth_audio_generate(synth);
        break;
    default:
        assert(false && "ERROR - synth not given a correct type");
    }

    synth->cursor = 0;
    //printf("lfo: %f. phase: %f\n", synth->lfo, synth->phase);

    pthread_mutex_unlock(&synth->mutex);  // Unlock when done
}
/* Interesting LFO generation to test
synth->buffer[i] = sin(synth->phase * synth->lfo) * 0.05; // *0.05 to get the sound down in line with other sampl
synth->buffer[++i] = sin(synth->phase * synth->lfo) * 0.05; // generating the same sample for both frames
*/

const char* synth_type_to_string(Synth_Type type)
{
    switch (type)
    {
    case SYNTH_TYPE_BASIC_SINEWAVE:
        return "Basic Sinewave";
    default:
        assert(false && "ERROR - Synth type unknow during print out");
    }
}

const char* lfo_type_string(LFO_Module_Type type)
{
    switch (type)
    {
    case LFO_TYPE_PHASE_MODULATION:
        return "Phase Modulation";
    default:
        assert(false && "ERROR - Incorrect type info on LFO print out");
    }
}

void print_synth_lfo_info(Synth* synth)
{
    if (synth->lfo == NULL)
        return;

    LFO_Module* lfo = synth->lfo;
    uint8_t saftey = 0;

    while(saftey < 255)
    {
        if (lfo->FLAGS & LFO_MODULE_ACTIVE)
            printf(GREEN "\t\t\tLFO type: %s - Frequency: %0.f2, intensity: %0.2f\n" RESET, lfo_type_string(lfo->type), lfo->frequency, lfo->intensity);
        else
            printf(YELLOW "\t\t\tLFO type: %s - Frequency: %0.f2, intensity: %0.2f\n" RESET, lfo_type_string(lfo->type), lfo->frequency, lfo->intensity);

        if (lfo->nextLFO == NULL)
            break;
        else
            lfo = lfo->nextLFO;

        ++saftey;
    }
    assert(saftey != 255 && "ERROR - print out lfo loop saftey triggered");
}

void synth_print_out(SoundController* sc)
{
    if (sc->synthCount > 0)
    {
        printf("\n");
        for (uint8_t i = 0; i < sc->synthCount; ++i)
        {
            printf(BOLD_MAGENTA "\t\tSynth: %s channel:%d, Frequency: %0.f2, Volume: %0.2f\n" RESET, sc->synth[i]->name, i +1, sc->synth[i]->frequency, sc->synth[i]->volume);
            print_synth_lfo_info(sc->synth[i]);
        }
        printf("\n");
    }
    else
        printf(MAGENTA "\t\tNo Synths attached\n" RESET);
}


/* MIDI_INTERFACE implmentation */
void note_off(SoundController* sc, uint8_t channel)
{
    assert(channel < sc->synthCount);
    Synth* synth = sc->synth[channel];
    if (!(synth->FLAGS & SYNTH_ACTIVE))
    {
        printf("WARNING - Synth %u is Deactive\n", channel +1);
        return;
    }
    synth->FLAGS |= SYNTH_NOTE_OFF;
    //printf("MIDI NOTE OFF synth: %u\n", channel +1);

}

void note_on(SoundController* sc, uint8_t channel, uint8_t key, uint8_t velocity)
{
    assert(channel < sc->synthCount);
    Synth* synth = sc->synth[channel];
    if(synth->FLAGS & SYNTH_NOTE_ON)
        printf("WARNING - Synth %u is already Deactive\n", channel +1);

    synth->frequency = midi_note_to_frequence(key);
    //synth->velocity = velocity;
    synth->FLAGS |= SYNTH_NOTE_ON;
    //printf("MIDI NOTE ON synth %u\n", channel +1);

}

void process_midi_commands(SoundController* sc)
{
    if (sc->midiController->command_count == 0)
        return;

    pthread_mutex_lock(&sc->midiController->mutex);

    //printf("Commands to process: %u\n", sc->midiController->command_count);

    for(uint8_t i = sc->midiController->commands_processed; i < sc->midiController->command_count; ++i)
    {
        //? Perhaps save all the commands first and then release the lock?
        MIDI_Command command = sc->midiController->commands[i];

        uint8_t command_nibble = 0;
        uint8_t channel = 0;
        midi_command_byte_parse(command.command_byte, &command_nibble, &channel);
        switch(command_nibble)
        {
        case MIDI_SYSTEM_MESSAGE:
            if (command.command_byte == (MIDI_SYSTEM_MESSAGE | MIDI_CLOCK))
                ;
            else
                printf("WARNING - MIDI system message not recognised\n");
            break;
        case MIDI_NOTE_OFF:
            note_off(sc, channel);
            break;
        case MIDI_NOTE_ON:
            note_on(sc, channel, command.param1, command.param2);
            break;
        case MIDI_AFTERTOUCH:
        case MIDI_CONTINUOUS_CONTROLLER:
        case MIDI_PATCH_CHANGE:
        case MIDI_CHANEL_PRESSURE:
        case MIDI_PITCH_BEND:
            printf("WARNING - midi command not yet implmented\n");
            break;
        default:
            assert(false && "ERROR - unknown MIDI command\n");
        }
        ++sc->midiController->commands_processed;
    }
    pthread_mutex_unlock(&sc->midiController->mutex);
}
