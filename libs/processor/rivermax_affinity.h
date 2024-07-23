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
#include "rivermax_os_affinity.h"

namespace rivermax
{
namespace libs
{

class Affinity : public OsSpecificAffinity
{
public:
    using mask = rmax_cpu_set_t;

protected:
    static const os_api default_api;

public:
    Affinity(const os_api &os_api = default_api);
    ~Affinity();
    void set(std::thread &thread, const size_t processor);
    void set(std::thread &thread, const mask &cpu_mask);
    void set(const size_t processor);
    void set(const mask &cpu_mask);
private:
    void fill_with(const mask &cpu_mask, editor &editor);
};

bool set_affinity(const size_t processor) noexcept;

bool set_affinity(const Affinity::mask &cpu_mask) noexcept;

} // namespace libs
} // namespace rivermax
