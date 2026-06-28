#include "Config.h"
#include <fstream>
#include <sstream>

Config Config::fromJson(const juce::File& jsonFile) {
    if (!jsonFile.existsAsFile()) {
        throw std::runtime_error("Config file does not exist: " + jsonFile.getFullPathName().toStdString());
    }

    juce::String jsonContent = jsonFile.loadFileAsString();
    return fromJsonString(jsonContent);
}

Config Config::fromJsonString(const juce::String& jsonString) {
    Config config;

    auto json = juce::JSON::parse(jsonString);
    if (!json.isObject()) {
        throw std::runtime_error("Invalid JSON: root must be an object");
    }

    auto root = json.getDynamicObject();

    // Plugin path
    if (root->hasProperty("pluginPath"))
        config.pluginPath = root->getProperty("pluginPath").toString();

    // Audio settings
    if (root->hasProperty("sampleRate"))
        config.sampleRate = (double)root->getProperty("sampleRate");
    if (root->hasProperty("seconds"))
        config.seconds = (double)root->getProperty("seconds");
    if (root->hasProperty("blockSize"))
        config.blockSize = (int)root->getProperty("blockSize");

    // Signal settings
    if (root->hasProperty("signalType"))
        config.signalType = root->getProperty("signalType").toString();
    if (root->hasProperty("sineFrequency"))
        config.sineFrequency = (double)root->getProperty("sineFrequency");
    if (root->hasProperty("sweepStartHz"))
        config.sweepStartHz = (double)root->getProperty("sweepStartHz");
    if (root->hasProperty("sweepEndHz"))
        config.sweepEndHz = (double)root->getProperty("sweepEndHz");

    // Tone burst parameters
    if (root->hasProperty("toneBurst")) {
        auto tb = root->getProperty("toneBurst").getDynamicObject();
        if (tb != nullptr) {
            if (tb->hasProperty("toneFrequency"))
                config.toneBurstFrequency = (double)tb->getProperty("toneFrequency");
            if (tb->hasProperty("burstDuration"))
                config.toneBurstDuration = (double)tb->getProperty("burstDuration");
            if (tb->hasProperty("silenceDuration"))
                config.toneBurstSilenceDuration = (double)tb->getProperty("silenceDuration");
            if (tb->hasProperty("attackRamp"))
                config.toneBurstAttackRamp = (double)tb->getProperty("attackRamp");
            if (tb->hasProperty("preSilence"))
                config.toneBurstPreSilence = (double)tb->getProperty("preSilence");
            if (tb->hasProperty("silenceAmplitude"))
                config.toneBurstSilenceAmplitude = (float)tb->getProperty("silenceAmplitude");
        }
    }

    // Impulse parameters
    if (root->hasProperty("impulse")) {
        auto imp = root->getProperty("impulse").getDynamicObject();
        if (imp != nullptr) {
            if (imp->hasProperty("type"))
                config.impulseType = imp->getProperty("type").toString();
            if (imp->hasProperty("amplitude"))
                config.impulseAmplitude = (float)imp->getProperty("amplitude");
        }
    }

    // Analyzer-specific parameters
    if (root->hasProperty("envelope")) {
        auto env = root->getProperty("envelope").getDynamicObject();
        if (env != nullptr && env->hasProperty("hopMs"))
            config.envelopeHopMs = (double)env->getProperty("hopMs");
    }
    if (root->hasProperty("phase")) {
        auto ph = root->getProperty("phase").getDynamicObject();
        if (ph != nullptr && ph->hasProperty("fftSize"))
            config.phaseFftSize = (int)ph->getProperty("fftSize");
    }
    if (root->hasProperty("transient")) {
        auto tr = root->getProperty("transient").getDynamicObject();
        if (tr != nullptr) {
            if (tr->hasProperty("attackThresholdPct"))
                config.transientAttackThresholdPct = (double)tr->getProperty("attackThresholdPct");
            if (tr->hasProperty("releaseThresholdPct"))
                config.transientReleaseThresholdPct = (double)tr->getProperty("releaseThresholdPct");
        }
    }

    // Input gain buckets
    if (root->hasProperty("inputGainBucketsDb")) {
        auto gainArray = root->getProperty("inputGainBucketsDb");
        if (gainArray.isArray()) {
            for (int i = 0; i < gainArray.size(); ++i) {
                config.inputGainBucketsDb.push_back((float)gainArray[i]);
            }
        }
    }

    // Parameter buckets
    if (root->hasProperty("parameterBuckets")) {
        auto bucketsArray = root->getProperty("parameterBuckets");
        if (bucketsArray.isArray()) {
            for (int i = 0; i < bucketsArray.size(); ++i) {
                auto bucketObj = bucketsArray[i].getDynamicObject();
                if (bucketObj == nullptr)
                    continue;

                ParameterBucketConfig bucket;
                if (bucketObj->hasProperty("paramName"))
                    bucket.paramName = bucketObj->getProperty("paramName").toString();
                if (bucketObj->hasProperty("strategy"))
                    bucket.strategy = bucketObj->getProperty("strategy").toString();
                if (bucketObj->hasProperty("min"))
                    bucket.min = (float)bucketObj->getProperty("min");
                if (bucketObj->hasProperty("max"))
                    bucket.max = (float)bucketObj->getProperty("max");
                if (bucketObj->hasProperty("numBuckets"))
                    bucket.numBuckets = (int)bucketObj->getProperty("numBuckets");
                if (bucketObj->hasProperty("values")) {
                    auto valuesArray = bucketObj->getProperty("values");
                    if (valuesArray.isArray()) {
                        for (int j = 0; j < valuesArray.size(); ++j) {
                            bucket.values.push_back((float)valuesArray[j]);
                        }
                    }
                }

                config.parameterBuckets.push_back(bucket);
            }
        }
    }

    // Fixed parameter values
    if (root->hasProperty("fixedParameterValues")) {
        auto fixedObj = root->getProperty("fixedParameterValues").getDynamicObject();
        if (fixedObj != nullptr) {
            for (const auto& key : fixedObj->getProperties()) {
                config.fixedParameterValues[key.name.toString().trim().toLowerCase()] =
                    (float)fixedObj->getProperty(key.name);
            }
        }
    }

    // Analyzers
    if (root->hasProperty("analyzers")) {
        auto analyzersArray = root->getProperty("analyzers");
        if (analyzersArray.isArray()) {
            for (int i = 0; i < analyzersArray.size(); ++i) {
                config.analyzers.push_back(analyzersArray[i].toString());
            }
        }
    }

    // Output sub-directory (for batch mode)
    if (root->hasProperty("out"))
        config.outSubDir = root->getProperty("out").toString();

    return config;
}

