#include "EnvelopeFollowerAnalyzer.h"
#include "JuceHeader.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

EnvelopeFollowerAnalyzer::EnvelopeFollowerAnalyzer(const juce::File& outDir, double hopMs, double toneFrequency,
                                                   const std::vector<juce::String>& paramNames,
                                                   const juce::String& signalType)
    : hopMs(hopMs), toneFrequency(toneFrequency), paramNames(paramNames), outputDir(outDir), signalType(signalType) {}

EnvelopeFollowerAnalyzer::~EnvelopeFollowerAnalyzer() {}

std::vector<float> EnvelopeFollowerAnalyzer::computeEnvelope(const std::vector<float>& signal, double sampleRate) {
    if (signal.empty())
        return {};

    // Sliding-window peak envelope: for each sample, find the peak |x| over a
    // window of one period of the tone frequency. This gives the instantaneous
    // peak amplitude with at most half a period of delay, and no filter time
    // constant that would bias attack/release measurements.
    int periodSamples = (int)(sampleRate / std::max(toneFrequency, 1.0));
    if (periodSamples < 2)
        periodSamples = 2;

    int halfWindow = periodSamples / 2;
    std::vector<float> envelope(signal.size());

    for (int i = 0; i < (int)signal.size(); ++i) {
        float maxVal = 0.0f;
        int start = std::max(0, i - halfWindow);
        int end = std::min((int)signal.size(), i + halfWindow + 1);
        for (int j = start; j < end; ++j) {
            float absVal = std::abs(signal[j]);
            if (absVal > maxVal)
                maxVal = absVal;
        }
        envelope[i] = maxVal;
    }

    return envelope;
}

void EnvelopeFollowerAnalyzer::processBlock(const BlockContext& ctx) {
    auto& data = perRunData[ctx.runId];

    if (data.inBuffer.empty()) {
        data.paramValues = ctx.paramNamedValues;
        data.inputGainDb = ctx.inputGainDb;
        data.sampleRate = ctx.sampleRate;
    }

    for (int i = 0; i < ctx.numSamples; ++i) {
        data.inBuffer.push_back(ctx.inL[i]);
        data.outBuffer.push_back(ctx.outL[i]);
    }
}

void EnvelopeFollowerAnalyzer::finish(const juce::File& outDir) {
    juce::String filename = "grid_envelope_" + signalType.toLowerCase() + ".csv";
    juce::File csvFile = outDir.getChildFile(filename);
    std::ofstream out(csvFile.getFullPathName().toStdString());

    if (!out.is_open()) {
        std::cerr << "Failed to open " << filename.toStdString() << " for writing" << std::endl;
        return;
    }

    // Header
    out << "runId,timeSec,envInDb,envOutDb,gainReductionDb";
    for (const auto& paramName : paramNames) {
        out << "," << paramName.toStdString();
    }
    out << ",inputGainDb\n";

    for (const auto& [runId, data] : perRunData) {
        if (data.inBuffer.empty())
            continue;

        int hop = std::max(1, (int)(hopMs * 0.001 * data.sampleRate));

        auto envIn = computeEnvelope(data.inBuffer, data.sampleRate);
        auto envOut = computeEnvelope(data.outBuffer, data.sampleRate);

        for (int i = 0; i < (int)envIn.size(); i += hop) {
            double timeSec = (double)i / data.sampleRate;
            double envInDb = 20.0 * std::log10(std::max((double)envIn[i], 1e-10));
            double envOutDb = 20.0 * std::log10(std::max((double)envOut[i], 1e-10));
            double gainReductionDb = envOutDb - envInDb;

            out << runId << "," << timeSec << "," << envInDb << "," << envOutDb << "," << gainReductionDb;

            for (const auto& paramName : paramNames) {
                float value = 0.0f;
                auto it = data.paramValues.find(paramName);
                if (it != data.paramValues.end())
                    value = it->second;
                out << "," << value;
            }

            out << "," << data.inputGainDb << "\n";
        }
    }
}

std::unique_ptr<Analyzer> createEnvelopeFollowerAnalyzer(const juce::File& outDir, double hopMs, double toneFrequency,
                                                         const std::vector<juce::String>& paramNames,
                                                         const juce::String& signalType) {
    return std::make_unique<EnvelopeFollowerAnalyzer>(outDir, hopMs, toneFrequency, paramNames, signalType);
}
