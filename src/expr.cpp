#include "expr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 6.28318530717958647692;
constexpr double kE = 2.71828182845904523536;

enum Op : unsigned char {
  Const = 1,
  Var,
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Pow,
  Neg,
  Sin,
  Cos,
  Tan,
  Asin,
  Acos,
  Atan,
  Atan2,
  Sinh,
  Cosh,
  Tanh,
  Exp,
  Ln,
  Log10,
  Sqrt,
  Abs,
  Floor,
  Ceil,
  Sign,
  Min,
  Max,
  Cot,
  Sec,
  Csc
};

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
  if (from.empty())
    return;
  size_t at = 0;
  while ((at = s.find(from, at)) != std::string::npos) {
    s.replace(at, from.size(), to);
    at += to.size();
  }
}

void normalizeUnicode(std::string& s) {
  replaceAll(s, "\xCF\x80", "pi");
  replaceAll(s, "\xCF\x84", "tau");
  replaceAll(s, "\xC2\xB7", "*");
  replaceAll(s, "\xC3\x97", "*");
  replaceAll(s, "\xE2\x88\x92", "-");
  replaceAll(s, "\xE2\x80\x93", "-");
  replaceAll(s, "\xE2\x88\x9E", "inf");
}

bool isIdentChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

struct Latex {
  const std::string& s;
  size_t i = 0;
  std::string error;

