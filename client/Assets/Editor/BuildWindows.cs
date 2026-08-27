using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;

// 一键打 Windows 包:菜单 Tools/Build Windows Player(也可被 REST editor_execute_menu 触发)
public static class BuildWindows
{
    [MenuItem("Tools/Build Windows Player")]
    public static void Build()
    {
        var opts = new BuildPlayerOptions
        {
            scenes = new[] { "Assets/Scenes/Battle.unity" },
            locationPathName = "Builds/gs-client/gs-client.exe", // 相对工程根目录
            target = BuildTarget.StandaloneWindows64,
            options = BuildOptions.None, // Mono 后端,增量快
        };
        BuildReport report = BuildPipeline.BuildPlayer(opts);
        BuildSummary s = report.summary;
        Debug.Log($"[Build] result={s.result} output={s.outputPath} " +
                  $"size={s.totalSize / 1024 / 1024}MB errors={s.totalErrors} time={s.totalTime}");
    }
}
