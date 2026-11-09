#include "project_serializer.h"
#include "projectgrid.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {

constexpr int QUARTER_TURN_DEGREES = 90;

class ProjectSerializerTestClass : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        inputDirectory = directory.filePath(QStringLiteral("input"));
        outputDirectory = directory.filePath(QStringLiteral("output"));
        ASSERT_TRUE(QDir().mkpath(inputDirectory));
        ASSERT_TRUE(QDir().mkpath(outputDirectory));
        firstTile = QDir(inputDirectory).filePath(QStringLiteral("tile-1.tif"));
        secondTile = QDir(inputDirectory).filePath(QStringLiteral("tile-2.tif"));
        writeFile(firstTile, "first");
        writeFile(secondTile, "second");
        state.tileDirectory = inputDirectory;
        state.tilePaths = {firstTile, secondTile};
        state.gridRows = 1;
        state.gridColumns = 2;
        state.gridTiles = {
            GridTile{firstTile, QUARTER_TURN_DEGREES, true, false},
            GridTile{secondTile, 0, false, false}
        };
        projectPath = directory.filePath(QStringLiteral("study.kikproj"));
    }

    static void writeFile(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        EXPECT_EQ(file.write(contents), contents.size());
    }

    bool save()
    {
        return ProjectSerializer::saveToFile(
            projectPath, QStringLiteral("Study"), outputDirectory, 1, state,
            nullptr, backendScript());
    }

    nlohmann::json backendScript() const
    {
        const nlohmann::json explicitTiles = nlohmann::json::array({
            {{"path", firstTile.toStdString()}, {"row", 0}, {"column", 0}},
            {{"path", secondTile.toStdString()}, {"row", 0}, {"column", 1}}
        });
        return {
            {"pipeline", {{"steps", nlohmann::json::array({
                {{"name", "import"}, {"parameters", nlohmann::json::array({
                    {{"valueType", "string"}, {"name", "path"}, {"value", inputDirectory.toStdString()}},
                    {{"valueType", "string"}, {"name", "tiles"}, {"value", explicitTiles.dump()}}
                })}},
                {{"name", "phase-correlation"}}
            })}}},
            {"run", {
                {"status", "idle"},
                {"completedSteps", nlohmann::json::array()},
                {"checkpointDirectory", QDir(outputDirectory)
                    .filePath(QStringLiteral("checkpoints")).toStdString()}
            }}
        };
    }

    QTemporaryDir directory;
    QString inputDirectory;
    QString outputDirectory;
    QString firstTile;
    QString secondTile;
    QString projectPath;
    ProjectGrid::State state;
};

TEST_F(ProjectSerializerTestClass, RoundTripsGridAndBackendConfiguration)
{
    ASSERT_TRUE(save());
    ProjectData loaded;
    QString error;
    ASSERT_TRUE(ProjectSerializer::loadFromFile(projectPath, &loaded, &error)) << error.toStdString();
    EXPECT_EQ(loaded.projectName, QStringLiteral("Study"));
    EXPECT_EQ(loaded.gridState.gridRows, 1);
    EXPECT_EQ(loaded.gridState.gridColumns, 2);
    ASSERT_EQ(loaded.gridState.gridTiles.size(), 2);
    EXPECT_EQ(loaded.gridState.gridTiles.at(0).path, firstTile);
    EXPECT_EQ(loaded.gridState.gridTiles.at(0).rotation, QUARTER_TURN_DEGREES);
    EXPECT_TRUE(loaded.gridState.gridTiles.at(0).flipped);
    EXPECT_EQ(loaded.backendScript["pipeline"]["steps"].size(), 2);
    EXPECT_TRUE(loaded.missingFiles.isEmpty());
    EXPECT_TRUE(loaded.modifiedFiles.isEmpty());
    EXPECT_TRUE(loaded.unverifiedFiles.isEmpty());
    const ProjectResumeSummary summary = ProjectSerializer::summarize(loaded);
    EXPECT_TRUE(summary.canResume());
    EXPECT_EQ(summary.neverRunSteps,
              QStringList({QStringLiteral("import"), QStringLiteral("phase-correlation")}));
}

