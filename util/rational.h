/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _UTILS_RATIONAL_H_
#define _UTILS_RATIONAL_H_

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <type_traits>

class RationalE : public std::runtime_error {
public:
    RationalE(const std::string& what): std::runtime_error(what) {}
};

class Rational {
public:
    Rational();
    Rational(uint64_t integer, uint64_t numerator, uint64_t denominator);
    explicit Rational(uint64_t integer);
    Rational(uint64_t numerator, uint64_t denominator);
    Rational(const Rational& num);

    friend std::ostream& operator<<(std::ostream& os, const Rational& num);

    Rational& operator=(const Rational& num);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational& operator=(T n)
    {
        m_integer = n;
        m_numerator = 0;
        m_denominator = 1;

        return *this;
    }

    Rational operator+(const Rational& num) const;
    Rational operator-(const Rational& num) const;
    Rational operator*(const Rational& num) const;
    Rational operator/(const Rational& num) const;

    Rational& operator+=(const Rational& num);
    Rational& operator-=(const Rational& num);
    Rational& operator*=(const Rational& num);
    Rational& operator/=(const Rational& num);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational operator+(T n) const
    {
        return *this + Rational(n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational& operator+=(T n)
    {
        return *this += Rational(n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational operator-(T n) const
    {
        return *this - Rational(n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational& operator-=(T n)
    {
        return *this -= Rational(n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational operator*(T n) const
    {
        return *this * Rational(n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational& operator*=(T n)
    {
        return *this *= Rational(n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational operator/(T n) const
    {
        return *this / Rational(n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    Rational& operator/=(const T n)
    {
        return *this /= Rational(n);
    }

    bool operator==(const Rational& num) const;
    bool operator!=(const Rational& num) const;
    bool operator<(const Rational& num) const;

    bool operator>(const Rational& num) const
    {
        return (num < *this);
    }

    bool operator<=(const Rational& num) const
    {
        return !(num < *this);
    }

    bool operator>=(const Rational& num) const
    {
        return (num <= *this);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    bool operator==(T n) const
    {
        return this->m_integer == n && !this->m_numerator;
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    bool operator!=(T n) const
    {
        return !(*this == n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    bool operator<(T n) const
    {
        return (this->m_integer < n);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    bool operator<=(T n) const
    {
        if (n < this->m_integer)
            return false;

        return (this->m_integer < n || !this->m_numerator);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    bool operator>(T n) const
    {
        if (this->m_integer < n)
            return false;

        return this->m_integer > n || this->m_numerator;
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    bool operator>=(T n) const
    {
        return (this->m_integer >= n);
    }

    uint64_t integer() const
    {
        return m_integer;
    }

    uint64_t numerator() const
    {
        return m_numerator;
    }

    uint64_t denominator() const
    {
        return m_denominator;
    }

    explicit operator bool() const
    {
        return m_integer || m_numerator;
    }

private:
    static uint64_t gcd(uint64_t i1, uint64_t i2);
    static uint64_t lcd(uint64_t i1, uint64_t i2);
    static uint64_t reduce_two(uint64_t& i1, uint64_t& i2);
    static void reduce(uint64_t& i1, uint64_t& i2);

    void init(uint64_t integer, uint64_t numerator, uint64_t denominator);
    void init();
    Rational mul_div(const Rational& num1, const Rational& num2, bool is_multiply) const;
    Rational& mul_div_assign(const Rational& num1, const Rational& num2, bool is_multiply);
    Rational add_sub(const Rational& num1, const Rational& num2, bool is_add) const;
    Rational& add_sub_assign(const Rational& num, bool is_add);

    uint64_t m_integer;
    uint64_t m_numerator;
    uint64_t m_denominator;
};

template <typename T> inline T rational_cast(const Rational& a)
{
    T ret = static_cast<T>(a.numerator());
    ret /= static_cast<T>(a.denominator());
    ret += static_cast<T>(a.integer());
    return ret;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
Rational operator+(T a, const Rational& b)
{
    return b + a;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
Rational operator-(T a, const Rational& b)
{
    return Rational(a) - b;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
Rational operator*(T a, const Rational& b)
{
    return b * a;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
Rational operator/(T a, const Rational& b)
{
    return Rational(a) / b;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
bool operator<(T a, const Rational& b)
{
    return b > a;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
bool operator<=(T a, const Rational& b)
{
    return b >= a;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
bool operator>(T a, const Rational& b)
{
    return b < a;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
bool operator>=(T a, const Rational& b)
{
    return b <= a;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
bool operator==(T a, const Rational& b)
{
    return b == a;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
bool operator!=(T a, const Rational& b)
{
    return b != a;
}

namespace std {
  inline string to_string(const Rational& r) {
    std::ostringstream ss;
    ss << r;
    return ss.str();
  }
}

#endif // _UTILS_RATIONAL_H_
