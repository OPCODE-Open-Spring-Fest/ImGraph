//
// Created by nishant on 09/10/25.
//

#ifndef IMGRAPH_FUNCS_HPP
#define IMGRAPH_FUNCS_HPP
#include <numbers>
#include <string>
#include <cctype>
#include "exprtk.hpp"

inline void addConstants(exprtk::symbol_table<double> &symbolTable) {
  symbolTable.add_constant("e", std::numbers::e);
  symbolTable.add_constant("π", std::numbers::pi); // "pi" is already added by add_constants()
  symbolTable.add_constant("phi", std::numbers::phi);
  symbolTable.add_constant("ϕ", std::numbers::phi);
  symbolTable.add_constant("φ", std::numbers::phi);
  symbolTable.add_constant("gamma", std::numbers::egamma);
  symbolTable.add_constant("γ", std::numbers::egamma);
}

inline std::string trim(const std::string& s) {
  const char* ws = " \t\n\r";
  size_t start = s.find_first_not_of(ws);
  size_t end = s.find_last_not_of(ws);
  if (start == std::string::npos)
    return std::string();
  return s.substr(start, end - start + 1);
}

#endif  // IMGRAPH_FUNCS_HPP