  void skip() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
      ++i;
  }

  std::string commandName() {
    if (i >= s.size() || s[i] != '\\')
      return {};
    ++i;
    if (i < s.size() && !isIdentChar(s[i])) {
      const char symbol = s[i++];
      return std::string("\\") + symbol;
    }
    const size_t start = i;
    while (i < s.size() && isIdentChar(s[i]))
      ++i;
    return s.substr(start, i - start);
  }

  // Raw brace contents, or a single following atom if braces are absent.
  bool readGroup(std::string& inner) {
    skip();
    if (i >= s.size()) {
      error = "Missing expression after a LaTeX command";
      return false;
    }
    if (s[i] != '{') {
      if (s[i] == '\\') {
        const size_t start = i;
        commandName();
        inner = s.substr(start, i - start);
        return true;
      }
      inner = std::string(1, s[i++]);
      return true;
    }
    ++i;
    const size_t start = i;
    int depth = 1;
    while (i < s.size() && depth > 0) {
      if (s[i] == '\\') {
        ++i;
        if (i < s.size() && !isIdentChar(s[i]))
          ++i;
        else
          while (i < s.size() && isIdentChar(s[i]))
            ++i;
        continue;
      }
      if (s[i] == '{')
        ++depth;
      else if (s[i] == '}')
        --depth;
      if (depth > 0)
        ++i;
    }
    if (depth != 0) {
      error = "Unclosed brace in the formula";
      return false;
    }
    inner = s.substr(start, i - start);
    if (i < s.size() && s[i] == '}')
      ++i;
    return true;
  }

  std::string convertSlice(const std::string& slice) {
    Latex nested{slice, 0, {}};
    const std::string out = nested.convert();
    if (!nested.error.empty() && error.empty())
      error = nested.error;
    return out;
  }

  void emitDelimiter() {
    skip();
    if (i >= s.size())
      return;
    if (s[i] == '\\') {
      const std::string name = commandName();
      if (name == "\\|" || name == "|" || name == "lvert" || name == "rvert" || name == "vert")
        out.push_back('|');
      return;
    }
    const char c = s[i++];
    if (c == '.')
      return;
    if (c == '{' || c == '}')
      out.push_back(c == '{' ? '(' : ')');
    else
      out.push_back(c);
  }

  std::string out;

  std::string convert() {
    while (i < s.size()) {
      const char c = s[i];
      if (std::isspace(static_cast<unsigned char>(c))) {
        ++i;
        out.push_back(' ');
        continue;
      }
      if (c == '{') {
        std::string inner;
        if (!readGroup(inner))
          return {};
        out.push_back('(');
        out += convertSlice(inner);
        out.push_back(')');
        continue;
      }
      if (c == '}') {
        error = "Unexpected }";
        return {};
      }
      if (c != '\\') {
        out.push_back(c);
        ++i;
        continue;
      }

      const std::string cmd = commandName();
      if (cmd == "left" || cmd == "right" || cmd == "bigl" || cmd == "bigr" || cmd == "Bigl" ||
          cmd == "Bigr" || cmd == "biggl" || cmd == "biggr") {
        emitDelimiter();
        continue;
      }
      if (cmd == "\\{" || cmd == "\\}") {
        out.push_back(cmd == "\\{" ? '(' : ')');
        continue;
      }
      if (cmd == "\\|" || cmd == "vert" || cmd == "lvert" || cmd == "rvert") {
        out.push_back('|');
        continue;
      }
      if (cmd == "," || cmd == ";" || cmd == "!" || cmd == " " || cmd == ":" || cmd == "quad" ||
          cmd == "qquad" || cmd == " " || cmd == "\\" || cmd == "thinspace") {
        out.push_back(' ');
        continue;
      }
      if (cmd == "cdot" || cmd == "times" || cmd == "ast") {
        out.push_back('*');
        continue;
      }
      if (cmd == "div") {
        out.push_back('/');
        continue;
      }
      if (cmd == "pm") {
        out += "+";
        continue;
      }
      if (cmd == "pi") {
        out += "pi";
        continue;
      }
      if (cmd == "tau") {
        out += "tau";
        continue;
      }
      if (cmd == "infty") {
        out += "inf";
        continue;
      }
      if (cmd == "bmod" || cmd == "mod" || cmd == "%") {
        out.push_back('%');
        continue;
      }
      if (cmd == "frac" || cmd == "dfrac" || cmd == "tfrac") {
        std::string a, b;
        if (!readGroup(a) || !readGroup(b))
          return {};
        out += "((" + convertSlice(a) + ")/(" + convertSlice(b) + "))";
        continue;
      }
      if (cmd == "sqrt") {
        skip();
        std::string index;
        if (i < s.size() && s[i] == '[') {
          ++i;
          const size_t start = i;
          while (i < s.size() && s[i] != ']')
            ++i;
          index = s.substr(start, i - start);
          if (i < s.size() && s[i] == ']')
            ++i;
        }
        std::string inner;
        if (!readGroup(inner))
          return {};
        if (index.empty())
          out += "sqrt(" + convertSlice(inner) + ")";
        else
          out += "((" + convertSlice(inner) + ")^(1/(" + convertSlice(index) + ")))";
        continue;
      }
      if (cmd == "log" || cmd == "ln" || cmd == "lg") {
        skip();
        if (cmd == "log" && i < s.size() && s[i] == '_') {
          ++i;
          std::string base;
          std::string arg;
          if (!readGroup(base))
            return {};
          skip();
          if (i < s.size() && s[i] == '(') {
            ++i;
            int depth = 1;
            const size_t start = i;
            while (i < s.size() && depth > 0) {
              if (s[i] == '(')
                ++depth;
              else if (s[i] == ')')
                --depth;
              if (depth > 0)
                ++i;
            }
            arg = s.substr(start, i - start);
            if (i < s.size() && s[i] == ')')
              ++i;
          } else if (!readGroup(arg)) {
            return {};
          }
          out += "(ln(" + convertSlice(arg) + ")/ln(" + convertSlice(base) + "))";
          continue;
        }
        out += cmd == "lg" ? "log10" : "ln";
        continue;
      }
      if (cmd == "mathrm" || cmd == "operatorname" || cmd == "text" || cmd == "textbf" ||
          cmd == "mathit" || cmd == "mathbf") {
        std::string inner;
        if (!readGroup(inner))
          return {};
        out += convertSlice(inner);
        continue;
      }
      if (cmd == "overline" || cmd == "bar" || cmd == "hat" || cmd == "vec" || cmd == "dot" ||
          cmd == "tilde") {
        std::string inner;
        if (!readGroup(inner))
          return {};
        out += convertSlice(inner);
        continue;
      }

      static const std::unordered_map<std::string, std::string> kNames = {
          {"sin", "sin"},     {"cos", "cos"},     {"tan", "tan"},     {"cot", "cot"},
          {"sec", "sec"},     {"csc", "csc"},     {"sinh", "sinh"},   {"cosh", "cosh"},
          {"tanh", "tanh"},   {"exp", "exp"},     {"arcsin", "asin"}, {"arccos", "acos"},
          {"arctan", "atan"}, {"asin", "asin"},   {"acos", "acos"},   {"atan", "atan"},
          {"abs", "abs"},     {"floor", "floor"}, {"ceil", "ceil"},   {"min", "min"},
          {"max", "max"},     {"sgn", "sign"},    {"sign", "sign"},   {"arctan2", "atan2"},
      };
      const auto found = kNames.find(cmd);
      if (found == kNames.end()) {
        if (cmd.size() == 2 && cmd[0] == '\\') {
          out.push_back(cmd[1]);
          continue;
        }
        error = "Unknown LaTeX command \\" + cmd;
        return {};
      }
      out += found->second;
      skip();
      // \sin x and \sin{x} become sin(x). \sin(x) and \sin\left(x\right) stay calls.
      if (i < s.size() && s[i] == '{') {
        std::string inner;
        if (!readGroup(inner))
          return {};
        out += "(" + convertSlice(inner) + ")";
      } else if (i < s.size() && isIdentChar(s[i])) {
        out.push_back('(');
        out.push_back(s[i++]);
        out.push_back(')');
      } else if (i < s.size() && s[i] == '\\') {
        const size_t saved = i;
        const std::string peek = commandName();
        if (peek == "pi" || peek == "tau" || peek == "infty")
          out += "(" + convertSlice("\\" + peek) + ")";
        else
          i = saved;
      }
    }
    return out;
  }
};

