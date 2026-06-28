#pragma once

#include "JuceHeader.h"
#include <map>
#include <string>
#include <vector>

struct ParameterBucketConfig {
    juce::String paramName;
    juce::String strategy; // "Linear", "ExplicitValues", "Log", "EdgeAndCenter"
    float min = 0.0f;
    float max = 1.0f;
    int numBuckets = 0;
    std::vector<float> values;
};

struct Config {
    juce::String pluginPath;
    double sampleRate = 48000.0;
    double seconds = 5.0;
    int blockSize = 256;
    juce::String signalType; // "sine", "noise", "sweep", "tone_burst", "impulse"
    double sineFrequency = 1000.0;
    double sweepStartHz = 20.0;
    double sweepEndHz = 20000.0;

    // Tone burst signal parameters
    double toneBurstFrequency = 1000.0;
    double toneBurstDuration = 0.5;       // burst on duration (seconds)
    double toneBurstSilenceDuration = 0.5; // silence between bursts (seconds)
    double toneBurstAttackRamp = 0.001;   // onset ramp to avoid clicks (seconds)
    double toneBurstPreSilence = 0.05;    // initial silence before first burst (seconds)
    float toneBurstSilenceAmplitude = 0.0f; // amplitude during "silence" (0 = silence, >0 = quiet tone for release)

    // Impulse signal parameters
    juce::String impulseType; // "dirac"
    float impulseAmplitude = 1.0f;

    // Analyzer-specific parameters
    double envelopeHopMs = 0.2;            // EnvelopeFollower output hop size
    int phaseFftSize = 4096;               // PhaseAnalyzer FFT size
    double transientAttackThresholdPct = 63.0; // TransientAnalyzer attack fit threshold (% of steady-state, 63% = 1 tau)
    double transientReleaseThresholdPct = 63.0; // TransientAnalyzer release fit threshold

    std::vector<float> inputGainBucketsDb;
    std::vector<ParameterBucketConfig> parameterBuckets;
    std::map<juce::String, float> fixedParameterValues; // params held constant across all runs
    std::vector<juce::String> analyzers;

    // For batch mode: sub-directory within the main output directory
    juce::String outSubDir;

    static Config fromJson(const juce::File& jsonFile);
    static Config fromJsonString(const juce::String& jsonString);
    static std::vector<Config> parseConfigs(const juce::File& jsonFile);
};
