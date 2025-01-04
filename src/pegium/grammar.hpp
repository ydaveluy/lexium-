#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <pegium/IParser.hpp>
#include <pegium/syntax-tree.hpp>
#include <ranges>
#include <source_location>
#include <string_view>
#include <utility>
#include <vector>

namespace pegium::grammar {

consteval auto make_tolower() {
  std::array<unsigned char, 256> lookup{};
  for (int c = 0; c < 256; ++c) {
    if (c >= 'A' && c <= 'Z') {
      lookup[c] = static_cast<unsigned char>(c) + ('a' - 'A');
    } else {
      lookup[c] = static_cast<unsigned char>(c);
    }
  }
  return lookup;
}
static constexpr auto tolower_array = make_tolower();

/// Fast helper function to convert a char to lower case
/// @param c the char to convert
/// @return the lower case char
constexpr char tolower(char c) {
  return static_cast<char>(tolower_array[static_cast<unsigned char>(c)]);
}

/// Build an array of char (remove the ending '\0')
/// @tparam N the number of char without the ending '\0'
template <std::size_t N> struct char_array_builder {
  std::array<char, N - 1> value;
  explicit(false) consteval char_array_builder(char const (&pp)[N]) {
    for (std::size_t i = 0; i < value.size(); ++i) {
      value[i] = pp[i];
    }
  }
};

static constexpr std::size_t PARSE_ERROR =
    std::numeric_limits<std::size_t>::max();
constexpr bool success(std::size_t len) { return len != PARSE_ERROR; }

constexpr bool fail(std::size_t len) { return len == PARSE_ERROR; }
class Context;
struct IElement {
  constexpr virtual ~IElement() noexcept = default;
  // parse the input text from a terminal: no hidden/ignored token between
  // elements
  virtual std::size_t parse_terminal(std::string_view sv) const noexcept = 0;
  // parse the input text from a rule: hidden/ignored token between elements are
  // skipped
  virtual std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                 Context &c) const = 0;
};

template <typename T>
concept IsGrammarElement = std::derived_from<T, IElement>;

struct IRule : IElement {
  virtual ParseResult parse(std::string_view text, Context &c) const = 0;
  virtual std::any getValue(const CstNode &node) const = 0;
};

struct TerminalRule;
class Context final {
public:
  explicit Context(std::vector<const TerminalRule *> &&hiddens)
      : _hiddens{std::move(hiddens)} {
    // TODO check dynamically that all rules are Terminal Rules
  }

  std::size_t skipHiddenNodes(std::string_view sv, CstNode &node) const;

private:
  std::vector<const TerminalRule *> _hiddens;
};
using ContextProvider = std::function<Context()>;

struct RuleCall final : IElement {

  // here the rule parameter is a reference on the shared_ptr instead of on the
  // rule because the rule may not be allocated/initialized yet
  explicit RuleCall(const std::shared_ptr<IRule> &rule) : _rule{rule} {}

  std::size_t parse_rule(std::string_view sv, CstNode &parent,
                         Context &c) const override {
    assert(_rule && "Call of an undefined rule.");
    return _rule->parse_rule(sv, parent, c);
  }

  std::size_t parse_terminal(std::string_view sv) const noexcept override {
    assert(_rule && "Call of an undefined rule.");
    return _rule->parse_terminal(sv);
  }

private:
  const std::shared_ptr<IRule> &_rule;
};
struct RuleWrapper final : IElement {

  // here the rule parameter is a reference on the shared_ptr instead of on the
  // rule because the rule may not be allocated/initialized yet
  explicit RuleWrapper(const IRule &rule) : _rule{rule} {}

  std::size_t parse_rule(std::string_view sv, CstNode &parent,
                         Context &c) const override {
    return _rule.parse_rule(sv, parent, c);
  }

  std::size_t parse_terminal(std::string_view sv) const noexcept override {
    return _rule.parse_terminal(sv);
  }

private:
  const IRule &_rule;
};
template <typename T>
concept IsRuleCall = std::same_as<T, RuleCall> || std::same_as<T, RuleWrapper>;

struct ParserRule final : IRule {
  ParserRule() = default;
  ParserRule(const ParserRule &) = delete;
  ParserRule &operator=(const ParserRule &) = delete;

  std::any getValue(const CstNode &node) const override {

    // TODO iterate over all nodes with an assignment or action or rull call
    // Instantiate the current object when the first assignment is encountered
    // skip DataRule & TerminalRule
    for (auto &n : node) {
    }
    return 0;
  }

  ParseResult parse(std::string_view text, Context &c) const override {
    ParseResult result;
    result.root_node = std::make_shared<RootCstNode>();
    result.root_node->fullText = text;
    std::string_view sv = result.root_node->fullText;
    result.root_node->text = result.root_node->fullText;
    result.root_node->grammarSource = this;

    auto i = c.skipHiddenNodes(sv, *result.root_node);

    result.len =
        i + parse_rule({sv.data() + i, sv.size() - i}, *result.root_node, c);

    result.ret = result.len == sv.size();

    return result;
  }
  std::size_t parse_terminal(std::string_view sv) const noexcept override {
    return _element->parse_terminal(sv);
  }
  std::size_t parse_rule(std::string_view sv, CstNode &parent,
                         Context &c) const override {
    auto size = parent.content.size();
    auto &node = parent.content.emplace_back();
    auto i = _element->parse_rule(sv, node, c);
    if (fail(i)) {
      parent.content.resize(size);
      return PARSE_ERROR;
    }
    node.text = {sv.data(), i};
    node.grammarSource = this;

    return i;
  }

