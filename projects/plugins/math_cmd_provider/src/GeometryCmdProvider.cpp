#include <iostream>
#include <memory>
#include <string>
#include <cmath>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "cmdsdk/CommandMetadata.hpp"
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/PluginApi.hpp"
#include "cmdsdk/SubCmd.hpp"
#include "cmdsdk/ProviderRegistrar.hpp"

// ---------------------------------------------------------------------------
// Sub-command naming convention:
//   GEO.<OPERATION>.<SHAPE>
//
// Operations : PERIMETER | AREA | VOLUME
// Shapes     :
//   PERIMETER -> TRIANGLE | RECTANGLE | CIRCLE
//   AREA      -> TRIANGLE | RECTANGLE | CIRCLE | PYRAMID | TETRAHEDRON
//   VOLUME    -> PYRAMID | TETRAHEDRON | CUBE | CUBOID | PRISM
//                CYLINDER | CONE | SPHERE
//
// Required parameters per sub-type are listed in the metadata and validated
// inside MathCmdProvider-style per-field checks.
// ---------------------------------------------------------------------------

namespace {
// ── Helpers ──────────────────────────────────────────────────────────────────

static nlohmann::json geoResultSchema(const std::string& subtype_name) {
  return {
      {"type", "object"},
      {"required", nlohmann::json::array({"subTypeExecuted", "operation", "shape", "formula", "value"})},
      {"properties", {
          {"subTypeExecuted", {{"type", "string"}, {"const", subtype_name}}},
          {"operation", {{"type", "string"}}},
          {"shape", {{"type", "string"}}},
          {"formula", {{"type", "string"}}},
          {"value", {{"type", "number"}}},
          {"semiPerimeter", {{"type", "number"}}},
          {"baseArea", {{"type", "number"}}},
          {"lateralArea", {{"type", "number"}}}
      }}
  };
}

// Returns true when the named numeric field is present and positive.
static bool requirePositive(const nlohmann::json& input,
                             const std::string& field,
                             std::string& error) {
  if (!input.contains(field) || !input.at(field).is_number()) {
    error = "Missing required numeric field: " + field + ".";
    return false;
  }
  if (input.at(field).get<double>() <= 0.0) {
    error = "Field '" + field + "' must be greater than zero.";
    return false;
  }
  return true;
}

static double get(const nlohmann::json& input, const std::string& field) {
  return input.at(field).get<double>();
}

static constexpr double PI = M_PI;

// ── Provider ─────────────────────────────────────────────────────────────────

class GeometryCmdProvider final : public cmdsdk::SubCmd {
 public:
  GeometryCmdProvider() : cmdsdk::SubCmd() {
    setPluginName("GEO");
 
    registerSubCmdType("GEO.PERIMETER.TRIANGLE",  {"GEO.PERIMETER.TRIANGLE",  "Perimeter of a triangle. Params: a, b, c."});
    registerSubCmdType("GEO.PERIMETER.RECTANGLE", {"GEO.PERIMETER.RECTANGLE", "Perimeter of a rectangle. Params: a (length), b (width)."});
    registerSubCmdType("GEO.PERIMETER.CIRCLE",    {"GEO.PERIMETER.CIRCLE",    "Circumference of a circle. Params: radius."});
 
    registerSubCmdType("GEO.AREA.TRIANGLE",       {"GEO.AREA.TRIANGLE",       "Area of a triangle via Heron's formula. Params: a, b, c."});
    registerSubCmdType("GEO.AREA.RECTANGLE",      {"GEO.AREA.RECTANGLE",      "Area of a rectangle. Params: a (length), b (width)."});
    registerSubCmdType("GEO.AREA.CIRCLE",         {"GEO.AREA.CIRCLE",         "Area of a circle. Params: radius."});
    registerSubCmdType("GEO.AREA.PYRAMID",        {"GEO.AREA.PYRAMID",        "Total surface area of a square-base pyramid. Params: a (base side), slant."});
    registerSubCmdType("GEO.AREA.TETRAHEDRON",    {"GEO.AREA.TETRAHEDRON",    "Total surface area of a regular tetrahedron. Params: side."});
 
    registerSubCmdType("GEO.VOLUME.PYRAMID",      {"GEO.VOLUME.PYRAMID",      "Volume of a square-base pyramid. Params: a (base side), height."});
    registerSubCmdType("GEO.VOLUME.TETRAHEDRON",  {"GEO.VOLUME.TETRAHEDRON",  "Volume of a regular tetrahedron. Params: side."});
    registerSubCmdType("GEO.VOLUME.CUBE",         {"GEO.VOLUME.CUBE",         "Volume of a cube. Params: side."});
    registerSubCmdType("GEO.VOLUME.CUBOID",       {"GEO.VOLUME.CUBOID",       "Volume of a cuboid (brick). Params: a (length), b (width), height."});
    registerSubCmdType("GEO.VOLUME.PRISM",        {"GEO.VOLUME.PRISM",        "Volume of a right rectangular prism. Params: a (base length), b (base width), height."});
    registerSubCmdType("GEO.VOLUME.CYLINDER",     {"GEO.VOLUME.CYLINDER",     "Volume of a cylinder. Params: radius, height."});
    registerSubCmdType("GEO.VOLUME.CONE",         {"GEO.VOLUME.CONE",         "Volume of a cone. Params: radius, height."});
    registerSubCmdType("GEO.VOLUME.SPHERE",       {"GEO.VOLUME.SPHERE",       "Volume of a sphere. Params: radius."});
  }
 
