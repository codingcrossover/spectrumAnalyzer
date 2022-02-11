//todo: put header gaurd
 
#include <stdio.h>
#include <string>
#include <sstream>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
 #include "analyzer.hh" 

#include "aquila/global.h"
#include "aquila/source/generator/SineGenerator.h"
#include "aquila/transform/FftFactory.h"
#include "aquila/tools/TextPlot.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <map>
#include <signal.h>
#include <chrono>

#define BUFSIZE 1024
static std::atomic<bool> running;

typedef struct {
    std::string action; //options are: 'sweep' 'single'
    Aquila::FrequencyType f1;
    Aquila::FrequencyType f2;
    Aquila::FrequencyType currentFrequency;
    Aquila::FrequencyType sampleFreq;
    double timeSlice; //1/sampleFrequency
    int sweepIncrement;
    std::chrono::nanoseconds sweepInterval; //time it takes to increment f1 up an increment

} generatorContorls;

typedef struct {
    pa_simple *player;
    pa_simple *recorder;
    FILE *inputFile;
    std::string inputFileName;
    void *audioSamples; //should actually point to uint8_t types
    std::string style;
    uint32_t sampleRate;
    std::string listenSink;
    std::string playbackSink;
    generatorContorls gc;

} audioControlsInfo;

//sytles of getting sampled input
static int signalGenerator(audioControlsInfo* ai) {
    //adjust frequency where appropriate
    if (ai->gc.action == "sweep") {
        if (ai->gc.currentFrequency >= ai->gc.f2) {
            std::cout << "sweep has finished, exiting" << std::endl;
            return -1;
        }
        static auto startTime = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        static auto currentTime = startTime;
        currentTime = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        auto timediff = currentTime - startTime;
        std::cout << "time diff is: " << timediff << ", interval count is: " << ai->gc.sweepInterval.count() << std::endl;
        if ( (currentTime-startTime) >= ai->gc.sweepInterval.count() ) {
            std::cout << "time elapsed is : " << currentTime-startTime << std::endl;
            ai->gc.currentFrequency += ai->gc.sweepIncrement;
            startTime = currentTime;
        }

    } else if (ai->gc.action == "single") {
        ai->gc.currentFrequency = ai->gc.currentFrequency;
    } else {
        std::cout << "unrecognized generator action '" << ai->gc.action << "', exiting" << std::endl;
        return -1;
    }

    //fill in the audio samples
    for(int i=0; i<BUFSIZE; i+=1) {
        ((float *)ai->audioSamples)[i] = std::sin(2*M_PI*ai->gc.currentFrequency*i*ai->gc.timeSlice);
    }
    std::cout << "current frequency is: '" << ai->gc.currentFrequency << "'Hz" << std::endl;

    return 0;
}

static int readAudioFromFile (audioControlsInfo* ai) {
    size_t r = fread(&((float*)ai->audioSamples)[0], sizeof(float), (size_t)BUFSIZE/sizeof(float), ai->inputFile);
    // std::cout << "read: '" << r << "' items from file" << std::endl;
    if (r == 0) {    
        if (!feof(ai->inputFile)) {
            std::cout << __FILE__ << ": fread() failed: %s\n" << strerror(errno);
            return -1;
        } else {
            std::cout << "reached the end of file, byeeee" << std::endl;
            running =false;
        }
    }
    return 0;
}

static int readAudioFromSystem(audioControlsInfo* ai) {
    /* Record some data ... */
    int error;
    if (pa_simple_read(ai->recorder, ai->audioSamples, BUFSIZE, &error) < 0) {
        fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
        return -1;
    }

    return 0;
}

static std::map<std::string, std::function<int(audioControlsInfo*)>> styleMap = {
    {"generator", signalGenerator},
    {"file", readAudioFromFile},
    {"system", readAudioFromSystem}
};

static std::map<int, std::string> colorMap = {
    {1, "\e[1;91m"}, //red
    {2, "\e[1;92m"}, //green
    {3, "\e[1;93m"} //yellow
};
 
static void moveCursor(uint r, uint c) {
    printf("\033[%d;%dH", r, c);
}
static void initTerminal(int height) {
for(int i=0; i<height; i++) {
        std::cout << "\n";
    }
    moveCursor(0,0);
}

