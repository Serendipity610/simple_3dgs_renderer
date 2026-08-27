#include "simple_3dgs/ply_loader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace simple_3dgs {
namespace {

enum class PlyFormat { Ascii, BinaryLittleEndian, BinaryBigEndian };
enum class ScalarType { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64 };

struct Property {
    std::string name;
    ScalarType valueType = ScalarType::Float32;
    bool isList = false;
    ScalarType listCountType = ScalarType::UInt8;
};

struct Element {
    std::string name;
    size_t count = 0;
    std::vector<Property> properties;
};

struct Header {
    PlyFormat format = PlyFormat::Ascii;
    bool hasFormat = false;
    std::vector<Element> elements;
};

enum class VertexOperation : uint8_t {
    Ignore, PositionX, PositionY, PositionZ, ScaleX, ScaleY, ScaleZ,
    RotationW, RotationX, RotationY, RotationZ, Opacity, Alpha,
    ColorR, ColorG, ColorB, ShDc0, ShDc1, ShDc2, ShRest,
    OpacityShDc, OpacityShRest, SortFreeInfo
};

struct CompiledProperty {
    const Property* property = nullptr;
    VertexOperation operation = VertexOperation::Ignore;
    size_t shRestIndex = 0;
};

[[nodiscard]] ScalarType ParseScalarType(const std::string& name)
{
    if (name == "char" || name == "int8") {
        return ScalarType::Int8;
    }
    if (name == "uchar" || name == "uint8") {
        return ScalarType::UInt8;
    }
    if (name == "short" || name == "int16") {
        return ScalarType::Int16;
    }
    if (name == "ushort" || name == "uint16") {
        return ScalarType::UInt16;
    }
    if (name == "int" || name == "int32") {
        return ScalarType::Int32;
    }
    if (name == "uint" || name == "uint32") {
        return ScalarType::UInt32;
    }
    if (name == "float" || name == "float32") {
        return ScalarType::Float32;
    }
    if (name == "double" || name == "float64") {
        return ScalarType::Float64;
    }
    throw std::runtime_error("unsupported PLY scalar type: " + name);
}

[[nodiscard]] Header ReadHeader(std::istream& input)
{
    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("PLY file is empty");
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line != "ply") {
        throw std::runtime_error("file does not start with the PLY signature");
    }

    Header header;
    Element* currentElement = nullptr;
    bool ended = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream tokens(line);
        std::string keyword;
        tokens >> keyword;
        if (keyword.empty() || keyword == "comment" || keyword == "obj_info") {
            continue;
        }
        if (keyword == "format") {
            std::string format;
            std::string version;
            if (!(tokens >> format >> version) || version != "1.0") {
                throw std::runtime_error("invalid PLY format declaration");
            }
            if (format == "ascii") {
                header.format = PlyFormat::Ascii;
            } else if (format == "binary_little_endian") {
                header.format = PlyFormat::BinaryLittleEndian;
            } else if (format == "binary_big_endian") {
                header.format = PlyFormat::BinaryBigEndian;
            } else {
                throw std::runtime_error("unsupported PLY format: " + format);
            }
            header.hasFormat = true;
        } else if (keyword == "element") {
            std::string name;
            uint64_t count = 0;
            if (!(tokens >> name >> count) ||
                count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
                throw std::runtime_error("invalid PLY element declaration");
            }
            header.elements.push_back({name, static_cast<size_t>(count), {}});
            currentElement = &header.elements.back();
        } else if (keyword == "property") {
            if (currentElement == nullptr) {
                throw std::runtime_error("PLY property declared before an element");
            }
            std::string type;
            if (!(tokens >> type)) {
                throw std::runtime_error("invalid PLY property declaration");
            }
            Property property;
            if (type == "list") {
                std::string countType;
                std::string valueType;
                if (!(tokens >> countType >> valueType >> property.name)) {
                    throw std::runtime_error("invalid PLY list property declaration");
                }
                property.isList = true;
                property.listCountType = ParseScalarType(countType);
                property.valueType = ParseScalarType(valueType);
            } else {
                if (!(tokens >> property.name)) {
                    throw std::runtime_error("invalid PLY scalar property declaration");
                }
                property.valueType = ParseScalarType(type);
            }
            currentElement->properties.push_back(std::move(property));
        } else if (keyword == "end_header") {
            ended = true;
            break;
        } else {
            throw std::runtime_error("unsupported PLY header directive: " + keyword);
        }
    }

    if (!ended || !header.hasFormat) {
        throw std::runtime_error("incomplete PLY header");
    }
    const auto vertex = std::find_if(header.elements.begin(), header.elements.end(),
                                     [](const Element& element) {
                                         return element.name == "vertex";
                                     });
    if (vertex == header.elements.end()) {
        throw std::runtime_error("PLY contains no vertex element");
    }
    return header;
}

