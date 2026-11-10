#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "f4vr/DebugInventory.h"

using f4cf::f4vr::DebugInventory;
using Category = DebugInventory::ItemCategory;
using Operation = DebugInventory::Operation;
using ItemClass = DebugInventory::ItemClass;

namespace
{
    /**
     * The request written as the inline table `text`, as a person writes one in aAddItemsOnce.
     */
    std::expected<DebugInventory::Request, std::string> parse(const std::string& text)
    {
        const auto table = toml::parse("r = " + text);
        return DebugInventory::parseRequest(*table.get("r"));
    }

    /**
     * Why the request written as `text` is not valid, which it has to not be.
     */
    std::string errorOf(const std::string& text)
    {
        const auto request = parse(text);
        REQUIRE_FALSE(request);
        return request.error();
    }

    bool contains(const std::string& text, const std::string_view part)
    {
        return text.find(part) != std::string::npos;
    }
}

TEST_CASE("DebugInventory: a request is an operation and a category, with no filter")
{
    const auto request = parse(R"({ op = "print", category = "weapons" })");

    REQUIRE(request);
    REQUIRE(request->operation == Operation::Print);
    REQUIRE(request->categories == std::vector{ Category::Weapons });
    REQUIRE(request->filter.name.empty());
    REQUIRE(request->filter.keyword.empty());
    REQUIRE_FALSE(request->filter.slotMask);
    REQUIRE_FALSE(request->filter.itemClass);
}

TEST_CASE("DebugInventory: each operation and category is read by its name, in any case")
{
    REQUIRE(parse(R"({ op = "get", category = "ammo" })")->operation == Operation::Get);
    REQUIRE(parse(R"({ op = "Get-All", category = "ammo" })")->operation == Operation::GetAll);
    REQUIRE(parse(R"({ op = "PRINT-ALL", category = "ammo" })")->operation == Operation::PrintAll);

    const auto request = parse(R"({ op = "get", category = ["weapons", "Throwables", "ammo", "armor", "aid", "misc"] })");
    REQUIRE(request);
    REQUIRE(request->categories == std::vector{ Category::Weapons, Category::Throwables, Category::Ammo, Category::Armor, Category::Aid, Category::Misc });
}

TEST_CASE("DebugInventory: the filter of a request is its other keys")
{
    const auto request = parse(R"({ op = "print-all", category = "armor", name = "Combat", keyword = "Laser", slot = ["torso", 33, "LEGS"], class = "heavy" })");

    REQUIRE(request);
    // the needles are kept as written, and matched with no regard to case
    REQUIRE(request->filter.name == "Combat");
    REQUIRE(request->filter.keyword == "Laser");
    // a bit for each CK slot from 30: torso 41, body 33, legs 44 and 45
    REQUIRE(request->filter.slotMask == ((1u << 11) | (1u << 3) | (1u << 14) | (1u << 15)));
    REQUIRE(request->filter.itemClass == ItemClass::ArmorHeavy);

    // one slot, not in an array
    REQUIRE(parse(R"({ op = "print", category = "armor", slot = 30 })")->filter.slotMask == 1u);
    REQUIRE(parse(R"({ op = "print", category = "weapons", class = "gun" })")->filter.itemClass == ItemClass::WeaponGun);
}

TEST_CASE("DebugInventory: a request that is not valid says why, by its key")
{
    CHECK(contains(errorOf(R"("get, weapons")"), "a request is a table"));
    CHECK(contains(errorOf(R"({ category = "weapons" })"), "op is missing"));
    CHECK(contains(errorOf(R"({ op = "get" })"), "category is missing"));
    CHECK(contains(errorOf(R"({ op = "take", category = "weapons" })"), "op \"take\""));
    CHECK(contains(errorOf(R"({ op = 1, category = "weapons" })"), "op 1"));
    CHECK(contains(errorOf(R"({ op = "get", category = "food" })"), "category \"food\""));
    CHECK(contains(errorOf(R"({ op = "get", category = ["weapons", "food"] })"), "category \"food\""));
    CHECK(contains(errorOf(R"({ op = "get", category = [] })"), "category is an empty array"));
    CHECK(contains(errorOf(R"({ op = "get", category = "weapons", name = 5 })"), "name is not a string"));
    CHECK(contains(errorOf(R"({ op = "get", category = "armor", slot = "tail" })"), "slot 'tail'"));
    CHECK(contains(errorOf(R"({ op = "get", category = "armor", slot = "41" })"), "slot '41'"));
    CHECK(contains(errorOf(R"({ op = "get", category = "armor", slot = 62 })"), "slot 62"));
    CHECK(contains(errorOf(R"({ op = "get", category = "armor", slot = [] })"), "slot is an empty array"));
    CHECK(contains(errorOf(R"({ op = "get", category = "armor", class = "medium" })"), "class \"medium\""));
    CHECK(contains(errorOf(R"({ op = "get", category = "weapons", kw = "laser" })"), "'kw' is not a key of a request"));
}
