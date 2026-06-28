#pragma once

#include "Analyzer.h"
#include "JuceHeader.h"
#include <complex>
#include <map>
#include <vector>

struct PhaseAnalyzer : public Analyzer {
    PhaseAnalyzer(const juce::File& outDir, int fftSize, const std::vector<juce::String>& paramNames,
                  const juce::String& signalType);
    ~PhaseAnalyzer() override;

    void processBlock(const BlockContext& ctx) override;
    void finish(const juce::File& outDir) override;

private:
    struct RunSpectrum {
        std::vector<double> sumSxx; // |X|^2 averaged
        std::vector<double> sumSyy; // |Y|^2 averaged
        std::vector<std::complex<double>> sumSxy; // conj(X)*Y averaged
        int numAverages = 0;
        std::vector<float> inBuffer;
        std::vector<float> outBuffer;
        std::map<juce::String, float> paramValues;
        float inputGainDb;
        double sampleRate = 48000.0;
    };

    std::map<int, RunSpectrum> perRunSpectra;
    int fftSize;
    std::vector<juce::String> paramNames;
    juce::File outputDir;
    juce::String signalType;

    void processFFTWindow(RunSpectrum& spectrum);
    void applyHannWindow(std::vector<float>& buffer);
};

std::unique_ptr<Analyzer> createPhaseAnalyzer(const juce::File& outDir, int fftSize,
                                              const std::vector<juce::String>& paramNames,
                                              const juce::String& signalType);
