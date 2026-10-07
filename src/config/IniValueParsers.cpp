#include "IniValueParsers.h"

#include <cctype>
#include <charconv>
#include <cstdio>
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
}

namespace f4cf::config
{
    /**
     * Parse a transform from "x,y,z;heading,roll,attitude;scale", the rotation in degrees. Spaces around a number
     * are allowed.
     * Returns no value when the text does not have the seven numbers.
     */
    std::optional<IniTransform> parseTransform(const char* text)
    {
        IniTransform transform;
        const int parsed = sscanf_s(text,
            " %f , %f , %f ; %f , %f , %f ; %f",
            &transform.x,
            &transform.y,
            &transform.z,
            &transform.heading,
            &transform.roll,
            &transform.attitude,
            &transform.scale);
        return parsed == 7 ? std::optional{ transform } : std::nullopt;
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

    /**
     * Take a name out of a list of names, as the value of sDumpDataOnceNames is, and return whether it had it.
     * A name is found only as a whole: "skelly" is not found in "fp_skelly". It is letters, digits and '_', and
     * anything else is between names. Only the name is removed, so the names can have any separator.
     */
    bool takeName(std::string& names, const std::string_view name)
    {
        const auto isNameChar = [](const char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
        };

        if (name.empty()) {
            return false;
        }
        for (auto at = names.find(name); at != std::string::npos; at = names.find(name, at + 1)) {
            const auto end = at + name.size();
            const bool startsName = at == 0 || !isNameChar(names[at - 1]);
            const bool endsName = end == names.size() || !isNameChar(names[end]);
            if (startsName && endsName) {
                names.erase(at, name.size());
                return true;
            }
        }
        return false;
    }
}
