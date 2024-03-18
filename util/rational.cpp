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

#include "rational.h"

Rational::Rational(uint64_t integer, uint64_t numerator, uint64_t denominator)
{
    init(integer, numerator, denominator);
}

Rational::Rational()
    : Rational(0, 0, 1)
{
}

Rational::Rational(uint64_t integer)
    : Rational(integer, 0, 1)
{
}

Rational::Rational(uint64_t nominator, uint64_t denominator)
    : Rational(0, nominator, denominator)
{
}

Rational::Rational(const Rational& num)
{
    m_integer = num.m_integer;
    m_numerator = num.m_numerator;
    m_denominator = num.m_denominator;
}

std::ostream& operator<<(std::ostream& os, const Rational& num)
{
    uint64_t integer = num.integer();
    uint64_t numerator = num.numerator();
    uint64_t denominator = num.denominator();

    os << "{";
    if (integer || !numerator)
        os << integer;
    if (integer && numerator)
        os << " ";
    if (numerator)
        os << numerator << "/" << denominator;
    os << "}";

    return os;
}

void Rational::init(uint64_t integer, uint64_t numerator, uint64_t denominator)
{
    uint64_t quotient;

    if (!denominator) {
        using namespace std;
        throw RationalE{"Rational: denominator cannot be zero: " + to_string(integer) + " " + to_string(numerator) + " / 0"};
    }

    m_integer = integer;
    m_numerator = numerator;
    m_denominator = denominator;

    reduce(m_numerator, m_denominator);

    quotient = m_numerator / m_denominator;
    m_integer += quotient;
    quotient *= m_denominator;
    m_numerator -= quotient;
}

void Rational::init()
{
    init(m_integer, m_numerator, m_denominator);
}

/*
 * Calculates Greatest common divisor
 * https://en.wikipedia.org/wiki/Greatest_common_divisor
 */
uint64_t Rational::gcd(uint64_t a, uint64_t b)
{
    if (a < b)
        std::swap(a, b);

    while (b) {
        uint64_t r;

        r = a % b;
        a = b;
        b = r;
    }

    return a;
}

/*
 * Calculates Lowest common denominator
 * https://en.wikipedia.org/wiki/Lowest_common_denominator
 */
uint64_t Rational::lcd(uint64_t d1, uint64_t d2)
{
    uint64_t r = gcd(d1, d2);

    return d1 / r * d2;
}

uint64_t Rational::reduce_two(uint64_t& i1, uint64_t& i2)
{
    uint64_t i = 0;

    while (!((i1 | i2) & 0x1)) {
        i1 >>= 1;
        i2 >>= 1;
        i++;
    }

    return i;
}

void Rational::reduce(uint64_t& i1, uint64_t& i2)
{
    uint64_t r;

    if (!i1)
        return;

    if (!i2) {
        using namespace std;
        throw RationalE{"Rational: division by zero: " + to_string(i1) + " / 0"};
    }

    reduce_two(i1, i2);
    r = gcd(i1, i2);

    i1 /= r;
    i2 /= r;
}

Rational& Rational::operator=(const Rational& num)
{
    m_integer = num.m_integer;
    m_numerator = num.m_numerator;
    m_denominator = num.m_denominator;

    return *this;
}

