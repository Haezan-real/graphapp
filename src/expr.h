#pragma once

#include <memory>
#include <string>
#include <vector>

// Immutable bytecode for a graph formula. Safe to evaluate from the audio thread.
struct Program {
  struct Instr {
    unsigned char op = 0;
    double a = 0.0;
  };

  std::vector<Instr> code;
  char variable = 'x';

  // Writes the value of the formula at x. Non-finite results are still written.
  // Returns false only when the program itself is empty or corrupt.
  bool eval(double x, double& out) const;

  static std::shared_ptr<Program> compile(const std::string& body, char variable, std::string& error);
  static bool selfTest(std::string& failure);
};

struct FunctionName {
  char symbol = 'f';
  char variable = 'x';
  bool ok = false;
  std::string error;
};

FunctionName parseFunctionName(const std::string& text);

// Replaces standalone occurrences of `from` with `to`, leaving LaTeX commands alone.
std::string rewriteVariable(const std::string& body, char from, char to);