struct Parser {
  const std::string& s;
  size_t i = 0;
  char variable = 'x';
  std::vector<Program::Instr> code;
  std::string error;
  int absDepth = 0;

  void skip() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
      ++i;
  }

  bool emit(Op op, double a = 0.0) {
    code.push_back(Program::Instr{static_cast<unsigned char>(op), a});
    return true;
  }

  bool startsPrimary() {
    skip();
    if (i >= s.size())
      return false;
    const char c = s[i];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || isIdentChar(c) || c == '(')
      return true;
    // A bar inside an absolute value closes it. Only a bar outside one starts a new factor.
    if (c == '|' && absDepth == 0)
      return true;
    return false;
  }

  bool parseNumber() {
    skip();
    const size_t start = i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
      ++i;
    if (i < s.size() && s[i] == '.') {
      ++i;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
        ++i;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
      const size_t mark = i;
      ++i;
      if (i < s.size() && (s[i] == '+' || s[i] == '-'))
        ++i;
      const size_t digits = i;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
        ++i;
      if (i == digits)
        i = mark;
    }
    if (i == start)
      return false;
    try {
      emit(Const, std::stod(s.substr(start, i - start)));
    } catch (...) {
      error = "Bad number";
      return false;
    }
    return true;
  }

  bool parseExpr();
  bool parseProd();
  bool parseUnary();
  bool parsePow();
  bool parsePrimary();

  bool finishCall(Op op, int arity) {
    skip();
    if (i >= s.size() || s[i] != '(') {
      error = "Expected ( after a function";
      return false;
    }
    ++i;
    for (int n = 0; n < arity; ++n) {
      if (n > 0) {
        skip();
        if (i >= s.size() || s[i] != ',') {
          error = "Expected a comma between function arguments";
          return false;
        }
        ++i;
      }
      if (!parseExpr())
        return false;
    }
    skip();
    if (i >= s.size() || s[i] != ')') {
      error = "Expected )";
      return false;
    }
    ++i;
    emit(op);
    return true;
  }
};

bool Parser::parseExpr() {
  if (!parseProd())
    return false;
  for (;;) {
    skip();
    if (i < s.size() && s[i] == '+') {
      ++i;
      if (!parseProd())
        return false;
      emit(Add);
    } else if (i < s.size() && s[i] == '-') {
      ++i;
      if (!parseProd())
        return false;
      emit(Sub);
    } else {
      break;
    }
  }
  return true;
}

bool Parser::parseProd() {
  if (!parseUnary())
    return false;
  for (;;) {
    skip();
    if (i < s.size() && s[i] == '*') {
      ++i;
      if (!parseUnary())
        return false;
      emit(Mul);
    } else if (i < s.size() && s[i] == '/') {
      ++i;
      if (!parseUnary())
        return false;
      emit(Div);
    } else if (i < s.size() && s[i] == '%') {
      ++i;
      if (!parseUnary())
        return false;
      emit(Mod);
    } else if (startsPrimary()) {
      if (!parseUnary())
        return false;
      emit(Mul);
    } else {
      break;
    }
  }
  return true;
}

