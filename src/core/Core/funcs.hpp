//
// Created by nishant on 09/10/25.
//

#ifndef IMGRAPH_FUNCS_HPP
#define IMGRAPH_FUNCS_HPP
#include <numbers>

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

#endif  // IMGRAPH_FUNCS_HPP