  /// Initialize the rule with an element
  /// @tparam Element
  /// @param element the element
  /// @return a reference to the rule
  template <typename Element>
    requires(IsGrammarElement<Element>)
  RuleWrapper operator=(Element element) {
    _element = std::make_shared<std::decay_t<Element>>(element);
    return RuleWrapper{*this};
  }

private:
  std::shared_ptr<IElement> _element;
};
struct DataTypeRule final : IRule {
  DataTypeRule(const DataTypeRule &) = delete;
  DataTypeRule &operator=(const DataTypeRule &) = delete;
  template <typename Func>
    requires(!std::same_as<std::decay_t<Func>, DataTypeRule>)
  explicit DataTypeRule(Func &&value_converter)
      : _value_converter{std::forward<Func>(value_converter)} {}

  std::any getValue(const CstNode &node) const override {
    return _value_converter(node);
  }

  ParseResult parse(std::string_view text, Context &c) const override {
    ParseResult result;
    result.root_node = std::make_shared<RootCstNode>();
    result.root_node->fullText = text;
    std::string_view sv = result.root_node->fullText;
    result.root_node->text = result.root_node->fullText;
    result.root_node->grammarSource = this;

    // skip leading hidden nodes
    auto i = c.skipHiddenNodes(sv, *result.root_node);

    result.len =
        i + parse_rule({sv.data() + i, sv.size() - i}, *result.root_node, c);

    result.ret = result.len == sv.size();
    result.value = getValue(*result.root_node);

    return result;
  }

  std::size_t parse_terminal(std::string_view sv) const noexcept override {
    return _element->parse_terminal(sv);
  }
  std::size_t parse_rule(std::string_view sv, CstNode &parent,
                         Context &c) const override {

    auto size = parent.content.size();
    auto &node = parent.content.emplace_back();
    auto i = _element->parse_rule(sv, node, c);
    if (fail(i)) {
      parent.content.resize(size);
      return PARSE_ERROR;
    }
    node.text = {sv.data(), i};
    node.grammarSource = this;

    return i;
  }

  /// Initialize the rule with an element
  /// @tparam Element
  /// @param element the element
  /// @return a reference to the rule

  template <typename Element>
    requires(IsGrammarElement<Element>)
  RuleWrapper operator=(Element element) {
    _element = std::make_shared<std::decay_t<Element>>(element);
    return RuleWrapper{*this};
  }

private:
  std::shared_ptr<IElement> _element;
  std::function<std::any(const CstNode &)> _value_converter;
};

struct TerminalRule final : IRule {
  TerminalRule(const TerminalRule &) = delete;
  TerminalRule &operator=(const TerminalRule &) = delete;
  template <typename Func>
    requires(!std::same_as<std::decay_t<Func>, TerminalRule>)
  explicit TerminalRule(Func &&value_converter)
      : _value_converter{std::forward<Func>(value_converter)} {}

  std::any getValue(const CstNode &node) const noexcept override {
    return _value_converter(node);
  }
  ParseResult parse(std::string_view text, Context &c) const override {
    ParseResult result;
    result.root_node = std::make_shared<RootCstNode>();
    result.root_node->fullText = text;
    result.root_node->text = result.root_node->fullText;
    std::string_view sv = result.root_node->fullText;
    result.root_node->grammarSource = this;

    result.len = parse_terminal(sv);
    result.root_node->isLeaf = true;

    result.value = _value_converter(*result.root_node);

    result.ret = result.len == sv.size();

    return result;
  }
  std::size_t parse_terminal(std::string_view sv) const noexcept override {
    return _element->parse_terminal(sv);
  }
  std::size_t parse_rule(std::string_view sv, CstNode &parent,
                         Context &c) const override {
    auto i = _element->parse_terminal(sv);
    if (fail(i)) {
      return PARSE_ERROR;
    }
    // Do not create a node if the rule is ignored
    assert(_kind != TerminalRule::Kind::Ignored);
    auto &node = parent.content.emplace_back();
    node.text = {sv.data(), i};
    node.grammarSource = this;
    node.isLeaf = true;
    node.hidden = _kind == TerminalRule::Kind::Hidden;

    // skip hidden nodes after the token
    return i + c.skipHiddenNodes({sv.data() + i, sv.size() - i}, parent);
  }

  /// Initialize the rule with an element
  /// @tparam Element
  /// @param element the element
  /// @return a reference to the rule
  template <typename Element>
    requires(IsGrammarElement<Element>)
  RuleWrapper operator=(Element element) {
    _element = std::make_shared<std::decay_t<Element>>(element);
    return RuleWrapper{*this};
  }

