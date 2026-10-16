#include "ValueParsers.h"

#include <cstdio>
#include <format>
#include <string_view>

namespace
{
    /**
     * A color channel of a whole number in 0..255 as its 0..1 value. No value for a float, as 0.5 would read as
     * the 0..1 form a channel is not written in, and for a number out of the range.
     */
    std::optional<float> channel255(const toml::node& node)
    {
        const auto* integer = node.as_integer();
        if (!integer || integer->get() < 0 || integer->get() > 255) {
            return std::nullopt;
        }
        return static_cast<float>(integer->get()) / 255.0f;
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
     * Read a color from its inline table into `color`'s 0..1 channels: `rgb` the color [r, g, b] and `alpha` its
     * opacity, each a whole number in 0..255: { rgb = [255, 51, 51], alpha = 40 }. Each key is optional, and one
     * that is missing keeps `color`'s own channels, so a color can change only the opacity of a preset.
     * Returns why not, and leaves `color` as it was, when the node is not a table, or has a key other than the two,
     * as a misspelled one would be read as its default, or one of them is not a value of its form.
     */
    std::expected<void, std::string> parseColor(const toml::node& node, std::array<float, 4>& color)
    {
        const auto* table = node.as_table();
        if (!table) {
            return std::unexpected(std::string("it is not a table"));
        }

        auto parsed = color;
        for (const auto& [key, value] : *table) {
            if (key.str() == "rgb") {
                const auto* array = value.as_array();
                if (!array || array->size() != 3) {
                    return std::unexpected(std::string("rgb is not an array of three numbers"));
                }
                for (std::size_t i = 0; i < 3; ++i) {
                    const auto channel = channel255((*array)[i]);
                    if (!channel) {
                        return std::unexpected(std::string("rgb has a number that is not a whole number in 0..255"));
                    }
                    parsed[i] = *channel;
                }
            } else if (key.str() == "alpha") {
                const auto alpha = channel255(value);
                if (!alpha) {
                    return std::unexpected(std::string("alpha is not a whole number in 0..255"));
                }
                parsed[3] = *alpha;
            } else {
                return std::unexpected(std::format("it has the key '{}', which is not rgb or alpha", key.str()));
            }
        }
        color = parsed;
        return {};
    }
}