static void signalHandler(int signal) {
    std::cout << "handling interupt" << std::endl;
    running = false;
}

static pa_usec_t getWriteStreamLatency(pa_simple *&sp) {
    pa_usec_t latency;
    int error;

    if ((latency = pa_simple_get_latency(sp, &error)) == (pa_usec_t) -1) {
        fprintf(stderr, __FILE__": pa_simple_get_latency() failed: %s\n", pa_strerror(error));
    }

    return latency;
}

static void cleanup(audioControlsInfo *ai) {
    std::cout << "cleaning up pulse audio streams" << std::endl;
    int error;
    /* Make sure that every single sample was played */
    if (pa_simple_drain(ai->player, &error) < 0) {
        fprintf(stderr, __FILE__": pa_simple_drain() failed: %s\n", pa_strerror(error));
    }

    if (ai->player)
        pa_simple_free(ai->player);
    if (ai->recorder)
        pa_simple_free(ai->recorder);
    if (ai->inputFile)
        fclose(ai->inputFile);
}

static int playAudio(pa_simple *&sp, void *out) {
    int error;
    if (pa_simple_write(sp, out, (size_t) BUFSIZE, &error) < 0) {
        fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
        return -1;
    }
    return 0;
}

static void displayFft(std::string fftOut) {
        printf("%s", fftOut.c_str());
        moveCursor(0,0);
}

static std::string constructFftOutput(std::string c, int *downsampledFft, int shellHeight, int shellWidth, uint32_t sampleRate) {
    int binWidth = BUFSIZE/shellWidth;
    std::string retval;
    for(int i=0; i<shellHeight-1; i++) {
        std::string row;
        for(int j=0; j<shellWidth; j++) {
            if (downsampledFft[j] >= (shellHeight-i)) {
                std::string tmp;
                if(i < .40*shellHeight) { tmp = colorMap[1]; }
                else if(i < .85*shellHeight) { tmp = colorMap[3]; }
                else { tmp = colorMap[2]; }
                row += tmp + c;
            } else {
                row += " ";
            }
        }
       retval += row + "\n";
    }
    // for(int i=0; i<shellWidth; i+=binWidth) {
    //     retval += std::to_string(i*binWidth*200000/(sampleRate)) + "  ";
    // }

    return retval;
}

static void downsampleFFT(int *downsampledFft, Aquila::SpectrumType& spectrum, int shellWidth, int shellHeight, int scale = 10) {   
    int startIdx=0;
    int endIdx = spectrum.size();
    int binWidth = endIdx/shellWidth;
    Aquila::FrequencyType passBand1 = 0;
    Aquila::FrequencyType passBand2 = 20*1e3; //20kHz

    //filter the spectrum to audio spectrum
    for(int i=startIdx; i<endIdx; ++i) {
        //set to zero if frequency is greater that 20kHz
        if ( (i >= (endIdx * passBand2 / 44100))
        || (i <= (endIdx * passBand1 / 44100)) ) {
            spectrum[i] = 0;
        }
    }

    //find max value in this spectrum batch 
    float fftMax = 1; 
    for(int i=startIdx; i<endIdx; i++) {
        // if (spectrum[i].real() >= 0) {
            float mag = std::abs(spectrum[i]);
            if ( mag > fftMax) {
                fftMax = mag;
            }
        // }        
    }

    int idx = 0;
    for(int i=startIdx; i<endIdx; i+=binWidth) {
        float avg;
        float sum = 0;
        for(int j=i; j<i+binWidth; j++) {
            // if (spectrum[j].real() >= 0) {
                float fftSize = std::abs(spectrum[j]);
                float shellFftSize = shellHeight * (fftSize)/fftMax;
                // std::cout << "this spectrum idx: " << spectrum[i] << ", shellFftSize: " << shellFftSize << ", fftSize: " << fftSize << ", fftMax" << std::endl;
                sum += shellFftSize;
            // } else { sum += 0; }
        }
        avg = sum/binWidth;
        avg *= scale;
        // std::cout << "using " << (int)avg << " as this sample" << std::endl;      

        // todo: this only works with specific shellWidth and therefore, binWidth. A good number is 256 for now.
        // but this does need to be fixed
        downsampledFft[idx++] = (int)avg;
    }
}

