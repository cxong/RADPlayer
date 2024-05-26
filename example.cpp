/*

    Example code for integrating RAD V2's player code into your productions.

*/



#define AUDIO_BLOCK_SIZE    4096
#define AUDIO_SAMPLE_RATE   44100



#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <vector>
using namespace std;

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <mmsystem.h>

#include "opal.cpp"
#define RAD_DETECT_REPEATS  1
#include "player20.cpp"
#include "validate20.cpp"



//==================================================================================================
// Globals.
//==================================================================================================
HWAVEOUT                g_AudioHandle;
WAVEHDR                 g_AudioBlock[2];
int16_t                 g_Samples[AUDIO_BLOCK_SIZE * 2 * 2];
int                     g_SampleIndex;
int                     g_BlockPlay;
int                     g_SampleCnt, g_SampleUpdate;
RADPlayer               g_Player;
Opal *                  g_Adlib;
bool                    g_Repeat;
bool                    g_Playing = false;
int                     g_TotalSecs, g_TotalMins, g_TotalHours;



//==================================================================================================
// OS pulse.  Runs the message loop.
//==================================================================================================
auto OSPulse() -> void {

    MSG msg;
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {

        if (msg.message == WM_QUIT)
            ExitProcess(0);

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}



//==================================================================================================
// Wave callback.
//==================================================================================================
auto CALLBACK WaveOutProc(HWAVEOUT handle, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2) -> void {

    // Block has finished playing?
    if (msg == WOM_DONE && g_Playing) {

        // Fill out block just finished playing
        auto d = g_Samples + g_BlockPlay * AUDIO_BLOCK_SIZE * 2;
        for (int i = 0; i < AUDIO_BLOCK_SIZE; i++) {

            g_Adlib->Sample(d, d + 1);
            d += 2;

            // Time to update player?
            g_SampleCnt++;
            if (g_SampleCnt >= g_SampleUpdate) {
                g_SampleCnt = 0;
                g_Repeat = g_Player.Update();
            }
        }

        // Re-queue it
        waveOutWrite(g_AudioHandle, &g_AudioBlock[g_BlockPlay], sizeof(WAVEHDR));
        g_BlockPlay ^= 1;
    }
}



//==================================================================================================
// Initialise audio.
//==================================================================================================
auto AudioInit(const uint8_t *tune, bool compute_total_time) -> bool {

    memset(g_Samples, 0, sizeof(g_Samples));

    g_Adlib = new Opal(AUDIO_SAMPLE_RATE);
    g_Player.Init(tune, [](void *arg, uint16_t reg, uint8_t data) {
        g_Adlib->Port(reg, data);
    }, 0);
    auto rate = g_Player.GetHertz();
    if (rate < 0)
        return false;

    // Do this before we start playing
    if (compute_total_time) {
        uint32_t total_time = g_Player.ComputeTotalTime();
        g_TotalSecs = total_time % 60;
        g_TotalMins = total_time / 60 % 60;
        g_TotalHours = total_time / 3600 % 60;
    }

    g_SampleCnt = 0;
    g_SampleUpdate = AUDIO_SAMPLE_RATE / rate;

    WAVEFORMATEX fmt = {
        WAVE_FORMAT_PCM,
        2,
        AUDIO_SAMPLE_RATE,
        AUDIO_SAMPLE_RATE * 4,
        4,
        16,
        0
    };
    if (waveOutOpen(&g_AudioHandle, WAVE_MAPPER, &fmt, (DWORD_PTR)WaveOutProc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        return false;

    // Prepare two audio blocks.  One is playing and one is queued.  When the queued one starts
    // playing we fill out the previously played block and queue it
    g_SampleIndex = 0;
    for (int i = 0; i < 2; i++) {

        auto blk = &g_AudioBlock[i];
        blk->lpData = LPSTR(g_Samples + i * AUDIO_BLOCK_SIZE * 2);
        blk->dwBufferLength = AUDIO_BLOCK_SIZE * 2 * sizeof(int16_t);
        blk->dwBytesRecorded = 0;
        blk->dwUser = 0;
        blk->dwFlags = 0;
        blk->dwLoops = 1;
        blk->lpNext = 0;
        blk->reserved = 0;

        waveOutPrepareHeader(g_AudioHandle, blk, sizeof(WAVEHDR));
    }

    // Start the two blocks playing
    g_BlockPlay = 0;
    waveOutWrite(g_AudioHandle, &g_AudioBlock[0], sizeof(WAVEHDR));
    waveOutWrite(g_AudioHandle, &g_AudioBlock[1], sizeof(WAVEHDR));

    g_Playing = true;
    return true;
}



//==================================================================================================
// Stop audio.
//==================================================================================================
auto AudioStop() -> void {

    if (!g_Playing)
        return;
    g_Playing = false;

    waveOutReset(g_AudioHandle);
    waveOutClose(g_AudioHandle);
    g_Player.Stop();
    delete g_Adlib;
}



//==================================================================================================
// Entry point.
//==================================================================================================
auto main(int argc, char *argv[]) -> int {

    // Get command line arguments
    vector<const char *> filenames;
    bool repeat = true;

    for (int i = 1; i < argc; i++) {

        if (argv[i][0] == '-') {

            if (!strcmp(argv[i], "-norepeat")) {
                repeat = false;
                continue;
            }

            printf("WARNING: Unknown command line option: '%s'.\n", argv[i]);
            continue;
        }

        filenames.push_back(argv[i]);
    }

    // Get filename
    if (filenames.empty()) {
        printf("ERROR: No RAD V2.0 tunes supplied.\n");
        return 1;
    }

    bool repeat_tune = (repeat && filenames.size() == 1);
    bool repeat_list = (repeat && filenames.size() > 1);

    do {

        for (auto filename : filenames) {

            // Load file in
            auto fd = fopen(filename, "rb");
            if (!fd) {
                printf("ERROR: File '%s' not found.\n", filename);
                return 2;
            }

            fseek(fd, 0, SEEK_END);
            auto size = ftell(fd);
            fseek(fd, 0, SEEK_SET);
            if (size == 0) {
                fclose(fd);
                printf("ERROR: File '%s' is empty.\n", filename);
                return 2;
            }

            auto tune = new uint8_t[size];
            bool ok = (fread(tune, size, 1, fd) > 0);
            fclose(fd);
            if (!ok) {
                printf("ERROR: File '%s' read error.\n", filename);
                return 2;
            }

            // Check tune is valid.  This is good to do for players that load tunes by request.  You don't
            // need to do this if you're playing a known tune
            auto err = RADValidate(tune, size);
            if (err) {
                printf("ERROR: %s\n", err);
                return 3;
            }

            // Play tune
            if (!AudioInit(tune, !repeat_tune)) {
                printf("ERROR: Cannot play tune '%s'.\n", filename);
                return 4;
            }

            printf("Playing '%s'...\n\nDescription:\n~~~~~~~~~~~~\n", filename);

            // Display tune description - we assume the description is correctly formatted
            auto s = (const char *)tune + 0x12;
            if (tune[0x11] & 0x20)
                tune += 2;

            char line[0x100];
            int i = 0;
            while (*s) {
                auto c = *s++;
                if (c == 1) {
                    line[i] = 0;
                    puts(line);
                    i = 0;
                    continue;
                }
                if (c < 32) {
                    memset(line + i, ' ', c);
                    i += c;
                    continue;
                }
                line[i++] = c;
            }

            printf("\nHit ESCAPE to quit%s.\n", filenames.size() > 1 ? ", or N for next tune" : "");
            g_Repeat = false;
            bool esc = false, next = false;
            while (1) {

                if (GetAsyncKeyState(VK_ESCAPE) < 0)
                    esc = true;
                else if (GetAsyncKeyState(VK_ESCAPE) >= 0 && esc)
                    break;

                if (filenames.size() > 1) {
                    if (GetAsyncKeyState('N') < 0)
                        next = true;
                    else if (GetAsyncKeyState('N') >= 0 && next)
                        break;
                }

                OSPulse();
                Sleep(10);

                auto play_time = g_Player.GetPlayTimeInSeconds();
                auto secs = play_time % 60;
                auto mins = play_time / 60 % 60;
                auto hours = play_time / 3600 % 60;
                if (repeat_tune)
                    printf("[ %02d  |  %02d / %02d  |  %d:%02d:%02d ]\r", g_Player.GetTuneLine(), g_Player.GetTunePos(), g_Player.GetTuneLength(), hours, mins, secs);
                else
                    printf("[ %02d  |  %02d / %02d  |  %d:%02d:%02d / %d:%02d:%02d ]\r", g_Player.GetTuneLine(), g_Player.GetTunePos(), g_Player.GetTuneLength(), hours, mins, secs, g_TotalHours, g_TotalMins, g_TotalSecs);

                if (g_Repeat && !repeat_tune)
                    break;
            }
            printf("                                              \r");

            AudioStop();

            if (esc) {
                repeat_list = false;
                break;
            }
        }

    } while (repeat_list);

    printf("Thanks for playing!\n");
    return 0;
}
