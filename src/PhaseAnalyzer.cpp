#include "PhaseAnalyzer.h"
#include "JuceHeader.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

PhaseAnalyzer::PhaseAnalyzer(const juce::File& outDir, int fftSize, const std::vector<juce::String>& paramNames,
                             const juce::String& signalType)
    : fftSize(fftSize), paramNames(paramNames), outputDir(outDir), signalType(signalType) {}

PhaseAnalyzer::~PhaseAnalyzer() {}

void PhaseAnalyzer::applyHannWindow(std::vector<float>& buffer) {
    const int N = (int)buffer.size();
    for (int i = 0; i < N; ++i) {
        float window = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * (float)i / (float)(N - 1)));
        buffer[i] *= window;
    }
}

void PhaseAnalyzer::processFFTWindow(RunSpectrum& spectrum) {
    if ((int)spectrum.inBuffer.size() < fftSize || (int)spectrum.outBuffer.size() < fftSize)
        return;

    applyHannWindow(spectrum.inBuffer);
    applyHannWindow(spectrum.outBuffer);

    juce::dsp::FFT fft((int)std::log2(fftSize));
    std::vector<std::complex<float>> inFFT(fftSize);
    std::vector<std::complex<float>> outFFT(fftSize);

    for (int i = 0; i < fftSize; ++i) {
        inFFT[i] = std::complex<float>(spectrum.inBuffer[i], 0.0f);
        outFFT[i] = std::complex<float>(spectrum.outBuffer[i], 0.0f);
    }

    // Perform in-place forward FFT on both input and output
    fft.perform(inFFT.data(), inFFT.data(), false);
    fft.perform(outFFT.data(), outFFT.data(), false);

    const int numBins = fftSize / 2;
    if ((int)spectrum.sumSxx.size() < numBins) {
        spectrum.sumSxx.resize(numBins, 0.0);
        spectrum.sumSyy.resize(numBins, 0.0);
        spectrum.sumSxy.resize(numBins, std::complex<double>(0.0, 0.0));
    }

    for (int k = 0; k < numBins; ++k) {
        spectrum.sumSxx[k] += (double)std::norm(inFFT[k]);
        spectrum.sumSyy[k] += (double)std::norm(outFFT[k]);
        spectrum.sumSxy[k] += std::conj(std::complex<double>(inFFT[k].real(), inFFT[k].imag())) *
                              std::complex<double>(outFFT[k].real(), outFFT[k].imag());
    }

    spectrum.numAverages++;

    spectrum.inBuffer.clear();
    spectrum.outBuffer.clear();
}

void PhaseAnalyzer::processBlock(const BlockContext& ctx) {
    auto& spectrum = perRunSpectra[ctx.runId];

    if (spectrum.sumSxx.empty()) {
        spectrum.paramValues = ctx.paramNamedValues;
        spectrum.inputGainDb = ctx.inputGainDb;
        spectrum.sampleRate = ctx.sampleRate;
    }

    for (int i = 0; i < ctx.numSamples; ++i) {
        spectrum.inBuffer.push_back(ctx.inL[i]);
        spectrum.outBuffer.push_back(ctx.outL[i]);

        if ((int)spectrum.inBuffer.size() >= fftSize) {
            processFFTWindow(spectrum);
        }
    }
}

