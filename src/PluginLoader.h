#pragma once

#include "JuceHeader.h"
#include <map>
#include <memory>

std::unique_ptr<juce::AudioPluginInstance> loadPluginInstance(const juce::File& pluginFile, double sampleRate,
                                                              int blockSize, juce::String& errorMessageOut);

std::map<juce::String, juce::AudioProcessorParameter*> buildParameterMap(juce::AudioPluginInstance& plugin,
                                                                         bool uiOnly = false);

void setParameterValue(juce::AudioPluginInstance& plugin,
                       const std::map<juce::String, juce::AudioProcessorParameter*>& paramMap,
                       const juce::String& paramName, float normalizedValue);

/// Reset all plugin parameters to their default values (prevents cross-config contamination)
void resetAllParametersToDefault(juce::AudioPluginInstance& plugin);
