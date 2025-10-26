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

// function to find top-level '=' that's not part of ==, <=, >=, !=
static size_t findTopLevelEquals(const std::string& str) {
  int depth = 0;

  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
    } else if (c == '=' && depth == 0) {
      // check it's not part of ==, <=, >=, !=
      bool isOperator = false;
      if (i > 0 && (str[i-1] == '=' || str[i-1] == '<' || str[i-1] == '>' || str[i-1] == '!')) {
        isOperator = true;
      }
      if (i + 1 < str.size() && str[i+1] == '=') {
        isOperator = true;
      }
      
      if (!isOperator) {
        return i;
      }
    }
  }
  return std::string::npos;
}

//function to check for == operator specifically
static bool hasEqualsEqualsOperator(const std::string& str) {
  int depth = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
    } else if (depth == 0) {
      if (c == '=' && i + 1 < str.size() && str[i+1] == '=') {
        return true;
      }
    }
  }
  return false;
}

// function to check if expression contains inequality operators
static bool hasInequalityOperator(const std::string& str) {
  int depth = 0;
  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
    } else if (depth == 0) {
      // check for <, >, <=, >=, !=
      if (c == '<' || c == '>') {
        return true;
      }
      if (c == '!' && i + 1 < str.size() && str[i+1] == '=') {
        return true;
      }
    }
  }
  return false;
}

#endif  // IMGRAPH_FUNCS_HPP

