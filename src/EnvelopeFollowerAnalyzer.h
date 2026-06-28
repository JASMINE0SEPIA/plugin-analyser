#pragma once

#include "Analyzer.h"
#include "JuceHeader.h"
#include <map>
#include <vector>

struct EnvelopeFollowerAnalyzer : public Analyzer {
    EnvelopeFollowerAnalyzer(const juce::File& outDir, double hopMs, double toneFrequency,
                             const std::vector<juce::String>& paramNames, const juce::String& signalType);
    ~EnvelopeFollowerAnalyzer() override;

    void processBlock(const BlockContext& ctx) override;
    void finish(const juce::File& outDir) override;

private:
    struct RunData {
        std::vector<float> inBuffer;
        std::vector<float> outBuffer;
        std::map<juce::String, float> paramValues;
        float inputGainDb;
        double sampleRate = 48000.0;
    };

    std::map<int, RunData> perRunData;
    double hopMs;
    double toneFrequency;
    std::vector<juce::String> paramNames;
    juce::File outputDir;
    juce::String signalType;

    // Sliding-window peak envelope: tracks the peak amplitude over one period of the tone
    std::vector<float> computeEnvelope(const std::vector<float>& signal, double sampleRate);
};

std::unique_ptr<Analyzer> createEnvelopeFollowerAnalyzer(const juce::File& outDir, double hopMs, double toneFrequency,
                                                         const std::vector<juce::String>& paramNames,
                                                         const juce::String& signalType);