  cmdsdk::CommandMetadata buildMetadata() const override {
    cmdsdk::CommandMetadata metadata;
    metadata.cmd_name   = "geo.calculate";
    metadata.description =
        "Geometry command provider extending abstract Cmd. Calculates perimeter, "
        "area, and volume for triangles, rectangles, circles, pyramids, "
        "tetrahedrons, cubes, cuboids, prisms, cylinders, cones, and spheres.";
    metadata.parameters = {
        {"subType",  "string", true,  "See sub_cmd_types for allowed values.",             "Selects the geometric operation and shape."},
        {"side",     "number", false, "Must be numeric (>0).",                              "Side length for equilateral triangle, square, cube face, or tetrahedron."},
        {"a",        "number", false, "Must be numeric (>0).",                              "First side / base length."},
        {"b",        "number", false, "Must be numeric (>0).",                              "Second side / width."},
        {"c",        "number", false, "Must be numeric (>0).",                              "Third side of a scalene triangle."},
        {"radius",   "number", false, "Must be numeric (>0).",                              "Radius for circles, cylinders, cones, or spheres."},
        {"height",   "number", false, "Must be numeric (>0).",                              "Height for 3-D shapes."},
        {"slant",    "number", false, "Must be numeric (>0).",                              "Slant height for a pyramid lateral face."},
    };
    metadata.sub_cmd_types = {
      {"GEO.PERIMETER.TRIANGLE",  "Perimeter of a triangle. Params: a, b, c.", geoResultSchema("GEO.PERIMETER.TRIANGLE")},
      {"GEO.PERIMETER.RECTANGLE", "Perimeter of a rectangle. Params: a (length), b (width).", geoResultSchema("GEO.PERIMETER.RECTANGLE")},
      {"GEO.PERIMETER.CIRCLE",    "Circumference of a circle. Params: radius.", geoResultSchema("GEO.PERIMETER.CIRCLE")},
      {"GEO.AREA.TRIANGLE",       "Area of a triangle via Heron's formula. Params: a, b, c.", geoResultSchema("GEO.AREA.TRIANGLE")},
      {"GEO.AREA.RECTANGLE",      "Area of a rectangle. Params: a (length), b (width).", geoResultSchema("GEO.AREA.RECTANGLE")},
      {"GEO.AREA.CIRCLE",         "Area of a circle. Params: radius.", geoResultSchema("GEO.AREA.CIRCLE")},
      {"GEO.AREA.PYRAMID",        "Total surface area of a square-base pyramid. Params: a, slant.", geoResultSchema("GEO.AREA.PYRAMID")},
      {"GEO.AREA.TETRAHEDRON",    "Total surface area of a regular tetrahedron. Params: side.", geoResultSchema("GEO.AREA.TETRAHEDRON")},
      {"GEO.VOLUME.PYRAMID",      "Volume of a square-base pyramid. Params: a, height.", geoResultSchema("GEO.VOLUME.PYRAMID")},
      {"GEO.VOLUME.TETRAHEDRON",  "Volume of a regular tetrahedron. Params: side.", geoResultSchema("GEO.VOLUME.TETRAHEDRON")},
      {"GEO.VOLUME.CUBE",         "Volume of a cube. Params: side.", geoResultSchema("GEO.VOLUME.CUBE")},
      {"GEO.VOLUME.CUBOID",       "Volume of a cuboid. Params: a, b, height.", geoResultSchema("GEO.VOLUME.CUBOID")},
      {"GEO.VOLUME.PRISM",        "Volume of a right rectangular prism. Params: a, b, height.", geoResultSchema("GEO.VOLUME.PRISM")},
      {"GEO.VOLUME.CYLINDER",     "Volume of a cylinder. Params: radius, height.", geoResultSchema("GEO.VOLUME.CYLINDER")},
      {"GEO.VOLUME.CONE",         "Volume of a cone. Params: radius, height.", geoResultSchema("GEO.VOLUME.CONE")},
      {"GEO.VOLUME.SPHERE",       "Volume of a sphere. Params: radius.", geoResultSchema("GEO.VOLUME.SPHERE")},
    };
    return metadata;
  }
 
