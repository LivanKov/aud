#include <QApplication>
#include <iostream>
#include <thread>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <regex>
#include <portaudio.h>
#include <pa_linux_alsa.h>
#include <fftw3.h>
#include "app.h"
#include "filt.h"

#define SAMPLE_RATE 44100.0
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 2

#define SPECTRO_FREQ_START 20  // Lower bound of the displayed spectrogram (Hz)
#define SPECTRO_FREQ_END 20000 // Upper bound of the displayed spectrogram (Hz)

#define FILTER_ORDER 101  // Number of FIR filter taps

// Forward declaration
static App* g_app = nullptr;

// Define callback data structure
typedef struct {
    double* in;
    double* out;
    double* filtered;     // Buffer for filtered audio
    double* filterHistory; // History buffer for FIR filter (holds previous samples)
    fftw_plan p;
    int inputChannels;
    int startIndex;  // First index of our FFT output to display in the spectrogram
    int spectroSize; // Number of elements in our FFT output to display from the start index
} streamCallbackData;

static streamCallbackData* spectroData;

static void checkErr(PaError err) {
    if (err != paNoError) {
        printf("PortAudio error: %s\n", Pa_GetErrorText(err));
        exit(EXIT_FAILURE);
    }
}

static inline float min(float a, float b) {
    return a < b ? a : b;
}

// Returns the ALSA card index for Yamaha THR5 if present, otherwise -1.
static int getThr5AlsaCardIndex() {
    FILE* cardsFile = fopen("/proc/asound/cards", "r");
    int cardIndex = -1;
    if (cardsFile != NULL) {
        char line[512];
        int currentCardIndex = -1;
        while (fgets(line, sizeof(line), cardsFile) != NULL) {
            int parsedIndex = -1;
            if (sscanf(line, " %d", &parsedIndex) == 1) {
                currentCardIndex = parsedIndex;
            }

            std::regex thr("THR5", std::regex::icase);
            std::regex ymh("Yamaha", std::regex::icase);

            if ((std::regex_search(line, thr) || std::regex_search(line, ymh))
                && currentCardIndex >= 0) {
                cardIndex = currentCardIndex;
                break;
            }
        }

        fclose(cardsFile);
    }

    if (cardIndex >= 0) {
        return cardIndex;
    }

    // Fallback: identify THR5 by USB VID:PID (Yamaha 0499:1506).
    DIR* asoundDir = opendir("/proc/asound");
    if (asoundDir == NULL) {
        return -1;
    }

    struct dirent* entry;
    while ((entry = readdir(asoundDir)) != NULL) {
        if (strncmp(entry->d_name, "card", 4) != 0) {
            continue;
        }

        int parsedIndex = -1;
        if (sscanf(entry->d_name, "card%d", &parsedIndex) != 1 || parsedIndex < 0) {
            continue;
        }

        char usbidPath[64];
        snprintf(usbidPath, sizeof(usbidPath), "/proc/asound/card%d/usbid", parsedIndex);
        FILE* usbidFile = fopen(usbidPath, "r");
        if (usbidFile == NULL) {
            continue;
        }

        char usbid[64] = {0};
        if (fgets(usbid, sizeof(usbid), usbidFile) != NULL) {
            if (strncmp(usbid, "0499:1506", 9) == 0) {
                fclose(usbidFile);
                closedir(asoundDir);
                return parsedIndex;
            }
        }
        fclose(usbidFile);
    }

    closedir(asoundDir);
    return -1;
}

// Finds an input-capable PortAudio device by case-insensitive name substring.
static int findInputDeviceByName(int numDevices, const char* pattern) {
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info == NULL || info->maxInputChannels <= 0) {
            continue;
        }
        std::regex dev_name(pattern, std::regex::icase);
        if (std::regex_search(info->name, dev_name)) {
            return i;
        }
    }
    return paNoDevice;
}

// Best-effort terminal width query for single-line CLI output.
static int getTerminalColumns() {
    if (!isatty(STDOUT_FILENO)) {
        return 100;
    }

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    return 100;
}