void PhaseAnalyzer::finish(const juce::File& outDir) {
    juce::String filename = "grid_phase_" + signalType.toLowerCase() + ".csv";
    juce::File csvFile = outDir.getChildFile(filename);
    std::ofstream out(csvFile.getFullPathName().toStdString());

    if (!out.is_open()) {
        std::cerr << "Failed to open " << filename.toStdString() << " for writing" << std::endl;
        return;
    }

    // Header
    out << "runId,freqHz,magDb,phaseDeg,groupDelayMs,coherence";
    for (const auto& paramName : paramNames) {
        out << "," << paramName.toStdString();
    }
    out << ",inputGainDb\n";

    for (const auto& [runId, spectrum] : perRunSpectra) {
        if (spectrum.numAverages == 0)
            continue;

        const int numBins = fftSize / 2;
        const double binHz = spectrum.sampleRate / (double)fftSize;

        // First pass: compute raw phase per bin for unwrapping
        std::vector<double> rawPhase(numBins, 0.0);
        std::vector<double> magDb(numBins, 0.0);
        std::vector<double> coherence(numBins, 0.0);

        for (int k = 0; k < numBins; ++k) {
            double sxx = spectrum.sumSxx[k] / spectrum.numAverages;
            double syy = spectrum.sumSyy[k] / spectrum.numAverages;
            std::complex<double> sxy = spectrum.sumSxy[k] / (double)spectrum.numAverages;

            if (sxx <= 0.0) {
                rawPhase[k] = 0.0;
                magDb[k] = 0.0;
                coherence[k] = 0.0;
                continue;
            }

            std::complex<double> H = sxy / sxx;
            magDb[k] = 20.0 * std::log10(std::max(std::abs(H), 1e-10));
            rawPhase[k] = std::arg(H);

            if (sxx * syy > 0.0)
                coherence[k] = std::min(std::norm(sxy) / (sxx * syy), 1.0);
            else
                coherence[k] = 0.0;
        }

        // Unwrap phase
        std::vector<double> unwrappedPhase(numBins, 0.0);
        unwrappedPhase[0] = rawPhase[0];
        for (int k = 1; k < numBins; ++k) {
            unwrappedPhase[k] = unwrappedPhase[k - 1] +
                                std::atan2(std::sin(rawPhase[k] - rawPhase[k - 1]),
                                           std::cos(rawPhase[k] - rawPhase[k - 1]));
        }

        // Compute group delay from unwrapped phase: GD = -d(phase)/d(omega)
        // omega = 2*pi*f, so d(omega) = 2*pi*binHz
        // GD in seconds = -dPhase / (2*pi*binHz)
        // GD in ms = GD_seconds * 1000
        for (int k = 0; k < numBins; ++k) {
            double freqHz = (double)k * binHz;
            double gdMs = 0.0;

            if (k > 0 && k < numBins - 1) {
                double dPhase = unwrappedPhase[k + 1] - unwrappedPhase[k - 1];
                double dOmega = 2.0 * juce::MathConstants<double>::pi * 2.0 * binHz;
                gdMs = -dPhase / dOmega * 1000.0;
            } else if (k == 0 && numBins > 1) {
                double dPhase = unwrappedPhase[1] - unwrappedPhase[0];
                double dOmega = 2.0 * juce::MathConstants<double>::pi * binHz;
                gdMs = -dPhase / dOmega * 1000.0;
            } else if (k == numBins - 1 && numBins > 1) {
                double dPhase = unwrappedPhase[k] - unwrappedPhase[k - 1];
                double dOmega = 2.0 * juce::MathConstants<double>::pi * binHz;
                gdMs = -dPhase / dOmega * 1000.0;
            }

            out << runId << "," << freqHz << "," << magDb[k] << "," << unwrappedPhase[k] * 180.0 /
                                                                                            juce::MathConstants<double>::pi
                << "," << gdMs << "," << coherence[k];

            for (const auto& paramName : paramNames) {
                float value = 0.0f;
                auto it = spectrum.paramValues.find(paramName);
                if (it != spectrum.paramValues.end())
                    value = it->second;
                out << "," << value;
            }

            out << "," << spectrum.inputGainDb << "\n";
        }
    }
}

std::unique_ptr<Analyzer> createPhaseAnalyzer(const juce::File& outDir, int fftSize,
                                              const std::vector<juce::String>& paramNames,
                                              const juce::String& signalType) {
    return std::make_unique<PhaseAnalyzer>(outDir, fftSize, paramNames, signalType);
}
