#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <portaudio.h> // PortAudio: Used for audio capture
#include <pa_linux_alsa.h>
#include <fftw3.h>     // FFTW:      Provides a discrete FFT algorithm to get
                       //            frequency data from captured audio

#define SAMPLE_RATE 44100.0   // How many audio samples to capture every second (44100 Hz is standard)
#define FRAMES_PER_BUFFER 512 // How many audio samples to send to our callback function for each channel
#define NUM_CHANNELS 2        // Number of audio channels to capture

#define SPECTRO_FREQ_START 20  // Lower bound of the displayed spectrogram (Hz)
#define SPECTRO_FREQ_END 20000 // Upper bound of the displayed spectrogram (Hz)

// Define our callback data (data that is passed to every callback function call)
typedef struct {
    double* in;      // Input buffer, will contain our audio sample
    double* out;     // Output buffer, FFTW will write to this based on the input buffer's contents
    fftw_plan p;     // Created by FFTW to facilitate FFT calculation
    int startIndex;  // First index of our FFT output to display in the spectrogram
    int spectroSize; // Number of elements in our FFT output to display from the start index
    int inputChannels; // Number of channels actually opened on the input stream
} streamCallbackData;

// Callback data, persisted between calls. Allows us to access the data it
// contains from within the callback function.
static streamCallbackData* spectroData;

// Checks the return value of a PortAudio function. Logs the message and exits
// if there was an error
static void checkErr(PaError err) {
    if (err != paNoError) {
        printf("PortAudio error: %s\n", Pa_GetErrorText(err));
        exit(EXIT_FAILURE);
    }
}

// Returns the lowest of the two given numbers
static inline float min(float a, float b) {
    return a < b ? a : b;
}

