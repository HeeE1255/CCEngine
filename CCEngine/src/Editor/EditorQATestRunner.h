#pragma once

#include "Core.h"
#include <functional>
#include <string>
#include <vector>

namespace CCEngine
{
    struct CC_API EditorQATestResult
    {
        std::string Name;
        bool Passed = false;
        std::string Message;
    };

    struct CC_API EditorQATestSummary
    {
        std::vector<EditorQATestResult> Results;
        int PassedCount = 0;
        int FailedCount = 0;

        bool Passed() const { return FailedCount == 0; }
    };

    class CC_API EditorQATestRunner
    {
    public:
        using TestBody = std::function<EditorQATestResult()>;

        void AddTest(const std::string& name, TestBody body);
        EditorQATestSummary Run();
        static void LogSummary(const EditorQATestSummary& summary);

    private:
        struct TestCase
        {
            std::string Name;
            TestBody Body;
        };

        std::vector<TestCase> m_Tests;
    };

    CC_API EditorQATestResult RunEditorUIInputRegressionChecks();
}
