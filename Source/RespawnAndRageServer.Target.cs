using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.Server)]
public class RespawnAndRageServerTarget : TargetRules
{
    public RespawnAndRageServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("fdm");
        string Python = BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64 ? "python" : "python3";
        PreBuildSteps.Add(Python + " \"$(ProjectDir)/scripts/stamp-build-version.py\"");
    }
}