  /// @return true if the rule is  hidden or ignored, false otherwise
  bool hidden() const noexcept { return _kind != Kind::Normal; }
  /// @return true if the rule is ignored, false otherwise
  bool ignored() const noexcept { return _kind == Kind::Ignored; }

  TerminalRule &hide() noexcept {
    _kind = Kind::Hidden;
    return *this;
  }
  TerminalRule &ignore() noexcept {
    _kind = Kind::Ignored;
    return *this;
  }

private:
  enum class Kind : std::uint8_t {
    // a terminal mapped to a normal CstNode (non-hidden)
    Normal,
    // a terminal mapped to an hidden CstNode
    Hidden,
    // a terminal not mapped to a CstNode
    Ignored
  };

  std::shared_ptr<IElement> _element;
  Kind _kind = Kind::Normal;
  std::function<std::any(const CstNode &)> _value_converter;
};

inline std::size_t Context::skipHiddenNodes(std::string_view sv,
                                            CstNode &node) const {

  std::size_t i = 0;
  while (true) {

    bool matched = false;

    for (const auto *rule : _hiddens) {
      const auto len = rule->parse_terminal({sv.data() + i, sv.size() - i});
      if (success(len)) {
        assert(len &&
               "An hidden terminal rule must consume at least one character.");

        if (!rule->ignored()) {
          auto &hiddenNode = node.content.emplace_back();
          hiddenNode.text = {sv.data() + i, len};
          hiddenNode.grammarSource = rule;
          hiddenNode.isLeaf = true;
          hiddenNode.hidden = true;
        }

        i += len;
        matched = true;
      }
    }

    if (!matched) {
      break;
    }
  }
  return i;
}

/// Build an array of char (remove the ending '\0')
/// @tparam N the number of char without the ending '\0'
template <std::size_t N> struct range_array_builder {
  std::array<bool, 256> value{};
  explicit(false) constexpr range_array_builder(char const (&s)[N]) {
    std::size_t i = 0;
    while (i < N - 1) {
      if (i + 2 < N - 1 && s[i + 1] == '-') {
        for (auto c = s[i]; c <= s[i + 2]; ++c) {
          value[static_cast<unsigned char>(c)] = true;
        }
        i += 3;
      } else {
        value[static_cast<unsigned char>(s[i])] = true;
        i += 1;
      }
    }
  }
};
static constexpr const auto isword_lookup =
    range_array_builder{"a-zA-Z0-9_"}.value;
constexpr bool isword(char c) {
  return isword_lookup[static_cast<unsigned char>(c)];
}

template <std::array<bool, 256> lookup>
struct CharactersRanges final : IElement {
  constexpr ~CharactersRanges() override = default;

  constexpr std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                   Context &c) const override {
    auto i = CharactersRanges::parse_terminal(sv);
    if (fail(i) || (sv.size() > i && isword(sv[i - 1]) && isword(sv[i]))) {
      return PARSE_ERROR;
    }

    auto &node = parent.content.emplace_back();
    node.text = {sv.data(), i};
    node.grammarSource = this;
    node.isLeaf = true;

    return i + c.skipHiddenNodes({sv.data() + i, sv.size() - i}, parent);
  }
  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {

    return (!sv.empty() && lookup[static_cast<unsigned char>(sv[0])])
               ? 1
               : PARSE_ERROR;
  }
  /// Create an insensitive Characters Ranges
  /// @return the insensitive Characters Ranges
  constexpr auto i() const noexcept {
    return CharactersRanges<toInsensitive()>{};
  }
  /// Negate the Characters Ranges
  /// @return the negated Characters Ranges
  constexpr auto operator!() const noexcept {
    return CharactersRanges<toNegated()>{};
  }

private:
  static constexpr auto toInsensitive() {
    auto newLookup = lookup;
    for (char c = 'a'; c <= 'z'; ++c) {
      auto lower = static_cast<unsigned char>(c);
      auto upper = static_cast<unsigned char>(c - 'a' + 'A');

      newLookup[lower] |= lookup[upper];
      newLookup[upper] |= lookup[lower];
    }
    return newLookup;
  }
  static constexpr auto toNegated() {
    decltype(lookup) newLookup;
    std::ranges::transform(lookup, newLookup.begin(), std::logical_not{});
    return newLookup;
  }
};

/// Concat 2 Characters Ranges
/// @tparam otherLookup the other Characters Ranges
/// @param
/// @return the concatenation of both Characters Ranges
template <std::array<bool, 256> lhs, std::array<bool, 256> rhs>
constexpr auto operator|(CharactersRanges<lhs>,
                         CharactersRanges<rhs>) noexcept {
  auto toOr = [] {
    std::array<bool, 256> newLookup;
    std::ranges::transform(lhs, rhs, newLookup.begin(), std::logical_or{});
    return newLookup;
  };
  return CharactersRanges<toOr()>{};
}

template <range_array_builder builder> consteval auto operator""_cr() {
  return CharactersRanges<builder.value>{};
}