  // ── validate ───────────────────────────────────────────────────────────────

  bool validate(const nlohmann::json& input, std::string& error) override {
    if (!input.is_object()) {
      error = "Input must be a JSON object.";
      return false;
    }

    if (!input.contains("subType") || !input.at("subType").is_string()) {
      error = "Missing required string field: subType.";
      return false;
    }

    const auto sub_type = resolveSubCmdType(input.at("subType").get<std::string>());
    if (sub_type == cmdsdk::UNKNOWN_SUBCMD_TYPE) {
      error =
          "subType must be one of: GEO.PERIMETER.TRIANGLE, GEO.PERIMETER.RECTANGLE, "
          "GEO.PERIMETER.CIRCLE, GEO.AREA.TRIANGLE, GEO.AREA.RECTANGLE, GEO.AREA.CIRCLE, "
          "GEO.AREA.PYRAMID, GEO.AREA.TETRAHEDRON, GEO.VOLUME.PYRAMID, "
          "GEO.VOLUME.TETRAHEDRON, GEO.VOLUME.CUBE, GEO.VOLUME.CUBOID, "
          "GEO.VOLUME.PRISM, GEO.VOLUME.CYLINDER, GEO.VOLUME.CONE, GEO.VOLUME.SPHERE.";
      return false;
    }

    // Per-subtype field validation
    if (sub_type == "GEO.PERIMETER.TRIANGLE" || sub_type == "GEO.AREA.TRIANGLE") {
      return requirePositive(input, "a", error) &&
             requirePositive(input, "b", error) &&
             requirePositive(input, "c", error) &&
             validateTriangleInequality(input, error);
    }
    if (sub_type == "GEO.PERIMETER.RECTANGLE" || sub_type == "GEO.AREA.RECTANGLE") {
      return requirePositive(input, "a", error) &&
             requirePositive(input, "b", error);
    }
    if (sub_type == "GEO.PERIMETER.CIRCLE" || sub_type == "GEO.AREA.CIRCLE" ||
        sub_type == "GEO.VOLUME.SPHERE") {
      return requirePositive(input, "radius", error);
    }
    if (sub_type == "GEO.AREA.PYRAMID") {
      return requirePositive(input, "a", error) &&
             requirePositive(input, "slant", error);
    }
    if (sub_type == "GEO.AREA.TETRAHEDRON" ||
        sub_type == "GEO.VOLUME.TETRAHEDRON" ||
        sub_type == "GEO.VOLUME.CUBE") {
      return requirePositive(input, "side", error);
    }
    if (sub_type == "GEO.VOLUME.PYRAMID") {
      return requirePositive(input, "a", error) &&
             requirePositive(input, "height", error);
    }
    if (sub_type == "GEO.VOLUME.CUBOID" || sub_type == "GEO.VOLUME.PRISM") {
      return requirePositive(input, "a", error) &&
             requirePositive(input, "b", error) &&
             requirePositive(input, "height", error);
    }
    if (sub_type == "GEO.VOLUME.CYLINDER" || sub_type == "GEO.VOLUME.CONE") {
      return requirePositive(input, "radius", error) &&
             requirePositive(input, "height", error);
    }

    // Should be unreachable after resolveSubCmdType check above.
    error = "Unrecognised subType during validation.";
    return false;
  }

  // ── execute ────────────────────────────────────────────────────────────────

