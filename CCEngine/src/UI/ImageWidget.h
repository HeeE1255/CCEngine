#pragma once
#include "Core.h"
#include "Widget.h"

namespace CCEngine 
{
    namespace UI 
    {

        class CC_API ImageWidget : public Widget 
        {
        public:
            ImageWidget(const std::string& name = "ImageWidget", void* textureID = nullptr);

            virtual void OnRender() override;

            void SetTexture(void* textureID) { m_TextureID = textureID; }
            void* GetTexture() const { return m_TextureID; }

            void SetOnMouseDown(std::function<void(float, float)> callback) { m_OnMouseDown = callback; }
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;

        private:
            void* m_TextureID = nullptr;
			std::function<void(float, float)> m_OnMouseDown;    
        };

    }
}