template <auto literal, bool case_sensitive = true>
struct Literal final : IElement {
  constexpr ~Literal() override = default;

  constexpr std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                   Context &c) const override {

    auto i = Literal::parse_terminal(sv);
    if (fail(i) || (isword(literal.back()) && isword(sv[i]))) {
      return PARSE_ERROR;
    }

    auto &node = parent.content.emplace_back();
    node.grammarSource = this;
    node.text = {sv.data(), i};
    node.isLeaf = true;

    return i + c.skipHiddenNodes({sv.data() + i, sv.size() - i}, parent);
  }
  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {

    if (literal.size() > sv.size()) {
      return PARSE_ERROR;
    }
    std::size_t i = 0;
    for (; i < literal.size(); i++) {
      if ((isCaseSensitive() ? sv[i] : tolower(sv[i])) != literal[i]) {
        return PARSE_ERROR;
      }
    }
    return i;
  }

  /// Create an insensitive Literal
  /// @return the insensitive Literal
  constexpr auto i() const noexcept { return Literal<toLower(), false>{}; }

private:
  static constexpr auto toLower() {
    decltype(literal) newLiteral;
    std::ranges::transform(literal, newLiteral.begin(),
                           [](char c) { return tolower(c); });
    return newLiteral;
  }

  static constexpr bool isCaseSensitive() {
    if (!case_sensitive) {
      return std::ranges::none_of(literal, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
      });
    }
    return case_sensitive;
  }
};

template <typename T> struct IsLiteralImpl : std::false_type {};
template <auto literal, bool case_sensitive>
struct IsLiteralImpl<Literal<literal, case_sensitive>> : std::true_type {};

template <typename T>
concept IsLiteral = IsLiteralImpl<T>::value;

template <char_array_builder builder> consteval auto operator""_kw() {
  static_assert(!builder.value.empty(), "A keyword cannot be empty.");
  return Literal<builder.value>{};
}

template <typename... Elements>
  requires(IsGrammarElement<std::decay_t<Elements>> && ...)
struct Group : IElement {
  static_assert(sizeof...(Elements) > 1,
                "A Group shall contains at least 2 elements.");
  constexpr ~Group() override = default;
  std::tuple<Elements...> elements;

  constexpr explicit Group(std::tuple<Elements...> elems)
      : elements{std::move(elems)} {}

  template <typename T>
  constexpr bool parse_rule_element(const T &element, std::string_view sv,
                                    CstNode &parent, Context &c,
                                    std::size_t size, std::size_t &i) const {

    auto len = element.parse_rule({sv.data() + i, sv.size() - i}, parent, c);
    if (fail(len)) {
      parent.content.resize(size);
      i = len;
      return false;
    }
    i += len;
    return true;
  }

  constexpr std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                   Context &c) const override {
    std::size_t i = 0;

    std::apply(
        [&](const auto &...element) {
          auto size = parent.content.size();
          (parse_rule_element(element, sv, parent, c, size, i) && ...);
        },
        elements);

    return i;
  }

  template <typename T>
  constexpr bool parse_terminal_element(const T &element, std::string_view sv,
                                        std::size_t &i) const noexcept {

    auto len = element.parse_terminal({sv.data() + i, sv.size() - i});
    if (fail(len)) {
      i = len;
      return false;
    }
    i += len;
    return true;
  }

  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {
    std::size_t i = 0;

    std::apply(
        [&](const auto &...element) {
          (parse_terminal_element(element, sv, i) && ...);
        },
        elements);

    return i;
  }
};

template <typename... Lhs, typename... Rhs>
constexpr auto operator+(Group<Lhs...> lhs, Group<Rhs...> rhs) {
  return Group<std::decay_t<Lhs>..., std::decay_t<Rhs>...>{
      std::tuple_cat(lhs.elements, rhs.elements)};
}
template <typename... Lhs, typename Rhs>
constexpr auto operator+(Group<Lhs...> lhs, Rhs rhs) {
  return Group<std::decay_t<Lhs>..., std::decay_t<Rhs>>{
      std::tuple_cat(lhs.elements, std::make_tuple(rhs))};
}
template <typename Lhs, typename... Rhs>
constexpr auto operator+(Lhs lhs, Group<Rhs...> rhs) {
  return Group<std::decay_t<Lhs>, std::decay_t<Rhs>...>{
      std::tuple_cat(std::make_tuple(lhs), rhs.elements)};
}

template <typename Lhs, typename Rhs>
constexpr auto operator+(Lhs lhs, Rhs rhs) {
  return Group<std::decay_t<Lhs>, std::decay_t<Rhs>>{std::make_tuple(lhs, rhs)};
}

template <typename... Elements>
  requires(IsGrammarElement<Elements> && ...)
struct UnorderedGroup : IElement {
  static_assert(sizeof...(Elements) > 1,
                "An UnorderedGroup shall contains at least 2 elements.");
  constexpr ~UnorderedGroup() override = default;
  std::tuple<Elements...> elements;

  constexpr explicit UnorderedGroup(std::tuple<Elements...> elems)
      : elements{std::move(elems)} {}

