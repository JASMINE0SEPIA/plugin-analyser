#pragma once

#include "Analyzer.h"
#include "JuceHeader.h"
#include <map>
#include <vector>

struct TransientAnalyzer : public Analyzer {
    TransientAnalyzer(const juce::File& outDir, double attackThresholdPct, double releaseThresholdPct,
                      double toneFrequency, const std::vector<juce::String>& paramNames,
                      const juce::String& signalType);
    ~TransientAnalyzer() override;

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
    double attackThresholdPct;
    double releaseThresholdPct;
    double toneFrequency;
    std::vector<juce::String> paramNames;
    juce::File outputDir;
    juce::String signalType;

    std::vector<float> computeEnvelope(const std::vector<float>& signal, double sampleRate);
};

std::unique_ptr<Analyzer> createTransientAnalyzer(const juce::File& outDir, double attackThresholdPct,
                                                  double releaseThresholdPct, double toneFrequency,
                                                  const std::vector<juce::String>& paramNames,
                                                  const juce::String& signalType);
