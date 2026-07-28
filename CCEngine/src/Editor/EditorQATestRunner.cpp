#include "EditorQATestRunner.h"
#include "Core/ConsoleLog.h"
#include "UI/Widget.h"
#include "UI/WindowPanel.h"

#include <exception>
#include <sstream>
#include <vector>

namespace CCEngine
{
    namespace
    {
        struct UIInputProbeWidget : public UI::Widget
        {
            UIInputProbeWidget(const std::string& name)
                : UI::Widget(name)
            {
            }

            bool ConsumeEvents = false;
            bool CaptureMouse = false;
            int MoveCount = 0;
            int PressCount = 0;
            int ReleaseCount = 0;

            bool WantsMouseCapture() const override
            {
                return CaptureMouse;
            }

        protected:
            bool OnMouseMoved(MouseMovedEvent& e) override
            {
                ++MoveCount;
                return ConsumeEvents;
            }

            bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override
            {
                ++PressCount;
                return ConsumeEvents;
            }

            bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) override
            {
                ++ReleaseCount;
                if (CaptureMouse)
                {
                    CaptureMouse = false;
                    UI::Widget::EndMouseInteraction(this);
                }
                return ConsumeEvents;
            }
        };

        struct UIInputSimulator
        {
            explicit UIInputSimulator(UI::Widget& root)
                : Root(root)
            {
            }

            bool Move(float x, float y)
            {
                MouseMovedEvent event(x, y);
                return Root.OnEvent(event) || event.Handled;
            }

            bool Press(int button, float x, float y)
            {
                MouseButtonPressedEvent event(button, x, y);
                return Root.OnEvent(event) || event.Handled;
            }

            bool Release(int button, float x, float y)
            {
                MouseButtonReleasedEvent event(button, x, y);
                return Root.OnEvent(event) || event.Handled;
            }

            UI::Widget& Root;
        };

