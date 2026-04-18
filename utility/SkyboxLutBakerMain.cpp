#include "SkyboxLutGenerator.h"

#include "Texture.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

/**
 * @brief 把字符串转换为小写（仅处理 ASCII 字母）。
 */
std::string ToLowerAscii(std::string text)
{
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

/**
 * @brief 判断输出路径是否为 PNG 扩展名。
 */
bool IsPngOutputPath(const std::string& outputPath)
{
    const std::filesystem::path path(outputPath);
    return ToLowerAscii(path.extension().string()) == ".png";
}

/**
 * @brief 离线 LUT 烘焙命令行配置。
 */
struct LutBakerCommandLine
{
    std::string cubemapDirectory;
    std::string outputPath;
    utility::SkyboxLutBuildConfig buildConfig;
};

/**
 * @brief 打印离线工具用法说明。
 */
void PrintUsage()
{
    std::cout
        << "Usage:\n"
        << "  skyboxLutBaker --cubemap-dir <dir> --output <file.(png|ppm)> [options]\n\n"
        << "Required:\n"
    << "  --cubemap-dir <dir>   Cubemap directory containing posx/negx/... faces (kept for compatibility)\n"
    << "  --output <path>       Output BRDF LUT image path (.png or .ppm)\n\n"
        << "Options:\n"
        << "  --width <int>         LUT width (default: 128)\n"
        << "  --height <int>        LUT height (default: 128)\n"
    << "  --samples <int>       BRDF integration sample count (default: 64)\n"
    << "  --blur-min <float>    Legacy option, ignored\n"
    << "  --blur-max <float>    Legacy option, ignored\n"
        << "  --help                Show this message\n";
}

/**
 * @brief 读取并前进到下一个命令行参数。
 * @return 成功读取返回 true，否则返回 false。
 */
bool TakeNextArgument(int argc, char* argv[], int& index, std::string& outValue)
{
    if (index + 1 >= argc) {
        return false;
    }
    ++index;
    outValue = argv[index];
    return true;
}

/**
 * @brief 解析整数参数。
 * @return 解析成功返回 true，否则返回 false。
 */
bool ParseIntValue(const std::string& text, int& outValue)
{
    try {
        outValue = std::stoi(text);
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * @brief 解析浮点参数。
 * @return 解析成功返回 true，否则返回 false。
 */
bool ParseFloatValue(const std::string& text, float& outValue)
{
    try {
        outValue = std::stof(text);
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * @brief 解析命令行参数到离线配置。
 * @return 解析成功返回 true；若参数无效返回 false。
 */
bool ParseCommandLine(int argc, char* argv[], LutBakerCommandLine& outConfig)
{
    if (argc <= 1) {
        PrintUsage();
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return false;
        }

        std::string value;
        if (arg == "--cubemap-dir") {
            if (!TakeNextArgument(argc, argv, i, value)) {
                std::cerr << "Missing value for --cubemap-dir\n";
                return false;
            }
            outConfig.cubemapDirectory = value;
            continue;
        }

        if (arg == "--output") {
            if (!TakeNextArgument(argc, argv, i, value)) {
                std::cerr << "Missing value for --output\n";
                return false;
            }
            outConfig.outputPath = value;
            continue;
        }

        if (arg == "--width") {
            if (!TakeNextArgument(argc, argv, i, value) || !ParseIntValue(value, outConfig.buildConfig.width)) {
                std::cerr << "Invalid value for --width\n";
                return false;
            }
            continue;
        }

        if (arg == "--height") {
            if (!TakeNextArgument(argc, argv, i, value) || !ParseIntValue(value, outConfig.buildConfig.height)) {
                std::cerr << "Invalid value for --height\n";
                return false;
            }
            continue;
        }

        if (arg == "--samples") {
            if (!TakeNextArgument(argc, argv, i, value) || !ParseIntValue(value, outConfig.buildConfig.sampleCount)) {
                std::cerr << "Invalid value for --samples\n";
                return false;
            }
            continue;
        }

        if (arg == "--blur-min") {
            if (!TakeNextArgument(argc, argv, i, value) || !ParseFloatValue(value, outConfig.buildConfig.blurRadiusMin)) {
                std::cerr << "Invalid value for --blur-min\n";
                return false;
            }
            continue;
        }

        if (arg == "--blur-max") {
            if (!TakeNextArgument(argc, argv, i, value) || !ParseFloatValue(value, outConfig.buildConfig.blurRadiusMax)) {
                std::cerr << "Invalid value for --blur-max\n";
                return false;
            }
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        return false;
    }

    if (outConfig.cubemapDirectory.empty() || outConfig.outputPath.empty()) {
        std::cerr << "Both --cubemap-dir and --output are required\n";
        PrintUsage();
        return false;
    }

    return true;
}

} // namespace

/**
 * @brief 离线执行天空盒模糊采样并输出 LUT 图像。
 */
int main(int argc, char* argv[])
{
    LutBakerCommandLine config;
    if (!ParseCommandLine(argc, argv, config)) {
        return 1;
    }

    CubemapTexture cubemap;
    if (!cubemap.loadFromDirectory(config.cubemapDirectory)) {
        std::cerr << "Failed to load cubemap from: " << config.cubemapDirectory << "\n";
        return 1;
    }

    const utility::SkyboxLutImage lutImage = utility::SkyboxLutGenerator::GenerateFromSkybox(cubemap, config.buildConfig);
    if (!lutImage.valid()) {
        std::cerr << "Failed to generate LUT image\n";
        return 1;
    }

    // 输出前自动创建目录，方便直接把结果写到指定离线路径。
    const std::filesystem::path outputPath(config.outputPath);
    std::error_code ec;
    const std::filesystem::path parentPath = outputPath.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath, ec);
    }

    const bool saveOk = IsPngOutputPath(config.outputPath)
        ? utility::SkyboxLutGenerator::SaveAsPNG(lutImage, config.outputPath)
        : utility::SkyboxLutGenerator::SaveAsPPM(lutImage, config.outputPath);

    if (!saveOk) {
        std::cerr << "Failed to save LUT image: " << config.outputPath << "\n";
        return 1;
    }

    std::cout
        << "LUT baked successfully\n"
        << "  Cubemap: " << config.cubemapDirectory << "\n"
        << "  Output : " << config.outputPath << "\n"
        << "  Size   : " << lutImage.width << "x" << lutImage.height << "\n"
        << "  Samples: " << config.buildConfig.sampleCount << "\n";

    return 0;
}
