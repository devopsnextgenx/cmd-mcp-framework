#include <iostream>
#include <memory>
#include <string>
#include <cmath>
#include <functional>
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
//   AREA      -> TRIANGLE | RECTANGLE | CIRCLE | SPHERE
//                | PYRAMID | TETRAHEDRON | CUBE
//                | CUBOID | PRISM | CYLINDER | CONE
//   VOLUME    -> PYRAMID | TETRAHEDRON | CUBE | CUBOID | PRISM
//                CYLINDER | CONE | SPHERE
// ---------------------------------------------------------------------------

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

static nlohmann::json geoResultSchema(const std::string& subtype_name) {
  return {
      {"type", "object"},
      {"required", nlohmann::json::array({
          "subTypeExecuted",
          "operation",
          "shape",
          "formula",
          "value"
      })},
      {"properties", {
          {"subTypeExecuted", {{"type", "string"}, {"const", subtype_name}}},
          {"operation", {{"type", "string"}}},
          {"shape", {{"type", "string"}}},
          {"formula", {{"type", "string"}}},
          {"value", {{"type", "number"}}},
          {"semiPerimeter", {{"type", "number"}}},
          {"baseArea", {{"type", "number"}}},
          {"lateralArea", {{"type", "number"}}},
          {"topArea", {{"type", "number"}}},
          {"bottomArea", {{"type", "number"}}},
          {"curvedSurfaceArea", {{"type", "number"}}}
      }}
  };
}

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

static double get(const nlohmann::json& input,
                  const std::string& field) {
  return input.at(field).get<double>();
}

static constexpr double PI = M_PI;

// ── Provider ─────────────────────────────────────────────────────────────────

class GeometryCmdProvider final : public cmdsdk::SubCmd {
 public:
  // Handler types for the dispatcher maps.
  using ExecHandler     = std::function<bool(GeometryCmdProvider*, const nlohmann::json&)>;
  using ValidateHandler = std::function<bool(const nlohmann::json&, std::string&)>;