std::vector<Config> Config::parseConfigs(const juce::File& jsonFile) {
    if (!jsonFile.existsAsFile()) {
        throw std::runtime_error("Config file does not exist: " + jsonFile.getFullPathName().toStdString());
    }

    juce::String jsonContent = jsonFile.loadFileAsString();
    auto json = juce::JSON::parse(jsonContent);
    if (!json.isObject()) {
        throw std::runtime_error("Invalid JSON: root must be an object");
    }

    auto root = json.getDynamicObject();

    // Batch mode: top-level "configs" array
    if (root->hasProperty("configs")) {
        auto configsArray = root->getProperty("configs");
        if (configsArray.isArray()) {
            // Build a base config from top-level properties (excluding "configs")
            // so sub-configs can inherit common settings like pluginPath, sampleRate, etc.
            juce::DynamicObject::Ptr baseObj = new juce::DynamicObject();
            for (const auto& prop : root->getProperties()) {
                juce::String name = prop.name.toString();
                if (!name.equalsIgnoreCase("configs"))
                    baseObj->setProperty(prop.name, prop.value);
            }
            juce::String baseJson = juce::JSON::toString(juce::var(baseObj.get()));

            std::vector<Config> configs;
            for (int i = 0; i < configsArray.size(); ++i) {
                auto subJson = configsArray[i];
                if (!subJson.isObject())
                    continue;

                // Merge: start from base, then override with sub-config properties
                auto subObj = subJson.getDynamicObject();
                auto mergedObj = new juce::DynamicObject();

                // Copy base properties first
                auto baseParsed = juce::JSON::parse(baseJson);
                if (baseParsed.isObject()) {
                    auto baseDyn = baseParsed.getDynamicObject();
                    if (baseDyn != nullptr) {
                        for (const auto& prop : baseDyn->getProperties())
                            mergedObj->setProperty(prop.name, prop.value);
                    }
                }

                // Override with sub-config properties
                if (subObj != nullptr) {
                    for (const auto& prop : subObj->getProperties())
                        mergedObj->setProperty(prop.name, prop.value);
                }

                configs.push_back(fromJsonString(juce::JSON::toString(juce::var(mergedObj))));
            }
            return configs;
        }
    }

    // Single config mode
    return {fromJsonString(jsonContent)};
}
