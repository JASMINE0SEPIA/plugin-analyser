#include "MeasurementEngine.h"
#include "BucketSpec.h"
#include "EnvelopeFollowerAnalyzer.h"
#include "LinearResponseAnalyzer.h"
#include "PhaseAnalyzer.h"
#include "PluginLoader.h"
#include "RawCsvAnalyzer.h"
#include "RmsPeakAnalyzer.h"
#include "ThdAnalyzer.h"
#include "TransferCurveAnalyzer.h"
#include "TransientAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>

std::vector<juce::String> getAllParamNames(const Config& config) {
    std::vector<juce::String> paramNames;
    for (const auto& bucket : config.parameterBuckets)
        paramNames.push_back(bucket.paramName);
    for (const auto& [name, value] : config.fixedParameterValues)
        paramNames.push_back(name);
    return paramNames;
}

std::vector<RunConfig> buildRunGrid(const Config& config, const std::vector<juce::String>& paramNames) {
    std::cerr << "[buildRunGrid] Starting with " << paramNames.size() << " parameters, "
              << config.parameterBuckets.size() << " bucket configs" << std::endl;
    std::vector<RunConfig> runs;

    std::vector<std::pair<juce::String, std::vector<float>>> paramValueLists;

    for (const auto& bucketConfig : config.parameterBuckets) {
        std::cerr << "[buildRunGrid] Processing bucket for parameter: " << bucketConfig.paramName << std::endl;
        BucketSpec spec;
        spec.paramName = bucketConfig.paramName;
        spec.strategy = BucketSpec::strategyFromString(bucketConfig.strategy);
        spec.min = bucketConfig.min;
        spec.max = bucketConfig.max;
        spec.numBuckets = bucketConfig.numBuckets;
        spec.values = bucketConfig.values;

        auto values = spec.generateValues();
        std::cerr << "[buildRunGrid] Generated " << values.size() << " values for " << bucketConfig.paramName
                  << std::endl;
        paramValueLists.push_back({bucketConfig.paramName, values});
    }

    int runId = 0;
    std::cerr << "[buildRunGrid] Building Cartesian product with " << config.inputGainBucketsDb.size()
              << " input gain buckets..." << std::endl;

    std::function<void(int, std::map<juce::String, float>)> generateCombinations;
    generateCombinations = [&](int paramIndex, std::map<juce::String, float> currentParams) {
        if (paramIndex >= (int)paramValueLists.size()) {
            for (const auto& [fixedName, fixedValue] : config.fixedParameterValues)
                currentParams[fixedName] = fixedValue;

            for (float inputGainDb : config.inputGainBucketsDb) {
                RunConfig run;
                run.runId = runId++;
                run.paramValues = currentParams;
                run.inputGainDb = inputGainDb;
                runs.push_back(run);
            }
            if (runId % 1000 == 0) {
                std::cerr << "[buildRunGrid] Generated " << runId << " runs so far..." << std::endl;
            }
            return;
        }

        const auto& [paramName, values] = paramValueLists[paramIndex];
        for (float value : values) {
            auto newParams = currentParams;
            newParams[paramName] = value;
            generateCombinations(paramIndex + 1, newParams);
        }
    };

    generateCombinations(0, {});
    std::cerr << "[buildRunGrid] Complete: generated " << runs.size() << " total runs" << std::endl;
    return runs;
}

