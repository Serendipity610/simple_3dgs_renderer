#include "simple_3dgs/ply_loader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
};

void AssignShRestProperty(VertexAccumulator& vertex, const std::string& name,
                          double value)
{
    constexpr size_t kPrefixLength = 7;
    if (name.size() <= kPrefixLength) {
        throw std::runtime_error("invalid PLY SH property: " + name);
    }
    size_t propertyIndex = 0;
    for (size_t index = kPrefixLength; index < name.size(); ++index) {
        const char digit = name[index];
        if (digit < '0' || digit > '9') {
            throw std::runtime_error("invalid PLY SH property: " + name);
        }
        propertyIndex = propertyIndex * 10 + static_cast<size_t>(digit - '0');
    }
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

void AssignVertexProperty(VertexAccumulator& vertex, const Property& property, double value)
{
    const std::string& name = property.name;
    if (name == "x" || name == "y" || name == "z") {
        const size_t index = name == "x" ? 0 : (name == "y" ? 1 : 2);
        vertex.gaussian.position[index] = ToFloat(value, name);
        vertex.hasPosition[index] = true;
    } else if (name == "scale_0" || name == "scale_x") {
        vertex.gaussian.scale[0] = ToFloat(value, name);
    } else if (name == "scale_1" || name == "scale_y") {
        vertex.gaussian.scale[1] = ToFloat(value, name);
    } else if (name == "scale_2" || name == "scale_z") {
        vertex.gaussian.scale[2] = ToFloat(value, name);
    } else if (name == "rot_0" || name == "rotation_w" || name == "rw") {
        vertex.gaussian.rotation[0] = ToFloat(value, name);
    } else if (name == "rot_1" || name == "rotation_x" || name == "rx") {
        vertex.gaussian.rotation[1] = ToFloat(value, name);
    } else if (name == "rot_2" || name == "rotation_y" || name == "ry") {
        vertex.gaussian.rotation[2] = ToFloat(value, name);
    } else if (name == "rot_3" || name == "rotation_z" || name == "rz") {
        vertex.gaussian.rotation[3] = ToFloat(value, name);
    } else if (name == "opacity") {
        vertex.gaussian.opacity = ToFloat(value, name);
    } else if (name == "alpha") {
        vertex.gaussian.opacity = NormalizeColor(value, property.valueType, name);
    } else if (name == "red" || name == "r" || name == "color_0") {
        vertex.gaussian.color[0] = NormalizeColor(value, property.valueType, name);
        vertex.hasDirectColor[0] = true;
    } else if (name == "green" || name == "g" || name == "color_1") {
        vertex.gaussian.color[1] = NormalizeColor(value, property.valueType, name);
        vertex.hasDirectColor[1] = true;
    } else if (name == "blue" || name == "b" || name == "color_2") {
        vertex.gaussian.color[2] = NormalizeColor(value, property.valueType, name);
        vertex.hasDirectColor[2] = true;
    } else if (name == "f_dc_0" || name == "f_dc_1" || name == "f_dc_2") {
        const size_t channel = static_cast<size_t>(name.back() - '0');
        vertex.gaussian.shCoefficients[channel] = ToFloat(value, name);
        vertex.hasShCoefficient[channel] = true;
    } else if (name.compare(0, 7, "f_rest_") == 0) {
        AssignShRestProperty(vertex, name, value);
    }
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

std::vector<Gaussian> PlyLoader::Load(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open PLY file: " + path.string());
    }
    const Header header = ReadHeader(input);
    const bool ascii = header.format == PlyFormat::Ascii;
    const bool fileLittleEndian = header.format == PlyFormat::BinaryLittleEndian;
    const bool swapBytes = !ascii && fileLittleEndian != HostIsLittleEndian();

    std::vector<Gaussian> gaussians;
    for (const Element& element : header.elements) {
        if (element.name == "vertex") {
            gaussians.reserve(element.count);
        }
        for (size_t record = 0; record < element.count; ++record) {
            VertexAccumulator vertex;
            for (const Property& property : element.properties) {
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
                        AssignVertexProperty(vertex, property, value);
                    }
                }
            }
            if (element.name == "vertex") {
                gaussians.push_back(FinishVertex(vertex));
            }
        }
    }
    return gaussians;
}

} // namespace simple_3dgs
