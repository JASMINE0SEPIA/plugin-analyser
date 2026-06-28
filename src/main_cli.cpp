#include "Config.h"
#include "JuceHeader.h"
#include "MeasurementEngine.h"
#include "PluginLoader.h"
#include <iostream>
#include <vector>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " --config <path> --out <path> [options]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  --config <path>     JSON configuration file (required)\n";
    std::cout << "  --out <path>        Output directory (required)\n";
    std::cout << "  --plugin <path>     Override pluginPath in JSON\n";
    std::cout << "  --seconds N         Override duration in seconds\n";
    std::cout << "  --samplerate SR     Override sample rate\n";
    std::cout << "  --blocksize BS      Override block size\n";
}

static void applyOverrides(Config& config, const juce::String& pluginPathOverride, double secondsOverride,
                           double sampleRateOverride, int blockSizeOverride) {
    if (!pluginPathOverride.isEmpty())
        config.pluginPath = pluginPathOverride;
    if (secondsOverride > 0.0)
        config.seconds = secondsOverride;
    if (sampleRateOverride > 0.0)
        config.sampleRate = sampleRateOverride;
    if (blockSizeOverride > 0)
        config.blockSize = blockSizeOverride;
}

static void runSingleConfig(const Config& config, const juce::File& baseOutDir,
                            juce::AudioPluginInstance& plugin) {
    juce::File outDir = baseOutDir;
    if (!config.outSubDir.isEmpty()) {
        outDir = baseOutDir.getChildFile(config.outSubDir);
    }
    if (!outDir.exists()) {
        outDir.createDirectory();
    }
    if (!outDir.isDirectory()) {
        std::cerr << "Error: Output path is not a directory: " << outDir.getFullPathName() << std::endl;
        return;
    }

    std::cout << "\n=== Running config: signalType=" << config.signalType
              << ", outDir=" << outDir.getFullPathName() << " ===" << std::endl;

    std::vector<juce::String> paramNames = getAllParamNames(config);

    std::cout << "Building measurement grid..." << std::endl;
    auto runs = buildRunGrid(config, paramNames);
    std::cout << "Generated " << runs.size() << " measurement runs" << std::endl;

    std::cout << "Creating analyzers..." << std::endl;
    auto analyzers = createAnalyzers(config, outDir, paramNames);
    std::cout << "Created " << analyzers.size() << " analyzers" << std::endl;

    int64_t totalSamples = (int64_t)(config.seconds * config.sampleRate);

    runMeasurementGrid(plugin, config.sampleRate, config.blockSize, totalSamples, runs, analyzers, config, outDir,
                       nullptr);

    std::cout << "Finalizing analyzers..." << std::endl;
    for (auto& analyzer : analyzers) {
        analyzer->finish(outDir);
    }

    resetAllParametersToDefault(plugin);

    std::cout << "Config complete: " << config.signalType << std::endl;
}

int main(int argc, char* argv[]) {
    juce::initialiseJuce_GUI();

    if (argc < 3) {
        printUsage(argv[0]);
        juce::shutdownJuce_GUI();
        return 1;
    }

    juce::String configPath;
    juce::String outPath;
    juce::String pluginPathOverride;
    double secondsOverride = -1.0;
    double sampleRateOverride = -1.0;
    int blockSizeOverride = -1;

    for (int i = 1; i < argc; ++i) {
        juce::String arg = argv[i];

        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--plugin" && i + 1 < argc) {
            pluginPathOverride = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            secondsOverride = juce::String(argv[++i]).getDoubleValue();
        } else if (arg == "--samplerate" && i + 1 < argc) {
            sampleRateOverride = juce::String(argv[++i]).getDoubleValue();
        } else if (arg == "--blocksize" && i + 1 < argc) {
            blockSizeOverride = juce::String(argv[++i]).getIntValue();
        }
    }

    if (configPath.isEmpty() || outPath.isEmpty()) {
        std::cerr << "Error: --config and --out are required\n";
        printUsage(argv[0]);
        return 1;
    }

    try {
        juce::File configFile(configPath);
        auto configs = Config::parseConfigs(configFile);

        if (configs.empty()) {
            std::cerr << "Error: No valid configurations found in " << configPath << std::endl;
            return 1;
        }

        for (auto& config : configs) {
            applyOverrides(config, pluginPathOverride, secondsOverride, sampleRateOverride, blockSizeOverride);
        }

        juce::File baseOutDir(outPath);
        if (!baseOutDir.exists()) {
            baseOutDir.createDirectory();
        }
        if (!baseOutDir.isDirectory()) {
            std::cerr << "Error: Output path is not a directory: " << outPath << std::endl;
            return 1;
        }

        std::cout << "Loading plugin: " << configs[0].pluginPath << std::endl;
        double sampleRate = configs[0].sampleRate;
        int blockSize = configs[0].blockSize;
        juce::String errorMessage;
        auto plugin = loadPluginInstance(juce::File(configs[0].pluginPath), sampleRate, blockSize, errorMessage);

        if (plugin == nullptr) {
            std::cerr << (errorMessage.isEmpty() ? "Failed to load plugin" : errorMessage.toStdString())
                      << std::endl;
            return 1;
        }

        std::cout << "Plugin loaded: " << plugin->getName() << std::endl;

        auto paramMap = buildParameterMap(*plugin, true);
        std::cout << "\nAvailable parameters (" << paramMap.size() << "):" << std::endl;
        std::cout << "========================================" << std::endl;
        int idx = 0;
        for (const auto& [name, param] : paramMap) {
            juce::String lowerName = name.toLowerCase();
            if (lowerName.contains("midi") || lowerName.contains("cc ") || lowerName.startsWith("cc")) {
                continue;
            }
            float value = param->getValue();
            float defaultValue = param->getDefaultValue();
            juce::String displayName = param->getName(512);
            std::cout << idx++ << ". " << displayName << " (internal: " << name << ")" << std::endl;
            std::cout << "   Value: " << value << " (default: " << defaultValue << ")" << std::endl;
        }
        std::cout << "========================================\n" << std::endl;

        for (int ci = 0; ci < (int)configs.size(); ++ci) {
            std::cout << "\n>>> Config " << (ci + 1) << " / " << configs.size() << " <<<" << std::endl;
            runSingleConfig(configs[ci], baseOutDir, *plugin);
        }

        plugin->releaseResources();

        std::cout << "\nAll measurements complete!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        juce::shutdownJuce_GUI();
        return 1;
    }

    juce::shutdownJuce_GUI();
    return 0;
}