static int streamCallback(
    const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags,
    void* userData
) {
    (void)outputBuffer;
    (void)timeInfo;
    (void)statusFlags;

    float* in = (float*)inputBuffer;
    streamCallbackData* callbackData = (streamCallbackData*)userData;

    // Copy audio sample to input buffer
    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        callbackData->in[i] = in[i * callbackData->inputChannels];
    }

    // Apply FIR lowpass filter (500Hz cutoff)
    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        // Shift history buffer and add new sample
        for (int j = FILTER_ORDER - 1; j > 0; j--) {
            callbackData->filterHistory[j] = callbackData->filterHistory[j - 1];
        }
        callbackData->filterHistory[0] = callbackData->in[i];

        // Convolve with filter coefficients
        double filteredSample = 0.0;
        for (int j = 0; j < FILTER_ORDER; j++) {
            filteredSample += b[j] * callbackData->filterHistory[j];
        }
        callbackData->filtered[i] = filteredSample;
    }

    // Perform FFT on filtered data
    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        callbackData->in[i] = callbackData->filtered[i];
    }
    fftw_execute(callbackData->p);

    // Send filtered data to GUI if available
    if (g_app != nullptr) {
        g_app->updateAudioData(callbackData->filtered, callbackData->out, 
                               FRAMES_PER_BUFFER, SAMPLE_RATE);
    }

    // Draw CLI spectrogram
    int dispSize = getTerminalColumns() - 1;
    if (dispSize < 10) {
        dispSize = 10;
    }
    printf("\r\033[2K");

    for (int i = 0; i < dispSize; i++) {
        // Sample frequency data logarithmically
        double proportion = std::pow(i / (double)dispSize, 4);
        double freq = callbackData->out[(int)(callbackData->startIndex
            + proportion * callbackData->spectroSize)];

        // Display full block characters with heights based on frequency intensity
        if (freq < 0.125) {
            printf("▁");
        } else if (freq < 0.25) {
            printf("▂");
        } else if (freq < 0.375) {
            printf("▃");
        } else if (freq < 0.5) {
            printf("▄");
        } else if (freq < 0.625) {
            printf("▅");
        } else if (freq < 0.75) {
            printf("▆");
        } else if (freq < 0.875) {
            printf("▇");
        } else {
            printf("█");
        }
    }
    fflush(stdout);

    return 0;
}

