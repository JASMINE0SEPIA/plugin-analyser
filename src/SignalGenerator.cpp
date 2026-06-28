#include "SignalGenerator.h"
#include <cmath>

void SineGenerator::fillBlock(juce::AudioBuffer<float>& buffer, int numSamples) {
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* channelData = buffer.getWritePointer(ch);
        double currentPhase = phase;

        for (int i = 0; i < numSamples; ++i) {
            channelData[i] = amplitude * (float)std::sin(currentPhase);
            currentPhase += phaseIncrement;

            if (currentPhase > 2.0 * juce::MathConstants<double>::pi)
                currentPhase -= 2.0 * juce::MathConstants<double>::pi;
        }
    }

    phase += phaseIncrement * numSamples;
    if (phase > 2.0 * juce::MathConstants<double>::pi)
        phase -= 2.0 * juce::MathConstants<double>::pi;
}

void NoiseGenerator::fillBlock(juce::AudioBuffer<float>& buffer, int numSamples) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* channelData = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            // Generate white noise in range [-amplitude, amplitude]
            channelData[i] = amplitude * (2.0f * rng.nextFloat() - 1.0f);
        }
    }
}

void SweepGenerator::reset() {
    currentPhase = 0.0;
    currentFreq = startHz;
    currentSample = 0;
}

void SweepGenerator::fillBlock(juce::AudioBuffer<float>& buffer, int numSamples) {
    const int64_t totalSamples = (int64_t)(duration * sampleRate);
    const double logStart = std::log(startHz);
    const double logEnd = std::log(endHz);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* channelData = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            if (currentSample >= totalSamples) {
                channelData[i] = 0.0f;
                continue;
            }

            // Logarithmic sweep
            double t = (double)currentSample / (double)totalSamples;
            double logFreq = logStart + t * (logEnd - logStart);
            currentFreq = std::exp(logFreq);

            const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * currentFreq / sampleRate;
            channelData[i] = amplitude * (float)std::sin(currentPhase);

            currentPhase += phaseIncrement;
            if (currentPhase > 2.0 * juce::MathConstants<double>::pi)
                currentPhase -= 2.0 * juce::MathConstants<double>::pi;

            currentSample++;
        }
    }
}

void ToneBurstGenerator::reset() {
    currentSample = 0;
    currentPhase = 0.0;
}

void ToneBurstGenerator::fillBlock(juce::AudioBuffer<float>& buffer, int numSamples) {
    const int64_t preSilenceSamples = (int64_t)(preSilence * sampleRate);
    const int64_t burstSamples = (int64_t)(burstDuration * sampleRate);
    const int64_t silenceSamples = (int64_t)(silenceDuration * sampleRate);
    const int64_t cycleSamples = burstSamples + silenceSamples;
    const int64_t attackRampSamples = (int64_t)(attackRamp * sampleRate);
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* channelData = buffer.getWritePointer(ch);
        double localPhase = currentPhase;

        for (int i = 0; i < numSamples; ++i) {
            int64_t absSample = currentSample + i;
            float sample = 0.0f;

            if (absSample >= preSilenceSamples) {
                int64_t posInPattern = absSample - preSilenceSamples;
                int64_t posInCycle = posInPattern % cycleSamples;

                if (posInCycle < burstSamples) {
                    // Inside burst — apply attack ramp at onset
                    float envelope = 1.0f;
                    if (posInCycle < attackRampSamples && attackRampSamples > 0) {
                        envelope = (float)posInCycle / (float)attackRampSamples;
                        // Smooth cosine ramp
                        envelope = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * envelope));
                    }
                    sample = amplitude * envelope * (float)std::sin(localPhase);
                } else {
                    // "Silence" period — output quiet tone if silenceAmplitude > 0
                    if (silenceAmplitude > 0.0f) {
                        // Apply ramp at the start of silence to avoid clicks
                        int64_t posInSilence = posInCycle - burstSamples;
                        float envelope = 1.0f;
                        if (posInSilence < attackRampSamples && attackRampSamples > 0) {
                            envelope = (float)posInSilence / (float)attackRampSamples;
                            envelope = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * envelope));
                        }
                        sample = silenceAmplitude * envelope * (float)std::sin(localPhase);
                    }
                    // else: true silence, sample stays 0
                }
            }

            channelData[i] = sample;
            localPhase += phaseIncrement;
            if (localPhase > 2.0 * juce::MathConstants<double>::pi)
                localPhase -= 2.0 * juce::MathConstants<double>::pi;
        }
    }

    // Advance phase by the whole block
    currentPhase += phaseIncrement * numSamples;
    if (currentPhase > 2.0 * juce::MathConstants<double>::pi)
        currentPhase -= 2.0 * juce::MathConstants<double>::pi;
    currentSample += numSamples;
}

void ImpulseGenerator::reset() { fired = false; }

void ImpulseGenerator::fillBlock(juce::AudioBuffer<float>& buffer, int numSamples) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto* channelData = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            if (!fired && i == 0) {
                channelData[i] = amplitude;
            } else {
                channelData[i] = 0.0f;
            }
        }
    }
    fired = true;
}