Rational Rational::add_sub(const Rational& num1, const Rational& num2, bool is_add) const
{
    uint64_t numerator1 = num1.m_numerator;
    uint64_t numerator2 = num2.m_numerator;
    uint64_t integer;
    uint64_t numerator;
    uint64_t denominator;
    uint64_t multiplier;
    Rational ret;

    denominator = lcd(num1.m_denominator, num2.m_denominator);

    multiplier = denominator / num1.m_denominator;
    numerator1 *= multiplier;
    multiplier = denominator / num2.m_denominator;
    numerator2 *= multiplier;
    if (is_add) {
        integer = num1.m_integer + num2.m_integer;
        numerator = numerator1 + numerator2;
    } else {
        if (num1.m_integer < num2.m_integer) {
            using namespace std;
            throw RationalE{"Rational: negative rationals are not supported: attempted operation " + to_string(num1) + " - " + to_string(num2)};
        }
        integer = num1.m_integer - num2.m_integer;

        if (numerator1 < numerator2) {
            if (integer < 1) {
                using namespace std;
                throw RationalE{"Rational: negative rationals are not supported: attempted operation " + to_string(num1) + " - " + to_string(num2)};
            }
            // fractional part of num2 is less than 1 so adding 1 to the minuend
            // makes the difference positive
            --integer;
            numerator1 += denominator;
        }
        numerator = numerator1 - numerator2;
    }

    ret.init(integer, numerator, denominator);
    return ret;
}

Rational& Rational::add_sub_assign(const Rational& num, bool is_add)
{
    Rational ret = add_sub(*this, num, is_add);

    this->m_integer = ret.m_integer;
    this->m_numerator = ret.m_numerator;
    this->m_denominator = ret.m_denominator;

    return *this;
}

Rational Rational::operator+(const Rational& num) const
{
    return add_sub(*this, num, true);
}

Rational& Rational::operator+=(const Rational& num)
{
    return add_sub_assign(num, true);
}

Rational Rational::operator-(const Rational& num) const
{
    return add_sub(*this, num, false);
}

Rational& Rational::operator-=(const Rational& num)
{
    return add_sub_assign(num, false);
}

Rational Rational::mul_div(const Rational& num1, const Rational& num2, bool is_multiply) const
{
    uint64_t numerator1;
    uint64_t denominator1;
    uint64_t numerator2;
    uint64_t denominator2;
    uint64_t numerator;
    uint64_t denominator;
    Rational ret;

    numerator1 = num1.m_integer * num1.m_denominator + num1.m_numerator;
    denominator1 = num1.m_denominator;
    numerator2 = num2.m_integer * num2.m_denominator + num2.m_numerator;
    denominator2 = num2.m_denominator;

    if (is_multiply) {
        reduce(numerator1, denominator2);
        reduce(numerator2, denominator1);

        numerator = numerator1 * numerator2;
        denominator = denominator1 * denominator2;
    } else {
        reduce(numerator1, numerator2);
        reduce(denominator1, denominator2);

        numerator = numerator1 * denominator2;
        denominator = denominator1 * numerator2;
    }

    ret.init(0, numerator, denominator);
    return ret;
}

Rational& Rational::mul_div_assign(const Rational& num1, const Rational& num2, bool is_multiply)
{
    Rational ret = mul_div(num1, num2, is_multiply);

    this->m_integer = ret.m_integer;
    this->m_numerator = ret.m_numerator;
    this->m_denominator = ret.m_denominator;

    return *this;
}

Rational Rational::operator*(const Rational& num) const
{
    return mul_div(*this, num, true);
}

Rational& Rational::operator*=(const Rational& num)
{
    return mul_div_assign(*this, num, true);
}

Rational Rational::operator/(const Rational& num) const
{
    return mul_div(*this, num, false);
}

Rational& Rational::operator/=(const Rational& num)
{
    return mul_div_assign(*this, num, false);
}

bool Rational::operator==(const Rational& num) const
{
    return this->m_integer == num.m_integer && this->m_numerator == num.m_numerator &&
        this->m_denominator == num.m_denominator;
}

bool Rational::operator!=(const Rational& num) const
{
    return !(*this == num);
}

bool Rational::operator<(const Rational& num) const
{
    if (this->m_integer == num.m_integer) {
        uint64_t r = lcd(this->m_denominator, num.m_denominator);

        return (r / this->m_denominator) * this->m_numerator <
            (r / num.m_denominator) * num.m_numerator;
    }

    return this->m_integer < num.m_integer;
}
