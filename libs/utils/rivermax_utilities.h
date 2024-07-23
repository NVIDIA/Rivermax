/*
 * Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */
#pragma once
#include <cstddef>

namespace rivermax {
namespace libs {

template <typename T, typename B>
bool is_bit_set(const T bitmap[], B bit) 
{
  constexpr auto entry_bit_size  = sizeof(T) * 8;
  return (bitmap[bit / entry_bit_size] & (1ULL << (bit % entry_bit_size))) != 0;
}

}
}
