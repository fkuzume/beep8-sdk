#pragma once
#include <span>
#include <submath.h>
#include <sublibc.h>

template <typename T>
class TableStepper {
  u16 up = 0;
  std::span<const T> table;
  T _ref;
public:
  void setTable(std::span<const T> table_) {
    up = 0;
    table = table_;
  }

  T step() {
    static const T default_value = T();
    _ref = table.empty() ? default_value : clamp_to_edge(table, up++);
    return  _ref;
  }

  const T& get() {
    return _ref;
  }

  TableStepper(){
    _ref = T();
  }
};