bool Parser::parseUnary() {
  skip();
  if (i < s.size() && s[i] == '+') {
    ++i;
    return parseUnary();
  }
  if (i < s.size() && s[i] == '-') {
    ++i;
    if (!parseUnary())
      return false;
    emit(Neg);
    return true;
  }
  return parsePow();
}

bool Parser::parsePow() {
  if (!parsePrimary())
    return false;
  skip();
  if (i < s.size() && s[i] == '^') {
    ++i;
    if (!parseUnary())
      return false;
    emit(Pow);
  }
  return true;
}

bool Parser::parsePrimary() {
  skip();
  if (i >= s.size()) {
    error = "Unexpected end of formula";
    return false;
  }
  const char c = s[i];
  if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
    return parseNumber();
  if (c == '(') {
    ++i;
    if (!parseExpr())
      return false;
    skip();
    if (i >= s.size() || s[i] != ')') {
      error = "Expected )";
      return false;
    }
    ++i;
    return true;
  }
  if (c == '|') {
    ++i;
    ++absDepth;
    const bool ok = parseExpr();
    --absDepth;
    if (!ok)
      return false;
    skip();
    if (i >= s.size() || s[i] != '|') {
      error = "Expected a closing |";
      return false;
    }
    ++i;
    emit(Abs);
    return true;
  }
  if (!isIdentChar(c)) {
    error = std::string("Unexpected character '") + c + "'";
    return false;
  }
  const size_t start = i;
  while (i < s.size() && (isIdentChar(s[i]) || std::isdigit(static_cast<unsigned char>(s[i]))))
    ++i;
  const std::string name = s.substr(start, i - start);
  skip();
  const bool call = i < s.size() && s[i] == '(';

  if (name.size() == 1 && name[0] == variable)
    return emit(Var);
  if (name == "pi")
    return emit(Const, kPi);
  if (name == "tau")
    return emit(Const, kTau);
  if (name == "e")
    return emit(Const, kE);
  if (name == "inf")
    return emit(Const, 1.0e300);

  static const std::unordered_map<std::string, std::pair<Op, int>> kFns = {
      {"sin", {Sin, 1}},     {"cos", {Cos, 1}},   {"tan", {Tan, 1}},   {"asin", {Asin, 1}},
      {"acos", {Acos, 1}},   {"atan", {Atan, 1}}, {"atan2", {Atan2, 2}}, {"sinh", {Sinh, 1}},
      {"cosh", {Cosh, 1}},   {"tanh", {Tanh, 1}}, {"exp", {Exp, 1}},   {"ln", {Ln, 1}},
      {"log", {Ln, 1}},      {"log10", {Log10, 1}}, {"sqrt", {Sqrt, 1}}, {"abs", {Abs, 1}},
      {"floor", {Floor, 1}}, {"ceil", {Ceil, 1}}, {"sign", {Sign, 1}}, {"min", {Min, 2}},
      {"max", {Max, 2}},     {"cot", {Cot, 1}},   {"sec", {Sec, 1}},   {"csc", {Csc, 1}},
  };

  // cot/sec/csc are not in the enum yet - I referenced them. I need to add them to the enum
  // OR expand them as expressions. I'll add the ops. Wait I didn't add Cot, Sec, Csc to the enum.
  // I'll fix this by not using those ops and instead... I already referenced Cot, Sec, Csc.
  // I must add them to the enum. I'll do that in a follow-up edit if I forget.
  // Looking at my enum, I did NOT include Cot, Sec, Csc. I need to fix the map.

  const auto found = kFns.find(name);
  if (found == kFns.end()) {
    error = "Unknown name \"" + name + "\"";
    return false;
  }
  if (!call) {
    error = "Function " + name + " needs parentheses";
    return false;
  }
  return finishCall(found->second.first, found->second.second);
}