[[nodiscard]] bool HostIsLittleEndian()
{
    const uint16_t value = 1;
    return *reinterpret_cast<const uint8_t*>(&value) == 1;
}

template<typename T>
[[nodiscard]] T ReadBinaryValue(std::istream& input, bool swapBytes)
{
    static_assert(std::is_trivially_copyable_v<T>);
    std::array<uint8_t, sizeof(T)> bytes {};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        throw std::runtime_error("unexpected end of binary PLY data");
    }
    if (swapBytes) {
        std::reverse(bytes.begin(), bytes.end());
    }
    T value {};
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

[[nodiscard]] double ReadBinaryScalar(std::istream& input, ScalarType type, bool swapBytes)
{
    switch (type) {
        case ScalarType::Int8:
            return ReadBinaryValue<int8_t>(input, false);
        case ScalarType::UInt8:
            return ReadBinaryValue<uint8_t>(input, false);
        case ScalarType::Int16:
            return ReadBinaryValue<int16_t>(input, swapBytes);
        case ScalarType::UInt16:
            return ReadBinaryValue<uint16_t>(input, swapBytes);
        case ScalarType::Int32:
            return ReadBinaryValue<int32_t>(input, swapBytes);
        case ScalarType::UInt32:
            return ReadBinaryValue<uint32_t>(input, swapBytes);
        case ScalarType::Float32:
            return ReadBinaryValue<float>(input, swapBytes);
        case ScalarType::Float64:
            return ReadBinaryValue<double>(input, swapBytes);
    }
    throw std::runtime_error("unreachable PLY scalar type");
}

[[nodiscard]] double ReadAsciiScalar(std::istream& input)
{
    double value = 0.0;
    if (!(input >> value)) {
        throw std::runtime_error("invalid or truncated ASCII PLY data");
    }
    return value;
}

[[nodiscard]] size_t ToListCount(double value)
{
    constexpr double kMaximumListSize = 100'000'000.0;
    if (!std::isfinite(value) || value < 0.0 || value > kMaximumListSize ||
        std::floor(value) != value) {
        throw std::runtime_error("invalid PLY list size");
    }
    return static_cast<size_t>(value);
}

[[nodiscard]] float ToFloat(double value, const std::string& propertyName)
{
    if (!std::isfinite(value) ||
        std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::runtime_error("PLY property is outside float range: " + propertyName);
    }
    return static_cast<float>(value);
}

[[nodiscard]] float NormalizeColor(double value, ScalarType type,
                                   const std::string& propertyName)
{
    double normalized = value;
    if (type == ScalarType::UInt8) {
        normalized /= 255.0;
    } else if (type == ScalarType::UInt16) {
        normalized /= 65535.0;
    }
    return std::clamp(ToFloat(normalized, propertyName), 0.0F, 1.0F);
}

struct VertexAccumulator {
    Gaussian gaussian;
    std::array<bool, 3> hasPosition {false, false, false};
    std::array<bool, 3> hasDirectColor {false, false, false};
    std::array<bool, kShCoefficientCount> hasShCoefficient {};
    float sortFreeInfo = 0.0F;
    bool hasSortFreeInfo = false;
};

[[nodiscard]] bool ParseIndexedProperty(const std::string& name,
                                        const std::string& prefix, size_t* index)
{
    if (name.compare(0, prefix.size(), prefix) != 0 || name.size() == prefix.size()) {
        return false;
    }
    size_t parsed = 0;
    for (size_t position = prefix.size(); position < name.size(); ++position) {
        if (name[position] < '0' || name[position] > '9') {
            return false;
        }
        parsed = parsed * 10 + static_cast<size_t>(name[position] - '0');
    }
    *index = parsed;
    return true;
}

