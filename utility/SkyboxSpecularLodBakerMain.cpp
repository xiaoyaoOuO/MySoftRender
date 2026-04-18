#include "Texture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <stb_image_write.h>

namespace {

constexpr int kSpecularLodCount = 6;

/**
 * @brief 固定的六个立方体面命名，输出时直接使用这些文件名。
 */
constexpr std::array<const char*, 6> kFaceNames = {
    "posx.png",
    "negx.png",
    "posy.png",
    "negy.png",
    "posz.png",
    "negz.png"
};

/**
 * @brief 方向卷积离线工具参数。
 */
struct SpecularLodBakerConfig
{
    std::string cubemapDirectory;
    std::string cubemapRoot;
    std::string outputRoot;
    int lodCount = kSpecularLodCount;
    int size = 64;
    // lod0 约定为无模糊，lod1 开始逐级加大卷积锥角。
    std::array<int, kSpecularLodCount> sampleCounts = {1, 64, 96, 128, 160, 192};
    std::array<float, kSpecularLodCount> coneAnglesDeg = {0.0f, 10.0f, 18.0f, 30.0f, 45.0f, 62.0f};
    bool hasSeed = false;
    std::uint32_t seed = 0u;
};

/**
 * @brief 打印命令行参数帮助。
 */
void PrintUsage()
{
    std::cout
        << "Usage:\n"
        << "  skyboxSpecularLodBaker --cubemap-dir <dir> [--output-root <dir>] [options]\n"
        << "  skyboxSpecularLodBaker --cubemap-root <dir> [options]\n\n"
        << "Required (choose one mode):\n"
        << "  --cubemap-dir <dir>    Bake one skybox directory\n"
        << "  --cubemap-root <dir>   Bake all skybox sub-directories under root\n\n"
        << "Options:\n"
        << "  --output-root <dir>    Output root for single skybox mode\n"
        << "  --lod-count <int>      Must be 6 in current task (default: 6)\n"
        << "  --size <int>           Face size per lod (default: 64)\n"
        << "  --samples <csv>        Sample counts for lod0~lod5 (default: 1,64,96,128,160,192)\n"
        << "  --angles-deg <csv>     Cone angles(deg) for lod0~lod5 (default: 0,10,18,30,45,62)\n"
        << "  --seed <uint>          Optional deterministic jitter seed\n"
        << "  --help                 Show this message\n\n"
        << "Output layout:\n"
        << "  <SkyboxDir>/<SkyboxName>_cov/lod0~lod5/{posx,negx,posy,negy,posz,negz}.png\n";
}

/**
 * @brief 读取下一个参数值。
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
 * @brief 解析无符号整数参数。
 */
bool ParseUIntValue(const std::string& text, std::uint32_t& outValue)
{
    try {
        const unsigned long parsed = std::stoul(text);
        outValue = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * @brief 解析浮点参数。
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
 * @brief 按逗号切分参数列表，并去除空项。
 */
std::vector<std::string> SplitCsv(const std::string& text)
{
    std::vector<std::string> values;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            values.push_back(item);
        }
    }
    return values;
}

/**
 * @brief 解析固定长度的整数 CSV 列表。
 */
bool ParseIntList(const std::string& text, std::array<int, kSpecularLodCount>& outValues)
{
    const std::vector<std::string> values = SplitCsv(text);
    if (values.size() != outValues.size()) {
        return false;
    }

    for (std::size_t i = 0; i < outValues.size(); ++i) {
        int parsed = 0;
        if (!ParseIntValue(values[i], parsed)) {
            return false;
        }
        outValues[i] = parsed;
    }

    return true;
}

/**
 * @brief 解析固定长度的浮点 CSV 列表。
 */
bool ParseFloatList(const std::string& text, std::array<float, kSpecularLodCount>& outValues)
{
    const std::vector<std::string> values = SplitCsv(text);
    if (values.size() != outValues.size()) {
        return false;
    }

    for (std::size_t i = 0; i < outValues.size(); ++i) {
        float parsed = 0.0f;
        if (!ParseFloatValue(values[i], parsed)) {
            return false;
        }
        outValues[i] = parsed;
    }

    return true;
}

/**
 * @brief 解析命令行参数。
 */
bool ParseCommandLine(int argc, char* argv[], SpecularLodBakerConfig& outConfig)
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

        if (arg == "--cubemap-root") {
            if (!TakeNextArgument(argc, argv, i, value)) {
                std::cerr << "Missing value for --cubemap-root\n";
                return false;
            }
            outConfig.cubemapRoot = value;
            continue;
        }

        if (arg == "--output-root") {
            if (!TakeNextArgument(argc, argv, i, value)) {
                std::cerr << "Missing value for --output-root\n";
                return false;
            }
            outConfig.outputRoot = value;
            continue;
        }

