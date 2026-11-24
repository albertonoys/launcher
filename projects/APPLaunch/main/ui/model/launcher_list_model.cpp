/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launcher_list_model.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

static char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

static bool name_less(const std::string &a, const std::string &b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) { return lower(x) < lower(y); });
}

std::vector<size_t> launcher_list_order(const std::vector<std::string> &names,
                                        const std::vector<char> &favorite,
                                        const std::vector<long long> &installed)
{
    auto is_favorite = [&](size_t i) { return i < favorite.size() && favorite[i] != 0; };
    auto time = [&](size_t i) { return i < installed.size() ? installed[i] : std::numeric_limits<long long>::min(); };
    std::vector<size_t> order(names.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (is_favorite(a) != is_favorite(b))
            return is_favorite(a);
        if (!installed.empty() && !is_favorite(a) && time(a) != time(b))
            return time(a) > time(b);
        return name_less(names[a], names[b]);
    });
    return order;
}

std::vector<size_t> launcher_alphabetical_order(const std::vector<std::string> &names)
{
    return launcher_list_order(names, {});
}

std::vector<std::string> launcher_favorites_parse(const std::string &stored)
{
    std::vector<std::string> execs;
    size_t start = 0;
    while (start <= stored.size()) {
        size_t end = stored.find('\n', start);
        if (end == std::string::npos)
            end = stored.size();
        if (end > start)
            execs.push_back(stored.substr(start, end - start));
        start = end + 1;
    }
    return execs;
}

std::string launcher_favorites_join(const std::vector<std::string> &execs)
{
    std::string joined;
    for (const std::string &exec : execs) {
        if (!joined.empty())
            joined += '\n';
        joined += exec;
    }
    return joined;
}

std::vector<std::string> launcher_favorites_toggle(std::vector<std::string> execs, const std::string &exec)
{
    if (exec.empty() || exec.find('\n') != std::string::npos)
        return execs;
    auto found = std::find(execs.begin(), execs.end(), exec);
    if (found != execs.end())
        execs.erase(found);
    else
        execs.push_back(exec);
    return execs;
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
                       size_t position, char letter, size_t first)
{
    unsigned char want = static_cast<unsigned char>(letter);
    if (!std::isalnum(want))
        return -1;
    if (first >= order.size())
        return -1;
    size_t count = order.size() - first;
    size_t relative = position >= first ? position - first : count - 1;
    for (size_t step = 1; step <= count; step++) {
        size_t pos = first + (relative + step) % count;
        const std::string &name = names[order[pos]];
        auto it = std::find_if(name.begin(), name.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0;
        });
        if (it != name.end() && lower(*it) == lower(letter))
            return static_cast<int>(pos);
    }
    return -1;
}

int launcher_favorite_next(size_t position, size_t favorites)
{
    if (favorites == 0)
        return -1;
    return static_cast<int>(position + 1 < favorites ? position + 1 : 0);
}

void launcher_dpkg_log_installs(const std::string &log, std::map<std::string, long long> &out)
{
    std::istringstream lines(log);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string date, time, action, package;
        if (!(fields >> date >> time >> action >> package) || action != "install" ||
            package.find(':') == std::string::npos)
            continue;
        long long stamp = 0;
        int digits = 0;
        for (char c : date + time) {
            if (c >= '0' && c <= '9') {
                stamp = stamp * 10 + (c - '0');
                ++digits;
            }
        }
        if (digits == 14) out[package] = stamp;
    }
}

std::vector<std::string> launcher_dpkg_list_desktops(const std::string &list, const std::string &dir)
{
    std::vector<std::string> found;
    const std::string prefix = dir + "/";
    const std::string suffix = ".desktop";
    std::istringstream lines(list);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.size() > prefix.size() + suffix.size() && line.compare(0, prefix.size(), prefix) == 0 &&
            line.find('/', prefix.size()) == std::string::npos &&
            line.compare(line.size() - suffix.size(), suffix.size(), suffix) == 0)
            found.push_back(line);
    }
    return found;
}
