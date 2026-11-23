// tests/frontend/test_project_serializer.cpp
#include "project_serializer.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {

class ProjectSerializerTestClass : public ::testing::Test {
protected:
    QTemporaryDir directory;
    ProjectGrid::State state;

    QString projectPath() const { return directory.filePath(QStringLiteral("étude.kikproj")); }

    nlohmann::json script() const {
        return {{"pipeline", {{"steps", {{{"name", "import"}}}}}},
                {"run", {{"checkpointDirectory", directory.filePath(QStringLiteral("checkpoints")).toStdString()}}}};
    }

    void writeJson(const QString &path, const nlohmann::json &json) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromStdString(json.dump());
        ASSERT_EQ(file.write(bytes), bytes.size());
    }

    ProjectData project() const {
        ProjectData data;
        data.outputDirectory = directory.path();
        data.backendScript = script();
        return data;
    }
};

TEST_F(ProjectSerializerTestClass, RoundTripsModeScriptAndChainChoices) {
    state.pipelineChoices.matchingStep = "sift";
    ASSERT_TRUE(ProjectSerializer::saveToFile(projectPath(), QStringLiteral("Étude"),
        directory.path(), 3, state, nullptr, script()));
    ProjectData loaded;
    ASSERT_TRUE(ProjectSerializer::loadFromFile(projectPath(), &loaded));
    EXPECT_EQ(loaded.mode, 3);
    EXPECT_EQ(loaded.backendScript, script());
    EXPECT_EQ(loaded.gridState.pipelineChoices, state.pipelineChoices);
}

TEST_F(ProjectSerializerTestClass, LoadsLegacyProjectWithoutScriptOrMode) {
    writeJson(projectPath(), {{"project_name", "Ancien projet"}, {"grid_rows", 3}});
    ProjectData loaded;
    ASSERT_TRUE(ProjectSerializer::loadFromFile(projectPath(), &loaded));
    EXPECT_EQ(loaded.projectName, QStringLiteral("Ancien projet"));
    EXPECT_EQ(loaded.mode, 0);
    EXPECT_TRUE(loaded.backendScript.is_null());
    EXPECT_FALSE(ProjectSerializer::summarize(loaded).canResume);
}

TEST_F(ProjectSerializerTestClass, RejectsInvalidMode) {
    writeJson(projectPath(), {{"mode", 100}});
    ProjectData loaded;
    EXPECT_FALSE(ProjectSerializer::loadFromFile(projectPath(), &loaded));
}

TEST_F(ProjectSerializerTestClass, RejectsMalformedScript) {
    ProjectData loaded;
    writeJson(projectPath(), {{"backendScript", "invalid"}});
    EXPECT_FALSE(ProjectSerializer::loadFromFile(projectPath(), &loaded));
}

TEST_F(ProjectSerializerTestClass, SummarizesNeverRunPipeline) {
    const auto summary = ProjectSerializer::summarize(project());
    EXPECT_TRUE(summary.canResume) << summary.text().toStdString();
    EXPECT_EQ(summary.neverRunSteps, QStringList{QStringLiteral("import")});
}

TEST_F(ProjectSerializerTestClass, ReadsLatestPausedStateAndFlagsMissingCheckpoint) {
    auto data = project();
    auto saved = script();
    saved["run"]["status"] = "paused";
    saved["run"]["completedSteps"] = {"import"};
    ASSERT_TRUE(QDir().mkpath(directory.filePath(QStringLiteral("checkpoints"))));
    writeJson(directory.filePath(QStringLiteral("checkpoints/run.json")), saved);
    const auto summary = ProjectSerializer::summarize(data);
    EXPECT_EQ(summary.runStatus, QStringLiteral("paused"));
    EXPECT_TRUE(summary.upToDateSteps.isEmpty());
    EXPECT_EQ(summary.stepsToVerify, QStringList{QStringLiteral("import")});
}

TEST_F(ProjectSerializerTestClass, RefusesSidecarFromDifferentPipeline) {
    auto saved = script();
    saved["pipeline"]["steps"] = {{{"name", "unknown"}}};
    ASSERT_TRUE(QDir().mkpath(directory.filePath(QStringLiteral("checkpoints"))));
    writeJson(directory.filePath(QStringLiteral("checkpoints/run.json")), saved);
    EXPECT_FALSE(ProjectSerializer::summarize(project()).canResume);
}

TEST_F(ProjectSerializerTestClass, ReportsMissingImage) {
    auto data = project();
    data.gridState.tilePaths.append(directory.filePath(QStringLiteral("absente.tif")));
    const auto summary = ProjectSerializer::summarize(data);
    EXPECT_FALSE(summary.canResume);
    EXPECT_EQ(summary.issues.size(), 1);
}

TEST_F(ProjectSerializerTestClass, ReportsInaccessibleOutput) {
    auto data = project();
    data.outputDirectory = directory.filePath(QStringLiteral("absent"));
    const auto summary = ProjectSerializer::summarize(data);
    EXPECT_FALSE(summary.canResume);
    EXPECT_EQ(summary.issues.size(), 1);
}

TEST_F(ProjectSerializerTestClass, ReportsMalformedRunState) {
    ASSERT_TRUE(QDir().mkpath(directory.filePath(QStringLiteral("checkpoints"))));
    writeJson(directory.filePath(QStringLiteral("checkpoints/run.json")), {{"invalid", true}});
    EXPECT_FALSE(ProjectSerializer::summarize(project()).canResume);
}

}
