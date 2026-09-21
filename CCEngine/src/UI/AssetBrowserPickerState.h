#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CCEngine::UI
{
    // Asset Browser의 일반 탐색 상태와 Object Picker 제약을 분리한다.
    // 슬롯 종류가 늘어나도 브라우저 본문은 허용 여부만 물어보고 타입 목록 수명은 이 객체가 맡는다.
    class AssetBrowserPickerState
    {
    public:
        void Begin(const std::string& label, const std::vector<std::string>& acceptedTypeKeys)
        {
            m_Active = true;
            m_Label = label;
            m_AcceptedTypeKeys.clear();
            for (std::string key : acceptedTypeKeys)
            {
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!key.empty())
                    m_AcceptedTypeKeys.insert(std::move(key));
            }
        }

        void Clear()
        {
            m_Active = false;
            m_Label.clear();
            m_AcceptedTypeKeys.clear();
        }

        bool IsActive() const { return m_Active; }
        bool Accepts(const std::string& typeKey) const
        {
            return !m_Active || m_AcceptedTypeKeys.find(typeKey) != m_AcceptedTypeKeys.end();
        }

        std::string GetToolbarLabel() const
        {
            std::string label = m_Label.empty() ? "Pick Asset" : m_Label;
            label = "Pick: " + label;
            if (label.size() > 15)
                label = label.substr(0, 12) + "...";
            return label;
        }

    private:
        bool m_Active = false;
        std::string m_Label;
        std::unordered_set<std::string> m_AcceptedTypeKeys;
    };
}
