#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <pegium/Parser.hpp>

namespace Xsmp {
struct Type;
struct Attribute : public pegium::AstNode {
  reference<Type> type;
};
struct NamedElement : public pegium::AstNode {
  string name;
  vector<containment<Attribute>> attributes;
};
struct VisibilityElement : public NamedElement {
  vector<string> modifiers;
};
struct Namespace;
struct Catalogue : public NamedElement {
  vector<containment<Namespace>> namespaces;
};

struct Namespace : public NamedElement {
  vector<containment<NamedElement>> members;
};

struct Type : public VisibilityElement {};
struct Structure : public Type {
  vector<containment<NamedElement>> members;
};

struct Class : public Structure {};

class XsmpParser : public pegium::Parser {
public:
  XsmpParser() {

    using namespace pegium::literals;
    terminal("WS").ignore()(+s);
    terminal("SL_COMMENT").hide()("//"_kw >> &(eol | eof));
    terminal("ML_COMMENT").hide()("/*"_kw >> "*/"_kw);
    terminal("ID")(cls("a-zA-Z_"), *w);
    rule("QualifiedName")(at_least_one_sep('.'_kw, call("ID")));

    static const auto Attributes =
        *append<&NamedElement::attributes>(call("Attribute"));
    auto Name = assign<&NamedElement::name>(call("ID"));
    auto Visibilities =
        *append<&VisibilityElement::modifiers>(call("Visibility"));

    rule<Attribute>("Attribute")(
        '@'_kw, assign<&Attribute::type>(call("QualifiedName")),
        opt('('_kw, ')'_kw));

    rule<Catalogue>("Catalogue")(
        Attributes, "catalogue"_kw, Name,
        many(append<&Catalogue::namespaces>(call("Namespace"))));

    rule<Namespace>("Namespace")(
        Attributes, "namespace"_kw, Name, '{'_kw,
        many(append<&Namespace::members>(call("Namespace") | call("Type"))),
        '}'_kw);

    rule<std::string>("Visibility")("private"_kw | "protected"_kw |
                                    "public"_kw);

    rule<Type>("Type")(call("Structure") | call("Class"));

    rule<Structure>("Structure")(Attributes, Visibilities, "struct"_kw, Name,
                                 '{'_kw,
                                 /*many(&Structure::members += call("Constant")
                                    | call("Field")),*/
                                 '}'_kw);

    rule<Class>("Class")(Attributes,
                         many(append<&VisibilityElement::modifiers>(
                             call("Visibility") | "abstract"_kw)),
                         "class"_kw, Name, '{'_kw,
                         /*many(&Structure::members += call("Constant") |
                            call("Field")),*/
                         '}'_kw);
  }
};

} // namespace Xsmp
TEST(XsmpTest, TestCatalogue) {
  Xsmp::XsmpParser g;
  auto result = g.parse("Catalogue", R"(
    /**
     * A demo catalogue
     */
    catalogue test 
    // a single line comment
    namespace A
    {
      @Abstract()
      public protected private struct MyStruct{}

      private public abstract public class MyClass{}
    }
    namespace B
    {
    }
  //)");
  EXPECT_TRUE(result.ret);
}
