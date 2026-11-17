/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/launcher_list_model.hpp"

#include <cassert>

int main()
{
    // sort: mixed case, stable for equal names, empty
    std::vector<std::string> names = {"zeta", "Beta", "alpha", "BETA", "Alpha"};
    auto order = launcher_alphabetical_order(names);
    assert((order == std::vector<size_t>{2, 4, 1, 3, 0}));
    assert(launcher_alphabetical_order({}).empty());

    // step
    assert(launcher_list_step(0, 0, 1, true) == 0);
    assert(launcher_list_step(0, 0, -1, false) == 0);
    assert(launcher_list_step(5, 4, 1, true) == 0);
    assert(launcher_list_step(5, 0, -1, true) == 4);
    assert(launcher_list_step(5, 2, -7, true) == 0);
    assert(launcher_list_step(5, 4, 1, false) == 4);
    assert(launcher_list_step(5, 0, -1, false) == 0);
    assert(launcher_list_step(5, 1, 3, false) == 4);
    assert(launcher_list_step(5, 3, -10, false) == 0);
    assert(launcher_list_step(5, 1, 2, false) == 3);

    // jump
    std::vector<std::string> n2 = {"apple", "Avocado", "banana", "Apricot", "!bang", "9lives"};
    auto o2 = launcher_alphabetical_order(n2);  // !bang, 9lives, apple, Apricot, Avocado, banana
    assert(n2[o2[0]] == "!bang");
    assert(launcher_list_jump(n2, o2, 0, 'a') == 2);
    assert(launcher_list_jump(n2, o2, 2, 'A') == 3);
    assert(launcher_list_jump(n2, o2, 3, 'a') == 4);
    assert(launcher_list_jump(n2, o2, 4, 'a') == 2);  // wraps
    assert(launcher_list_jump(n2, o2, 2, 'b') == 5);
    assert(launcher_list_jump(n2, o2, 5, 'b') == 0);  // "!bang" counts as b
    assert(launcher_list_jump(n2, o2, 1, '9') == 1);  // only match is itself
    assert(launcher_list_jump(n2, o2, 0, 'z') == -1);
    assert(launcher_list_jump(n2, o2, 0, '9') == 1);
    assert(launcher_list_jump(n2, o2, 0, '!') == -1);  // non-alphanumeric letter
    assert(launcher_list_jump(n2, o2, 5, 'x') == -1);
    assert(launcher_list_jump({}, {}, 0, 'a') == -1);
}
