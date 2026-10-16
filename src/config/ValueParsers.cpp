#include "ValueParsers.h"

#include <cctype>
#include <charconv>
#include <cstdio>
#include <format>
#include <string_view>
#include <vector>

namespace
{
    /**
     * The parts of a text between commas, each without the spaces around it. An empty part is left out.
     * As common::splitTrimmed, which does not build apart from the game.
     */
    std::vector<std::string_view> splitByComma(const std::string_view text)
    {
        const auto isSpace = [](const char ch) {
            return std::isspace(static_cast<unsigned char>(ch)) != 0;
        };

        std::vector<std::string_view> parts;
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find(',', start);
            auto part = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            while (!part.empty() && isSpace(part.front())) {
                part.remove_prefix(1);
            }
            while (!part.empty() && isSpace(part.back())) {
                part.remove_suffix(1);
            }
            if (!part.empty()) {
                parts.push_back(part);
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return parts;
    }

    /**
     * Read the three numbers of an array of a transform, each an integer or a float.
     * Returns false and leaves the numbers as they were when the node is not an array of three numbers.
     */
    bool readThree(const toml::node& node, float& a, float& b, float& c)
    {
        const auto* array = node.as_array();
        if (!array || array->size() != 3) {
            return false;
        }
        const auto first = (*array)[0].value<double>();
        const auto second = (*array)[1].value<double>();
        const auto third = (*array)[2].value<double>();
        if (!first || !second || !third) {
            return false;
        }
        a = static_cast<float>(*first);
        b = static_cast<float>(*second);
        c = static_cast<float>(*third);
        return true;
    }

    /**
     * An array of three numbers, for a transform's table.
     */
    toml::array arrayOfThree(const double a, const double b, const double c)
    {
        toml::array array;
        array.push_back(a);
        array.push_back(b);
        array.push_back(c);
        return array;
    }
}

namespace f4cf::config
{
    /**
     * Read a transform from its inline table: `pos` the position [x, y, z], `rot` the rotation as Euler angles in
     * degrees [heading, roll, attitude], and `scale` a number, each optional, for no move, no rotation and a scale of
     * 1: { pos = [5, -2, 1], rot = [0, 90, 0], scale = 24 }.
     * Returns why not when the node is not a table, or has a key other than the three, as a misspelled one would be
     * read as its default, or one of them is not a value of its form.
     */
    std::expected<ParsedTransform, std::string> parseTransform(const toml::node& node)
    {
        const auto* table = node.as_table();
        if (!table) {
            return std::unexpected(std::string("it is not a table"));
        }

        ParsedTransform transform;
        for (const auto& [key, value] : *table) {
            if (key.str() == "pos") {
                if (!readThree(value, transform.x, transform.y, transform.z)) {
                    return std::unexpected(std::string("pos is not an array of three numbers"));
                }
            } else if (key.str() == "rot") {
                if (!readThree(value, transform.heading, transform.roll, transform.attitude)) {
                    return std::unexpected(std::string("rot is not an array of three numbers"));
                }
            } else if (key.str() == "scale") {
                const auto scale = value.value<double>();
                if (!scale) {
                    return std::unexpected(std::string("scale is not a number"));
                }
                transform.scale = static_cast<float>(*scale);
            } else {
                return std::unexpected(std::format("it has the key '{}', which is not pos, rot or scale", key.str()));
            }
        }
        return transform;
    }

    /**
     * The table parseTransform reads, of a transform in the text form of a mod's INI config,
     * "x,y,z;heading,roll,attitude;scale" with the rotation in degrees and spaces around a number allowed, for the
     * import of the INI config, see config::iniToToml: { pos = [x, y, z], rot = [heading, roll, attitude], scale = s }.
     * The numbers are read as doubles, so each is written as the text has it, not as the float nearest to it.
     * Returns no value when the text does not have the seven numbers.
     */
    std::optional<toml::table> transformTableFromText(const std::string_view text)
    {
        const std::string terminated(text);
        double x, y, z, heading, roll, attitude, scale;
        const int parsed = sscanf_s(terminated.c_str(), " %lf , %lf , %lf ; %lf , %lf , %lf ; %lf", &x, &y, &z, &heading, &roll, &attitude, &scale);
        if (parsed != 7) {
            return std::nullopt;
        }

        toml::table table;
        table.insert("pos", arrayOfThree(x, y, z));
        table.insert("rot", arrayOfThree(heading, roll, attitude));
        table.insert("scale", scale);
        table.is_inline(true);
        return table;
    }

    /**
     * Parse a 22-float hand pose: 5 ';'-separated groups of 4 ','-separated floats (thumb, index, middle, ring,
     * pinky, each prox,mid,dist,splay), then the 2 of the palm (palmPitch,palmYaw). Spaces around a separator
     * are allowed. Returns no value when the structure doesn't match.
     */
    std::optional<std::array<float, 22>> parseHandPose(const char* text)
    {
        std::array<float, 22> r{};
        const int parsed = sscanf_s(text,
            " %f , %f , %f , %f ;" // thumb
            " %f , %f , %f , %f ;" // index
            " %f , %f , %f , %f ;" // middle
            " %f , %f , %f , %f ;" // ring
            " %f , %f , %f , %f ;" // pinky
            " %f , %f", // palmPitch, palmYaw
            &r[0],
            &r[1],
            &r[2],
            &r[3],
            &r[4],
            &r[5],
            &r[6],
            &r[7],
            &r[8],
            &r[9],
            &r[10],
            &r[11],
            &r[12],
            &r[13],
            &r[14],
            &r[15],
            &r[16],
            &r[17],
            &r[18],
            &r[19],
            &r[20],
            &r[21]);
        return parsed == 22 ? std::optional{ r } : std::nullopt;
    }

    /**
     * Parse "r,g,b" or "r,g,b,a", each channel a whole number in 0..255, into `color`'s 0..1 channels. Without the
     * alpha, `color`'s own alpha stays. Anything else returns false and leaves `color` untouched.
     */
    bool parseColor255(const std::string& text, std::array<float, 4>& color)
    {
        const auto tokens = splitByComma(text);
        if (tokens.size() != 3 && tokens.size() != 4) {
            return false;
        }

        auto parsed = color;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            int channel = -1;
            const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), channel);
            if (error != std::errc{} || end != token.data() + token.size() || channel < 0 || channel > 255) {
                return false;
            }
            parsed[i] = static_cast<float>(channel) / 255.0f;
        }
        color = parsed;
        return true;
    }
}
