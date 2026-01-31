#include "planetary_loop_machine/planetary_loop_machine.h"

#define SAMPLE_RATE     44100
#define CHANNEL_COUNT   2
#define SAMPLE_FORMAT   ma_format_f32


void sanity_checks(SoundController* sc, InputController* ic)
{
    // check active count matches actual active samples
    uint8_t activeSampleCount = 0;
    for (uint32_t i = 0; i < MAX_ACTIVE_SAMPLES; ++i)
        if (sc->activeSamples[i] != NULL)
            ++activeSampleCount;
    if (activeSampleCount != sc->activeCount)
        assert(false && "ERROR: active samples count mismatch");

    // check active index points to valid samples
    for (uint32_t i = 0; i < sc->activeCount; ++i)
        if (sc->activeSamples[sc->activeIndex[i]] == NULL)
            assert(false && "ERROR: active sample index points to NULL sample");

    // Checking rest of active index pointing to nothing
    for (uint32_t i = sc->activeCount; i < MAX_ACTIVE_SAMPLES; i++)
        assert(sc->activeIndex[i] == NO_ACTIVE_SAMPLE && "ERROR: non-active sampleIndex doesn't point to NO_ACTIVE_SAMPLE");

}


int main(int argc, char** argv)
{
    MIDI_Controller midiController;
    midi_controller_set(&midiController, "src/audio_data/midi_commands_test.midi");
    // fining the context of the connected audio-interfaces
    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
        return -1;

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info* pCaptureInfos;
    uint32_t captureCount;
    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS)
        return -2;

    for (uint32_t i = 0; i < playbackCount; i += 1)
    {
        printf("%d - %s %u Channel: %u format: %d\n", i, pPlaybackInfos[i].name,
               pPlaybackInfos[i].nativeDataFormats->sampleRate, pPlaybackInfos[i].nativeDataFormats->channels, pPlaybackInfos[i].nativeDataFormats->format);
    }
    /* for future to pick output device
    printf("Connect device to: ");
    uint32_t indexDecoder = 0;
    scanf("%u", &indexDecoder);
    if (indexDecoder >= playbackCount)
    {
        printf("Invalid device index.\n");
        return -3;
    }
    */
    InputController ic = {0};
    int i = input_controller_init(&ic, 16);
    printf("%d\n", i);
    SoundController* s = sound_controller_init(122, "src/audio_data/song_1/", 4, 2, SAMPLE_RATE, CHANNEL_COUNT, SAMPLE_FORMAT, 3, &midiController);
    Synth* synth1 = synth_init(s, "synth1", SYNTH_TYPE_BASIC_SINEWAVE, SAMPLE_RATE, 440, 0.5f, 1.0f, SYNTH_ACTIVE);
    //Synth* synth2 = synth_init(s, "synth2", SYNTH_TYPE_BASIC_SINEWAVE, SAMPLE_RATE, 2990, SYNTH_ACTIVE);
    //LFO_attach(s, synth2, LFO_TYPE_PHASE_MODULATION, 0.02, bpm_to_hz((float)122/2), LFO_MODULE_ACTIVE);
    //LFO_attach(s, synth2, LFO_TYPE_PHASE_MODULATION, 0.02, bpm_to_hz((float)122/4), LFO_MODULE_ACTIVE);
    //Synth* synth3 = synth_init(s, "synth3", SYNTH_TYPE_BASIC_SINEWAVE, SAMPLE_RATE, 880, SYNTH_ACTIVE);
    //LFO_attach(s, synth3, LFO_TYPE_PHASE_MODULATION, 0.02, bpm_to_hz((float)122/2), LFO_MODULE_ACTIVE);
    //LFO_attach(s, synth2, LFO_TYPE_PHASE_MODULATION, 0.02, bpm_to_hz((float)122/8), LFO_MODULE_ACTIVE);
    //LFO_attach(s, synth1, LFO_TYPE_PHASE_MODULATION, 0.02, bpm_to_hz((float)122/2), LFO_MODULE_ACTIVE);
    synth_print_out(s);

    s->activeCount = 0;

    /*
    FILE* file = fopen("sample_val.cvs", "a");
    for (int i = 0; i < s->samples[1]->length; ++i)
    {
        for (int j = -1; j < s->sampleCount; ++j)
        {
            fprintf(file, "%f,", (j == -1) ? s->synth->buffer[i] : s->samples[j]->buffer[i]);
        }
        fprintf(file, "\n");
    }
    fclose(file);
    sound_controller_destroy(s);
    input_controller_destroy(&ic);
    return 0;
    */

    ma_result result;
    ma_device device;
    ma_device_config deviceConfig;

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.pDeviceID = &pPlaybackInfos[0].id;
    deviceConfig.playback.format   = SAMPLE_FORMAT;
    deviceConfig.playback.channels = CHANNEL_COUNT;
    deviceConfig.sampleRate        = SAMPLE_RATE;
    deviceConfig.dataCallback      = data_callback_f32;
    deviceConfig.pUserData         = s;

    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS)
    {
        printf("Failed to initialize audio device\n");
        return -1;
    }
    if (ma_device_start(&device) != MA_SUCCESS)
    {
        ma_device_uninit(&device);

        printf("Failed to start playback device.\n");
        return -4;
    }

    s->activeIndex[s->activeCount++] = 1;
    s->activeSamples[1] = s->samples[1];
    s->newQueued = true;
    printf("Sample index 1 has been queued up to channel 1\n");

    s->activeIndex[s->activeCount++] = 2;
    s->activeSamples[2] = s->samples[2];
    s->newQueued = true;
    printf("Sample index 2 has been queued up to channel 2\n");

    s->activeIndex[s->activeCount++] = 3;
    s->activeSamples[3] = s->samples[3];
    s->newQueued = true;
    printf("Sample index 3 has been queued up to channel 3\n");

    s->activeIndex[s->activeCount++] = 0;
    s->activeSamples[0] = s->samples[0];
    s->newQueued = true;
    printf("Sample index 0 has been queued up to channel 0\n");

    bool running = true;
    while (running)
    {
        process_midi_commands(s);
        controller_synth_generate_audio(s);
        poll_keyboard(&ic);

        if (input_process(&ic, s) == END_MISSION)
            running = false;

        slider_update(&ic, s);
        one_shot_check(s);

        sanity_checks(s, &ic);



        usleep(16666);
    }

    ma_device_uninit(&device);
    ma_context_uninit(&context);

    sound_controller_destroy(s);
    input_controller_destroy(&ic);

    return 0;
}