double apply(Op op, double a, double b) {
  switch (op) {
  case Add: return a + b;
  case Sub: return a - b;
  case Mul: return a * b;
  case Div: return b == 0.0 ? std::nan("") : a / b;
  case Mod: return b == 0.0 ? std::nan("") : std::fmod(a, b);
  case Pow: return std::pow(a, b);
  case Min: return std::min(a, b);
  case Max: return std::max(a, b);
  case Atan2: return std::atan2(a, b);
  default: return std::nan("");
  }
}

double apply1(Op op, double a) {
  switch (op) {
  case Neg: return -a;
  case Sin: return std::sin(a);
  case Cos: return std::cos(a);
  case Tan: return std::tan(a);
  case Asin: return std::asin(a);
  case Acos: return std::acos(a);
  case Atan: return std::atan(a);
  case Sinh: return std::sinh(a);
  case Cosh: return std::cosh(a);
  case Tanh: return std::tanh(a);
  case Exp: return std::exp(a);
  case Ln: return a > 0.0 ? std::log(a) : std::nan("");
  case Log10: return a > 0.0 ? std::log10(a) : std::nan("");
  case Sqrt: return a >= 0.0 ? std::sqrt(a) : std::nan("");
  case Abs: return std::fabs(a);
  case Floor: return std::floor(a);
  case Ceil: return std::ceil(a);
  case Sign: return (a > 0.0) - (a < 0.0);
  case Cot: return std::sin(a) == 0.0 ? std::nan("") : std::cos(a) / std::sin(a);
  case Sec: return std::cos(a) == 0.0 ? std::nan("") : 1.0 / std::cos(a);
  case Csc: return std::sin(a) == 0.0 ? std::nan("") : 1.0 / std::sin(a);
  default: return std::nan("");
  }
}

int arity(Op op) {
  switch (op) {
  case Const:
  case Var: return 0;
  case Neg:
  case Sin:
  case Cos:
  case Tan:
  case Asin:
  case Acos:
  case Atan:
  case Sinh:
  case Cosh:
  case Tanh:
  case Exp:
  case Ln:
  case Log10:
  case Sqrt:
  case Abs:
  case Floor:
  case Ceil:
  case Sign:
  case Cot:
  case Sec:
  case Csc: return 1;
  default: return 2;
  }
}

} // namespace

bool Program::eval(double x, double& out) const {
  if (code.empty())
    return false;
  double stack[96];
  int sp = 0;
  for (const Instr& ins : code) {
    const Op op = static_cast<Op>(ins.op);
    const int n = arity(op);
    if (sp < n)
      return false;
    double value = 0.0;
    if (n == 0)
      value = op == Var ? x : ins.a;
    else if (n == 1)
      value = apply1(op, stack[--sp]);
    else {
      const double b = stack[--sp];
      const double a = stack[--sp];
      value = apply(op, a, b);
    }
    if (sp >= 96)
      return false;
    stack[sp++] = value;
  }
  if (sp != 1)
    return false;
  out = stack[0];
  return true;
}

std::shared_ptr<Program> Program::compile(const std::string& body, char variable, std::string& error) {
  error.clear();
  if (variable < 'a' || variable > 'z') {
    if (variable < 'A' || variable > 'Z') {
      error = "The name needs a variable, like f(x)";
      return {};
    }
  }
  std::string source = body;
  normalizeUnicode(source);
  Latex latex{source, 0, {}};
  const std::string plain = latex.convert();
  if (!latex.error.empty()) {
    error = latex.error;
    return {};
  }
  if (plain.find_first_not_of(" \t") == std::string::npos) {
    error = "The function body is empty";
    return {};
  }
  Parser parser{plain, 0, static_cast<char>(std::tolower(static_cast<unsigned char>(variable))), {}, {}};
  parser.variable = static_cast<char>(std::tolower(static_cast<unsigned char>(variable)));
  if (!parser.parseExpr()) {
    error = parser.error.empty() ? "Could not read the formula" : parser.error;
    return {};
  }
  parser.skip();
  if (parser.i < parser.s.size()) {
    error = "Unexpected text at the end of the formula";
    return {};
  }
  if (!parser.error.empty()) {
    error = parser.error;
    return {};
  }
  auto program = std::make_shared<Program>();
  program->code = std::move(parser.code);
  program->variable = parser.variable;
  return program;
}