TEST_F(ProjectSerializerTestClass, DetectsChangedAndMissingSourceFiles)
{
    ASSERT_TRUE(save());
    writeFile(firstTile, "changed");
    ASSERT_TRUE(QFile::remove(secondTile));

    ProjectData loaded;
    QString error;
    ASSERT_TRUE(ProjectSerializer::loadFromFile(projectPath, &loaded, &error)) << error.toStdString();
    EXPECT_EQ(loaded.modifiedFiles, QStringList({firstTile}));
    EXPECT_EQ(loaded.missingFiles, QStringList({secondTile}));
    EXPECT_FALSE(ProjectSerializer::summarize(loaded).canResume());
}

TEST_F(ProjectSerializerTestClass, RejectsUnsupportedVersionAndIncoherentGrid)
{
    ASSERT_TRUE(save());
    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonObject project = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    project.insert(QStringLiteral("formatVersion"), QStringLiteral("99.0"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(file.write(QJsonDocument(project).toJson()), 0);
    file.close();
    ProjectData loaded;
    QString error;
    EXPECT_FALSE(ProjectSerializer::loadFromFile(projectPath, &loaded, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("non compatible")));

    project.insert(QStringLiteral("formatVersion"), QStringLiteral("2.0"));
    project.insert(QStringLiteral("gridTiles"), QJsonArray{project.value(
        QStringLiteral("gridTiles")).toArray().first()});
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(file.write(QJsonDocument(project).toJson()), 0);
    file.close();
    EXPECT_FALSE(ProjectSerializer::loadFromFile(projectPath, &loaded, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("cases")));
}

TEST_F(ProjectSerializerTestClass, RejectsMalformedJson)
{
    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_GT(file.write("{"), 0);
    file.close();
    ProjectData loaded;
    QString error;
    EXPECT_FALSE(ProjectSerializer::loadFromFile(projectPath, &loaded, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("JSON valide")));
}

TEST_F(ProjectSerializerTestClass, RejectsDuplicateTilePaths)
{
    ASSERT_TRUE(save());
    QFile file(projectPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonObject project = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    project.insert(QStringLiteral("tiles"),
                   QJsonArray{QJsonValue(firstTile), QJsonValue(firstTile)});
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_GT(file.write(QJsonDocument(project).toJson()), 0);
    file.close();
    ProjectData loaded;
    QString error;
    EXPECT_FALSE(ProjectSerializer::loadFromFile(projectPath, &loaded, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("double")));
}

TEST_F(ProjectSerializerTestClass, SummarizesSavedResultsAndErrors)
{
    ASSERT_TRUE(save());
    ProjectData loaded;
    QString error;
    ASSERT_TRUE(ProjectSerializer::loadFromFile(projectPath, &loaded, &error)) << error.toStdString();
    loaded.backendScript["run"]["stepResults"] = {
        {"import", {{"tiles", 2}}}
    };
    loaded.backendScript["run"]["failedStep"] = "phase-correlation";
    loaded.backendScript["run"]["error"] = "image illisible";
    const ProjectResumeSummary summary = ProjectSerializer::summarize(loaded);
    ASSERT_EQ(summary.availableResults.size(), 1);
    EXPECT_TRUE(summary.availableResults.first().contains(QStringLiteral("tiles")));
    ASSERT_EQ(summary.errors.size(), 1);
    EXPECT_TRUE(summary.errors.first().contains(QStringLiteral("image illisible")));
}

TEST_F(ProjectSerializerTestClass, RejectsBackendGridMismatch)
{
    nlohmann::json script = backendScript();
    const nlohmann::json mismatchedTiles = nlohmann::json::array({
        {{"path", firstTile.toStdString()}, {"row", 0}, {"column", 1}},
        {{"path", secondTile.toStdString()}, {"row", 0}, {"column", 0}}
    });
    script["pipeline"]["steps"][0]["parameters"][1]["value"] =
        mismatchedTiles.dump();
    ASSERT_TRUE(ProjectSerializer::saveToFile(
        projectPath, QStringLiteral("Study"), outputDirectory, 1, state,
        nullptr, script));
    ProjectData loaded;
    QString error;
    EXPECT_FALSE(ProjectSerializer::loadFromFile(projectPath, &loaded, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("ne correspondent pas")));
}

} // namespace
