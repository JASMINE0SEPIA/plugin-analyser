#include "TransientAnalyzer.h"
#include "JuceHeader.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

TransientAnalyzer::TransientAnalyzer(const juce::File& outDir, double attackThresholdPct,
                                     double releaseThresholdPct, double toneFrequency,
                                     const std::vector<juce::String>& paramNames,
                                     const juce::String& signalType)
    : attackThresholdPct(attackThresholdPct), releaseThresholdPct(releaseThresholdPct),
      toneFrequency(toneFrequency), paramNames(paramNames), outputDir(outDir), signalType(signalType) {}

TransientAnalyzer::~TransientAnalyzer() {}

std::vector<float> TransientAnalyzer::computeEnvelope(const std::vector<float>& signal, double sampleRate) {
    if (signal.empty())
        return {};

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

void TransientAnalyzer::processBlock(const BlockContext& ctx) {
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

void TransientAnalyzer::finish(const juce::File& outDir) {
    juce::String filename = "grid_transient_" + signalType.toLowerCase() + ".csv";
    juce::File csvFile = outDir.getChildFile(filename);
    std::ofstream out(csvFile.getFullPathName().toStdString());

    if (!out.is_open()) {
        std::cerr << "Failed to open " << filename.toStdString() << " for writing" << std::endl;
        return;
    }

    // Header
    out << "runId,attackTimeMs,releaseTimeMs,steadyStateGainReductionDb,overshootDb";
    for (const auto& paramName : paramNames) {
        out << "," << paramName.toStdString();
    }
    out << ",inputGainDb\n";

    const double onsetThreshold = 0.1;
    const int minBurstSamples = 64;

    for (const auto& [runId, data] : perRunData) {
        if (data.inBuffer.empty() || data.inBuffer.size() < (size_t)minBurstSamples) {
            out << runId << ",NaN,NaN,NaN,NaN";
            for (const auto& paramName : paramNames) {
                float value = 0.0f;
                auto it = data.paramValues.find(paramName);
                if (it != data.paramValues.end())
                    value = it->second;
                out << "," << value;
            }
            out << "," << data.inputGainDb << "\n";
            continue;
        }

        auto envIn = computeEnvelope(data.inBuffer, data.sampleRate);
        auto envOut = computeEnvelope(data.outBuffer, data.sampleRate);

        // Find steady-state input amplitude (max envelope in the middle portion)
        float envInMax = 0.0f;
        int searchStart = std::min((int)envIn.size() / 4, (int)envIn.size() - 1);
        int searchEnd = std::min((int)envIn.size() * 3 / 4, (int)envIn.size());
        for (int i = searchStart; i < searchEnd; ++i)
            envInMax = std::max(envInMax, envIn[i]);

        if (envInMax < 1e-6f) {
            out << runId << ",NaN,NaN,NaN,NaN";
            for (const auto& paramName : paramNames) {
                float value = 0.0f;
                auto it = data.paramValues.find(paramName);
                if (it != data.paramValues.end())
                    value = it->second;
                out << "," << value;
            }
            out << "," << data.inputGainDb << "\n";
            continue;
        }

        float onsetThresh = (float)(onsetThreshold * envInMax);

        // Find first onset: first sample where envIn > onsetThresh
        int onsetSample = -1;
        for (int i = 0; i < (int)envIn.size(); ++i) {
            if (envIn[i] > onsetThresh) {
                onsetSample = i;
                break;
            }
        }

        // Find first offset: first sample after onset+minBurst where envIn < onsetThresh
        int offsetSample = -1;
        if (onsetSample >= 0) {
            for (int i = onsetSample + minBurstSamples; i < (int)envIn.size(); ++i) {
                if (envIn[i] < onsetThresh) {
                    offsetSample = i;
                    break;
                }
            }
        }

        // Gain reduction function: GR(t) = 20*log10(envOut/envIn)
        auto gainReductionDb = [&](int i) -> double {
            if (envIn[i] < 1e-10f)
                return 0.0;
            double ratio = (double)envOut[i] / (double)envIn[i];
            return 20.0 * std::log10(std::max(ratio, 1e-10));
        };

        double attackTimeMs = -1.0;
        double releaseTimeMs = -1.0;
        double steadyStateGR = 0.0;
        double overshootDb = 0.0;

        if (onsetSample >= 0 && offsetSample > onsetSample) {
            // Steady-state GR: average GR in the latter half of the burst
            int ssStart = onsetSample + (offsetSample - onsetSample) / 2;
            int ssEnd = offsetSample;
            int ssCount = 0;
            double ssSum = 0.0;
            for (int i = ssStart; i < ssEnd; ++i) {
                ssSum += gainReductionDb(i);
                ssCount++;
            }
            if (ssCount > 0)
                steadyStateGR = ssSum / ssCount;

            // Attack: time from onset to when GR reaches attackThresholdPct% of steadyStateGR
            double attackTarget = steadyStateGR * (attackThresholdPct / 100.0);
            for (int i = onsetSample; i < offsetSample; ++i) {
                double gr = gainReductionDb(i);
                if (steadyStateGR > 0 && gr >= attackTarget) {
                    attackTimeMs = (double)(i - onsetSample) / data.sampleRate * 1000.0;
                    break;
                } else if (steadyStateGR < 0 && gr <= attackTarget) {
                    attackTimeMs = (double)(i - onsetSample) / data.sampleRate * 1000.0;
                    break;
                }
            }

            // Overshoot: max excursion beyond steady-state during attack phase
            int attackEnd = std::min(onsetSample + (int)(data.sampleRate * 0.5), offsetSample);
            for (int i = onsetSample; i < attackEnd; ++i) {
                double gr = gainReductionDb(i);
                double excursion = gr - steadyStateGR;
                if (steadyStateGR < 0)
                    excursion = -excursion;
                overshootDb = std::max(overshootDb, excursion);
            }

            // Release: track GR after offset, skip initial transient, then measure
            // time for GR to drop to (1-threshold)% of its post-transient value.
            // This captures the downward compressor's release time.
            int skipSamples = (int)(0.002 * data.sampleRate); // 2ms skip for transient
            int releaseStart = offsetSample + skipSamples;

            if (releaseStart < (int)envIn.size()) {
                double grInitial = gainReductionDb(releaseStart);
                double grTarget = grInitial * (1.0 - releaseThresholdPct / 100.0);

                for (int i = releaseStart; i < (int)envIn.size(); ++i) {
                    double gr = gainReductionDb(i);
                    if (grInitial > 0 && gr <= grTarget) {
                        releaseTimeMs = (double)(i - releaseStart) / data.sampleRate * 1000.0;
                        break;
                    } else if (grInitial < 0 && gr >= grTarget) {
                        releaseTimeMs = (double)(i - releaseStart) / data.sampleRate * 1000.0;
                        break;
                    } else if (std::abs(grInitial) < 0.1) {
                        // GR is already near 0, release is instantaneous
                        releaseTimeMs = 0.0;
                        break;
                    }
                }
            }
        }

        out << runId << "," << (attackTimeMs >= 0 ? std::to_string(attackTimeMs) : "NaN") << ","
            << (releaseTimeMs >= 0 ? std::to_string(releaseTimeMs) : "NaN") << ","
            << steadyStateGR << "," << overshootDb;

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

std::unique_ptr<Analyzer> createTransientAnalyzer(const juce::File& outDir, double attackThresholdPct,
                                                  double releaseThresholdPct, double toneFrequency,
                                                  const std::vector<juce::String>& paramNames,
                                                  const juce::String& signalType) {
    return std::make_unique<TransientAnalyzer>(outDir, attackThresholdPct, releaseThresholdPct, toneFrequency,
                                               paramNames, signalType);
}