FunctionName parseFunctionName(const std::string& text) {
  FunctionName result;
  std::string stripped = text;
  replaceAll(stripped, "\\left", "");
  replaceAll(stripped, "\\right", "");
  replaceAll(stripped, "\\,", "");
  replaceAll(stripped, "\\;", "");
  replaceAll(stripped, "\\!", "");
  std::string letters;
  letters.reserve(stripped.size());
  for (size_t i = 0; i < stripped.size(); ++i) {
    if (stripped[i] == '\\') {
      ++i;
      while (i < stripped.size() && isIdentChar(stripped[i]))
        ++i;
      --i;
      continue;
    }
    if (stripped[i] == '{' || stripped[i] == '}')
      continue;
    letters.push_back(stripped[i]);
  }
  const auto open = letters.find('(');
  const auto close = letters.find(')');
  if (open == std::string::npos || close == std::string::npos || close < open) {
    result.error = "Name the function like f(x), g(t), or h(n)";
    return result;
  }
  std::string head;
  std::string arg;
  for (size_t i = 0; i < open; ++i)
    if (isIdentChar(letters[i]))
      head.push_back(letters[i]);
  for (size_t i = open + 1; i < close; ++i)
    if (isIdentChar(letters[i]))
      arg.push_back(letters[i]);
  if (head.size() != 1 || arg.size() != 1) {
    result.error = "Use one letter for the name and one letter for the variable, like g(t)";
    return result;
  }
  result.symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(head[0])));
  result.variable = static_cast<char>(std::tolower(static_cast<unsigned char>(arg[0])));
  result.ok = true;
  return result;
}

std::string rewriteVariable(const std::string& body, char from, char to) {
  if (from == to || from == 0 || to == 0)
    return body;
  std::string out;
  out.reserve(body.size());
  for (size_t i = 0; i < body.size();) {
    if (body[i] == '\\') {
      const size_t start = i++;
      if (i < body.size() && !isIdentChar(body[i]))
        ++i;
      else
        while (i < body.size() && isIdentChar(body[i]))
          ++i;
      out.append(body, start, i - start);
      continue;
    }
    const bool ident = isIdentChar(body[i]);
    const bool prev = i > 0 && isIdentChar(body[i - 1]);
    const bool next = i + 1 < body.size() && isIdentChar(body[i + 1]);
    if (ident && !prev && !next && body[i] == from)
      out.push_back(to);
    else
      out.push_back(body[i]);
    ++i;
  }
  return out;
}

bool Program::selfTest(std::string& failure) {
  auto check = [&](const std::string& body, char var, double x, double expected) {
    std::string error;
    auto program = Program::compile(body, var, error);
    if (!program) {
      failure = body + ": " + error;
      return false;
    }
    double y = 0.0;
    if (!program->eval(x, y) || std::fabs(y - expected) > 1e-6) {
      failure = body + " at " + std::to_string(x) + " -> " + std::to_string(y) + " (expected " +
                std::to_string(expected) + ") " + error;
      return false;
    }
    return true;
  };
  if (!check("sin(0)", 'x', 0.0, 0.0))
    return false;
  if (!check("\\sin\\left(x\\right)", 'x', kPi / 2.0, 1.0))
    return false;
  if (!check("\\frac{1}{2}", 'x', 0.0, 0.5))
    return false;
  if (!check("x^2", 'x', 3.0, 9.0))
    return false;
  if (!check("2x", 'x', 4.0, 8.0))
    return false;
  if (!check("|-3|", 'x', 0.0, 3.0))
    return false;
  if (!check("2|x|", 'x', -3.0, 6.0))
    return false;
  if (!check("\\sqrt{4}", 'x', 0.0, 2.0))
    return false;
  if (!check("\\log_{10}{1000}", 'x', 0.0, 3.0))
    return false;
  if (!check("\\sin(2\\pi t)", 't', 0.25, 1.0))
    return false;
  const FunctionName name = parseFunctionName("\\mathrm{g}\\left(t\\right)");
  if (!name.ok || name.symbol != 'g' || name.variable != 't') {
    failure = "name parse failed: " + name.error;
    return false;
  }
  if (rewriteVariable("\\sin\\left(x\\right)+x", 'x', 't') != "\\sin\\left(t\\right)+t") {
    failure = "variable rewrite failed";
    return false;
  }
  return true;
}