void AssignShRestProperty(VertexAccumulator& vertex, size_t propertyIndex,
                          const std::string& name, double value)
{
    constexpr size_t kRestCoefficientsPerChannel = kShCoefficientsPerChannel - 1;
    constexpr size_t kRestCoefficientCount =
        kRestCoefficientsPerChannel * kShChannelCount;
    if (propertyIndex >= kRestCoefficientCount) {
        throw std::runtime_error("PLY SH property exceeds degree 3: " + name);
    }

    // The reference 3DGS PLY layout stores f_rest channel-major. Convert it to
    // coefficient-major RGB for contiguous shader access.
    const size_t channel = propertyIndex / kRestCoefficientsPerChannel;
    const size_t coefficient = propertyIndex % kRestCoefficientsPerChannel + 1;
    const size_t targetIndex = coefficient * kShChannelCount + channel;
    vertex.gaussian.shCoefficients[targetIndex] = ToFloat(value, name);
    vertex.hasShCoefficient[targetIndex] = true;
}

[[nodiscard]] CompiledProperty CompileProperty(const Property& property)
{
    const std::string& name = property.name;
    if (name == "x") return {&property, VertexOperation::PositionX};
    if (name == "y") return {&property, VertexOperation::PositionY};
    if (name == "z") return {&property, VertexOperation::PositionZ};
    if (name == "scale_0" || name == "scale_x") return {&property, VertexOperation::ScaleX};
    if (name == "scale_1" || name == "scale_y") return {&property, VertexOperation::ScaleY};
    if (name == "scale_2" || name == "scale_z") return {&property, VertexOperation::ScaleZ};
    if (name == "rot_0" || name == "rotation_w" || name == "rw") return {&property, VertexOperation::RotationW};
    if (name == "rot_1" || name == "rotation_x" || name == "rx") return {&property, VertexOperation::RotationX};
    if (name == "rot_2" || name == "rotation_y" || name == "ry") return {&property, VertexOperation::RotationY};
    if (name == "rot_3" || name == "rotation_z" || name == "rz") return {&property, VertexOperation::RotationZ};
    if (name == "opacity") return {&property, VertexOperation::Opacity};
    if (name == "alpha") return {&property, VertexOperation::Alpha};
    if (name == "red" || name == "r" || name == "color_0") return {&property, VertexOperation::ColorR};
    if (name == "green" || name == "g" || name == "color_1") return {&property, VertexOperation::ColorG};
    if (name == "blue" || name == "b" || name == "color_2") return {&property, VertexOperation::ColorB};
    if (name == "f_dc_0") return {&property, VertexOperation::ShDc0};
    if (name == "f_dc_1") return {&property, VertexOperation::ShDc1};
    if (name == "f_dc_2") return {&property, VertexOperation::ShDc2};
    if (name == "f_do_0") return {&property, VertexOperation::OpacityShDc};
    if (name == "info") return {&property, VertexOperation::SortFreeInfo};
    size_t opacityIndex = 0;
    if (ParseIndexedProperty(name, "f_ro_", &opacityIndex)) {
        if (opacityIndex < kOpacityShCoefficientCount - 1) {
            return {&property, VertexOperation::OpacityShRest, opacityIndex};
        }
        return {&property, VertexOperation::Ignore};
    }
    if (name.compare(0, 7, "f_rest_") == 0) {
        if (name.size() == 7) throw std::runtime_error("invalid PLY SH property: " + name);
        size_t index = 0;
        for (size_t i = 7; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') throw std::runtime_error("invalid PLY SH property: " + name);
            index = index * 10 + static_cast<size_t>(name[i] - '0');
        }
        return {&property, VertexOperation::ShRest, index};
    }
    return {&property, VertexOperation::Ignore};
}

