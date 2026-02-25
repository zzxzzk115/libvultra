#include "vultra/function/scene/vscn_reader.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vultra
{
    namespace
    {
        inline void ltrim_inplace(std::string& s)
        {
            size_t i = 0;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            s.erase(0, i);
        }

        inline void rtrim_inplace(std::string& s)
        {
            size_t i = s.size();
            while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1])))
                --i;
            s.erase(i);
        }

        inline void trim_inplace(std::string& s)
        {
            ltrim_inplace(s);
            rtrim_inplace(s);
        }

        inline bool starts_with(std::string_view s, std::string_view prefix)
        {
            return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
        }

        std::string strip_comment(std::string line)
        {
            // '#' comments
            if (auto pos = line.find('#'); pos != std::string::npos)
                line = line.substr(0, pos);
            // '//' comments
            if (auto pos = line.find("//"); pos != std::string::npos)
                line = line.substr(0, pos);
            return line;
        }

        std::string unquote(std::string_view s)
        {
            if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
            {
                return std::string(s.substr(1, s.size() - 2));
            }
            return std::string(s);
        }

        // Parse key=value pairs from something like: "node id=1 name=\"Root\" parent=0"
        std::unordered_map<std::string, std::string> parse_kv_pairs(std::string_view header)
        {
            std::unordered_map<std::string, std::string> out;

            size_t i = 0;
            while (i < header.size())
            {
                while (i < header.size() && std::isspace(static_cast<unsigned char>(header[i])))
                    ++i;
                if (i >= header.size())
                    break;

                // key
                size_t keyStart = i;
                while (i < header.size() && header[i] != '=' && !std::isspace(static_cast<unsigned char>(header[i])))
                    ++i;
                size_t keyEnd = i;

                while (i < header.size() && std::isspace(static_cast<unsigned char>(header[i])))
                    ++i;
                if (i >= header.size() || header[i] != '=')
                {
                    // token without '=' (e.g. "node")
                    continue;
                }
                ++i; // '='

                while (i < header.size() && std::isspace(static_cast<unsigned char>(header[i])))
                    ++i;

                // value (quoted or bare)
                std::string value;
                if (i < header.size() && (header[i] == '"' || header[i] == '\''))
                {
                    char   quote    = header[i++];
                    size_t valStart = i;
                    while (i < header.size() && header[i] != quote)
                        ++i;
                    if (i >= header.size())
                        throw std::runtime_error("Unterminated quoted string in header");
                    value = std::string(header.substr(valStart, i - valStart));
                    ++i; // closing quote
                }
                else
                {
                    size_t valStart = i;
                    while (i < header.size() && !std::isspace(static_cast<unsigned char>(header[i])))
                        ++i;
                    value = std::string(header.substr(valStart, i - valStart));
                }

                std::string key(header.substr(keyStart, keyEnd - keyStart));
                if (!key.empty())
                    out[std::move(key)] = std::move(value);
            }

            return out;
        }
    } // namespace

    VSceneDocument VSceneReader::parse(std::string_view text)
    {
        VSceneDocument doc;
        doc.clear();

        VSceneDocument::Node* currentNode = nullptr;
        bool                  inVscn      = false;

        std::string line;
        line.reserve(1024);

        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos)
                end = text.size();

            line.assign(text.substr(start, end - start));
            start = end + 1;

            line = strip_comment(std::move(line));
            trim_inplace(line);
            if (line.empty())
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                std::string_view header = std::string_view(line).substr(1, line.size() - 2);
                // Section name is first token
                size_t           spacePos = header.find(' ');
                std::string_view sectionName =
                    (spacePos == std::string_view::npos) ? header : header.substr(0, spacePos);
                std::string_view rest =
                    (spacePos == std::string_view::npos) ? std::string_view {} : header.substr(spacePos + 1);

                if (sectionName == "vscn")
                {
                    inVscn      = true;
                    currentNode = nullptr;
                    continue;
                }
                if (sectionName == "node")
                {
                    if (!inVscn)
                        throw std::runtime_error("[node] encountered before [vscn]");

                    auto                 kv = parse_kv_pairs(rest);
                    VSceneDocument::Node n;
                    if (auto it = kv.find("id"); it != kv.end())
                        n.id = std::stoi(it->second);
                    else
                        throw std::runtime_error("[node] missing id=");

                    if (auto it = kv.find("parent"); it != kv.end())
                        n.parent = std::stoi(it->second);
                    else
                        n.parent = 0;

                    if (auto it = kv.find("name"); it != kv.end())
                        n.name = it->second;

                    doc.nodes().push_back(std::move(n));
                    currentNode = &doc.nodes().back();
                    continue;
                }

                throw std::runtime_error("Unknown section: [" + std::string(sectionName) + "]");
            }

            // Key-value under [vscn]
            if (inVscn && !currentNode)
            {
                auto eq = line.find('=');
                if (eq == std::string::npos)
                    throw std::runtime_error("Malformed key=value in [vscn]");
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                trim_inplace(k);
                trim_inplace(v);

                if (k == "version")
                    doc.setVersion(std::stoi(v));
                else if (k == "root")
                    doc.setRootId(std::stoi(v));
                continue;
            }

            // Property under node: Component/field = value
            if (!currentNode)
                throw std::runtime_error("Property line outside of [node]");

            auto eq = line.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error("Malformed property, expected '='");

            std::string lhs = line.substr(0, eq);
            std::string rhs = line.substr(eq + 1);
            trim_inplace(lhs);
            trim_inplace(rhs);

            auto slash = lhs.find('/');
            if (slash == std::string::npos)
                throw std::runtime_error("Malformed property, expected Component/field");

            VSceneDocument::Property p;
            p.component = lhs.substr(0, slash);
            p.field     = lhs.substr(slash + 1);
            trim_inplace(p.component);
            trim_inplace(p.field);
            p.value = std::move(rhs);

            currentNode->properties.push_back(std::move(p));
        }

        return doc;
    }
} // namespace vultra
