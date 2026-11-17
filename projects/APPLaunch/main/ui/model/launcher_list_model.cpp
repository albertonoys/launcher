/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launcher_list_model.hpp"

#include <algorithm>
#include <cctype>

static char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::vector<size_t> launcher_alphabetical_order(const std::vector<std::string> &names)
{
    std::vector<size_t> order(names.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return std::lexicographical_compare(
            names[a].begin(), names[a].end(), names[b].begin(), names[b].end(),
            [](char x, char y) { return lower(x) < lower(y); });
    });
    return order;
}

size_t launcher_list_step(size_t count, size_t position, int delta, bool wrap)
{
    if (count == 0)
        return 0;
    long long target = static_cast<long long>(position) + delta;
    long long n = static_cast<long long>(count);
    if (wrap)
        return static_cast<size_t>(((target % n) + n) % n);
    return static_cast<size_t>(std::min(std::max(target, 0LL), n - 1));
}

int launcher_list_jump(const std::vector<std::string> &names, const std::vector<size_t> &order,
                       size_t position, char letter)
{
    unsigned char want = static_cast<unsigned char>(letter);
    if (!std::isalnum(want))
        return -1;
    size_t count = order.size();
    for (size_t step = 1; step <= count; step++) {
        size_t pos = (position + step) % count;
        const std::string &name = names[order[pos]];
        auto it = std::find_if(name.begin(), name.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0;
        });
        if (it != name.end() && lower(*it) == lower(letter))
            return static_cast<int>(pos);
    }
    return -1;
}