void ApplyVertexProperty(VertexAccumulator& vertex, const CompiledProperty& compiled,
                         double value)
{
    const Property& property = *compiled.property;
    const auto valueFloat = [&] { return ToFloat(value, property.name); };
    switch (compiled.operation) {
        case VertexOperation::PositionX: vertex.gaussian.position[0] = valueFloat(); vertex.hasPosition[0] = true; break;
        case VertexOperation::PositionY: vertex.gaussian.position[1] = valueFloat(); vertex.hasPosition[1] = true; break;
        case VertexOperation::PositionZ: vertex.gaussian.position[2] = valueFloat(); vertex.hasPosition[2] = true; break;
        case VertexOperation::ScaleX: vertex.gaussian.scale[0] = valueFloat(); break;
        case VertexOperation::ScaleY: vertex.gaussian.scale[1] = valueFloat(); break;
        case VertexOperation::ScaleZ: vertex.gaussian.scale[2] = valueFloat(); break;
        case VertexOperation::RotationW: vertex.gaussian.rotation[0] = valueFloat(); break;
        case VertexOperation::RotationX: vertex.gaussian.rotation[1] = valueFloat(); break;
        case VertexOperation::RotationY: vertex.gaussian.rotation[2] = valueFloat(); break;
        case VertexOperation::RotationZ: vertex.gaussian.rotation[3] = valueFloat(); break;
        case VertexOperation::Opacity: vertex.gaussian.opacity = valueFloat(); break;
        case VertexOperation::Alpha: vertex.gaussian.opacity = NormalizeColor(value, property.valueType, property.name); break;
        case VertexOperation::ColorR: vertex.gaussian.color[0] = NormalizeColor(value, property.valueType, property.name); vertex.hasDirectColor[0] = true; break;
        case VertexOperation::ColorG: vertex.gaussian.color[1] = NormalizeColor(value, property.valueType, property.name); vertex.hasDirectColor[1] = true; break;
        case VertexOperation::ColorB: vertex.gaussian.color[2] = NormalizeColor(value, property.valueType, property.name); vertex.hasDirectColor[2] = true; break;
        case VertexOperation::ShDc0: vertex.gaussian.shCoefficients[0] = valueFloat(); vertex.hasShCoefficient[0] = true; break;
        case VertexOperation::ShDc1: vertex.gaussian.shCoefficients[1] = valueFloat(); vertex.hasShCoefficient[1] = true; break;
        case VertexOperation::ShDc2: vertex.gaussian.shCoefficients[2] = valueFloat(); vertex.hasShCoefficient[2] = true; break;
        case VertexOperation::ShRest: AssignShRestProperty(vertex, compiled.shRestIndex, property.name, value); break;
        case VertexOperation::OpacityShDc:
            vertex.gaussian.opacityShCoefficients[0] = valueFloat();
            break;
        case VertexOperation::OpacityShRest:
            if (compiled.shRestIndex >= kOpacityShCoefficientCount - 1) {
                throw std::runtime_error("PLY opacity SH property exceeds degree 3: " +
                                         property.name);
            }
            vertex.gaussian.opacityShCoefficients[compiled.shRestIndex + 1] =
                valueFloat();
            break;
        case VertexOperation::SortFreeInfo:
            if (std::isfinite(value) &&
                std::abs(value) <=
                    static_cast<double>(std::numeric_limits<float>::max())) {
                vertex.sortFreeInfo = static_cast<float>(value);
                vertex.hasSortFreeInfo = true;
            }
            break;
        case VertexOperation::Ignore: break;
    }
}

[[nodiscard]] std::string SortFreeHeaderDiagnostic(const Header& header)
{
    const auto vertex = std::find_if(header.elements.begin(), header.elements.end(),
                                     [](const Element& element) {
                                         return element.name == "vertex";
                                     });
    std::vector<std::string> missing;
    const auto hasProperty = [&vertex](const std::string& name) {
        return std::any_of(vertex->properties.begin(), vertex->properties.end(),
                           [&name](const Property& property) {
                               return !property.isList && property.name == name;
                           });
    };
    if (!hasProperty("f_do_0")) missing.push_back("f_do_0");
    for (size_t index = 0; index < kOpacityShCoefficientCount - 1; ++index) {
        const std::string name = "f_ro_" + std::to_string(index);
        if (!hasProperty(name)) missing.push_back(name);
    }
    if (!hasProperty("info")) missing.push_back("info");
    if (missing.empty()) return {};
    std::ostringstream message;
    message << "missing sort-free PLY properties: ";
    for (size_t index = 0; index < missing.size(); ++index) {
        if (index != 0) message << ", ";
        message << missing[index];
    }
    return message.str();
}