// Returns true if `text` contains `pattern` (case-insensitive).
static bool containsIgnoreCase(const char* text, const char* pattern) {
    std::string textStr = text == NULL ? "" : text;
    std::string patternStr = pattern == NULL ? "" : pattern;

    std::transform(textStr.begin(), textStr.end(), textStr.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    std::transform(patternStr.begin(), patternStr.end(), patternStr.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    return textStr.find(patternStr) != std::string::npos;
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

            if ((containsIgnoreCase(line, "THR5") || containsIgnoreCase(line, "Yamaha"))
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
        if (containsIgnoreCase(info->name, pattern)) {
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

// PortAudio stream callback function. Will be called after every
// `FRAMES_PER_BUFFER` audio samples PortAudio captures. Used to process the
// resulting audio sample.
static int streamCallback(
    const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags,
    void* userData
) {
    // Cast our input buffer to a float pointer (since our sample format is `paFloat32`)
    float* in = (float*)inputBuffer;

    // We will not be modifying the output buffer. This line is a no-op.
    (void)outputBuffer;

    // Cast our user data to streamCallbackData* so we can access its struct members
    streamCallbackData* callbackData = (streamCallbackData*)userData;

    // Keep the spectrogram on one terminal line.
    int dispSize = getTerminalColumns() - 1;
    if (dispSize < 10) {
        dispSize = 10;
    }
    printf("\r\033[2K");

    // Copy audio sample to FFTW's input buffer
    for (unsigned long i = 0; i < framesPerBuffer; i++) {
        callbackData->in[i] = in[i * callbackData->inputChannels];
    }

    // Perform FFT on callbackData->in (results will be stored in callbackData->out)
    fftw_execute(callbackData->p);

    // Draw the spectrogram
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

    // Display the buffered changes to stdout in the terminal
    fflush(stdout);

    return 0;
}

int main() {
    // Initialize PortAudio
    PaError err;
    err = Pa_Initialize();
    checkErr(err);

    // Allocate and define the callback data used to calculate/display the spectrogram
    spectroData = (streamCallbackData*)malloc(sizeof(streamCallbackData));
    spectroData->in = (double*)malloc(sizeof(double) * FRAMES_PER_BUFFER);
    spectroData->out = (double*)malloc(sizeof(double) * FRAMES_PER_BUFFER);
    if (spectroData->in == NULL || spectroData->out == NULL) {
        printf("Could not allocate spectro data\n");
        exit(EXIT_FAILURE);
    }
    spectroData->p = fftw_plan_r2r_1d(
        FRAMES_PER_BUFFER, spectroData->in, spectroData->out,
        FFTW_R2HC, FFTW_ESTIMATE
    );
    spectroData->inputChannels = NUM_CHANNELS;
    double sampleRatio = FRAMES_PER_BUFFER / SAMPLE_RATE;
    spectroData->startIndex = std::ceil(sampleRatio * SPECTRO_FREQ_START);
    spectroData->spectroSize = min(
        std::ceil(sampleRatio * SPECTRO_FREQ_END),
        FRAMES_PER_BUFFER / 2.0
    ) - spectroData->startIndex;

    // Get and display the number of audio devices accessible to PortAudio
    int numDevices = Pa_GetDeviceCount();
    printf("Number of devices: %d\n", numDevices);

    if (numDevices < 0) {
        printf("Error getting device count.\n");
        exit(EXIT_FAILURE);
    } else if (numDevices == 0) {
        printf("There are no available audio devices on this machine.\n");
        exit(EXIT_SUCCESS);
    }

    // Display audio device information for each device accessible to PortAudio
    const PaDeviceInfo* deviceInfo;
    for (int i = 0; i < numDevices; i++) {
        deviceInfo = Pa_GetDeviceInfo(i);
        printf("Device %d:\n", i);
        printf("  name: %s\n", deviceInfo->name);
        printf("  maxInputChannels: %d\n", deviceInfo->maxInputChannels);
        printf("  maxOutputChannels: %d\n", deviceInfo->maxOutputChannels);
        printf("  defaultSampleRate: %f\n", deviceInfo->defaultSampleRate);
    }

    // Prefer direct ALSA THR5 capture by card index (works even when PortAudio
    // device enumeration doesn't expose a THR5-named entry).
    int thr5CardIndex = getThr5AlsaCardIndex();
    bool useAlsaDeviceString = false;
    char alsaDeviceString[32] = {0};

    // Fallback to regular PortAudio enumerated-device selection.
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
            // Some hosts expose USB audio generically.
            device = findInputDeviceByName(numDevices, "USB");
        }
        if (device == paNoDevice) {
            printf("Could not find Yamaha THR5 input device.\n");
            exit(EXIT_FAILURE);
        }

        selectedDeviceInfo = Pa_GetDeviceInfo(device);
        if (selectedDeviceInfo == NULL) {
            printf("Could not read selected device info.\n");
            exit(EXIT_FAILURE);
        }
        if (selectedDeviceInfo->maxInputChannels < NUM_CHANNELS) {
            printf(
                "Selected device '%s' does not support %d input channels.\n",
                selectedDeviceInfo->name,
                NUM_CHANNELS
            );
            exit(EXIT_FAILURE);
        }
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
                inputParameters.suggestedLatency = 0.010; // 10 ms

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
            // If PulseAudio owns the hardware node, use its shared input path.
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
            exit(EXIT_FAILURE);
        }
    } else {
        inputParameters.device = device;
        inputParameters.hostApiSpecificStreamInfo = NULL;
        inputParameters.suggestedLatency = selectedDeviceInfo->defaultLowInputLatency;
        inputParameters.channelCount = NUM_CHANNELS;

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
        checkErr(openErr);
    }

    // Begin capturing audio
    err = Pa_StartStream(stream);
    checkErr(err);

    // Wait 30 seconds (PortAudio will continue to capture audio)
    Pa_Sleep(30 * 1000);

    // Stop capturing audio
    err = Pa_StopStream(stream);
    checkErr(err);

    // Close the PortAudio stream
    err = Pa_CloseStream(stream);
    checkErr(err);

    // Terminate PortAudio
    err = Pa_Terminate();
    checkErr(err);

    // Free allocated resources used for FFT calculation
    fftw_destroy_plan(spectroData->p);
    fftw_free(spectroData->in);
    fftw_free(spectroData->out);
    free(spectroData);

    printf("\n");

    return EXIT_SUCCESS;
}
