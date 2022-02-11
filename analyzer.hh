
#include <pulse/simple.h>
#include <pulse/error.h>
#include <string>

const std::string prog_name = "spectrumAnalyzer";

/* The Sample format to use */
static const pa_sample_spec generator_spec = {
    .format = PA_SAMPLE_FLOAT32LE,
    .channels = 2
};
static const pa_sample_spec system_spec = {
    .format = PA_SAMPLE_S16LE,
    .channels = 2
};
static const pa_sample_spec file_spec = system_spec;

static const pa_buffer_attr playback_attrs = {
    .prebuf = 0
};