  using ProcessedFlags = std::array<bool, sizeof...(Elements)>;

  template <typename T>
  static constexpr bool
  parse_rule_element(const T &element, std::string_view sv, CstNode &parent,
                     Context &c, std::size_t &i, ProcessedFlags &processed,
                     std::size_t index) {
    if (processed[index]) {
      return false;
    }

    if (auto len =
            element.parse_rule({sv.data() + i, sv.size() - i}, parent, c);
        success(len)) {
      i += len;
      processed[index] = true;
      return true;
    }
    return false;
  }

  constexpr std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                   Context &c) const override {
    std::size_t i = 0;
    ProcessedFlags processed{};

    while (!std::ranges::all_of(processed, [](bool p) { return p; })) {
      bool anyProcessed = std::apply(
          [&](const auto &...element) {
            std::size_t index = 0;
            return (parse_rule_element(element, sv, parent, c, i, processed,
                                       index++) ||
                    ...);
          },
          elements);

      if (!anyProcessed) {
        break;
      }
    }
    return std::ranges::all_of(processed, [](bool p) { return p; })
               ? i
               : PARSE_ERROR;
  }

  template <typename T>
  static constexpr bool
  parse_terminal_element(const T &element, std::string_view sv, std::size_t &i,
                         ProcessedFlags &processed,
                         std::size_t index) noexcept {
    if (processed[index]) {
      return false;
    }

    if (auto len = element.parse_terminal({sv.data() + i, sv.size() - i});
        success(len)) {
      i += len;
      processed[index] = true;
      return true;
    }
    return false;
  }

  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {
    std::size_t i = 0;
    ProcessedFlags processed{};

    while (!std::ranges::all_of(processed, [](bool p) { return p; })) {
      bool anyProcessed = std::apply(
          [&](const auto &...element) {
            std::size_t index = 0;
            return (
                parse_terminal_element(element, sv, i, processed, index++) ||
                ...);
          },
          elements);

      if (!anyProcessed) {
        break;
      }
    }
    return std::ranges::all_of(processed, [](bool p) { return p; })
               ? i
               : PARSE_ERROR;
  }
};

template <typename... Lhs, typename... Rhs>
constexpr auto operator&(UnorderedGroup<Lhs...> lhs,
                         UnorderedGroup<Rhs...> rhs) {
  return UnorderedGroup<std::decay_t<Lhs>..., std::decay_t<Rhs>...>{
      std::tuple_cat(lhs.elements, rhs.elements)};
}
template <typename... Lhs, typename Rhs>
constexpr auto operator&(UnorderedGroup<Lhs...> lhs, Rhs rhs) {
  return UnorderedGroup<std::decay_t<Lhs>..., std::decay_t<Rhs>>{
      std::tuple_cat(lhs.elements, std::make_tuple(rhs))};
}
template <typename Lhs, typename... Rhs>
constexpr auto operator&(Lhs lhs, UnorderedGroup<Rhs...> rhs) {
  return UnorderedGroup<std::decay_t<Lhs>, std::decay_t<Rhs>...>{
      std::tuple_cat(std::make_tuple(lhs), rhs.elements)};
}
template <typename Lhs, typename Rhs>
constexpr auto operator&(Lhs lhs, Rhs rhs) {
  return UnorderedGroup<std::decay_t<Lhs>, std::decay_t<Rhs>>{
      std::make_tuple(lhs, rhs)};
}

template <typename... Elements>
  requires(IsGrammarElement<Elements> && ...)
struct OrderedChoice : IElement {
  static_assert(sizeof...(Elements) > 1,
                "An OrderedChoice shall contains at least 2 elements.");
  constexpr ~OrderedChoice() override = default;
  std::tuple<Elements...> elements;

  constexpr explicit OrderedChoice(std::tuple<Elements...> elems)
      : elements{std::move(elems)} {}

  template <typename T>
  static constexpr bool
  parse_rule_element(const T &element, std::string_view sv, CstNode &parent,
                     Context &c, std::size_t size, std::size_t &i) {
    i = element.parse_rule(sv, parent, c);
    if (success(i)) {
      return true;
    }
    parent.content.resize(size);
    return false;
  }

  constexpr std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                   Context &c) const override {
    std::size_t i = PARSE_ERROR;
    std::apply(
        [&](const auto &...element) {
          auto size = parent.content.size();
          (parse_rule_element(element, sv, parent, c, size, i) || ...);
        },
        elements);

    return i;
  }
  template <typename T>
  static constexpr bool parse_terminal_element(const T &element,
                                               std::string_view sv,
                                               std::size_t &i) noexcept {
    i = element.parse_terminal(sv);
    return success(i);
  }

  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {
    std::size_t i = PARSE_ERROR;

    std::apply(
        [&](const auto &...element) {
          (parse_terminal_element(element, sv, i) || ...);
        },
        elements);
    return i;
  }
};
template <typename... Lhs, typename... Rhs>
constexpr auto operator|(OrderedChoice<Lhs...> lhs, OrderedChoice<Rhs...> rhs) {
  return OrderedChoice<std::decay_t<Lhs>..., std::decay_t<Rhs>...>{
      std::tuple_cat(lhs.elements, rhs.elements)};
}
template <typename... Lhs, typename Rhs>
  requires IsGrammarElement<Rhs>