static void getFFT(Aquila::SpectrumType& spectrum, audioControlsInfo *ai) {
    Aquila::SampleType aquilaSamples[BUFSIZE];
    for(int i=0; i<BUFSIZE; i+=1) {
        if (ai->style == "system" || ai->style == "file") {
            aquilaSamples[i] = ((uint8_t*)ai->audioSamples)[i];
        } else {
            aquilaSamples[i] = ((float *)ai->audioSamples)[i];
        }
    }

    auto fft = Aquila::FftFactory::getFft(BUFSIZE);
    spectrum = fft->fft(aquilaSamples);
}

static int getSampledData(audioControlsInfo* ai) {
    auto it = styleMap.find(ai->style);
    if (it == styleMap.end()) {
        std::cout << "The style: '" << ai->style << "' is not recognized, exiting" << std::endl;
        return -1;
    }

    return it->second(ai);
}

static int setupStreams(audioControlsInfo *ai) {
    //todo: turn this if block into a map
    static pa_sample_spec ss;
    if (ai->style == "system") {
        ss = system_spec;
    } else if (ai->style == "generator") {
        ss = generator_spec;
    } else if (ai->style == "file") {
        ss = file_spec;
    } else {
        std::cout << "un recognized style, exiting" << std::endl;
        running = false;
        return -1;
    }
    ss.rate = ai->sampleRate;

    int error;
    if (ai->style == "file") {
        std::cout << "opening file: " << ai->inputFileName << std::endl;
        ai->inputFile = fopen(ai->inputFileName.c_str(), "r");
        if (!ai->inputFile) {
            std::cout << __FILE__ << ": fopen() failed: %s\n" << strerror(errno);
            running = false;
            fclose(ai->inputFile);
            return -1;
        } 
    }
    

    if (ai->style == "system") {
        /* Create the recording stream */
        if (!(ai->recorder = pa_simple_new(NULL, "recorder", PA_STREAM_RECORD, ai->listenSink.c_str(), "record", &ss, NULL, NULL, &error))) {
            fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
            return -1;
        }
    }    
 
    if (ai->style != "system") {
        /* Create a new playback stream */
        //NOTE: the dev argument corresponds to the sink index numbers listed via 'pacmd list-cards' i.e. 1 or 2 or 4 etc NULL used default index
        if (!(ai->player = pa_simple_new(NULL, "player", PA_STREAM_PLAYBACK, ai->playbackSink.c_str(), "playback", &ss, NULL, NULL, &error))) {
            fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
            return -1;
        }
    }    

    return 0;
}

void printUsage() {
    std::string usage;
    usage = "\n" + prog_name + " -- An application to display sound to the frequncy spectrum in a terminal\n\n";
    usage += "\tUSAGE: " + prog_name + "\t<terminal-height> <terminal-width> <sample_rate> <mode> <MODE_OPTIONS>\n\n";

    usage += "\tterminal-height -- the height of the terminal the application is being run in (typically using $LINES is good enough)\n\n";
    usage += "\tterminal-width -- the width of the terminal the application is being run in (typically using $COLUMNS is good enough)\n\n";

    usage += "\tsample_rate -- the sample rate of signal being played\n\n";

    usage += "modes:\n";
    usage += "\tsystem -- listens/displays what sound the system plays\n\n\tsystem options:\n\t\t<listen_sink> -- virtual audio card to record audio from (use `pacmd list-cards` to list the indexes) NOTE: specify the index number\n\n\n";
    
    usage += "\tfile -- plays/displays music from a file to the frequency spectrum\n\n\t\tfile options:\n\t\t<playback_sink> -- virtual audio card to playback audio to (use `pacmd list-cards` to list the indexes) NOTE: specify the index number\n\t\t<file_location> -- location to where the file is (only support binary and wav files currenctly)\n\n\n";

    usage += "\tgenerator -- generates/displays frequencies to the frequency spectrum\n\n\t\tgenerator options:\n\t\t<playback_sink> -- virtual audio card to playback audio to (use `pacmd list-cards` to list the indexes) NOTE: specify the index number\n\t\t<action> -- actions the generator should take (supports 'single' and 'sweep')\n";
    usage += "\t\t\tactions:\n\t\t\tsingle -- plays a single tone\n\t\t\t\t<frequency>\n\n\t\t\tsweep -- sweeps the frequency spectrum starting from 'start frequency' to 'stop frequency' incrementing by 'step frequency' every 'time between steps' (in milliseconds).\n\t\t\t\t<start_frequency>\t<stop_frequency>\t<step_frequency>\t<time_between_steps>";

    usage += "\n\n\nExample Usage:\n\n\t./" + prog_name + " $LINES $COLUMNS 44100 generator 3 sweep 0 22000 100 1000\n\nor\n\n\t./" + prog_name + " $LINES $COLUMNS 44100 file 0 ../Foyf.wav\n\nor\n\n\t./" + prog_name + " $LINES $COLUMNS 44100 system 1\n\n";
    std::cout << usage << std::endl;
}

