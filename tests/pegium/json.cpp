#include <charconv>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <pegium/Parser.hpp>
#include <pegium/grammar.hpp>
#include <string>
#include <string_view>
#include <variant>

namespace Json {

struct JsonValue;

struct Pair : pegium::AstNode {
  string key;
  pointer<JsonValue> value;
};

struct JsonObject : pegium::AstNode {
  vector<pointer<Pair>> values;
};

struct JsonArray : pegium::AstNode {
  vector<pointer<JsonValue>> values;
};

struct JsonValue : pegium::AstNode {

  std::variant<std::string, double, pointer<JsonObject>, pointer<JsonArray>,
               bool, std::monostate>
      value;
};

class Parser : public pegium::Parser {
public:
  Parser() {

    using namespace pegium::grammar;
    terminal("WS").ignore() = +s;

    auto STRING = terminal("STRING") = "\""_kw + *(!"\""_cr) + "\""_kw;

    auto Number = terminal<double>(
        "Number",
        [](const pegium::CstNode &n) {
          double value;
          std::from_chars(n.text.data(), n.text.data() + n.text.size(), value);
          return value;
        }) = opt("-"_kw) + ("0"_kw | "1-9"_cr + *"0-9"_cr) +
             opt("."_kw + +"0-9"_cr) +
             opt("e"_kw.i() + opt("-+"_cr) + +"0-9"_cr);

    auto TRUE = terminal<bool>("TRUE", true) = "true"_kw;
    auto FALSE = terminal<bool>("FALSE", false) = "false"_kw;
    auto NULL_KW = terminal<std::monostate>("NULL", std::monostate{}) =
        "null"_kw;

    /// STRING ':' value
    auto pair = rule<Pair>("Pair") = assign<&Pair::key>(STRING) + ":"_kw +
                                     assign<&Pair::value>(call("JsonValue"));

    /// '{' pair (',' pair)* '}' | '{' '}'
    auto Obj = rule<JsonObject>("JsonObject") =
        "{"_kw + many_sep(","_kw, assign<&JsonObject::values>(pair)) + "}"_kw;

    /// '[' value (',' value)* ']' | '[' ']'
    auto Arr = rule<JsonArray>("JsonArray") =
        "["_kw +
        many_sep(","_kw, assign<&JsonArray::values>(call("JsonValue"))) +
        "]"_kw;

    /// STRING | NUMBER | obj | arr | 'true' | 'false' | 'null'
    rule<JsonValue>("JsonValue") = assign<&JsonValue::value>(
        STRING | Number | Obj | Arr | TRUE | FALSE | NULL_KW);
  }
};

} // namespace Json

TEST(JsonTest, TestJson) {
  Json::Parser g;

  std::string input = R"(
{ "type": "FeatureCollection",
  "features": [
{
    "type": "Feature",
"properties": { "name": "Canada" }
}
]
}

  )";

  using namespace std::chrono;
  auto start = high_resolution_clock::now();

  auto result = g.parse("JsonValue", input);
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(end - start).count();

  std::cout << "Parsed " << result.len << " / " << input.size()
            << " characters in " << duration << "ms: "
            << ((1000 * double(result.len) / double(duration)) / 1'000'000)
            << " Mo/s\n";

  EXPECT_TRUE(result.ret);
}