        if (arg == "--lod-count") {
            if (!TakeNextArgument(argc, argv, i, value) || !ParseIntValue(value, outConfig.lodCount)) {
                std::cerr << "Invalid value for --lod-count\n";
                return false;
            }
            continue;
        }

        if (arg == "--size") {
            if (!TakeNextArgument(argc, argv, i, value) || !ParseIntValue(value, outConfig.size)) {
                std::cerr << "Invalid value for --size\n";
                return false;
            }
            continue;
        }

        if (arg == "--samples") {
            if (!TakeNextArgument(argc, argv, i, value)
                || !ParseIntList(value, outConfig.sampleCounts)) {
                std::cerr << "Invalid value for --samples, expect 6 csv ints\n";
                return false;
            }
            continue;
        }

        if (arg == "--angles-deg") {
            if (!TakeNextArgument(argc, argv, i, value)
                || !ParseFloatList(value, outConfig.coneAnglesDeg)) {
                std::cerr << "Invalid value for --angles-deg, expect 6 csv floats\n";
                return false;
            }
            continue;
        }

        if (arg == "--seed") {
            if (!TakeNextArgument(argc, argv, i, value) || !ParseUIntValue(value, outConfig.seed)) {
                std::cerr << "Invalid value for --seed\n";
                return false;
            }
            outConfig.hasSeed = true;
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        return false;
    }

    const bool hasSingleMode = !outConfig.cubemapDirectory.empty();
    const bool hasBatchMode = !outConfig.cubemapRoot.empty();
    if (hasSingleMode == hasBatchMode) {
        std::cerr << "Choose exactly one mode: --cubemap-dir or --cubemap-root\n";
        return false;
    }

    if (outConfig.lodCount != kSpecularLodCount) {
        std::cerr << "Current task fixes lod-count to 6\n";
        return false;
    }

    if (outConfig.size <= 0) {
        std::cerr << "--size must be > 0\n";
        return false;
    }

    for (int i = 0; i < kSpecularLodCount; ++i) {
        if (outConfig.sampleCounts[static_cast<std::size_t>(i)] <= 0) {
            std::cerr << "sample count must be > 0 at lod" << i << "\n";
            return false;
        }
        if (outConfig.coneAnglesDeg[static_cast<std::size_t>(i)] < 0.0f) {
            std::cerr << "cone angle must be >= 0 at lod" << i << "\n";
            return false;
        }
    }

    return true;
}

/**
 * @brief Van der Corput 低差异序列（base-2）。
 */
float RadicalInverseVdC(std::uint32_t bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

/**
 * @brief Hammersley 2D 采样点。
 */
glm::vec2 Hammersley(std::uint32_t sampleIndex, std::uint32_t sampleCount)
{
    const float x = (sampleCount > 0u)
        ? static_cast<float>(sampleIndex) / static_cast<float>(sampleCount)
        : 0.0f;
    const float y = RadicalInverseVdC(sampleIndex);
    return glm::vec2(x, y);
}

/**
 * @brief 基于方向向量构建切线空间基。
 */
void BuildDirectionBasis(const glm::vec3& centerDir, glm::vec3& tangent, glm::vec3& bitangent)
{
    const glm::vec3 helperUp = (std::abs(centerDir.y) < 0.999f)
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);

    tangent = glm::normalize(glm::cross(helperUp, centerDir));
    bitangent = glm::normalize(glm::cross(centerDir, tangent));
}

/**
 * @brief 把立方体面上的 uv 映射回世界方向，保持与运行时 CubemapTexture 采样约定一致。
 */
glm::vec3 FaceUvToDirection(int faceIndex, float u, float v)
{
    const float localU = std::clamp(u * 2.0f - 1.0f, -1.0f, 1.0f);
    const float localV = std::clamp(v * 2.0f - 1.0f, -1.0f, 1.0f);

    glm::vec3 direction(0.0f, 0.0f, 1.0f);
    switch (faceIndex) {
    case 0: // +X
        direction = glm::vec3(1.0f, -localV, -localU);
        break;
    case 1: // -X
        direction = glm::vec3(-1.0f, -localV, localU);
        break;
    case 2: // +Y
        direction = glm::vec3(localU, 1.0f, localV);
        break;
    case 3: // -Y
        direction = glm::vec3(localU, -1.0f, -localV);
        break;
    case 4: // +Z
        direction = glm::vec3(localU, -localV, 1.0f);
        break;
    case 5: // -Z
        direction = glm::vec3(-localU, -localV, -1.0f);
        break;
    default:
        break;
    }

    if (glm::dot(direction, direction) <= 1e-12f) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return glm::normalize(direction);
}

/**
 * @brief 浮点颜色转 8-bit。
 */
std::uint8_t ToByte(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::round(clamped * 255.0f));
}

/**
 * @brief 计算单方向卷积颜色（方向空间圆锥采样）。
 */
