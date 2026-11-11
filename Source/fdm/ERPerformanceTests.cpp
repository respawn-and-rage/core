#include "ERPerformanceSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERPerformancePreferencesTest,"EclipseRecoil.Menu.PerformancePreferences",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERPerformancePreferencesTest::RunTest(const FString&)
{
    const FString TestIni=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Testing/PerformancePreferences.ini"));
    GConfig->Add(TestIni,FConfigFile());
    TGuardValue<FString> IsolatedIni(GGameUserSettingsIni,TestIni);
    ERPerformance::FSettings S;
    S.Load();
    TestEqual(TEXT("Fresh profile uses balanced scale"),S.Scale,67);
    TestEqual(TEXT("Fresh profile uses balanced frame cap"),S.FrameCap,1);
    TestEqual(TEXT("Fresh profile enables automatic resolution"),S.Automatic,1);
    GConfig->SetInt(TEXT("EclipseRecoil.Performance"),TEXT("Defaults"),1,TestIni);
    S.Load();
    TestEqual(TEXT("Saved default choice follows balanced"),S.Effects,2);
    TestEqual(TEXT("Saved default choice enables automatic resolution"),S.Automatic,1);
    S.Defaults=0; S.Automatic=0; S.Scale=1; S.AA=2; S.Textures=3; S.MotionBlur=1; S.DepthOfField=1;
    S.Save();
    if(!FParse::Param(FCommandLine::Get(),TEXT("Multiprocess")) || FParse::Param(FCommandLine::Get(),TEXT("MultiprocessSaveConfig")))
    {
        GConfig->UnloadFile(TestIni);
        GConfig->LoadFile(TestIni);
    }
    else AddInfo(TEXT("Disk reload requires -MultiprocessSaveConfig; checking cached preferences in this run."));
    ERPerformance::FSettings Loaded; Loaded.Load();
    TestEqual(TEXT("Manual mode survives restart"),Loaded.Automatic,0);
    TestEqual(TEXT("1 percent survives restart"),Loaded.Scale,1);
    TestEqual(TEXT("Temporal method survives restart"),Loaded.AA,2);
    TestEqual(TEXT("Texture quality survives restart"),Loaded.Textures,3);
    TestEqual(TEXT("Post processing survives restart"),Loaded.DepthOfField,1);
    TestEqual(TEXT("Temporal scale stays in supported range"),Loaded.ManualPrimary(),.5f);
    Loaded.Scale=100; TestEqual(TEXT("Full resolution"),Loaded.ManualPrimary(),1.f);
    Loaded.Scale=1; Loaded.AA=1; TestEqual(TEXT("TAA uses spatial scale"),Loaded.ManualPrimary(),1.f);
    Loaded.PerformancePreset();
    auto Value=[](const TCHAR* Name) { return IConsoleManager::Get().FindConsoleVariable(Name)->GetInt(); };
    TestEqual(TEXT("Preset disables Lumen in renderer"),Value(TEXT("r.DynamicGlobalIlluminationMethod")),0);
    TestEqual(TEXT("Preset disables hardware RT in renderer"),Value(TEXT("r.RayTracing.ForceAllRayTracingEffects")),0);
    TestEqual(TEXT("Preset selects temporal AA"),Value(TEXT("r.AntiAliasingMethod")),2);
    TestEqual(TEXT("Preset disables motion blur"),Value(TEXT("r.MotionBlurQuality")),0);
    TestEqual(TEXT("Preset disables depth of field"),Value(TEXT("r.DepthOfFieldQuality")),0);
    ERPerformance::FSettings Preset; Preset.Load();
    TestEqual(TEXT("Preset is saved"),Preset.HardwareRT,0);
    Loaded.ApplyPreset(ERPerformance::EPreset::Competitive);
    TestEqual(TEXT("Competitive preset fixes resolution above the 60 FPS governor"),Loaded.Automatic,0);
    TestEqual(TEXT("Competitive preset raises the frame cap"),Value(TEXT("t.MaxFPS")),144);
    TestEqual(TEXT("Competitive retains authored material layers"),Value(TEXT("r.MaterialQualityLevel")),1);
    TestEqual(TEXT("Competitive still minimizes effects"),Value(TEXT("sg.EffectsQuality")),0);
    TestEqual(TEXT("Competitive still disables cloth"),Value(TEXT("p.ClothPhysics")),0);
    TestEqual(TEXT("Competitive preset trims bullet holes"),Loaded.BulletHoleLimit(),16);
    Loaded.ApplyPreset(ERPerformance::EPreset::Quality);
    TestEqual(TEXT("Quality preset uses Lumen"),Value(TEXT("r.DynamicGlobalIlluminationMethod")),1);
    TestEqual(TEXT("Quality preset uses high Lumen group"),Value(TEXT("sg.GlobalIlluminationQuality")),3);
    const auto* SunShadowBias=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"));
    TestEqual(TEXT("Quality preset keeps the top sun shadow resolution"),SunShadowBias->GetFloat(),-1.5f);
    Loaded.SetDefaults();
    TestEqual(TEXT("Default uses the standard sun shadow resolution"),SunShadowBias->GetFloat(),0.f);
    TestEqual(TEXT("Default selects balanced frame cap"),Value(TEXT("t.MaxFPS")),90);
    TestEqual(TEXT("Default keeps authored material quality"),Value(TEXT("r.MaterialQualityLevel")),1);
    TestEqual(TEXT("Default uses balanced shadows"),Value(TEXT("sg.ShadowQuality")),2);
    TestEqual(TEXT("Default keeps Lumen"),Value(TEXT("r.DynamicGlobalIlluminationMethod")),1);
    TestEqual(TEXT("Default uses the medium Lumen group"),Value(TEXT("sg.GlobalIlluminationQuality")),1);
    ERPerformance::FSettings Restored; Restored.Load();
    TestEqual(TEXT("Default preset is saved"),Restored.Defaults,0);
    TestEqual(TEXT("Default uses automatic resolution"),Restored.Automatic,1);
    TestEqual(TEXT("Default uses balanced scale"),Restored.Scale,67);
    TestEqual(TEXT("Default uses balanced effects"),Restored.Effects,2);
    GConfig->UnloadFile(TestIni);
    IFileManager::Get().Delete(*TestIni);
    return true;
}
#endif
