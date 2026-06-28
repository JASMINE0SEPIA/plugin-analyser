#pragma once

#include "JuceHeader.h"
#include <cstdint>

struct SineGenerator {
    double sampleRate = 48000.0;
    double frequency = 1000.0;
    double phase = 0.0;
    float amplitude = 0.5f;

    void fillBlock(juce::AudioBuffer<float>& buffer, int numSamples);
};

struct NoiseGenerator {
    float amplitude = 0.5f;
    juce::Random rng;

    void fillBlock(juce::AudioBuffer<float>& buffer, int numSamples);
};

struct SweepGenerator {
    double sampleRate = 48000.0;
    double startHz = 20.0;
    double endHz = 20000.0;
    double duration = 5.0;
    float amplitude = 0.5f;

    double currentPhase = 0.0;
    double currentFreq = 20.0;
    int64_t currentSample = 0;

    void reset();
    void fillBlock(juce::AudioBuffer<float>& buffer, int numSamples);
};

struct ToneBurstGenerator {
    double sampleRate = 48000.0;
    double frequency = 1000.0;
    double burstDuration = 0.5;       // burst on duration (seconds)
    double silenceDuration = 0.5;     // silence between bursts (seconds)
    double attackRamp = 0.001;        // onset ramp to avoid clicks (seconds)
    double preSilence = 0.05;         // initial silence before first burst (seconds)
    float amplitude = 0.5f;           // burst amplitude
    float silenceAmplitude = 0.0f;    // amplitude during "silence" (0 = true silence, >0 = quiet tone for release measurement)

    int64_t currentSample = 0;        // absolute position within the pattern
    double currentPhase = 0.0;

    void reset();
    void fillBlock(juce::AudioBuffer<float>& buffer, int numSamples);
};

struct ImpulseGenerator {
    float amplitude = 1.0f;
    bool fired = false;

    void reset();
    void fillBlock(juce::AudioBuffer<float>& buffer, int numSamples);
};
