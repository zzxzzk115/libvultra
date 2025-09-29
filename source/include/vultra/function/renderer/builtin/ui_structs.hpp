#pragma once

#include "vultra/core/rhi/texture.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace vultra
{
    namespace gfx
    {
        struct UIDrawCommand
        {
            enum class Type
            {
                eCircle
            } type;

            rhi::Texture* target {nullptr};

            glm::vec2 position;
            float     radius;
            float     outlineThickness; // For outlined circles
            glm::vec4 fillColor;
            glm::vec4 outlineColor;
            bool      filled;
        };

        class UIDrawList
        {
        public:
            void addCircleFilled(rhi::Texture* target,
                                 glm::vec2     position,
                                 float         radius,
                                 glm::vec4     fillColor,
                                 glm::vec4     outlineColor     = glm::vec4(0.0f),
                                 float         outlineThickness = 0.0f)
            {
                UIDrawCommand cmd {};
                cmd.type             = UIDrawCommand::Type::eCircle;
                cmd.target           = target;
                cmd.position         = position;
                cmd.radius           = radius;
                cmd.outlineThickness = outlineThickness;
                cmd.fillColor        = fillColor;
                cmd.outlineColor     = outlineColor;
                cmd.filled           = true;
                commands.push_back(cmd);
            }

            void addCircleOutlineOnly(rhi::Texture* target,
                                      glm::vec2     position,
                                      float         radius,
                                      glm::vec4     outlineColor,
                                      float         outlineThickness = 1.0f)
            {
                UIDrawCommand cmd {};
                cmd.type             = UIDrawCommand::Type::eCircle;
                cmd.target           = target;
                cmd.position         = position;
                cmd.radius           = radius;
                cmd.outlineThickness = outlineThickness;
                cmd.fillColor        = glm::vec4(0.0f);
                cmd.outlineColor     = outlineColor;
                cmd.filled           = false;
                commands.push_back(cmd);
            }
            std::vector<UIDrawCommand> commands;
        };
    } // namespace gfx
} // namespace vultra