std::vector<std::unique_ptr<Analyzer>> createAnalyzers(const Config& config, const juce::File& outDir,
                                                       const std::vector<juce::String>& paramNames) {
    std::vector<std::unique_ptr<Analyzer>> analyzers;

    for (const auto& analyzerName : config.analyzers) {
        if (analyzerName.equalsIgnoreCase("RawCsv")) {
            analyzers.push_back(createRawCsvAnalyzer(outDir, config.signalType));
        } else if (analyzerName.equalsIgnoreCase("RmsPeak")) {
            analyzers.push_back(createRmsPeakAnalyzer(outDir, paramNames, config.signalType));
        } else if (analyzerName.equalsIgnoreCase("TransferCurve")) {
            analyzers.push_back(createTransferCurveAnalyzer(outDir, 512, paramNames, config.signalType));
        } else if (analyzerName.equalsIgnoreCase("LinearResponse")) {
            if (config.signalType.equalsIgnoreCase("noise") || config.signalType.equalsIgnoreCase("sweep")) {
                analyzers.push_back(createLinearResponseAnalyzer(outDir, 4096, paramNames, config.signalType));
            } else {
                std::cerr << "Warning: LinearResponse analyzer requires noise or sweep signal type" << std::endl;
            }
        } else if (analyzerName.equalsIgnoreCase("Thd")) {
            if (config.signalType.equalsIgnoreCase("sine")) {
                analyzers.push_back(
                    createThdAnalyzer(outDir, 2048, config.sineFrequency, paramNames, config.signalType));
            } else {
                std::cerr << "Warning: Thd analyzer requires sine signal type" << std::endl;
            }
        } else if (analyzerName.equalsIgnoreCase("Phase")) {
            if (config.signalType.equalsIgnoreCase("noise") || config.signalType.equalsIgnoreCase("sweep")) {
                analyzers.push_back(
                    createPhaseAnalyzer(outDir, config.phaseFftSize, paramNames, config.signalType));
            } else {
                std::cerr << "Warning: Phase analyzer requires noise or sweep signal type" << std::endl;
            }
        } else if (analyzerName.equalsIgnoreCase("EnvelopeFollower")) {
            if (config.signalType.equalsIgnoreCase("tone_burst")) {
                analyzers.push_back(createEnvelopeFollowerAnalyzer(outDir, config.envelopeHopMs,
                                                                   config.toneBurstFrequency, paramNames,
                                                                   config.signalType));
            } else {
                std::cerr << "Warning: EnvelopeFollower analyzer requires tone_burst signal type" << std::endl;
            }
        } else if (analyzerName.equalsIgnoreCase("Transient")) {
            if (config.signalType.equalsIgnoreCase("tone_burst")) {
                analyzers.push_back(createTransientAnalyzer(outDir, config.transientAttackThresholdPct,
                                                            config.transientReleaseThresholdPct,
                                                            config.toneBurstFrequency, paramNames,
                                                            config.signalType));
            } else {
                std::cerr << "Warning: Transient analyzer requires tone_burst signal type" << std::endl;
            }
        } else {
            std::cerr << "Warning: Unknown analyzer: " << analyzerName << std::endl;
        }
    }

    return analyzers;
}