glm::vec3 ConvolveDirection(
    const CubemapTexture& inputCubemap,
    const glm::vec3& normal,
    int sampleCount,
    float coneAngleRad,
    float rotation)
{
    const glm::vec3 safeNormal = (glm::dot(normal, normal) > 1e-12f)
        ? glm::normalize(normal)
        : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 tangent(1.0f, 0.0f, 0.0f);
    glm::vec3 bitangent(0.0f, 0.0f, 1.0f);
    BuildDirectionBasis(safeNormal, tangent, bitangent);

    const float safeCone = std::clamp(coneAngleRad, 0.0f, glm::half_pi<float>());
    const float cosMin = std::cos(safeCone);

    glm::vec3 accumulatedColor(0.0f);
    float accumulatedWeight = 0.0f;

    for (int i = 0; i < sampleCount; ++i) {
        const glm::vec2 xi = Hammersley(
            static_cast<std::uint32_t>(i),
            static_cast<std::uint32_t>(sampleCount));

        // 在指定圆锥内均匀分布采样，使用余弦权重近似漫反射卷积。
        const float phi = 2.0f * glm::pi<float>() * std::fmod(xi.x + rotation, 1.0f);
        const float cosTheta = glm::mix(1.0f, cosMin, xi.y);
        const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));

        const glm::vec3 localSample(
            std::cos(phi) * sinTheta,
            std::sin(phi) * sinTheta,
            cosTheta);

        glm::vec3 sampleDir = tangent * localSample.x
            + bitangent * localSample.y
            + safeNormal * localSample.z;

        if (glm::dot(sampleDir, sampleDir) <= 1e-12f) {
            continue;
        }
        sampleDir = glm::normalize(sampleDir);

        const float weight = std::max(localSample.z, 0.001f);
        accumulatedColor += inputCubemap.sample(sampleDir) * weight;
        accumulatedWeight += weight;
    }

    if (accumulatedWeight <= 1e-6f) {
        return inputCubemap.sample(safeNormal);
    }
    return accumulatedColor / accumulatedWeight;
}

/**
 * @brief 计算每个 LOD 的稳定旋转偏移，避免固定采样形状导致条纹。
 */
float BuildLodRotation(const SpecularLodBakerConfig& config, int lodIndex)
{
    if (!config.hasSeed) {
        return 0.0f;
    }

    // 简单整数 hash，确保同一 seed 下各 LOD 可复现。
    std::uint32_t state = config.seed ^ (0x9E3779B9u + static_cast<std::uint32_t>(lodIndex) * 0x85EBCA6Bu);
    state ^= (state >> 16);
    state *= 0x7FEB352Du;
    state ^= (state >> 15);
    state *= 0x846CA68Bu;
    state ^= (state >> 16);

    const float normalized = static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    return normalized;
}

/**
 * @brief 对单个天空盒执行六级卷积并输出 PNG。
 */
