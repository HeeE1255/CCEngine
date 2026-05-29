#pragma once
#include "Core.h"
#include "Widget.h"
#include "Renderer/RendererHandle.h"

namespace CCEngine 
{
    namespace UI 
    {

        class CC_API ImageWidget : public Widget 
        {
        public:
            ImageWidget(const std::string& name = "ImageWidget", RendererHandle textureID = nullptr);

            virtual void OnRender() override;

            void SetTexture(RendererHandle textureID) { m_TextureID = textureID; }
            RendererHandle GetTexture() const { return m_TextureID; }

            void SetOnMouseDown(std::function<void(float, float)> callback) { m_OnMouseDown = callback; }
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;

        private:
            RendererHandle m_TextureID = nullptr;
			std::function<void(float, float)> m_OnMouseDown;    
        };

    }
}
