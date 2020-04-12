// Copyright 2019-2020 Laurynas Biveinis

#include "global.hpp"

#include "test_utils.hpp"

#include "art.hpp"
#include "mutex_art.hpp"

template class tree_verifier<unodb::db>;
template class tree_verifier<unodb::mutex_db>;
