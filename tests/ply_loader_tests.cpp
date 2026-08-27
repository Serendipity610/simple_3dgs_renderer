#include "simple_3dgs/ply_loader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireNear(float actual, float expected, const std::string& message)
{
    if (std::abs(actual - expected) > 1.0e-5F) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

class TemporaryFile {
public:
    explicit TemporaryFile(std::string name)
        : path_(std::filesystem::temp_directory_path() / std::move(name))
    {
    }

    ~TemporaryFile()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

template<typename T>
void WriteBinary(std::ostream& output, T value, bool bigEndian)
{
    std::array<uint8_t, sizeof(T)> bytes {};
    std::memcpy(bytes.data(), &value, sizeof(value));
    const uint16_t endianProbe = 1;
    const bool hostLittleEndian = *reinterpret_cast<const uint8_t*>(&endianProbe) == 1;
    if (bigEndian == hostLittleEndian) {
        std::reverse(bytes.begin(), bytes.end());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void TestAsciiRgb()
{
    const std::filesystem::path file =
        std::filesystem::path(SIMPLE_3DGS_TEST_DATA_DIR) / "gaussians_ascii.ply";
    const auto gaussians = simple_3dgs::PlyLoader::Load(file);
    Require(gaussians.size() == 3, "ASCII vertex count");
    RequireNear(gaussians[0].position[0], -1.4F, "ASCII position");
    RequireNear(gaussians[0].scale[1], -1.6F, "ASCII scale");
    RequireNear(gaussians[0].rotation[0], 1.0F, "ASCII rotation");
    RequireNear(gaussians[0].opacity, 2.0F, "ASCII opacity");
    RequireNear(gaussians[0].color[0], 1.0F, "ASCII red");
    RequireNear(gaussians[0].color[1], 64.0F / 255.0F, "ASCII green");
    RequireNear(gaussians[2].color[2], 1.0F, "ASCII blue");
    Require(gaussians[0].shDegree == -1, "direct RGB must disable SH evaluation");
}

void TestBinarySh(bool bigEndian)
{
    TemporaryFile file(bigEndian ? "simple_3dgs_binary_be_test.ply"
                                 : "simple_3dgs_binary_le_test.ply");
    {
        std::ofstream output(file.Path(), std::ios::binary);
        output << "ply\nformat "
               << (bigEndian ? "binary_big_endian" : "binary_little_endian")
               << " 1.0\nelement vertex 1\n"
                  "property float x\nproperty float y\nproperty float z\n"
                  "property float scale_0\nproperty float scale_1\nproperty float scale_2\n"
                  "property float rot_0\nproperty float rot_1\n"
                  "property float rot_2\nproperty float rot_3\n"
                  "property float opacity\n"
                  "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n";
        for (size_t index = 0; index < 45; ++index) {
            output << "property float f_rest_" << index << '\n';
        }
        output << "end_header\n";
        const std::array<float, 14> values = {
            1.25F, -2.5F, 3.75F, -1.0F, -2.0F, -3.0F, 1.0F,
            0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 1.0F, -1.0F};
        for (float value : values) {
            WriteBinary(output, value, bigEndian);
        }
        for (size_t index = 0; index < 45; ++index) {
            WriteBinary(output, static_cast<float>(index + 1), bigEndian);
        }
    }

    const auto gaussians = simple_3dgs::PlyLoader::Load(file.Path());
    Require(gaussians.size() == 1, "binary vertex count");
    RequireNear(gaussians[0].position[1], -2.5F, "binary position");
    RequireNear(gaussians[0].scale[2], -3.0F, "binary scale");
    RequireNear(gaussians[0].opacity, 2.0F, "binary opacity");
    RequireNear(gaussians[0].color[0], 0.5F, "binary SH red");
    RequireNear(gaussians[0].color[1], 0.5F + 0.28209479177387814F,
                "binary SH green");
    RequireNear(gaussians[0].color[2], 0.5F - 0.28209479177387814F,
                "binary SH blue");
    Require(gaussians[0].shDegree == 3, "complete SH coefficients select degree 3");
    RequireNear(gaussians[0].shCoefficients[0], 0.0F, "SH DC red mapping");
    RequireNear(gaussians[0].shCoefficients[1], 1.0F, "SH DC green mapping");
    RequireNear(gaussians[0].shCoefficients[2], -1.0F, "SH DC blue mapping");
    RequireNear(gaussians[0].shCoefficients[3], 1.0F, "SH degree 1 red mapping");
    RequireNear(gaussians[0].shCoefficients[4], 16.0F,
                "SH degree 1 green mapping");
    RequireNear(gaussians[0].shCoefficients[5], 31.0F,
                "SH degree 1 blue mapping");
    RequireNear(gaussians[0].shCoefficients[45], 15.0F,
                "SH degree 3 red mapping");
    RequireNear(gaussians[0].shCoefficients[47], 45.0F,
                "SH degree 3 blue mapping");
}

void TestRejectsMissingPosition()
{
    TemporaryFile file("simple_3dgs_invalid_test.ply");
    {
        std::ofstream output(file.Path());
        output << "ply\nformat ascii 1.0\nelement vertex 1\n"
                  "property float x\nproperty float y\nend_header\n1 2\n";
    }
    bool rejected = false;
    try {
        static_cast<void>(simple_3dgs::PlyLoader::Load(file.Path()));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    Require(rejected, "missing z property must be rejected");
}

void TestBatchBoundaries()
{
    const std::filesystem::path file =
        std::filesystem::path(SIMPLE_3DGS_TEST_DATA_DIR) / "gaussians_ascii.ply";
    std::vector<simple_3dgs::Gaussian> collected;
    const auto statistics = simple_3dgs::PlyLoader::LoadBatches(
        file, 2, [&](const simple_3dgs::Gaussian* values, size_t count) {
            collected.insert(collected.end(), values, values + count);
        });
    Require(statistics.gaussianCount == 3, "batch gaussian count");
    Require(statistics.batchCount == 2, "batch count");
    Require(collected.size() == 3, "batch callback count");
    RequireNear(collected.front().position[0], -1.4F, "batch first record");
    RequireNear(collected.back().position[0], 1.4F, "batch last record");
}

void WriteSortFreeHeader(std::ostream& output, const std::string& format,
                         size_t vertexCount, bool includeLastCoefficient = true)
{
    output << "ply\nformat " << format << " 1.0\nelement vertex " << vertexCount
           << "\nproperty float x\nproperty float y\nproperty float z\n"
              "property float f_do_0\n";
    const size_t count = includeLastCoefficient ? 15 : 14;
    for (size_t index = 0; index < count; ++index) {
        output << "property float f_ro_" << index << '\n';
    }
    output << "property float info\nend_header\n";
}

void TestSortFreeAsciiMetadata()
{
    const std::filesystem::path file =
        std::filesystem::path(SIMPLE_3DGS_TEST_DATA_DIR) /
        "gaussians_sort_free_ascii.ply";
    simple_3dgs::PlyModelMetadata metadata;
    const auto gaussians = simple_3dgs::PlyLoader::Load(file, nullptr, &metadata);
    Require(metadata.supportsSortFree, "complete sort-free ASCII header");
    RequireNear(metadata.weightBackground, 0.02F, "sort-free background weight");
    RequireNear(metadata.sigma, 10.0F, "sort-free sigma");
    RequireNear(gaussians[0].opacityShCoefficients[0], 0.35449077F,
                "sort-free opacity SH DC");
    RequireNear(gaussians[0].opacityShCoefficients[15], 0.0F,
                "sort-free opacity SH degree 3");
}

void TestSortFreeBinaryMetadata()
{
    TemporaryFile file("simple_3dgs_sort_free_binary_test.ply");
    {
        std::ofstream output(file.Path(), std::ios::binary);
        WriteSortFreeHeader(output, "binary_little_endian", 2);
        for (size_t record = 0; record < 2; ++record) {
            WriteBinary(output, static_cast<float>(record), false);
            WriteBinary(output, 0.0F, false);
            WriteBinary(output, 1.0F, false);
            for (size_t coefficient = 0; coefficient < 16; ++coefficient) {
                WriteBinary(output,
                            static_cast<float>(record * 100 + coefficient), false);
            }
            WriteBinary(output, record == 0 ? 0.03F : 12.0F, false);
        }
    }
    simple_3dgs::PlyModelMetadata metadata;
    const auto gaussians = simple_3dgs::PlyLoader::Load(file.Path(), nullptr,
                                                        &metadata);
    Require(metadata.supportsSortFree, "complete sort-free binary header");
    RequireNear(metadata.weightBackground, 0.03F, "binary background weight");
    RequireNear(metadata.sigma, 12.0F, "binary sigma");
    RequireNear(gaussians[1].opacityShCoefficients[15], 115.0F,
                "binary opacity SH mapping");
}

void TestSortFreeUnsupportedInputs()
{
    TemporaryFile missing("simple_3dgs_sort_free_missing_test.ply");
    {
        std::ofstream output(missing.Path());
        WriteSortFreeHeader(output, "ascii", 2, false);
        for (size_t record = 0; record < 2; ++record) {
            output << "0 0 1";
            for (size_t value = 0; value < 15; ++value) output << " 0";
            output << (record == 0 ? " 0.02\n" : " 10\n");
        }
    }
    simple_3dgs::PlyModelMetadata metadata;
    static_cast<void>(simple_3dgs::PlyLoader::Load(missing.Path(), nullptr,
                                                   &metadata));
    Require(!metadata.supportsSortFree, "missing opacity SH must be unsupported");
    Require(metadata.sortFreeDiagnostic.find("f_ro_14") != std::string::npos,
            "missing property diagnostic");

    TemporaryFile invalid("simple_3dgs_sort_free_invalid_sigma_test.ply");
    {
        std::ofstream output(invalid.Path());
        WriteSortFreeHeader(output, "ascii", 2);
        for (size_t record = 0; record < 2; ++record) {
            output << "0 0 1";
            for (size_t value = 0; value < 16; ++value) output << " 0";
            output << (record == 0 ? " 0.02\n" : " 0\n");
        }
    }
    static_cast<void>(simple_3dgs::PlyLoader::Load(invalid.Path(), nullptr,
                                                   &metadata));
    Require(!metadata.supportsSortFree, "non-positive sigma must be unsupported");
    Require(metadata.sortFreeDiagnostic.find("sigma") != std::string::npos,
            "invalid sigma diagnostic");

    TemporaryFile tooSmall("simple_3dgs_sort_free_too_small_test.ply");
    {
        std::ofstream output(tooSmall.Path());
        WriteSortFreeHeader(output, "ascii", 1);
        output << "0 0 1";
        for (size_t value = 0; value < 16; ++value) output << " 0";
        output << " 0.02\n";
    }
    static_cast<void>(simple_3dgs::PlyLoader::Load(tooSmall.Path(), nullptr,
                                                   &metadata));
    Require(!metadata.supportsSortFree, "single vertex must be unsupported");
    Require(metadata.sortFreeDiagnostic.find("two vertices") != std::string::npos,
            "minimum vertex diagnostic");

    TemporaryFile invalidBackground(
        "simple_3dgs_sort_free_invalid_background_test.ply");
    {
        std::ofstream output(invalidBackground.Path());
        WriteSortFreeHeader(output, "ascii", 2);
        for (size_t record = 0; record < 2; ++record) {
            output << "0 0 1";
            for (size_t value = 0; value < 16; ++value) output << " 0";
            output << (record == 0 ? " -0.1\n" : " 10\n");
        }
    }
    static_cast<void>(simple_3dgs::PlyLoader::Load(
        invalidBackground.Path(), nullptr, &metadata));
    Require(!metadata.supportsSortFree,
            "negative background weight must be unsupported");
    Require(metadata.sortFreeDiagnostic.find("background") != std::string::npos,
            "invalid background diagnostic");
}

} // namespace

int main()
{
    try {
        TestAsciiRgb();
        TestBinarySh(false);
        TestBinarySh(true);
        TestRejectsMissingPosition();
        TestBatchBoundaries();
        TestSortFreeAsciiMetadata();
        TestSortFreeBinaryMetadata();
        TestSortFreeUnsupportedInputs();
        std::cout << "PLY loader tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PLY loader test failure: " << error.what() << '\n';
        return 1;
    }
}
