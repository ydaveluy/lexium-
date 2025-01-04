

#pragma once

#include <any>
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <pegium/IParser.hpp>
#include <pegium/grammar.hpp>
#include <pegium/syntax-tree.hpp>
#include <string>
#include <string_view>

namespace pegium {

class Parser : public IParser {
public:
  ParseResult parse(const std::string &input) const override { return {}; }
  ParseResult parse(const std::string &name, std::string_view text) const {
    auto c = createContext();
    auto result = _rules.at(name)->parse(text, c);

    // result.value = getValue(*result.root_node);
    return result;
  }
  ~Parser() noexcept override = default;

protected:
  template <typename T>
    requires std::derived_from<T, AstNode>
  grammar::ParserRule &rule(std::string name) {
    auto rule = std::make_shared<grammar::ParserRule>(
        /*name, [this] { return this->createContext(); }, make_converter<T>()*/);

    _rules[name] = rule;
    return *rule.get();
  }

  template <typename T = std::string, typename Func>
    requires(!std::derived_from<T, AstNode>) &&
            std::same_as<std::invoke_result_t<Func, const CstNode &>, T>
  grammar::DataTypeRule &rule(std::string name, Func &&func) {
    auto rule = std::make_shared<grammar::DataTypeRule>(std::forward<Func>(func)
        /*name, [this] { return this->createContext(); }, make_converter<T>()*/);

    _rules[name] = rule;
    return *rule.get();
  }
  template <typename T = std::string>
    requires(!std::derived_from<T, AstNode>)
  grammar::DataTypeRule &rule(std::string name) {
    // TODO provide value_converter for "standard" types
    return rule<T>(name, [](const CstNode &node) {
      std::string result;
      for (auto &n : node)
        if (n.isLeaf && !n.hidden)
          result += n.text;
      return result;
    });
  }

  template <typename T = std::string, typename Func>
    requires(!std::derived_from<T, AstNode>) &&
            std::same_as<std::invoke_result_t<Func, const CstNode &>, T>
  grammar::TerminalRule &terminal(std::string name, Func &&func) {

    auto rule = std::make_shared<grammar::TerminalRule>(std::forward<Func>(func)
        /*name, [this] { return this->createContext(); }, make_converter<T>()*/);

    _rules[name] = rule;
    return *rule.get();
  }
    template <typename T = std::string>
    requires(!std::derived_from<T, AstNode>)
  grammar::TerminalRule &terminal(std::string name, const T& value) {
    return terminal<T>(
        name, [value](const CstNode &node) { return value; });
  }

  template <typename T = std::string>
    requires(!std::derived_from<T, AstNode>)
  grammar::TerminalRule &terminal(std::string name) {
    // TODO provide value_converter for "standard" types
    return terminal<T>(
        name, [](const CstNode &node) { return std::string{node.text}; });
  }

  /// Call an other rule
  /// @param name the rule name
  /// @return the call element
  grammar::RuleCall call(const std::string &name) {
    return grammar::RuleCall{_rules[name]};
  }

private:
  grammar::Context createContext() const {

    std::vector<const grammar::TerminalRule *> hiddens;
    for (auto &[_, def] : _rules) {
      if (const auto *terminal =
              dynamic_cast<grammar::TerminalRule *>(def.get())) {
        if (terminal->hidden())
          hiddens.push_back(terminal);
      }
    }

    return grammar::Context{std::move(hiddens)};
  }

  std::map<std::string, std::shared_ptr<grammar::IRule>, std::less<>> _rules;
};

} // namespace pegium