[[nodiscard]] Gaussian FinishVertex(VertexAccumulator& vertex)
{
    if (!std::all_of(vertex.hasPosition.begin(), vertex.hasPosition.end(),
                     [](bool present) { return present; })) {
        throw std::runtime_error("PLY vertex is missing x, y, or z");
    }
    constexpr float kShC0 = 0.28209479177387814F;
    for (size_t channel = 0; channel < vertex.gaussian.color.size(); ++channel) {
        if (!vertex.hasDirectColor[channel] && vertex.hasShCoefficient[channel]) {
            vertex.gaussian.color[channel] = std::clamp(
                0.5F + kShC0 * vertex.gaussian.shCoefficients[channel], 0.0F,
                1.0F);
        }
    }

    const bool hasDirectColor = std::any_of(
        vertex.hasDirectColor.begin(), vertex.hasDirectColor.end(),
        [](bool present) { return present; });
    const bool hasCompleteDc = std::all_of(
        vertex.hasShCoefficient.begin(),
        vertex.hasShCoefficient.begin() + static_cast<ptrdiff_t>(kShChannelCount),
        [](bool present) { return present; });
    if (!hasDirectColor && hasCompleteDc) {
        vertex.gaussian.shDegree = 0;
        for (int32_t degree = 1; degree <= 3; ++degree) {
            const size_t coefficientCount =
                static_cast<size_t>((degree + 1) * (degree + 1));
            const size_t requiredValueCount = coefficientCount * kShChannelCount;
            const bool complete = std::all_of(
                vertex.hasShCoefficient.begin(),
                vertex.hasShCoefficient.begin() +
                    static_cast<ptrdiff_t>(requiredValueCount),
                [](bool present) { return present; });
            if (!complete) {
                break;
            }
            vertex.gaussian.shDegree = degree;
        }
    }
    return vertex.gaussian;
}

} // namespace

