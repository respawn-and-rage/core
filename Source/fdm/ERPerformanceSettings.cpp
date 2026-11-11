#include "ERPerformanceSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

namespace ERPerformance
{
namespace
{
struct FOriginalValue { const TCHAR* Name; float Value; };
TArray<FOriginalValue>& Originals()
{
    static TArray<FOriginalValue> Values;
    if(Values.IsEmpty())
    {
        for(const TCHAR* Name : {TEXT("r.AntiAliasingMethod"),TEXT("r.TemporalAA.Upsampling"),TEXT("r.TSR.History.ScreenPercentage"),TEXT("r.TSR.History.UpdateQuality"),TEXT("r.TSR.ShadingRejection.Flickering"),TEXT("r.DynamicGlobalIlluminationMethod"),TEXT("r.Lumen.HardwareRayTracing"),TEXT("r.RayTracing.ForceAllRayTracingEffects"),TEXT("sg.ShadowQuality"),TEXT("r.ShadowQuality"),TEXT("r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel"),TEXT("r.Shadow.MaxResolution"),TEXT("r.Shadow.DistanceScale"),TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal"),TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"),TEXT("r.ReflectionMethod"),TEXT("r.SSR.Quality"),TEXT("r.ReflectionEnvironment"),TEXT("r.DefaultFeature.MotionBlur"),TEXT("r.MotionBlurQuality"),TEXT("r.DepthOfFieldQuality"),TEXT("sg.EffectsQuality"),TEXT("r.MaterialQualityLevel"),TEXT("sg.TextureQuality"),TEXT("r.Streaming.LimitPoolSizeToVRAM"),TEXT("r.Streaming.MipBias"),TEXT("r.MaxAnisotropy"),TEXT("sg.GlobalIlluminationQuality"),TEXT("sg.ReflectionQuality"),TEXT("sg.PostProcessQuality"),TEXT("t.MaxFPS"),TEXT("p.ClothPhysics")})
            if(auto* CVar=IConsoleManager::Get().FindConsoleVariable(Name)) Values.Add({Name,CVar->GetFloat()});
    }
    return Values;
}
const int32 FrameCaps[]={60,90,120,144,0};
const TCHAR* Section=TEXT("EclipseRecoil.Performance");
void Set(const TCHAR* Name, float Value)
{
    if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name))
        Variable->Set(Value,ECVF_SetByGameOverride);
}
}
FSettings& Get() { static FSettings Settings; return Settings; }
void FSettings::Load()
{
    if(!GConfig->GetInt(Section,TEXT("Defaults"),Defaults,GGameUserSettingsIni))
    {
        int32 SavedScale=0;
        Defaults=GConfig->GetInt(Section,TEXT("Scale"),SavedScale,GGameUserSettingsIni) ? 0:1;
    }
    Defaults=FMath::Clamp(Defaults,0,1);
    GConfig->GetInt(Section,TEXT("Automatic"),Automatic,GGameUserSettingsIni);
    Automatic=FMath::Clamp(Automatic,0,1);
    GConfig->GetInt(Section,TEXT("Scale"),Scale,GGameUserSettingsIni);
    Scale=FMath::Clamp(Scale,1,100);
    GConfig->GetInt(Section,TEXT("AA"),AA,GGameUserSettingsIni);
    AA=FMath::Clamp(AA,0,2);
    GConfig->GetInt(Section,TEXT("GI"),GI,GGameUserSettingsIni);
    GI=FMath::Clamp(GI,0,1);
    GConfig->GetInt(Section,TEXT("HardwareRT"),HardwareRT,GGameUserSettingsIni);
    HardwareRT=FMath::Clamp(HardwareRT,0,1);
    GConfig->GetInt(Section,TEXT("Shadows"),Shadows,GGameUserSettingsIni);
    Shadows=FMath::Clamp(Shadows,0,3);
    GConfig->GetInt(Section,TEXT("Reflections"),Reflections,GGameUserSettingsIni);
    Reflections=FMath::Clamp(Reflections,0,2);
    GConfig->GetInt(Section,TEXT("MotionBlur"),MotionBlur,GGameUserSettingsIni);
    MotionBlur=FMath::Clamp(MotionBlur,0,1);
    GConfig->GetInt(Section,TEXT("DepthOfField"),DepthOfField,GGameUserSettingsIni);
    DepthOfField=FMath::Clamp(DepthOfField,0,1);
    GConfig->GetInt(Section,TEXT("Effects"),Effects,GGameUserSettingsIni);
    Effects=FMath::Clamp(Effects,0,3);
    GConfig->GetInt(Section,TEXT("Textures"),Textures,GGameUserSettingsIni);
    Textures=FMath::Clamp(Textures,0,3);
    GConfig->GetInt(Section,TEXT("PostProcess"),PostProcess,GGameUserSettingsIni);
    PostProcess=FMath::Clamp(PostProcess,0,3);
    GConfig->GetInt(Section,TEXT("LumenQuality"),LumenQuality,GGameUserSettingsIni);
    LumenQuality=FMath::Clamp(LumenQuality,0,1);
    GConfig->GetInt(Section,TEXT("FrameCap"),FrameCap,GGameUserSettingsIni);
    FrameCap=FMath::Clamp(FrameCap,0,int32(UE_ARRAY_COUNT(FrameCaps))-1);
    if(Defaults) *this=FSettings();
}
void FSettings::Save() const
{
    GConfig->SetInt(Section,TEXT("Defaults"),Defaults,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("Automatic"),Automatic,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("Scale"),Scale,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("AA"),AA,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("GI"),GI,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("HardwareRT"),HardwareRT,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("Shadows"),Shadows,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("Reflections"),Reflections,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("MotionBlur"),MotionBlur,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("DepthOfField"),DepthOfField,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("Effects"),Effects,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("Textures"),Textures,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("PostProcess"),PostProcess,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("LumenQuality"),LumenQuality,GGameUserSettingsIni);
    GConfig->SetInt(Section,TEXT("FrameCap"),FrameCap,GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);
}
void FSettings::Apply() const
{
    auto& Baseline=Originals();
    if(Defaults)
    {
        for(const auto& Original:Baseline) Set(Original.Name,Original.Value);
        return;
    }
    // Scalability groups first: they rewrite the individual r.* values set below.
    // Medium Lumen is the engine's irradiance-volume gather: on these maps it keeps the bounce
    // light of the screen-probe gather at a lower GPU cost. High uses the top group.
    Set(TEXT("sg.GlobalIlluminationQuality"),GI && LumenQuality ? 3:1);
    Set(TEXT("sg.ReflectionQuality"),Reflections==2 ? 2+LumenQuality:1);
    Set(TEXT("sg.PostProcessQuality"),PostProcess);
    Set(TEXT("t.MaxFPS"),FrameCaps[FrameCap]);
    Set(TEXT("p.ClothPhysics"),Effects>0);
    Set(TEXT("r.AntiAliasingMethod"),AA==0 ? 4:2);
    Set(TEXT("r.TemporalAA.Upsampling"),AA==1 ? 0:1);
    Set(TEXT("r.TSR.History.ScreenPercentage"),100);
    Set(TEXT("r.TSR.History.UpdateQuality"),1);
    Set(TEXT("r.TSR.ShadingRejection.Flickering"),0);
    Set(TEXT("r.DynamicGlobalIlluminationMethod"),GI ? 1:0);
    Set(TEXT("r.Lumen.HardwareRayTracing"),HardwareRT);
    Set(TEXT("r.RayTracing.ForceAllRayTracingEffects"),HardwareRT ? -1:0);
    Set(TEXT("sg.ShadowQuality"),Shadows);
    Set(TEXT("r.ShadowQuality"),Shadows==0 ? 0:Shadows==1 ? 2:5);
    Set(TEXT("r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel"),Shadows<=1 ? 4:16);
    Set(TEXT("r.Shadow.MaxResolution"),Shadows==0 ? 256:Shadows==1 ? 512:Shadows==2 ? 1024:2048);
    Set(TEXT("r.Shadow.DistanceScale"),Shadows==0 ? .25f:Shadows==1 ? .5f:1.f);
    Set(TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal"),Shadows<=1 ? 2:0);
    // Only the high level pays for the engine's top sun-shadow resolution.
    Set(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"),Shadows<=1 ? 1.f:Shadows==2 ? 0.f: -1.5f);
    Set(TEXT("r.ReflectionMethod"),Reflections==2 ? 1:Reflections==1 ? 2:0);
    Set(TEXT("r.SSR.Quality"),Reflections ? 1:0);
    Set(TEXT("r.ReflectionEnvironment"),Reflections ? 1:0);
    Set(TEXT("r.DefaultFeature.MotionBlur"),MotionBlur);
    Set(TEXT("r.MotionBlurQuality"),MotionBlur ? 3:0);
    Set(TEXT("r.DepthOfFieldQuality"),DepthOfField ? 2:0);
    Set(TEXT("sg.EffectsQuality"),Effects);
    // Keep authored character material layers when trimming effects. The vendor
    // Low permutation can replace layers independently of the texture budget.
    // Set every time so leaving Competitive and restoring Defaults are reversible.
    Set(TEXT("r.MaterialQualityLevel"),Effects==1 ? 2:1);
    Set(TEXT("sg.TextureQuality"),Textures);
    // Let the streaming manager use the detected hardware budget, rather than
    // inventing a dedicated-VRAM figure on unified-memory machines.
    Set(TEXT("r.Streaming.LimitPoolSizeToVRAM"),1);
    Set(TEXT("r.Streaming.MipBias"),3-Textures);
    Set(TEXT("r.MaxAnisotropy"),Textures==0 ? 1:Textures==1 ? 2:Textures==2 ? 4:8);
}
void FSettings::ApplyPreset(EPreset Preset)
{
    Defaults=0; Automatic=1; Scale=67; AA=2; MotionBlur=DepthOfField=0;
    switch(Preset)
    {
    case EPreset::Competitive:
        // The automatic governor would spend headroom above 60 FPS on resolution.
        Automatic=0; GI=HardwareRT=Reflections=LumenQuality=0;
        Shadows=Effects=PostProcess=0; Textures=1; FrameCap=3;
        break;
    case EPreset::Performance:
        GI=HardwareRT=Reflections=LumenQuality=0;
        Shadows=Effects=Textures=PostProcess=1; FrameCap=1;
        break;
    case EPreset::Balanced:
        AA=0; GI=1; HardwareRT=LumenQuality=0; Reflections=1;
        Shadows=Effects=Textures=PostProcess=2; FrameCap=1;
        break;
    case EPreset::Quality:
        AA=0; GI=HardwareRT=LumenQuality=1; Reflections=2;
        Shadows=Effects=Textures=PostProcess=3; FrameCap=1;
        break;
    }
    Apply(); Save();
}
void FSettings::PerformancePreset() { ApplyPreset(EPreset::Performance); }
void FSettings::SetDefaults()
{
    ApplyPreset(EPreset::Balanced);
}
}
