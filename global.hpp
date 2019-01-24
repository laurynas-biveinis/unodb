// Copyright 2019 Laurynas Biveinis
#ifndef UNODB_GLOBAL_HPP_
#define UNODB_GLOBAL_HPP_

#include "config.hpp"

#ifdef DEBUG
// Nothing
#else
#define NDEBUG
#define GSL_UNENFORCED_ON_CONTRACT_VIOLATION
#endif

#endif  // UNODB_GLOBAL_HPP_