constexpr auto operator|(OrderedChoice<Lhs...> lhs, Rhs rhs) {
  return OrderedChoice<std::decay_t<Lhs>..., std::decay_t<Rhs>>{
      std::tuple_cat(lhs.elements, std::make_tuple(rhs))};
}
template <typename Lhs, typename... Rhs>
  requires IsGrammarElement<Lhs>
constexpr auto operator|(Lhs lhs, OrderedChoice<Rhs...> rhs) {
  return OrderedChoice<std::decay_t<Lhs>, std::decay_t<Rhs>...>{
      std::tuple_cat(std::make_tuple(lhs), rhs.elements)};
}
template <typename Lhs, typename Rhs>
  requires IsGrammarElement<Lhs> && IsGrammarElement<Rhs>
constexpr auto operator|(Lhs lhs, Rhs rhs) {
  return OrderedChoice<std::decay_t<Lhs>, std::decay_t<Rhs>>{
      std::make_tuple(lhs, rhs)};
}

template <std::size_t min, std::size_t max, typename Element>
  requires IsGrammarElement<Element>
struct Repetition : IElement {
  constexpr ~Repetition() override = default;
  Element element;
  constexpr explicit Repetition(Element element) : element{element} {}

  constexpr std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                   Context &c) const override {
    std::size_t count = 0;
    std::size_t i = 0;
    auto size = parent.content.size();
    while (count < min) {
      auto len = element.parse_rule({sv.data() + i, sv.size() - i}, parent, c);
      if (fail(len)) {
        parent.content.resize(size);
        return len;
      }
      i += len;
      count++;
    }
    while (count < max) {
      size = parent.content.size();
      auto len = element.parse_rule({sv.data() + i, sv.size() - i}, parent, c);
      if (fail(len)) {
        parent.content.resize(size);
        break;
      }
      i += len;
      count++;
    }
    return i;
  }

  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {
    std::size_t count = 0;
    std::size_t i = 0;
    while (count < min) {
      auto len = element.parse_terminal({sv.data() + i, sv.size() - i});
      if (fail(len)) {
        return len;
      }
      i += len;
      count++;
    }
    while (count < max) {
      auto len = element.parse_terminal({sv.data() + i, sv.size() - i});
      if (fail(len)) {
        break;
      }
      i += len;
      count++;
    }
    return i;
  }
};

/// Create an option (zero or one)
/// @tparam Element
/// @param element the element to be repeated
/// @return The created Repetition
template <typename Element>
  requires(IsGrammarElement<Element>)
constexpr auto opt(Element element) {
  return Repetition<0, 1, std::decay_t<Element>>{element};
}

/// Create a repetition of zero or more elements
/// @tparam Element
/// @param element the element to be repeated
/// @return The created Repetition
template <typename Element>
  requires(IsGrammarElement<Element>)
constexpr auto many(Element element) {
  return Repetition<0, std::numeric_limits<std::size_t>::max(),
                    std::decay_t<Element>>{element};
}

/// Create a repetition of one or more elements
/// @tparam Element
/// @param element the element to be repeated
/// @return The created Repetition
template <typename Element>
  requires(IsGrammarElement<Element>)
constexpr auto at_least_one(Element element) {
  return Repetition<1, std::numeric_limits<std::size_t>::max(),
                    std::decay_t<Element>>{element};
}

/// Create a repetition of one or more elements with a separator
/// `element (sep element)*`
/// @tparam Element
/// @param sep the separator to be used between elements
/// @param element the element to be repeated
/// @return The created Repetition
template <typename Sep, typename Element>
  requires IsGrammarElement<Sep> && (IsGrammarElement<Element>)
constexpr auto at_least_one_sep(Sep sep, Element element) {
  return element + many(sep + element);
}

/// Create a repetition of zero or more elements with a separator
/// `(element (sep element)*)?`
/// @tparam Element
/// @param sep the separator to be used between elements
/// @param element the element to be repeated
/// @return The created Repetition
template <typename Sep, typename Element>
  requires IsGrammarElement<Sep> && (IsGrammarElement<Element>)
constexpr auto many_sep(Sep sep, Element element) {
  return opt(at_least_one_sep(sep, element));
}

/// Create a custom repetition with min and max.
/// @tparam Element
/// @param element the elements to be repeated
/// @param min the min number of occurence (inclusive)
/// @param max the maw number of occurence (inclusive)
/// @return The created Repetition
template <std::size_t min, std::size_t max, typename Element>
  requires(IsGrammarElement<Element>)
constexpr auto rep(Element element) {
  return Repetition<min, max, std::decay_t<Element>>{element};
}

/// Create a repetition of one or more elements
/// @tparam Element
/// @param arg the element to be repeated
/// @return The created Repetition
template <typename Element>
  requires IsGrammarElement<Element>