PlyLoadStatistics PlyLoader::LoadBatches(const std::filesystem::path& path,
                                         size_t batchSize,
                                         const BatchCallback& callback,
                                         PlyModelMetadata* metadata)
{
    if (batchSize == 0 || !callback) throw std::invalid_argument("PLY batch size and callback must be valid");
    PlyLoadStatistics statistics;
    statistics.fileBytes = static_cast<size_t>(std::filesystem::file_size(path));
    const auto headerStart = std::chrono::steady_clock::now();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open PLY file: " + path.string());
    }
    const Header header = ReadHeader(input);
    PlyModelMetadata modelMetadata;
    modelMetadata.sortFreeDiagnostic = SortFreeHeaderDiagnostic(header);
    const bool sortFreeHeader = modelMetadata.sortFreeDiagnostic.empty();
    std::array<float, 2> sortFreeInfo {};
    std::array<bool, 2> hasSortFreeInfo {};
    statistics.headerSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - headerStart).count();
    const bool ascii = header.format == PlyFormat::Ascii;
    const bool fileLittleEndian = header.format == PlyFormat::BinaryLittleEndian;
    const bool swapBytes = !ascii && fileLittleEndian != HostIsLittleEndian();

    const auto dataStart = std::chrono::steady_clock::now();
    std::vector<Gaussian> batch;
    batch.reserve(batchSize);
    for (const Element& element : header.elements) {
        std::vector<CompiledProperty> compiled;
        if (element.name == "vertex") {
            compiled.reserve(element.properties.size());
            for (const Property& property : element.properties) compiled.push_back(CompileProperty(property));
        }
        const bool nativeFloatVertex = element.name == "vertex" && !ascii &&
            !swapBytes && std::all_of(element.properties.begin(), element.properties.end(),
                [](const Property& property) {
                    return !property.isList && property.valueType == ScalarType::Float32;
                });
        std::vector<float> nativeRecord;
        if (nativeFloatVertex) nativeRecord.resize(element.properties.size());
        for (size_t record = 0; record < element.count; ++record) {
            VertexAccumulator vertex;
            if (nativeFloatVertex) {
                const size_t bytes = nativeRecord.size() * sizeof(float);
                input.read(reinterpret_cast<char*>(nativeRecord.data()),
                           static_cast<std::streamsize>(bytes));
                if (!input) throw std::runtime_error("unexpected end of binary PLY data");
                for (size_t propertyIndex = 0; propertyIndex < compiled.size(); ++propertyIndex) {
                    ApplyVertexProperty(vertex, compiled[propertyIndex],
                                        nativeRecord[propertyIndex]);
                }
            }
            for (size_t propertyIndex = 0;
                 !nativeFloatVertex && propertyIndex < element.properties.size();
                 ++propertyIndex) {
                const Property& property = element.properties[propertyIndex];
                const auto readScalar = [&](ScalarType type) {
                    return ascii ? ReadAsciiScalar(input)
                                 : ReadBinaryScalar(input, type, swapBytes);
                };
                if (property.isList) {
                    const size_t listSize = ToListCount(readScalar(property.listCountType));
                    for (size_t index = 0; index < listSize; ++index) {
                        static_cast<void>(readScalar(property.valueType));
                    }
                } else {
                    const double value = readScalar(property.valueType);
                    if (element.name == "vertex") {
                        ApplyVertexProperty(vertex, compiled[propertyIndex], value);
                    }
                }
            }
            if (element.name == "vertex") {
                if (sortFreeHeader && record < sortFreeInfo.size() &&
                    vertex.hasSortFreeInfo) {
                    sortFreeInfo[record] = vertex.sortFreeInfo;
                    hasSortFreeInfo[record] = true;
                }
                batch.push_back(FinishVertex(vertex));
                ++statistics.gaussianCount;
                if (batch.size() == batchSize) {
                    callback(batch.data(), batch.size());
                    batch.clear();
                    ++statistics.batchCount;
                }
            }
        }
    }
    if (!batch.empty()) { callback(batch.data(), batch.size()); ++statistics.batchCount; }
    statistics.dataSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - dataStart).count();
    statistics.path = ascii ? "generic-ascii" :
        (swapBytes ? "generic-binary-swapped" : "binary-native-float-fast-or-generic");
    if (sortFreeHeader) {
        if (statistics.gaussianCount < 2) {
            modelMetadata.sortFreeDiagnostic =
                "sort-free PLY requires at least two vertices";
        } else if (!hasSortFreeInfo[0] || !hasSortFreeInfo[1]) {
            modelMetadata.sortFreeDiagnostic =
                "sort-free PLY info values must be finite floats";
        } else if (sortFreeInfo[0] < 0.0F) {
            modelMetadata.sortFreeDiagnostic =
                "sort-free background weight must be non-negative";
        } else if (sortFreeInfo[1] <= 0.0F) {
            modelMetadata.sortFreeDiagnostic = "sort-free sigma must be positive";
        } else {
            modelMetadata.supportsSortFree = true;
            modelMetadata.weightBackground = sortFreeInfo[0];
            modelMetadata.sigma = sortFreeInfo[1];
            modelMetadata.sortFreeDiagnostic = "supported";
        }
    }
    if (metadata != nullptr) *metadata = std::move(modelMetadata);
    return statistics;
}

std::vector<Gaussian> PlyLoader::Load(const std::filesystem::path& path)
{
    return Load(path, nullptr);
}

std::vector<Gaussian> PlyLoader::Load(const std::filesystem::path& path,
                                      PlyLoadStatistics* statistics)
{
    return Load(path, statistics, nullptr);
}

std::vector<Gaussian> PlyLoader::Load(const std::filesystem::path& path,
                                      PlyLoadStatistics* statistics,
                                      PlyModelMetadata* metadata)
{
    std::vector<Gaussian> gaussians;
    const auto result = LoadBatches(path, 65'536, [&](const Gaussian* values, size_t count) {
        gaussians.insert(gaussians.end(), values, values + count);
    }, metadata);
    if (statistics != nullptr) *statistics = result;
    return gaussians;
}

} // namespace simple_3dgs
