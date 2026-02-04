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
                eCircle,
                eImage
            } type;

            rhi::Texture* target {nullptr};

            union
            {
                struct
                {
                    glm::vec2 position;
                    float     radius;
                    float     outlineThickness; // For outlined circles
                    glm::vec4 fillColor;
                    glm::vec4 outlineColor;
                    bool      filled;
                } circle;
                struct
                {
                    glm::vec2     position;
                    glm::vec2     size;
                    rhi::Texture* texture;
                    glm::vec4     tintColor;
                    bool          stretchToFit;
                } image;
            };
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
                cmd.type                    = UIDrawCommand::Type::eCircle;
                cmd.target                  = target;
                cmd.circle.position         = position;
                cmd.circle.radius           = radius;
                cmd.circle.outlineThickness = outlineThickness;
                cmd.circle.fillColor        = fillColor;
                cmd.circle.outlineColor     = outlineColor;
                cmd.circle.filled           = true;
                commands.push_back(cmd);
            }

            void addCircleOutlineOnly(rhi::Texture* target,
                                      glm::vec2     position,
                                      float         radius,
                                      glm::vec4     outlineColor,
                                      float         outlineThickness = 1.0f)
            {
                UIDrawCommand cmd {};
                cmd.type                    = UIDrawCommand::Type::eCircle;
                cmd.target                  = target;
                cmd.circle.position         = position;
                cmd.circle.radius           = radius;
                cmd.circle.outlineThickness = outlineThickness;
                cmd.circle.fillColor        = glm::vec4(0.0f);
                cmd.circle.outlineColor     = outlineColor;
                cmd.circle.filled           = false;
                commands.push_back(cmd);
            }

            void addImage(rhi::Texture* target,
                          rhi::Texture* texture,
                          glm::vec2     position,
                          glm::vec2     size,
                          bool          stretchToFit = false,
                          glm::vec4     tintColor    = glm::vec4(1.0f))
            {
                UIDrawCommand cmd {};

                glm::vec2 targetSize =
                    size != glm::vec2(0.0f) ? size : glm::vec2(texture->getExtent().width, texture->getExtent().height);

                cmd.type               = UIDrawCommand::Type::eImage;
                cmd.target             = target;
                cmd.image.position     = position;
                cmd.image.size         = targetSize;
                cmd.image.texture      = texture;
                cmd.image.tintColor    = tintColor;
                cmd.image.stretchToFit = stretchToFit;
                commands.push_back(cmd);
            }

            std::vector<UIDrawCommand> commands;
        };
    } // namespace gfx
} // namespace vultra