void audioThreadFunc() {
    std::cout << "Audio thread starting..." << std::endl;

    // Initialize PortAudio
    PaError err;
    err = Pa_Initialize();
    checkErr(err);

    // Allocate callback data
    spectroData = (streamCallbackData*)malloc(sizeof(streamCallbackData));
    spectroData->in = (double*)malloc(sizeof(double) * FRAMES_PER_BUFFER);
    spectroData->out = (double*)malloc(sizeof(double) * FRAMES_PER_BUFFER);
    spectroData->filtered = (double*)malloc(sizeof(double) * FRAMES_PER_BUFFER);
    spectroData->filterHistory = (double*)calloc(FILTER_ORDER, sizeof(double)); // Zero-initialized
    if (spectroData->in == NULL || spectroData->out == NULL || 
        spectroData->filtered == NULL || spectroData->filterHistory == NULL) {
        printf("Could not allocate spectro data\n");
        exit(EXIT_FAILURE);
    }
    spectroData->p = fftw_plan_r2r_1d(
        FRAMES_PER_BUFFER, spectroData->in, spectroData->out,
        FFTW_R2HC, FFTW_ESTIMATE
    );
    spectroData->inputChannels = NUM_CHANNELS;
    
    // Calculate spectrogram indices for CLI display
    double sampleRatio = FRAMES_PER_BUFFER / SAMPLE_RATE;
    spectroData->startIndex = std::ceil(sampleRatio * SPECTRO_FREQ_START);
    spectroData->spectroSize = min(
        std::ceil(sampleRatio * SPECTRO_FREQ_END),
        FRAMES_PER_BUFFER / 2.0
    ) - spectroData->startIndex;

    int numDevices = Pa_GetDeviceCount();
    printf("Number of devices: %d\n", numDevices);

    if (numDevices < 0) {
        printf("Error getting device count.\n");
        Pa_Terminate();
        return;
    } else if (numDevices == 0) {
        printf("There are no available audio devices on this machine.\n");
        Pa_Terminate();
        return;
    }

    // Display audio device information
    const PaDeviceInfo* deviceInfo;
    for (int i = 0; i < numDevices; i++) {
        deviceInfo = Pa_GetDeviceInfo(i);
        printf("Device %d:\n", i);
        printf("  name: %s\n", deviceInfo->name);
        printf("  maxInputChannels: %d\n", deviceInfo->maxInputChannels);
        printf("  maxOutputChannels: %d\n", deviceInfo->maxOutputChannels);
        printf("  defaultSampleRate: %f\n", deviceInfo->defaultSampleRate);
    }

    // Try to find THR5 device
    int thr5CardIndex = getThr5AlsaCardIndex();
    bool useAlsaDeviceString = false;
    char alsaDeviceString[32] = {0};

    int device = paNoDevice;
    const PaDeviceInfo* selectedDeviceInfo = NULL;

    if (thr5CardIndex >= 0) {
        useAlsaDeviceString = true;
    } else {
        device = findInputDeviceByName(numDevices, "THR5");
        if (device == paNoDevice) {
            device = findInputDeviceByName(numDevices, "Yamaha");
        }
        if (device == paNoDevice) {
            device = findInputDeviceByName(numDevices, "USB");
        }
        if (device == paNoDevice) {
            // Fallback to default input device
            device = Pa_GetDefaultInputDevice();
            if (device == paNoDevice) {
                printf("No input device available.\n");
                Pa_Terminate();
                return;
            }
            printf("Using default input device.\n");
        }

        selectedDeviceInfo = Pa_GetDeviceInfo(device);
        if (selectedDeviceInfo == NULL) {
            printf("Could not read selected device info.\n");
            Pa_Terminate();
            return;
        }
        printf("Selected device: %s\n", selectedDeviceInfo->name);
    }

    // Define stream capture specifications
    PaStreamParameters inputParameters;
    memset(&inputParameters, 0, sizeof(inputParameters));
    inputParameters.sampleFormat = paFloat32;
    inputParameters.channelCount = NUM_CHANNELS;

    PaAlsa_SetRetriesBusy(25);

    // Open the PortAudio stream
    PaStream* stream = NULL;
    PaError openErr = paNoError;
    if (useAlsaDeviceString) {
        const int channelCandidates[2] = {NUM_CHANNELS, 1};
        char deviceCandidates[5][32];
        snprintf(deviceCandidates[0], sizeof(deviceCandidates[0]), "plughw:%d,0", thr5CardIndex);
        snprintf(deviceCandidates[1], sizeof(deviceCandidates[1]), "hw:%d,0", thr5CardIndex);
        snprintf(deviceCandidates[2], sizeof(deviceCandidates[2]), "dsnoop:%d,0", thr5CardIndex);
        snprintf(deviceCandidates[3], sizeof(deviceCandidates[3]), "sysdefault:%d,0", thr5CardIndex);
        snprintf(deviceCandidates[4], sizeof(deviceCandidates[4]), "sysdefault:%d", thr5CardIndex);

        bool opened = false;
        for (int d = 0; d < 5 && !opened; d++) {
            for (int c = 0; c < 2 && !opened; c++) {
                PaAlsaStreamInfo alsaInfo;
                PaAlsa_InitializeStreamInfo(&alsaInfo);
                alsaInfo.deviceString = deviceCandidates[d];

                inputParameters.device = paUseHostApiSpecificDeviceSpecification;
                inputParameters.hostApiSpecificStreamInfo = &alsaInfo;
                inputParameters.channelCount = channelCandidates[c];
                inputParameters.suggestedLatency = 0.010;

                openErr = Pa_OpenStream(
                    &stream,
                    &inputParameters,
                    NULL,
                    SAMPLE_RATE,
                    FRAMES_PER_BUFFER,
                    paNoFlag,
                    streamCallback,
                    spectroData
                );
                if (openErr == paNoError) {
                    snprintf(alsaDeviceString, sizeof(alsaDeviceString), "%s", deviceCandidates[d]);
                    spectroData->inputChannels = channelCandidates[c];
                    opened = true;
                }
            }
        }

        if (!opened) {
            int pulseDevice = findInputDeviceByName(numDevices, "pulse");
            if (pulseDevice == paNoDevice) {
                pulseDevice = findInputDeviceByName(numDevices, "default");
            }
            if (pulseDevice != paNoDevice) {
                const PaDeviceInfo* pulseInfo = Pa_GetDeviceInfo(pulseDevice);
                if (pulseInfo != NULL && pulseInfo->maxInputChannels > 0) {
                    int pulseChannels = pulseInfo->maxInputChannels >= NUM_CHANNELS ? NUM_CHANNELS : 1;
                    inputParameters.device = pulseDevice;
                    inputParameters.hostApiSpecificStreamInfo = NULL;
                    inputParameters.channelCount = pulseChannels;
                    inputParameters.suggestedLatency = pulseInfo->defaultLowInputLatency;

                    openErr = Pa_OpenStream(
                        &stream,
                        &inputParameters,
                        NULL,
                        SAMPLE_RATE,
                        FRAMES_PER_BUFFER,
                        paNoFlag,
                        streamCallback,
                        spectroData
                    );
                    if (openErr == paNoError) {
                        spectroData->inputChannels = pulseChannels;
                        opened = true;
                    }
                }
            }
        }

        if (!opened) {
            printf("Could not open THR5 ALSA device on card %d.\n", thr5CardIndex);
            printf("Last PortAudio error: %s\n", Pa_GetErrorText(openErr));
            Pa_Terminate();
            return;
        }
    } else {
        // Adjust channel count if device doesn't support requested channels
        if (selectedDeviceInfo->maxInputChannels < NUM_CHANNELS) {
            inputParameters.channelCount = selectedDeviceInfo->maxInputChannels;
            spectroData->inputChannels = selectedDeviceInfo->maxInputChannels;
        }
        
        inputParameters.device = device;
        inputParameters.hostApiSpecificStreamInfo = NULL;
        inputParameters.suggestedLatency = selectedDeviceInfo->defaultLowInputLatency;

        openErr = Pa_OpenStream(
            &stream,
            &inputParameters,
            NULL,
            SAMPLE_RATE,
            FRAMES_PER_BUFFER,
            paNoFlag,
            streamCallback,
            spectroData
        );
        if (openErr != paNoError) {
            printf("Error opening stream: %s\n", Pa_GetErrorText(openErr));
            Pa_Terminate();
            return;
        }
    }

    // Begin capturing audio
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        printf("Error starting stream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        Pa_Terminate();
        return;
    }

    std::cout << "Audio capture started. Close the window to stop." << std::endl;

    // Run until GUI signals to stop
    while (g_app != nullptr && !g_app->shouldStop) {
        Pa_Sleep(100);
    }

    std::cout << "Audio thread stopping..." << std::endl;

    // Stop and clean up
    err = Pa_StopStream(stream);
    if (err != paNoError) {
        printf("Error stopping stream: %s\n", Pa_GetErrorText(err));
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError) {
        printf("Error closing stream: %s\n", Pa_GetErrorText(err));
    }

    err = Pa_Terminate();
    if (err != paNoError) {
        printf("Error terminating PortAudio: %s\n", Pa_GetErrorText(err));
    }

    // Free allocated resources
    fftw_destroy_plan(spectroData->p);
    fftw_free(spectroData->in);
    fftw_free(spectroData->out);
    free(spectroData->filtered);
    free(spectroData->filterHistory);
    free(spectroData);

    printf("\n");
    std::cout << "Audio thread finished." << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "Starting application..." << std::endl;
    
    QApplication qapp(argc, argv);
    
    App app;
    g_app = &app;
    
    // Start audio capture in a separate thread
    std::thread audioThread(audioThreadFunc);
    
    app.run();
    
    int result = qapp.exec();
    
    // Signal the audio thread to stop and wait for it
    app.shouldStop = true;
    if (audioThread.joinable()) {
        audioThread.join();
    }
    
    g_app = nullptr;
    
    return result;
}