bool BakeSingleSkybox(
    const SpecularLodBakerConfig& config,
    const std::filesystem::path& cubemapDir,
    const std::filesystem::path& outputRoot,
    std::string& outError)
{
    CubemapTexture inputCubemap;
    if (!inputCubemap.loadFromDirectory(cubemapDir.string())) {
        outError = "failed to load cubemap faces";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::create_directories(outputRoot, ec) && ec) {
        outError = "failed to create output root";
        return false;
    }

    const int outputSize = std::max(config.size, 1);

    for (int lod = 0; lod < kSpecularLodCount; ++lod) {
        const int sampleCount = std::max(config.sampleCounts[static_cast<std::size_t>(lod)], 1);
        const float coneAngleDeg = std::max(config.coneAnglesDeg[static_cast<std::size_t>(lod)], 0.0f);
        const float coneAngleRad = coneAngleDeg * (glm::pi<float>() / 180.0f);
        const float rotation = BuildLodRotation(config, lod);

        const std::filesystem::path lodPath = outputRoot / ("lod" + std::to_string(lod));
        if (!std::filesystem::create_directories(lodPath, ec) && ec) {
            outError = "failed to create lod directory";
            return false;
        }

        for (int faceIndex = 0; faceIndex < static_cast<int>(kFaceNames.size()); ++faceIndex) {
            std::vector<std::uint8_t> facePixels(
                static_cast<std::size_t>(outputSize) * static_cast<std::size_t>(outputSize) * 3u,
                0u);

            for (int y = 0; y < outputSize; ++y) {
                for (int x = 0; x < outputSize; ++x) {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(outputSize);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(outputSize);
                    const glm::vec3 normal = FaceUvToDirection(faceIndex, u, v);

                    const glm::vec3 color = ConvolveDirection(
                        inputCubemap,
                        normal,
                        sampleCount,
                        coneAngleRad,
                        rotation);

                    const std::size_t pixelIndex =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(outputSize)
                        + static_cast<std::size_t>(x)) * 3u;

                    facePixels[pixelIndex + 0u] = ToByte(color.r);
                    facePixels[pixelIndex + 1u] = ToByte(color.g);
                    facePixels[pixelIndex + 2u] = ToByte(color.b);
                }
            }

            const std::filesystem::path outputFile = lodPath / kFaceNames[static_cast<std::size_t>(faceIndex)];
            const int writeOk = stbi_write_png(
                outputFile.string().c_str(),
                outputSize,
                outputSize,
                3,
                facePixels.data(),
                outputSize * 3);

            if (writeOk == 0) {
                outError = "failed to write png: " + outputFile.string();
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief 单天空盒模式下解析默认输出目录（<Skybox>/<SkyboxName>_cov）。
 */
std::filesystem::path ResolveSingleOutputRoot(
    const SpecularLodBakerConfig& config,
    const std::filesystem::path& cubemapDir)
{
    if (!config.outputRoot.empty()) {
        return std::filesystem::path(config.outputRoot);
    }

    const std::string skyboxName = cubemapDir.filename().string();
    return cubemapDir / (skyboxName + "_cov");
}

/**
 * @brief 处理单个天空盒目录。
 */
int RunSingleMode(const SpecularLodBakerConfig& config)
{
    const std::filesystem::path cubemapDir(config.cubemapDirectory);
    std::error_code ec;
    if (!std::filesystem::exists(cubemapDir, ec) || ec || !std::filesystem::is_directory(cubemapDir, ec) || ec) {
        std::cerr << "Invalid --cubemap-dir: " << cubemapDir.string() << "\n";
        return 1;
    }

    const std::filesystem::path outputRoot = ResolveSingleOutputRoot(config, cubemapDir);

    std::string error;
    if (!BakeSingleSkybox(config, cubemapDir, outputRoot, error)) {
        std::cerr << "Bake failed: " << cubemapDir.string() << "\n";
        std::cerr << "Reason: " << error << "\n";
        return 1;
    }

    std::cout << "Specular LOD bake success\n";
    std::cout << "  Skybox : " << cubemapDir.string() << "\n";
    std::cout << "  Output : " << outputRoot.string() << "\n";
    for (int i = 0; i < kSpecularLodCount; ++i) {
        std::cout << "  LOD" << i
                  << " -> samples=" << config.sampleCounts[static_cast<std::size_t>(i)]
                  << ", angleDeg=" << config.coneAnglesDeg[static_cast<std::size_t>(i)]
                  << "\n";
    }

    return 0;
}

/**
 * @brief 批量扫描根目录下所有天空盒并逐个烘焙。
 */
int RunBatchMode(const SpecularLodBakerConfig& config)
{
    const std::filesystem::path cubemapRoot(config.cubemapRoot);
    std::error_code ec;
    if (!std::filesystem::exists(cubemapRoot, ec) || ec || !std::filesystem::is_directory(cubemapRoot, ec) || ec) {
        std::cerr << "Invalid --cubemap-root: " << cubemapRoot.string() << "\n";
        return 1;
    }

    int successCount = 0;
    int failureCount = 0;
    std::vector<std::string> failedSkyboxes;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(cubemapRoot, ec)) {
        if (ec) {
            break;
        }

        if (!entry.is_directory(ec) || ec) {
            ec.clear();
            continue;
        }

        const std::filesystem::path skyboxDir = entry.path();
        const std::string skyboxName = skyboxDir.filename().string();
        const std::filesystem::path outputRoot = skyboxDir / (skyboxName + "_cov");

        std::string error;
        const bool baked = BakeSingleSkybox(config, skyboxDir, outputRoot, error);
        if (baked) {
            ++successCount;
            std::cout << "[OK] " << skyboxName << " -> " << outputRoot.string() << "\n";
        } else {
            ++failureCount;
            failedSkyboxes.push_back(skyboxName + " (" + error + ")");
            std::cout << "[FAIL] " << skyboxName << " -> " << error << "\n";
        }
    }

    std::cout << "\nBatch summary\n";
    std::cout << "  Success: " << successCount << "\n";
    std::cout << "  Failed : " << failureCount << "\n";

    if (!failedSkyboxes.empty()) {
        std::cout << "  Failure list:\n";
        for (const std::string& item : failedSkyboxes) {
            std::cout << "    - " << item << "\n";
        }
    }

    return (failureCount == 0) ? 0 : 1;
}

} // namespace

/**
 * @brief Specular 六级卷积离线入口。
 */
int main(int argc, char* argv[])
{
    SpecularLodBakerConfig config;
    if (!ParseCommandLine(argc, argv, config)) {
        return 1;
    }

    if (!config.cubemapDirectory.empty()) {
        return RunSingleMode(config);
    }
    return RunBatchMode(config);
}
