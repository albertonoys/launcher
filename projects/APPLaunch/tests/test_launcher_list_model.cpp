/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/launcher_list_model.hpp"

#include <cassert>
#include <climits>

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

    // favorites first, both groups A-Z, stable
    std::vector<std::string> n3 = {"zeta", "Beta", "alpha", "BETA", "Alpha"};
    assert((launcher_list_order(n3, {0, 0, 0, 1, 1}) == std::vector<size_t>{4, 3, 2, 1, 0}));
    assert((launcher_list_order(n3, {1, 1}) == std::vector<size_t>{1, 0, 2, 4, 3}));  // short flags: rest are not favorites
    assert((launcher_list_order(n3, {}) == launcher_alphabetical_order(n3)));
    assert(launcher_list_order({}, {1}).empty());

    // recent: favorites stay A-Z on top, the rest newest first, equal times A-Z
    assert((launcher_list_order(n3, {0, 0, 0, 1, 1}, {5, 7, 5, 1, 9}) == std::vector<size_t>{4, 3, 1, 2, 0}));
    assert((launcher_list_order(n3, {}, {0, 0, 3}) == std::vector<size_t>{2, 1, 0, 4, 3}));  // short times are oldest
    // real file times can be negative (libstdc++ file clock); a missing time is still the oldest
    assert((launcher_list_order({"Built", "Old", "New"}, {}, {LLONG_MIN, -900, -100}) == std::vector<size_t>{2, 1, 0}));
    assert((launcher_list_order({"Old", "Built"}, {}, {-900}) == std::vector<size_t>{0, 1}));

    // stored favorites
    assert(launcher_favorites_parse("").empty());
    assert((launcher_favorites_parse("\n/a b\n\n/c\n") == std::vector<std::string>{"/a b", "/c"}));
    assert(launcher_favorites_join({}) == "");
    assert(launcher_favorites_join({"/a", "/b"}) == "/a\n/b");
    assert((launcher_favorites_toggle({"/a"}, "/b") == std::vector<std::string>{"/a", "/b"}));
    assert((launcher_favorites_toggle({"/a", "/b"}, "/a") == std::vector<std::string>{"/b"}));
    assert((launcher_favorites_toggle({"/a"}, "") == std::vector<std::string>{"/a"}));
    assert((launcher_favorites_toggle({"/a"}, "x\ny") == std::vector<std::string>{"/a"}));

    // jump skips the favorites block
    std::vector<std::string> n4 = {"Apple", "Avocado", "Banana", "Apricot"};
    auto o4 = launcher_list_order(n4, {1, 0, 0, 0});  // Apple | Apricot, Avocado, Banana
    assert((o4 == std::vector<size_t>{0, 3, 1, 2}));
    assert(launcher_list_jump(n4, o4, 0, 'a', 1) == 1);  // from the favorite: first non-favorite match
    assert(launcher_list_jump(n4, o4, 1, 'a', 1) == 2);
    assert(launcher_list_jump(n4, o4, 2, 'a', 1) == 1);  // wraps inside the non-favorites
    assert(launcher_list_jump(n4, o4, 0, 'b', 1) == 3);
    assert(launcher_list_jump(n4, o4, 0, 'a', 4) == -1);  // everything is a favorite
    assert(launcher_list_jump({"Apple", "Zed"}, launcher_list_order({"Apple", "Zed"}, {1, 0}), 1, 'a', 1) == -1);

    // space cycles favorites
    assert(launcher_favorite_next(3, 0) == -1);
    assert(launcher_favorite_next(5, 2) == 0);  // non-favorite
    assert(launcher_favorite_next(0, 2) == 1);
    assert(launcher_favorite_next(1, 2) == 0);  // last wraps
    assert(launcher_favorite_next(0, 1) == 0);
}