constexpr auto operator+(Element arg) {
  return Repetition<1, std::numeric_limits<std::size_t>::max(),
                    std::decay_t<Element>>{arg};
}

/// Create a repetition of zero or more elements
/// @tparam Element
/// @param arg the element to be repeated
/// @return The created Repetition
template <typename Element>
  requires IsGrammarElement<Element>
constexpr auto operator*(Element arg) {
  return Repetition<0, std::numeric_limits<std::size_t>::max(),
                    std::decay_t<Element>>{arg};
}

template <typename Element>
  requires IsGrammarElement<Element>
struct AndPredicate : IElement {
  constexpr ~AndPredicate() override = default;
  Element element;
  explicit constexpr AndPredicate(Element element) : element{element} {}

  constexpr std::size_t parse_rule(std::string_view sv, CstNode &,
                                   Context &c) const override {
    CstNode node;
    return success(element.parse_rule(sv, node, c)) ? 0 : PARSE_ERROR;
  }
  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {
    return success(element.parse_terminal(sv)) ? 0 : PARSE_ERROR;
  }
};

template <typename Element>
  requires IsGrammarElement<Element>
constexpr auto operator&(Element element) {
  return AndPredicate<std::decay_t<Element>>{element};
}

template <typename Element>
  requires IsGrammarElement<Element>
struct NotPredicate : IElement {
  constexpr ~NotPredicate() override = default;
  Element element;
  explicit constexpr NotPredicate(Element element) : element{element} {}
  constexpr std::size_t parse_rule(std::string_view sv, CstNode &,
                                   Context &c) const override {
    CstNode node;
    return success(element.parse_rule(sv, node, c)) ? PARSE_ERROR : 0;
  }

  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {
    return success(element.parse_terminal(sv)) ? PARSE_ERROR : 0;
  }
};

template <typename Element>
  requires IsGrammarElement<Element>
constexpr auto operator!(Element element) {
  return NotPredicate<std::decay_t<Element>>{element};
}

struct AnyCharacter final : IElement {
  constexpr ~AnyCharacter() noexcept override = default;

  constexpr std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                   Context &c) const override {
    auto i = codepoint_length(sv);
    if (fail(i)) {
      return PARSE_ERROR;
    }
    auto &node = parent.content.emplace_back();
    node.grammarSource = this;
    node.text = {sv.data(), i};
    node.isLeaf = true;

    return i + c.skipHiddenNodes({sv.data() + i, sv.size() - i}, parent);
  }
  constexpr std::size_t
  parse_terminal(std::string_view sv) const noexcept override {
    return codepoint_length(sv);
  }

private:
  static constexpr std::size_t codepoint_length(std::string_view sv) noexcept {
    if (!sv.empty()) {
      auto b = static_cast<std::byte>(sv.front());
      if ((b & std::byte{0x80}) == std::byte{0}) {
        return 1;
      }
      if ((b & std::byte{0xE0}) == std::byte{0xC0} && sv.size() >= 2) {
        return 2;
      }
      if ((b & std::byte{0xF0}) == std::byte{0xE0} && sv.size() >= 3) {
        return 3;
      }
      if ((b & std::byte{0xF8}) == std::byte{0xF0} && sv.size() >= 4) {
        return 4;
      }
    }
    return PARSE_ERROR;
  }
};

template <typename T> struct IsAssignableOrderedChoiceImpl : std::false_type {};

template <typename... Ts>
struct IsAssignableOrderedChoiceImpl<OrderedChoice<Ts...>>
    : std::conjunction<std::bool_constant<IsRuleCall<Ts> || IsLiteral<Ts>>...> {
};

template <typename T>
concept IsAssignableOrderedChoice = IsAssignableOrderedChoiceImpl<T>::value;

template <typename T>
concept IsAssignable =
    IsRuleCall<T> || IsLiteral<T> || IsAssignableOrderedChoice<T>;

struct IAssignment : IElement {
  virtual void execute(AstNode *current, const CstNode &node) const = 0;
};

// Generic
template <typename T> struct AnyConverterAssigner {
  void operator()(T &member, std::any value) const {
    member = std::any_cast<T>(value);
  }
};
template <typename T> struct AnyConverterAssigner<Reference<T>> {
  void operator()(Reference<T> &member, std::any value) const {
    member = std::any_cast<std::string>(value);
  }
};
template <typename T> struct AnyConverterAssigner<std::shared_ptr<T>> {
  void operator()(std::shared_ptr<T> &member, std::any value) const {
    member = std::dynamic_pointer_cast<T>(
        std::any_cast<std::shared_ptr<AstNode>>(value));
  }
};

template <typename T> struct AnyConverterAssigner<std::vector<T>> {
  void operator()(std::vector<T> &member, std::any value) const {
    member.emplace_back(std::any_cast<T>(value));
  }
};
template <typename T>
struct AnyConverterAssigner<std::vector<std::shared_ptr<T>>> {
  void operator()(std::vector<std::shared_ptr<T>> &member,
                  std::any value) const {
    member.emplace_back(std::dynamic_pointer_cast<T>(
        std::any_cast<std::shared_ptr<AstNode>>(value)));
  }
};
template<auto...  Vs> [[nodiscard]] constexpr auto function_name() noexcept -> std::string_view { return std::source_location::current().function_name(); }
template<class... Ts> [[nodiscard]] constexpr auto function_name() noexcept -> std::string_view { return std::source_location::current().function_name(); }