  bool execute(const nlohmann::json& input, std::string& error) override {
    const auto sub_type = resolveSubCmdType(input.at("subType").get<std::string>());

    // Perimeter
    if (sub_type == "GEO.PERIMETER.TRIANGLE")  return execPerimeterTriangle(input);
    if (sub_type == "GEO.PERIMETER.RECTANGLE") return execPerimeterRectangle(input);
    if (sub_type == "GEO.PERIMETER.CIRCLE")    return execPerimeterCircle(input);

    // Area
    if (sub_type == "GEO.AREA.TRIANGLE")       return execAreaTriangle(input);
    if (sub_type == "GEO.AREA.RECTANGLE")      return execAreaRectangle(input);
    if (sub_type == "GEO.AREA.CIRCLE")         return execAreaCircle(input);
    if (sub_type == "GEO.AREA.PYRAMID")        return execAreaPyramid(input);
    if (sub_type == "GEO.AREA.TETRAHEDRON")    return execAreaTetrahedron(input);

    // Volume
    if (sub_type == "GEO.VOLUME.PYRAMID")      return execVolumePyramid(input);
    if (sub_type == "GEO.VOLUME.TETRAHEDRON")  return execVolumeTetrahedron(input);
    if (sub_type == "GEO.VOLUME.CUBE")         return execVolumeCube(input);
    if (sub_type == "GEO.VOLUME.CUBOID")       return execVolumeCuboid(input);
    if (sub_type == "GEO.VOLUME.PRISM")        return execVolumePrism(input);
    if (sub_type == "GEO.VOLUME.CYLINDER")     return execVolumeCylinder(input);
    if (sub_type == "GEO.VOLUME.CONE")         return execVolumeCone(input);
    if (sub_type == "GEO.VOLUME.SPHERE")       return execVolumeSphere(input);

    error = "Unsupported subType requested.";
    return false;
  }

 private:

  // ── Validation helpers ────────────────────────────────────────────────────

  static bool validateTriangleInequality(const nlohmann::json& input,
                                          std::string& error) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double c = get(input, "c");
    if (a + b <= c || a + c <= b || b + c <= a) {
      error = "Sides a, b, c do not satisfy the triangle inequality.";
      return false;
    }
    return true;
  }

  // ── Perimeter implementations ─────────────────────────────────────────────

  // P = a + b + c
  bool execPerimeterTriangle(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double c = get(input, "c");
    setResult({
        {"subTypeExecuted", "GEO.PERIMETER.TRIANGLE"},
        {"operation",       "perimeter"},
        {"shape",           "triangle"},
        {"formula",         "a + b + c"},
        {"value",           a + b + c},
    });
    return true;
  }

  // P = 2 * (a + b)
  bool execPerimeterRectangle(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    setResult({
        {"subTypeExecuted", "GEO.PERIMETER.RECTANGLE"},
        {"operation",       "perimeter"},
        {"shape",           "rectangle"},
        {"formula",         "2 * (a + b)"},
        {"value",           2.0 * (a + b)},
    });
    return true;
  }

  // C = 2 * π * r
  bool execPerimeterCircle(const nlohmann::json& input) {
    const double r = get(input, "radius");
    setResult({
        {"subTypeExecuted", "GEO.PERIMETER.CIRCLE"},
        {"operation",       "circumference"},
        {"shape",           "circle"},
        {"formula",         "2 * π * radius"},
        {"value",           2.0 * PI * r},
    });
    return true;
  }

  // ── Area implementations ──────────────────────────────────────────────────

  // Heron's formula: s = (a+b+c)/2, A = sqrt(s*(s-a)*(s-b)*(s-c))
  bool execAreaTriangle(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double c = get(input, "c");
    const double s = (a + b + c) / 2.0;
    const double area = std::sqrt(s * (s - a) * (s - b) * (s - c));
    setResult({
        {"subTypeExecuted", "GEO.AREA.TRIANGLE"},
        {"operation",       "area"},
        {"shape",           "triangle"},
        {"formula",         "sqrt(s*(s-a)*(s-b)*(s-c)), s=(a+b+c)/2"},
        {"semiPerimeter",   s},
        {"value",           area},
    });
    return true;
  }

  // A = a * b
  bool execAreaRectangle(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    setResult({
        {"subTypeExecuted", "GEO.AREA.RECTANGLE"},
        {"operation",       "area"},
        {"shape",           "rectangle"},
        {"formula",         "a * b"},
        {"value",           a * b},
    });
    return true;
  }

  // A = π * r²
  bool execAreaCircle(const nlohmann::json& input) {
    const double r = get(input, "radius");
    setResult({
        {"subTypeExecuted", "GEO.AREA.CIRCLE"},
        {"operation",       "area"},
        {"shape",           "circle"},
        {"formula",         "π * radius²"},
        {"value",           PI * r * r},
    });
    return true;
  }