        bool Expect(bool condition, const std::string& message, std::vector<std::string>& failures)
        {
            if (!condition)
            {
                failures.push_back(message);
                return false;
            }

            return true;
        }
    }

    void EditorQATestRunner::AddTest(const std::string& name, TestBody body)
    {
        if (!body)
            return;

        m_Tests.push_back({ name, std::move(body) });
    }

    EditorQATestSummary EditorQATestRunner::Run()
    {
        EditorQATestSummary summary;
        summary.Results.reserve(m_Tests.size());

        for (const TestCase& test : m_Tests)
        {
            EditorQATestResult result;
            result.Name = test.Name;

            try
            {
                result = test.Body();
                if (result.Name.empty())
                    result.Name = test.Name;
            }
            catch (const std::exception& e)
            {
                result.Passed = false;
                result.Message = std::string("Unhandled exception: ") + e.what();
            }
            catch (...)
            {
                result.Passed = false;
                result.Message = "Unhandled unknown exception.";
            }

            if (result.Passed)
                ++summary.PassedCount;
            else
                ++summary.FailedCount;

            summary.Results.push_back(std::move(result));
        }

        return summary;
    }

    void EditorQATestRunner::LogSummary(const EditorQATestSummary& summary)
    {
        ConsoleLog::Info("Editor QA started: " + std::to_string(summary.Results.size()) + " test(s).");

        for (const EditorQATestResult& result : summary.Results)
        {
            std::string prefix = result.Passed ? "[PASS] " : "[FAIL] ";
            std::string line = prefix + result.Name;
            if (!result.Message.empty())
                line += " - " + result.Message;

            if (result.Passed)
                ConsoleLog::Info(line);
            else
                ConsoleLog::Error(line);
        }

        std::string summaryText =
            "Editor QA finished. Passed: " + std::to_string(summary.PassedCount) +
            ", Failed: " + std::to_string(summary.FailedCount);

        if (summary.Passed())
            ConsoleLog::Info(summaryText);
        else
            ConsoleLog::Error(summaryText);
    }

    EditorQATestResult RunEditorUIInputRegressionChecks()
    {
        EditorQATestResult result;
        result.Name = "EditorUI.InputSimulator";

        std::vector<std::string> failures;
        UI::Widget::EndMouseInteraction(nullptr);
        UI::Widget::ClearKeyboardFocus(nullptr);

        UI::Widget root("QA_UI_Root");
        root.SetPosition(0.0f, 0.0f);
        root.SetSize(800.0f, 600.0f);

        auto* backPanel = new UIInputProbeWidget("QA_BackPanel");
        backPanel->SetPosition(100.0f, 100.0f);
        backPanel->SetSize(260.0f, 180.0f);
        backPanel->SetBlockMouseEvents(true);
        root.AddChild(backPanel);

        auto* frontPanel = new UIInputProbeWidget("QA_FrontPanel");
        frontPanel->SetPosition(160.0f, 130.0f);
        frontPanel->SetSize(260.0f, 180.0f);
        frontPanel->SetBlockMouseEvents(true);
        root.AddChild(frontPanel);

        root.UpdateLayout({ 0.0f, 0.0f }, { 800.0f, 600.0f });

        UIInputSimulator input(root);

        // 겹친 창에서는 자식 순서의 가장 뒤쪽, 즉 화면상 최상위 창만 입력을 받는다.
        // 이 검사가 깨지면 메뉴를 열었을 때 뒤에 있는 하이어라키나 에셋 브라우저가 같이 반응한다.
        input.Move(190.0f, 160.0f);
        Expect(frontPanel->MoveCount == 1, "Top overlapping panel did not receive hover input.", failures);
        Expect(backPanel->MoveCount == 0, "Back panel received hover through top panel.", failures);
        Expect(backPanel->IsMouseBlockedByWidgetAbove(190.0f, 160.0f), "Back panel did not report that it is blocked by the top panel.", failures);
        Expect(!frontPanel->IsMouseBlockedByWidgetAbove(190.0f, 160.0f), "Top panel incorrectly reported itself as blocked.", failures);

        input.Press(0, 190.0f, 160.0f);
        Expect(frontPanel->PressCount == 1, "Top overlapping panel did not receive click input.", failures);
        Expect(backPanel->PressCount == 0, "Back panel received click through top panel.", failures);

        auto* capturePanel = new UIInputProbeWidget("QA_CapturePanel");
        capturePanel->SetPosition(420.0f, 100.0f);
        capturePanel->SetSize(180.0f, 140.0f);
        capturePanel->CaptureMouse = true;
        capturePanel->ConsumeEvents = true;
        root.AddChild(capturePanel);

        auto* sidePanel = new UIInputProbeWidget("QA_SidePanel");
        sidePanel->SetPosition(610.0f, 100.0f);
        sidePanel->SetSize(160.0f, 140.0f);
        sidePanel->SetBlockMouseEvents(true);
        root.AddChild(sidePanel);

        root.UpdateLayout({ 0.0f, 0.0f }, { 800.0f, 600.0f });

        UI::Widget::BeginMouseInteraction(capturePanel);

        // 창 이동/리사이즈 중에는 마우스가 다른 창 위를 지나가도 캡처 소유자만 입력을 받는다.
        // 이 규칙이 있어야 드래그 중 뒤 창 hover, 우클릭 메뉴, 선택 변경이 새지 않는다.
        input.Move(650.0f, 130.0f);
        Expect(capturePanel->MoveCount == 1, "Mouse capture owner did not receive move input outside its bounds.", failures);
        Expect(sidePanel->MoveCount == 0, "Sibling panel received hover while another widget captured the mouse.", failures);
        Expect(UI::Widget::IsMouseInteractionActive(), "Mouse interaction was not marked active during capture.", failures);

        input.Release(0, 650.0f, 130.0f);
        Expect(capturePanel->ReleaseCount == 1, "Mouse capture owner did not receive release input.", failures);
        Expect(sidePanel->ReleaseCount == 0, "Sibling panel received release while another widget captured the mouse.", failures);
        Expect(!UI::Widget::IsMouseInteractionActive(), "Mouse interaction stayed active after release.", failures);

        UI::WindowPanel windowPanel("QA_WindowPanel", "QA Window");
        windowPanel.SetPosition(250.0f, 330.0f);
        windowPanel.SetSize(220.0f, 150.0f);
        windowPanel.UpdateLayout({ 0.0f, 0.0f }, { 800.0f, 600.0f });

        // 테두리 hit-test는 실제 리사이즈를 시작하기 전 단계에서 검증한다.
        // 왼쪽/아래쪽 같은 얇은 영역이 빠지면 사용자는 창을 잡았는데 아무 반응이 없는 것처럼 느낀다.
        Expect(windowPanel.IsPointInside(246.0f, 380.0f), "Window left resize padding is not hittable.", failures);
        Expect(windowPanel.IsPointInside(360.0f, 484.0f), "Window bottom resize padding is not hittable.", failures);
        Expect(!windowPanel.IsPointInside(230.0f, 380.0f), "Window left resize padding is too wide.", failures);

        UI::Widget::EndMouseInteraction(nullptr);
        result.Passed = failures.empty();

        if (result.Passed)
        {
            result.Message = "UI mouse layering, capture, and resize hit-test checks passed.";
        }
        else
        {
            std::ostringstream stream;
            stream << failures.size() << " UI input issue(s): ";
            for (size_t i = 0; i < failures.size(); ++i)
            {
                if (i > 0)
                    stream << " | ";
                stream << failures[i];
            }
            result.Message = stream.str();
        }

        return result;
    }
}