template <auto feature, typename Element>
  requires std::is_member_object_pointer_v<decltype(feature)>
struct Assignment final : IAssignment {
  static_assert(IsAssignable<Element>,
                "An assignment can only contains a RuleCall or a Literal or an "
                "OrderedChoice of RuleCall or Literal.");

  Element element;
  constexpr explicit Assignment(Element element) : element{element} {}
  constexpr std::size_t parse_rule(std::string_view sv, CstNode &parent,
                                   Context &c) const override {

    auto index = parent.content.size();
    auto i = element.parse_rule(sv, parent, c);
    if (success(i)) {
      parent.content[index].action = this;
    }
    return i;
  }
  constexpr std::size_t
  parse_terminal(std::string_view) const noexcept override {
    assert(false && "An Assignment cannot be in a terminal.");
    return PARSE_ERROR;
  }
  void execute(AstNode *current, const CstNode &node) const override {

    assert(node.content.size() == 1);
    const auto *source = node.content.front().grammarSource;

    std::any value;
    // By design the source is either a Rule or a Literal
    // TODO replace dynamic_cast with visitor pattern
    if (const auto *rule = dynamic_cast<const IRule *>(source)) {
      value = rule->getValue(node.content.front());
    } else {
      value = std::string{node.content.front().text};
    }
    do_execute(current, feature, value);
  }

  template <typename C, typename R>
  static void do_execute(AstNode *current, R C::*member, std::any value) {
    auto *node = dynamic_cast<C *>(current);
    assert(node && "Tryed to assign a feature on an AstNode with wrong type.");
    ::pegium::grammar::AnyConverterAssigner<R>{}(node->*member, value);
  }

private:

  /// Helper to get the name of a member from a member object pointer
  /// @tparam e the member object pointer
  /// @return the name of the member
  template <auto e> static constexpr std::string_view member_name() noexcept {
    std::string_view func_name = function_name<e>();
    func_name = func_name.substr(0, func_name.rfind(REF_STRUCT::end_marker));
    return func_name.substr(func_name.rfind("::") + 2);
  }
  struct REF_STRUCT {
    int MEMBER;

    static constexpr auto name = function_name<&REF_STRUCT::MEMBER>();
    static constexpr auto end_marker =
        name.substr(name.find("REF_STRUCT::MEMBER") +
                    std::string_view{"REF_STRUCT::MEMBER"}.size());
  };
};

template <auto e, typename Element>
  requires std::is_member_object_pointer_v<decltype(e)>
static constexpr auto operator+=(auto, Element &&element) {
  return Assignment<e, std::decay_t<Element>>(std::forward<Element>(element));
}

/*template<auto e, typename Element>
requires std::is_member_object_pointer_v<decltype(e)>
static constexpr auto operator +=(auto, Element element){
  return Assignment<e, std::decay_t<Element>>(element);

}*/

/// Assign an element to a member of the current object
/// @tparam Element
/// @tparam e the member pointer
/// @param args the list of grammar elements
/// @return
template <auto e, typename Element>
  requires IsGrammarElement<Element>
static constexpr auto assign(Element element) {
  return Assignment<e, std::decay_t<Element>>(element);
}

/// Append an element to a member of the current object
/// @tparam Element
/// @tparam e the member pointer
/// @param element the  element
/// @return
template <auto e, typename Element>
  requires IsGrammarElement<Element>
static constexpr auto append(Element element) {
  return Assignment<e, std::decay_t<Element>>(element);
}

/// any character equivalent to regex `.`
static constexpr AnyCharacter dot{};
/// The end of file token
static constexpr auto eof = !dot;
/// The end of line token
static constexpr auto eol = "\r\n"_kw | "\n"_kw | "\r"_kw;
/// a space character equivalent to regex `\s`
static constexpr auto s = " \t\r\n\f\v"_cr;
/// a non space character equivalent to regex `\S`
static constexpr auto S = !s;
/// a word character equivalent to regex `\w`
static constexpr auto w = "a-zA-Z0-9_"_cr;
/// a non word character equivalent to regex `\W`
static constexpr auto W = !w;
/// a digit character equivalent to regex `\d`
static constexpr auto d = "0-9"_cr;
/// a non-digit character equivalent to regex `\D`
static constexpr auto D = !d;

/// An until operation that starts from element `from` and ends to element
/// `to`. e.g `"#*"_kw >> "*#"_kw` to match a multiline comment
/// @param from the starting element
/// @param to the ending element
/// @return the until element
template <typename T, typename U>
  requires IsGrammarElement<T> && IsGrammarElement<U>
constexpr auto operator>>(T from, U to) {
  return from + *(!to + dot) + to;
}
} // namespace pegium::grammar