  // Square-base pyramid total surface area:
  //   A = a² + 2 * a * slant
  //   (base area  +  4 triangular lateral faces)
  bool execAreaPyramid(const nlohmann::json& input) {
    const double a     = get(input, "a");
    const double slant = get(input, "slant");
    const double baseArea    = a * a;
    const double lateralArea = 2.0 * a * slant;
    setResult({
        {"subTypeExecuted", "GEO.AREA.PYRAMID"},
        {"operation",       "surfaceArea"},
        {"shape",           "squareBasePyramid"},
        {"formula",         "a² + 2*a*slant"},
        {"baseArea",        baseArea},
        {"lateralArea",     lateralArea},
        {"value",           baseArea + lateralArea},
    });
    return true;
  }

  // Regular tetrahedron total surface area: A = √3 * side²
  bool execAreaTetrahedron(const nlohmann::json& input) {
    const double s = get(input, "side");
    setResult({
        {"subTypeExecuted", "GEO.AREA.TETRAHEDRON"},
        {"operation",       "surfaceArea"},
        {"shape",           "regularTetrahedron"},
        {"formula",         "√3 * side²"},
        {"value",           std::sqrt(3.0) * s * s},
    });
    return true;
  }

  // ── Volume implementations ────────────────────────────────────────────────

  // Square-base pyramid: V = (1/3) * a² * h
  bool execVolumePyramid(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double h = get(input, "height");
    setResult({
        {"subTypeExecuted", "GEO.VOLUME.PYRAMID"},
        {"operation",       "volume"},
        {"shape",           "squareBasePyramid"},
        {"formula",         "(1/3) * a² * height"},
        {"value",           (1.0 / 3.0) * a * a * h},
    });
    return true;
  }

  // Regular tetrahedron: V = side³ / (6 * √2)
  bool execVolumeTetrahedron(const nlohmann::json& input) {
    const double s = get(input, "side");
    setResult({
        {"subTypeExecuted", "GEO.VOLUME.TETRAHEDRON"},
        {"operation",       "volume"},
        {"shape",           "regularTetrahedron"},
        {"formula",         "side³ / (6√2)"},
        {"value",           (s * s * s) / (6.0 * std::sqrt(2.0))},
    });
    return true;
  }

  // Cube: V = side³
  bool execVolumeCube(const nlohmann::json& input) {
    const double s = get(input, "side");
    setResult({
        {"subTypeExecuted", "GEO.VOLUME.CUBE"},
        {"operation",       "volume"},
        {"shape",           "cube"},
        {"formula",         "side³"},
        {"value",           s * s * s},
    });
    return true;
  }

  // Cuboid (brick): V = a * b * h
  bool execVolumeCuboid(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double h = get(input, "height");
    setResult({
        {"subTypeExecuted", "GEO.VOLUME.CUBOID"},
        {"operation",       "volume"},
        {"shape",           "cuboid"},
        {"formula",         "a * b * height"},
        {"value",           a * b * h},
    });
    return true;
  }

  // Right rectangular prism (same formula as cuboid, distinct semantic intent):
  // V = a * b * h
  bool execVolumePrism(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double h = get(input, "height");
    setResult({
        {"subTypeExecuted", "GEO.VOLUME.PRISM"},
        {"operation",       "volume"},
        {"shape",           "rectangularPrism"},
        {"formula",         "a * b * height"},
        {"value",           a * b * h},
    });
    return true;
  }

  // Cylinder: V = π * r² * h
  bool execVolumeCylinder(const nlohmann::json& input) {
    const double r = get(input, "radius");
    const double h = get(input, "height");
    setResult({
        {"subTypeExecuted", "GEO.VOLUME.CYLINDER"},
        {"operation",       "volume"},
        {"shape",           "cylinder"},
        {"formula",         "π * radius² * height"},
        {"value",           PI * r * r * h},
    });
    return true;
  }

  // Cone: V = (1/3) * π * r² * h
  bool execVolumeCone(const nlohmann::json& input) {
    const double r = get(input, "radius");
    const double h = get(input, "height");
    setResult({
        {"subTypeExecuted", "GEO.VOLUME.CONE"},
        {"operation",       "volume"},
        {"shape",           "cone"},
        {"formula",         "(1/3) * π * radius² * height"},
        {"value",           (1.0 / 3.0) * PI * r * r * h},
    });
    return true;
  }

  // Sphere: V = (4/3) * π * r³
  bool execVolumeSphere(const nlohmann::json& input) {
    const double r = get(input, "radius");
    setResult({
        {"subTypeExecuted", "GEO.VOLUME.SPHERE"},
        {"operation",       "volume"},
        {"shape",           "sphere"},
        {"formula",         "(4/3) * π * radius³"},
        {"value",           (4.0 / 3.0) * PI * r * r * r},
    });
    return true;
  }
};

}  // namespace

CMDSDK_REGISTER_PROVIDER(GeometryCmdProvider);