  GeometryCmdProvider() : cmdsdk::SubCmd() {
    setPluginName("GEO");

    // ── PERIMETER ───────────────────────────────────────────────────────────

    registerSubCmdType(
        "GEO.PERIMETER.TRIANGLE",
        {"GEO.PERIMETER.TRIANGLE",
         "Perimeter of a triangle. Params: a, b, c."});

    registerSubCmdType(
        "GEO.PERIMETER.RECTANGLE",
        {"GEO.PERIMETER.RECTANGLE",
         "Perimeter of a rectangle. Params: a (length), b (width)."});

    registerSubCmdType(
        "GEO.PERIMETER.CIRCLE",
        {"GEO.PERIMETER.CIRCLE",
         "Circumference of a circle. Params: radius."});

    // ── AREA ────────────────────────────────────────────────────────────────

    registerSubCmdType(
        "GEO.AREA.TRIANGLE",
        {"GEO.AREA.TRIANGLE",
         "Area of a triangle via Heron's formula. Params: a, b, c."});

    registerSubCmdType(
        "GEO.AREA.RECTANGLE",
        {"GEO.AREA.RECTANGLE",
         "Area of a rectangle. Params: a (length), b (width)."});

    registerSubCmdType(
        "GEO.AREA.CIRCLE",
        {"GEO.AREA.CIRCLE",
         "Area of a circle. Params: radius."});

    registerSubCmdType(
        "GEO.AREA.SPHERE",
        {"GEO.AREA.SPHERE",
         "Surface area of a sphere. Params: radius."});

    registerSubCmdType(
        "GEO.AREA.PYRAMID",
        {"GEO.AREA.PYRAMID",
         "Total surface area of a square-base pyramid. Params: a, slant."});

    registerSubCmdType(
        "GEO.AREA.TETRAHEDRON",
        {"GEO.AREA.TETRAHEDRON",
         "Total surface area of a regular tetrahedron. Params: side."});

    registerSubCmdType(
        "GEO.AREA.CUBE",
        {"GEO.AREA.CUBE",
         "Surface area of a cube. Params: side."});

    registerSubCmdType(
        "GEO.AREA.CUBOID",
        {"GEO.AREA.CUBOID",
         "Surface area of a cuboid. Params: a, b, height."});

    registerSubCmdType(
        "GEO.AREA.PRISM",
        {"GEO.AREA.PRISM",
         "Surface area of a rectangular prism. Params: a, b, height."});

    registerSubCmdType(
        "GEO.AREA.CYLINDER",
        {"GEO.AREA.CYLINDER",
         "Surface area of a cylinder. Params: radius, height."});

    registerSubCmdType(
        "GEO.AREA.CONE",
        {"GEO.AREA.CONE",
         "Surface area of a cone. Params: radius, slant."});

    // ── VOLUME ──────────────────────────────────────────────────────────────

    registerSubCmdType(
        "GEO.VOLUME.PYRAMID",
        {"GEO.VOLUME.PYRAMID",
         "Volume of a square-base pyramid. Params: a, height."});

    registerSubCmdType(
        "GEO.VOLUME.TETRAHEDRON",
        {"GEO.VOLUME.TETRAHEDRON",
         "Volume of a regular tetrahedron. Params: side."});

    registerSubCmdType(
        "GEO.VOLUME.CUBE",
        {"GEO.VOLUME.CUBE",
         "Volume of a cube. Params: side."});

    registerSubCmdType(
        "GEO.VOLUME.CUBOID",
        {"GEO.VOLUME.CUBOID",
         "Volume of a cuboid. Params: a, b, height."});

    registerSubCmdType(
        "GEO.VOLUME.PRISM",
        {"GEO.VOLUME.PRISM",
         "Volume of a right rectangular prism. Params: a, b, height."});

    registerSubCmdType(
        "GEO.VOLUME.CYLINDER",
        {"GEO.VOLUME.CYLINDER",
         "Volume of a cylinder. Params: radius, height."});

    registerSubCmdType(
        "GEO.VOLUME.CONE",
        {"GEO.VOLUME.CONE",
         "Volume of a cone. Params: radius, height."});

    registerSubCmdType(
        "GEO.VOLUME.SPHERE",
        {"GEO.VOLUME.SPHERE",
         "Volume of a sphere. Params: radius."});

    // ── Execution dispatcher ────────────────────────────────────────────────
    // O(1) lookup in execute() replaces a 21-branch if-chain.
    // To add a new shape: register its subtype above, add entries here and in
    // the validator map, then implement the private exec/validate methods.
    exec_dispatcher_ = {
      {"GEO.PERIMETER.TRIANGLE",   [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execPerimeterTriangle(in);   }},
      {"GEO.PERIMETER.RECTANGLE",  [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execPerimeterRectangle(in);  }},
      {"GEO.PERIMETER.CIRCLE",     [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execPerimeterCircle(in);     }},
      {"GEO.AREA.TRIANGLE",        [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaTriangle(in);        }},
      {"GEO.AREA.RECTANGLE",       [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaRectangle(in);       }},
      {"GEO.AREA.CIRCLE",          [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaCircle(in);          }},
      {"GEO.AREA.SPHERE",          [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaSphere(in);          }},
      {"GEO.AREA.PYRAMID",         [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaPyramid(in);         }},
      {"GEO.AREA.TETRAHEDRON",     [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaTetrahedron(in);     }},
      {"GEO.AREA.CUBE",            [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaCube(in);            }},
      {"GEO.AREA.CUBOID",          [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaCuboid(in);          }},
      {"GEO.AREA.PRISM",           [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaPrism(in);           }},
      {"GEO.AREA.CYLINDER",        [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaCylinder(in);        }},
      {"GEO.AREA.CONE",            [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execAreaCone(in);            }},
      {"GEO.VOLUME.PYRAMID",       [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execVolumePyramid(in);       }},
      {"GEO.VOLUME.TETRAHEDRON",   [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execVolumeTetrahedron(in);   }},
      {"GEO.VOLUME.CUBE",          [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execVolumeCube(in);          }},
      {"GEO.VOLUME.CUBOID",        [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execVolumeCuboid(in);        }},
      {"GEO.VOLUME.PRISM",         [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execVolumePrism(in);         }},
      {"GEO.VOLUME.CYLINDER",      [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execVolumeCylinder(in);      }},
      {"GEO.VOLUME.CONE",          [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execVolumeCone(in);          }},
      {"GEO.VOLUME.SPHERE",        [](GeometryCmdProvider* s, const nlohmann::json& in){ return s->execVolumeSphere(in);        }},
    };

    // ── Validation dispatcher ───────────────────────────────────────────────
    // Maps each subtype to its parameter-validation lambda (free function, no
    // instance capture needed since requirePositive is a static helper).
    validate_dispatcher_ = {
      {"GEO.PERIMETER.TRIANGLE",  [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"b",err) && requirePositive(in,"c",err)
              && validateTriangleInequalityStatic(in,err); }},
      {"GEO.AREA.TRIANGLE",       [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"b",err) && requirePositive(in,"c",err)
              && validateTriangleInequalityStatic(in,err); }},
      {"GEO.PERIMETER.RECTANGLE", [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"b",err); }},
      {"GEO.AREA.RECTANGLE",      [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"b",err); }},
      {"GEO.PERIMETER.CIRCLE",    [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"radius",err); }},
      {"GEO.AREA.CIRCLE",         [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"radius",err); }},
      {"GEO.AREA.SPHERE",         [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"radius",err); }},
      {"GEO.VOLUME.SPHERE",       [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"radius",err); }},
      {"GEO.AREA.PYRAMID",        [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"slant",err); }},
      {"GEO.AREA.TETRAHEDRON",    [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"side",err); }},
      {"GEO.VOLUME.TETRAHEDRON",  [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"side",err); }},
      {"GEO.AREA.CUBE",           [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"side",err); }},
      {"GEO.VOLUME.CUBE",         [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"side",err); }},
      {"GEO.VOLUME.PYRAMID",      [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"height",err); }},
      {"GEO.VOLUME.CUBOID",       [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"b",err) && requirePositive(in,"height",err); }},
      {"GEO.VOLUME.PRISM",        [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"b",err) && requirePositive(in,"height",err); }},
      {"GEO.AREA.CUBOID",         [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"b",err) && requirePositive(in,"height",err); }},
      {"GEO.AREA.PRISM",          [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"a",err) && requirePositive(in,"b",err) && requirePositive(in,"height",err); }},
      {"GEO.VOLUME.CYLINDER",     [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"radius",err) && requirePositive(in,"height",err); }},
      {"GEO.AREA.CYLINDER",       [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"radius",err) && requirePositive(in,"height",err); }},
      {"GEO.VOLUME.CONE",         [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"radius",err) && requirePositive(in,"height",err); }},
      {"GEO.AREA.CONE",           [](const nlohmann::json& in, std::string& err){
          return requirePositive(in,"radius",err) && requirePositive(in,"slant",err); }},
    };
  }

  cmdsdk::CommandMetadata buildMetadata() const override {
    cmdsdk::CommandMetadata metadata;

    metadata.cmd_name = "geo.calculate";

    metadata.description =
        "Geometry command provider extending abstract Cmd. "
        "Calculates perimeter, area, and volume for common shapes.";

    metadata.parameters = {
        {"a", "number", false,
         "Must be numeric (>0).",
         "First side / base length."},

        {"b", "number", false,
         "Must be numeric (>0).",
         "Second side / width."},

        {"c", "number", false,
         "Must be numeric (>0).",
         "Third side of a triangle."},

        {"side", "number", false,
         "Must be numeric (>0).",
         "Side length."},

        {"radius", "number", false,
         "Must be numeric (>0).",
         "Radius."},

        {"height", "number", false,
         "Must be numeric (>0).",
         "Height."},

        {"slant", "number", false,
         "Must be numeric (>0).",
         "Slant height."},

        {"subType", "string", true,
         "See sub_cmd_types for allowed values.",
         "Selects operation and shape."},
    };

    metadata.sub_cmd_types = {

        // ── PERIMETER ───────────────────────────────────────────────────────

        {"GEO.PERIMETER.TRIANGLE",
         "Perimeter of a triangle.",
         geoResultSchema("GEO.PERIMETER.TRIANGLE")},

        {"GEO.PERIMETER.RECTANGLE",
         "Perimeter of a rectangle.",
         geoResultSchema("GEO.PERIMETER.RECTANGLE")},

        {"GEO.PERIMETER.CIRCLE",
         "Circumference of a circle.",
         geoResultSchema("GEO.PERIMETER.CIRCLE")},

        // ── AREA ───────────────────────────────────────────────────────────

        {"GEO.AREA.TRIANGLE",
         "Area of triangle.",
         geoResultSchema("GEO.AREA.TRIANGLE")},

        {"GEO.AREA.RECTANGLE",
         "Area of rectangle.",
         geoResultSchema("GEO.AREA.RECTANGLE")},

        {"GEO.AREA.CIRCLE",
         "Area of circle.",
         geoResultSchema("GEO.AREA.CIRCLE")},

        {"GEO.AREA.SPHERE",
         "Surface area of sphere.",
         geoResultSchema("GEO.AREA.SPHERE")},

        {"GEO.AREA.PYRAMID",
         "Surface area of pyramid.",
         geoResultSchema("GEO.AREA.PYRAMID")},

        {"GEO.AREA.TETRAHEDRON",
         "Surface area of tetrahedron.",
         geoResultSchema("GEO.AREA.TETRAHEDRON")},

        {"GEO.AREA.CUBE",
         "Surface area of cube.",
         geoResultSchema("GEO.AREA.CUBE")},

        {"GEO.AREA.CUBOID",
         "Surface area of cuboid.",
         geoResultSchema("GEO.AREA.CUBOID")},

        {"GEO.AREA.PRISM",
         "Surface area of prism.",
         geoResultSchema("GEO.AREA.PRISM")},

        {"GEO.AREA.CYLINDER",
         "Surface area of cylinder.",
         geoResultSchema("GEO.AREA.CYLINDER")},

        {"GEO.AREA.CONE",
         "Surface area of cone.",
         geoResultSchema("GEO.AREA.CONE")},

        // ── VOLUME ─────────────────────────────────────────────────────────

        {"GEO.VOLUME.PYRAMID",
         "Volume of pyramid.",
         geoResultSchema("GEO.VOLUME.PYRAMID")},

        {"GEO.VOLUME.TETRAHEDRON",
         "Volume of tetrahedron.",
         geoResultSchema("GEO.VOLUME.TETRAHEDRON")},

        {"GEO.VOLUME.CUBE",
         "Volume of cube.",
         geoResultSchema("GEO.VOLUME.CUBE")},

        {"GEO.VOLUME.CUBOID",
         "Volume of cuboid.",
         geoResultSchema("GEO.VOLUME.CUBOID")},

        {"GEO.VOLUME.PRISM",
         "Volume of prism.",
         geoResultSchema("GEO.VOLUME.PRISM")},

        {"GEO.VOLUME.CYLINDER",
         "Volume of cylinder.",
         geoResultSchema("GEO.VOLUME.CYLINDER")},

        {"GEO.VOLUME.CONE",
         "Volume of cone.",
         geoResultSchema("GEO.VOLUME.CONE")},

        {"GEO.VOLUME.SPHERE",
         "Volume of sphere.",
         geoResultSchema("GEO.VOLUME.SPHERE")},
    };

    metadata.is_app_tool = true;
    metadata.resource_uri = "ui://ui/geo-form.html";

    return metadata;
  }

  // validate() now dispatches in O(1) via validate_dispatcher_ instead of
  // the former O(n) if-chain of grouped subtype checks.
  bool validate(const nlohmann::json& input,
                std::string& error) override {
    if (!input.is_object()) {
      error = "Input must be a JSON object.";
      return false;
    }

    if (!input.contains("subType") ||
        !input.at("subType").is_string()) {
      error = "Missing required string field: subType.";
      return false;
    }

    const auto sub_type =
        resolveSubCmdType(input.at("subType").get<std::string>());

    if (sub_type == cmdsdk::UNKNOWN_SUBCMD_TYPE) {
      error = "Unknown subType.";
      return false;
    }

    const auto it = validate_dispatcher_.find(sub_type);
    if (it == validate_dispatcher_.end()) {
      error = "Validation not implemented for subType: " + sub_type + ".";
      return false;
    }

    return it->second(input, error);
  }

  // execute() dispatches in O(1) via exec_dispatcher_ instead of the former
  // 21-branch if-chain.  Adding a shape only requires a handler entry in the
  // constructor map and a new private execXxx() method.
  bool execute(const nlohmann::json& input,
               std::string& error) override {

    const auto sub_type =
        resolveSubCmdType(input.at("subType").get<std::string>());

    const auto it = exec_dispatcher_.find(sub_type);
    if (it == exec_dispatcher_.end()) {
      error = "Unsupported subType.";
      return false;
    }

    return it->second(this, input);
  }

 private:
  std::unordered_map<std::string, ExecHandler>     exec_dispatcher_;
  std::unordered_map<std::string, ValidateHandler> validate_dispatcher_;

  // Static wrapper so validate_dispatcher_ lambdas can call it without
  // capturing `this` (they are free-function-style ValidateHandlers).
  static bool validateTriangleInequalityStatic(
      const nlohmann::json& input,
      std::string& error) {
    return validateTriangleInequality(input, error);
  }

  static bool validateTriangleInequality(
      const nlohmann::json& input,
      std::string& error) {

    const double a = get(input, "a");
    const double b = get(input, "b");
    const double c = get(input, "c");

    if (a + b <= c ||
        a + c <= b ||
        b + c <= a) {

      error =
          "Sides a, b, c do not satisfy triangle inequality.";
      return false;
    }

    return true;
  }

  // -------------------------------------------------------------------------
  // PERIMETER
  // -------------------------------------------------------------------------

  bool execPerimeterTriangle(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double c = get(input, "c");

    setResult({
        {"subTypeExecuted", "GEO.PERIMETER.TRIANGLE"},
        {"operation", "perimeter"},
        {"shape", "triangle"},
        {"formula", "a + b + c"},
        {"value", a + b + c},
    });

    return true;
  }

  bool execPerimeterRectangle(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");

    setResult({
        {"subTypeExecuted", "GEO.PERIMETER.RECTANGLE"},
        {"operation", "perimeter"},
        {"shape", "rectangle"},
        {"formula", "2 * (a + b)"},
        {"value", 2.0 * (a + b)},
    });

    return true;
  }

  bool execPerimeterCircle(const nlohmann::json& input) {
    const double r = get(input, "radius");

    setResult({
        {"subTypeExecuted", "GEO.PERIMETER.CIRCLE"},
        {"operation", "circumference"},
        {"shape", "circle"},
        {"formula", "2 * π * radius"},
        {"value", 2.0 * PI * r},
    });

    return true;
  }

  // -------------------------------------------------------------------------
  // AREA
  // -------------------------------------------------------------------------

  bool execAreaTriangle(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double c = get(input, "c");

    const double s = (a + b + c) / 2.0;

    const double area =
        std::sqrt(s * (s - a) * (s - b) * (s - c));

    setResult({
        {"subTypeExecuted", "GEO.AREA.TRIANGLE"},
        {"operation", "area"},
        {"shape", "triangle"},
        {"formula", "sqrt(s*(s-a)*(s-b)*(s-c))"},
        {"semiPerimeter", s},
        {"value", area},
    });

    return true;
  }

  bool execAreaRectangle(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");

    setResult({
        {"subTypeExecuted", "GEO.AREA.RECTANGLE"},
        {"operation", "area"},
        {"shape", "rectangle"},
        {"formula", "a * b"},
        {"value", a * b},
    });

    return true;
  }

  bool execAreaCircle(const nlohmann::json& input) {
    const double r = get(input, "radius");

    setResult({
        {"subTypeExecuted", "GEO.AREA.CIRCLE"},
        {"operation", "area"},
        {"shape", "circle"},
        {"formula", "π * radius²"},
        {"value", PI * r * r},
    });

    return true;
  }

  bool execAreaSphere(const nlohmann::json& input) {
    const double r = get(input, "radius");

    setResult({
        {"subTypeExecuted", "GEO.AREA.SPHERE"},
        {"operation", "surfaceArea"},
        {"shape", "sphere"},
        {"formula", "4 * π * radius²"},
        {"value", 4.0 * PI * r * r},
    });

    return true;
  }

  bool execAreaPyramid(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double slant = get(input, "slant");

    const double baseArea = a * a;
    const double lateralArea = 2.0 * a * slant;

    setResult({
        {"subTypeExecuted", "GEO.AREA.PYRAMID"},
        {"operation", "surfaceArea"},
        {"shape", "squareBasePyramid"},
        {"formula", "a² + 2*a*slant"},
        {"baseArea", baseArea},
        {"lateralArea", lateralArea},
        {"value", baseArea + lateralArea},
    });

    return true;
  }

  bool execAreaTetrahedron(const nlohmann::json& input) {
    const double s = get(input, "side");

    setResult({
        {"subTypeExecuted", "GEO.AREA.TETRAHEDRON"},
        {"operation", "surfaceArea"},
        {"shape", "regularTetrahedron"},
        {"formula", "√3 * side²"},
        {"value", std::sqrt(3.0) * s * s},
    });

    return true;
  }

  bool execAreaCube(const nlohmann::json& input) {
    const double s = get(input, "side");

    setResult({
        {"subTypeExecuted", "GEO.AREA.CUBE"},
        {"operation", "surfaceArea"},
        {"shape", "cube"},
        {"formula", "6 * side²"},
        {"value", 6.0 * s * s},
    });

    return true;
  }

  bool execAreaCuboid(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double h = get(input, "height");

    setResult({
        {"subTypeExecuted", "GEO.AREA.CUBOID"},
        {"operation", "surfaceArea"},
        {"shape", "cuboid"},
        {"formula", "2 * (a*b + a*height + b*height)"},
        {"value", 2.0 * (a * b + a * h + b * h)},
    });

    return true;
  }

  bool execAreaPrism(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double h = get(input, "height");

    setResult({
        {"subTypeExecuted", "GEO.AREA.PRISM"},
        {"operation", "surfaceArea"},
        {"shape", "rectangularPrism"},
        {"formula", "2 * (a*b + a*height + b*height)"},
        {"value", 2.0 * (a * b + a * h + b * h)},
    });

    return true;
  }

  bool execAreaCylinder(const nlohmann::json& input) {
    const double r = get(input, "radius");
    const double h = get(input, "height");

    const double curved = 2.0 * PI * r * h;
    const double top = PI * r * r;

    setResult({
        {"subTypeExecuted", "GEO.AREA.CYLINDER"},
        {"operation", "surfaceArea"},
        {"shape", "cylinder"},
        {"formula", "2 * π * radius * (radius + height)"},
        {"topArea", top},
        {"bottomArea", top},
        {"curvedSurfaceArea", curved},
        {"value", 2.0 * PI * r * (r + h)},
    });

    return true;
  }

  bool execAreaCone(const nlohmann::json& input) {
    const double r = get(input, "radius");
    const double s = get(input, "slant");

    const double base = PI * r * r;
    const double curved = PI * r * s;

    setResult({
        {"subTypeExecuted", "GEO.AREA.CONE"},
        {"operation", "surfaceArea"},
        {"shape", "cone"},
        {"formula", "π * radius * (radius + slant)"},
        {"baseArea", base},
        {"curvedSurfaceArea", curved},
        {"value", PI * r * (r + s)},
    });

    return true;
  }

  // -------------------------------------------------------------------------
  // VOLUME
  // -------------------------------------------------------------------------

  bool execVolumePyramid(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double h = get(input, "height");

    setResult({
        {"subTypeExecuted", "GEO.VOLUME.PYRAMID"},
        {"operation", "volume"},
        {"shape", "squareBasePyramid"},
        {"formula", "(1/3) * a² * height"},
        {"value", (1.0 / 3.0) * a * a * h},
    });

    return true;
  }

  bool execVolumeTetrahedron(const nlohmann::json& input) {
    const double s = get(input, "side");

    setResult({
        {"subTypeExecuted", "GEO.VOLUME.TETRAHEDRON"},
        {"operation", "volume"},
        {"shape", "regularTetrahedron"},
        {"formula", "side³ / (6√2)"},
        {"value", (s * s * s) / (6.0 * std::sqrt(2.0))},
    });

    return true;
  }

  bool execVolumeCube(const nlohmann::json& input) {
    const double s = get(input, "side");

    setResult({
        {"subTypeExecuted", "GEO.VOLUME.CUBE"},
        {"operation", "volume"},
        {"shape", "cube"},
        {"formula", "side³"},
        {"value", s * s * s},
    });

    return true;
  }

  bool execVolumeCuboid(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double h = get(input, "height");

    setResult({
        {"subTypeExecuted", "GEO.VOLUME.CUBOID"},
        {"operation", "volume"},
        {"shape", "cuboid"},
        {"formula", "a * b * height"},
        {"value", a * b * h},
    });

    return true;
  }

  bool execVolumePrism(const nlohmann::json& input) {
    const double a = get(input, "a");
    const double b = get(input, "b");
    const double h = get(input, "height");

    setResult({
        {"subTypeExecuted", "GEO.VOLUME.PRISM"},
        {"operation", "volume"},
        {"shape", "rectangularPrism"},
        {"formula", "a * b * height"},
        {"value", a * b * h},
    });

    return true;
  }

  bool execVolumeCylinder(const nlohmann::json& input) {
    const double r = get(input, "radius");
    const double h = get(input, "height");

    setResult({
        {"subTypeExecuted", "GEO.VOLUME.CYLINDER"},
        {"operation", "volume"},
        {"shape", "cylinder"},
        {"formula", "π * radius² * height"},
        {"value", PI * r * r * h},
    });

    return true;
  }

  bool execVolumeCone(const nlohmann::json& input) {
    const double r = get(input, "radius");
    const double h = get(input, "height");

    setResult({
        {"subTypeExecuted", "GEO.VOLUME.CONE"},
        {"operation", "volume"},
        {"shape", "cone"},
        {"formula", "(1/3) * π * radius² * height"},
        {"value", (1.0 / 3.0) * PI * r * r * h},
    });

    return true;
  }

  bool execVolumeSphere(const nlohmann::json& input) {
    const double r = get(input, "radius");

    setResult({
        {"subTypeExecuted", "GEO.VOLUME.SPHERE"},
        {"operation", "volume"},
        {"shape", "sphere"},
        {"formula", "(4/3) * π * radius³"},
        {"value", (4.0 / 3.0) * PI * r * r * r},
    });

    return true;
  }
};

}  // namespace

// SIOF safety note:
// CMDSDK_REGISTER_PROVIDER expands to a file-static AutoRegister<T> whose
// constructor calls ProviderRegistrar::instance().addFactory(...).
// ProviderRegistrar::instance() returns a *function-local* static, which
// C++11 §6.7 guarantees is initialised exactly once, on first call, before
// any use – regardless of cross-TU static-init order.  This eliminates the
// Static Initialisation Order Fiasco even when multiple plugin shared libraries
// are loaded in unpredictable order by the plugin host.
CMDSDK_REGISTER_PROVIDER(GeometryCmdProvider);