static void processRun(const RunConfig& run, juce::AudioPluginInstance& plugin,
                       const std::map<juce::String, juce::AudioProcessorParameter*>& paramMap,
                       const std::vector<juce::String>& paramNames, const Config& config, double sampleRate,
                       int blockSize, int64_t totalSamples, std::vector<std::unique_ptr<Analyzer>>& analyzers) {
    for (const auto& [paramName, value] : run.paramValues) {
        setParameterValue(plugin, paramMap, paramName, value);
    }

    float inputGainLinear = std::pow(10.0f, run.inputGainDb / 20.0f);

    std::unique_ptr<SineGenerator> sineGen;
    std::unique_ptr<NoiseGenerator> noiseGen;
    std::unique_ptr<SweepGenerator> sweepGen;
    std::unique_ptr<ToneBurstGenerator> toneBurstGen;
    std::unique_ptr<ImpulseGenerator> impulseGen;

    if (config.signalType.equalsIgnoreCase("sine")) {
        sineGen = std::make_unique<SineGenerator>();
        sineGen->sampleRate = sampleRate;
        sineGen->frequency = config.sineFrequency;
        sineGen->amplitude = inputGainLinear;
    } else if (config.signalType.equalsIgnoreCase("noise")) {
        noiseGen = std::make_unique<NoiseGenerator>();
        noiseGen->amplitude = inputGainLinear;
    } else if (config.signalType.equalsIgnoreCase("sweep")) {
        sweepGen = std::make_unique<SweepGenerator>();
        sweepGen->sampleRate = sampleRate;
        sweepGen->startHz = config.sweepStartHz;
        sweepGen->endHz = config.sweepEndHz;
        sweepGen->duration = config.seconds;
        sweepGen->amplitude = inputGainLinear;
        sweepGen->reset();
    } else if (config.signalType.equalsIgnoreCase("tone_burst")) {
        toneBurstGen = std::make_unique<ToneBurstGenerator>();
        toneBurstGen->sampleRate = sampleRate;
        toneBurstGen->frequency = config.toneBurstFrequency;
        toneBurstGen->burstDuration = config.toneBurstDuration;
        toneBurstGen->silenceDuration = config.toneBurstSilenceDuration;
        toneBurstGen->attackRamp = config.toneBurstAttackRamp;
        toneBurstGen->preSilence = config.toneBurstPreSilence;
        toneBurstGen->amplitude = inputGainLinear;
        toneBurstGen->silenceAmplitude = config.toneBurstSilenceAmplitude * inputGainLinear;
        toneBurstGen->reset();
    } else if (config.signalType.equalsIgnoreCase("impulse")) {
        impulseGen = std::make_unique<ImpulseGenerator>();
        impulseGen->amplitude = inputGainLinear;
        impulseGen->reset();
    }

    juce::AudioBuffer<float> inputBuffer(2, blockSize);
    juce::AudioBuffer<float> outputBuffer(2, blockSize);
    juce::MidiBuffer midiBuffer;

    int64_t currentSample = 0;
    while (currentSample < totalSamples) {
        int numThisBlock = (int)std::min((int64_t)blockSize, totalSamples - currentSample);

        inputBuffer.clear();
        outputBuffer.clear();

        if (sineGen) {
            sineGen->fillBlock(inputBuffer, numThisBlock);
        } else if (noiseGen) {
            noiseGen->fillBlock(inputBuffer, numThisBlock);
        } else if (sweepGen) {
            sweepGen->fillBlock(inputBuffer, numThisBlock);
        } else if (toneBurstGen) {
            toneBurstGen->fillBlock(inputBuffer, numThisBlock);
        } else if (impulseGen) {
            impulseGen->fillBlock(inputBuffer, numThisBlock);
        }

        outputBuffer.makeCopyOf(inputBuffer);

        plugin.processBlock(outputBuffer, midiBuffer);

        BlockContext ctx;
        ctx.firstSample = currentSample;
        ctx.sampleRate = sampleRate;
        ctx.numSamples = numThisBlock;
        ctx.inL = inputBuffer.getReadPointer(0);
        ctx.inR = inputBuffer.getNumChannels() > 1 ? inputBuffer.getReadPointer(1) : nullptr;
        ctx.outL = outputBuffer.getReadPointer(0);
        ctx.outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getReadPointer(1) : nullptr;
        ctx.runId = run.runId;
        ctx.paramNamedValues = run.paramValues;
        ctx.inputGainDb = run.inputGainDb;

        for (const auto& paramName : paramNames) {
            float value = 0.0f;
            auto it = run.paramValues.find(paramName);
            if (it != run.paramValues.end())
                value = it->second;
            ctx.params.push_back(value);
        }

        for (auto& analyzer : analyzers) {
            analyzer->processBlock(ctx);
        }

        currentSample += numThisBlock;
    }
}

void runMeasurementGrid(juce::AudioPluginInstance& plugin, double sampleRate, int blockSize, int64_t totalSamples,
                        const std::vector<RunConfig>& runs, std::vector<std::unique_ptr<Analyzer>>& analyzers,
                        const Config& config, const juce::File& outDir,
                        std::function<void(int)> progressCallback) {
    std::cerr << "[runMeasurementGrid] Starting with " << runs.size() << " runs, " << totalSamples
              << " samples per run" << std::endl;

    std::vector<juce::String> paramNames = getAllParamNames(config);

    auto paramMap = buildParameterMap(plugin, false);
    for (const auto& run : runs) {
        if (progressCallback) {
            progressCallback(run.runId);
        }
        processRun(run, plugin, paramMap, paramNames, config, sampleRate, blockSize, totalSamples, analyzers);
    }
}