int main(int argc, char*argv[]) {

    if (argc == 1 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        printUsage();
        return(EXIT_FAILURE);
    }

    signal(SIGINT, signalHandler);

    //make space for the fft
    int shellHeight = std::stoi(argv[1]);
    int shellWidth = std::stoi(argv[2]);

    audioControlsInfo audioInfo;
    audioInfo.sampleRate = std::stoi(argv[3]);
    audioInfo.style = std::string(argv[4]);
    int scaler = 1;
    std::cout << "using height: " << shellHeight << ", using width: " << shellWidth << ", for style: " << audioInfo.style << ", with sample rate: " << audioInfo.sampleRate << std::endl;

    if (audioInfo.style == "file") {
        audioInfo.playbackSink = std::string(argv[5]);
        audioInfo.inputFileName = std::string(argv[6]);
    } else if (audioInfo.style == "generator") {
        audioInfo.playbackSink = std::string(argv[5]);
        audioInfo.playbackSink = audioInfo.playbackSink == "-1" ? "" : audioInfo.playbackSink;
        audioInfo.gc.action = std::string(argv[6]);
        audioInfo.gc.f1 = std::stoi(argv[7]);
        audioInfo.gc.currentFrequency = audioInfo.gc.f1;
        audioInfo.gc.sampleFreq = audioInfo.sampleRate;
        audioInfo.gc.timeSlice = 1.0/audioInfo.gc.sampleFreq;
        if (audioInfo.gc.action == "sweep") {
            audioInfo.gc.f2 = std::stoi(argv[8]);
            audioInfo.gc.sweepIncrement = std::stoi(argv[9]);
            audioInfo.gc.sweepInterval = std::chrono::milliseconds(std::stoi(argv[10]));
        }        
    } else if (audioInfo.style == "system" ) {
        audioInfo.listenSink = std::string(argv[5]);
    }

    int error;
    int ret = 0;
    running = true;
    
    if ( (ret = setupStreams(&audioInfo)) ) {
        running = false;
        printUsage();
    }
 
    initTerminal(shellHeight);
    while (running) {
        ssize_t r;
        float buf[BUFSIZE];
        audioInfo.audioSamples = &buf[0];
 
#if 0
        auto latency = getWriteStreamLatency(simple_player);
        if (latency < 0) {
            ret = 1;
            running =false;
        }
#endif
 
        /* Read some data ... */
        if ( (ret = getSampledData(&audioInfo) )) {
            running = false;
        }

        //convert the audio signal to frequency spectrum
        Aquila::SpectrumType spectrum;
        getFFT(spectrum, &audioInfo);        


        //down sample BUFSIZE(1024) samples to shellWidth;
        //todo fix the bin width issue not evenly fitting into shellWidth
        int downSampledShellFft[shellWidth];
        downsampleFFT(&downSampledShellFft[0], spectrum, shellWidth, shellHeight, scaler);


        //print the downsampled array to screen in form of a '|' character
        std::string c = "|";
        std::string fftOutput = constructFftOutput(c, &downSampledShellFft[0], shellHeight, shellWidth, audioInfo.sampleRate);        


        if (audioInfo.style != "system" ) {
            if ( (ret = playAudio(audioInfo.player, audioInfo.audioSamples)) ) {
                running = false;
            }
        }
        displayFft(fftOutput);
    }
 
    cleanup(&audioInfo); 
    return ret;
}