#include "EROutdoorBackdrop.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
constexpr const TCHAR* BackdropPath=TEXT("/Game/EclipseRecoil/Art/OutdoorBackdrop/");
constexpr float SunsetElevation = 19.25f;

constexpr int32 PanoramaCount=15;
// Approximate azimuth of the sun or brightest cloud opening in each source image.
constexpr float PanoramaSunYaw[PanoramaCount]={
    -21,-125,22,91,101,-107,88,177,169,154,-96,-29,-171,-52,85
};
struct FSunsetSky
{
    const TCHAR* Name;
    const TCHAR* Texture;
    float SunYaw;
};

constexpr FSunsetSky SunsetSkies[] = {
    {TEXT("FreeStylized 03"), TEXT("T_Sky_FreeStylized_03"), -115.92f},
    {TEXT("FreeStylized 12"), TEXT("T_Sky_FreeStylized_12"), 107.28f},
    {TEXT("FreeStylized 24"), TEXT("T_Sky_FreeStylized_24"), 104.4f},
    {TEXT("FreeStylized 111"), TEXT("T_Sky_FreeStylized_111"), -151.92f},
    {TEXT("FreeStylized 117"), TEXT("T_Sky_FreeStylized_117"), 167.4f},
    {TEXT("FreeStylized 132"), TEXT("T_Sky_FreeStylized_132"), -10.8f},
};
constexpr const TCHAR* NightSkies[] = {TEXT("17"), TEXT("77"), TEXT("85"), TEXT("92")};
constexpr int32 FirstNightChoice = PanoramaCount + UE_ARRAY_COUNT(SunsetSkies) + 1;
constexpr int32 SkyChoiceCount = FirstNightChoice + UE_ARRAY_COUNT(NightSkies) - 1;

int32 SelectedSky(const UWorld& World)
{
    if (World.GetNetMode() != NM_Standalone) return 0;
    const int32 Choice = FCString::Atoi(World.URL.GetOption(TEXT("EROfflineSky="), TEXT("0")));
    return Choice > 0 && Choice <= SkyChoiceCount ? Choice : 0;
}

FString NightTextureName(int32 Choice)
{
    return FString(TEXT("T_Sky_Night_")) + NightSkies[Choice - FirstNightChoice];
}

FString StandardTextureName(int32 Index)
{
    return FString::Printf(TEXT("T_Sky_CSS_%03d"), Index + 1);
}

UTexture2D* LoadSkyTexture(const FString& Name, uint32 Flags = LOAD_None)
{
    const FString Path = FString(BackdropPath) + Name + TEXT(".") + Name;
    return LoadObject<UTexture2D>(nullptr, *Path, nullptr, Flags);
}

void ApplySunsetLighting(AActor& Host, UDirectionalLightComponent& Sun, float Elevation)
{
    FRotator SunRotation = Sun.GetComponentRotation();
    SunRotation.Pitch = -Elevation;
    if (Sun.Mobility != EComponentMobility::Movable) Sun.SetMobility(EComponentMobility::Movable);
    Sun.SetWorldRotation(SunRotation);
    Sun.SetUseTemperature(false);
    Sun.SetLightColor(FLinearColor(1.f, .62f, .34f));
    Sun.SetIntensity(6.f);
    Sun.SetLightSourceAngle(1.5f);

    for (TActorIterator<ASkyLight> It(Host.GetWorld()); It; ++It)
    {
        auto* Ambient = It->GetLightComponent();
        if (!Ambient || !Ambient->IsVisible()) continue;
        Ambient->SetLightColor(FLinearColor(.60f, .78f, 1.f));
        Ambient->SetIntensity(1.65f);
        Ambient->SetLowerHemisphereColor(FLinearColor(.12f, .15f, .21f));
        if (!Ambient->IsRealTimeCaptureEnabled()) Ambient->SetCaptureIsDirty();
    }

    // The first-person rig receives a soft fill even inside deep cover.
    static const FName FillName(TEXT("EROutdoorViewmodelFill"));
    if (!FindObjectFast<UDirectionalLightComponent>(&Host, FillName))
    {
        auto* Fill = NewObject<UDirectionalLightComponent>(&Host, FillName);
        Host.AddInstanceComponent(Fill);
        Fill->SetupAttachment(Host.GetRootComponent());
        Fill->SetMobility(EComponentMobility::Movable);
        Fill->SetLightingChannels(false, true, false);
        Fill->SetCastShadows(false);
        Fill->SetAtmosphereSunLight(false);
        Fill->SetAffectGlobalIllumination(false);
        Fill->SetAffectReflection(false);
        Fill->SetAffectTranslucentLighting(false);
        Fill->SetVolumetricScatteringIntensity(0.f);
        Fill->SetLightColor(FLinearColor(.75f, .86f, 1.f));
        Fill->SetIntensity(.85f);
        Fill->SetWorldRotation(FRotator(-43.f, SunRotation.Yaw + 150.f, 0.f));
        Fill->RegisterComponent();
    }
    UE_LOG(LogTemp, Display, TEXT("ER_OUTDOOR_SUNSET sun_elevation=%.2f sun_lux=%.1f"),
        -Sun.GetComponentRotation().Pitch, Sun.Intensity);
}

UStaticMeshComponent* AddBackdropPart(AActor& Host, FName Name, UStaticMesh* Asset, UMaterialInterface* Finish,
    const FVector& Offset, const FVector& Scale)
{
    auto* Part=NewObject<UStaticMeshComponent>(&Host,Name);
    Host.AddInstanceComponent(Part);
    Part->SetMobility(EComponentMobility::Movable);
    Part->SetupAttachment(Host.GetRootComponent());
    Part->SetStaticMesh(Asset);
    Part->SetMaterial(0,Finish);
    Part->SetRelativeLocation(Offset);
    Part->SetRelativeScale3D(Scale);
    Part->SetCollisionProfileName(TEXT("NoCollision"));
    Part->ComponentTags.Add(TEXT("ERTemplateVisual"));
    Part->SetCanEverAffectNavigation(false);
    Part->SetCastShadow(false);
    Part->SetHiddenInSceneCapture(true);
    Part->SetVisibleInRayTracing(false);
    Part->SetAffectDistanceFieldLighting(false);
    Part->SetAffectDynamicIndirectLighting(false);
    Part->bVisibleInReflectionCaptures=false;
    Part->bVisibleInRealTimeSkyCaptures=(Name==TEXT("EROutdoorSky"));
    Part->RegisterComponent();
    return Part;
}
}

bool EROutdoorBackdrop::IsOutdoorMap(const UWorld& World)
{
    return IsOutdoorMap(World.GetMapName());
}

bool EROutdoorBackdrop::IsOutdoorMap(const FString& Name)
{
    return Name.EndsWith(TEXT("L_Village")) || Name.EndsWith(TEXT("L_Depot")) ||
        Name.EndsWith(TEXT("L_CentralBuilding")) || Name.EndsWith(TEXT("L_MiddleEasternAlleys")) ||
        Name.EndsWith(TEXT("L_Town"));
}

bool EROutdoorBackdrop::IsNight(const UWorld& World)
{
    return IsOutdoorMap(World) && SelectedSky(World) >= FirstNightChoice;
}

TArray<FString> EROutdoorBackdrop::OfflineSkyLabels()
{
    TArray<FString> Labels{TEXT("Random")};
    for (int32 Index = 1; Index <= PanoramaCount; ++Index)
        Labels.Add(FString::Printf(TEXT("Standard %02d"), Index));
    for (const FSunsetSky& Sky : SunsetSkies)
        Labels.Add(FString(TEXT("Sunset · ")) + Sky.Name);
    for (const TCHAR* Number : NightSkies)
        Labels.Add(FString(TEXT("Night · Sky ")) + Number);
    return Labels;
}

UTexture2D* EROutdoorBackdrop::LoadOfflineSkyPreview(int32 Choice, FVector2D& Size)
{
    if (Choice <= 0 || Choice > SkyChoiceCount) return nullptr;
    Size = Choice <= PanoramaCount ? FVector2D(1, 1) : FVector2D(2, 1);
    if (Choice <= PanoramaCount) return LoadSkyTexture(StandardTextureName(Choice - 1));
    if (Choice >= FirstNightChoice) return LoadSkyTexture(NightTextureName(Choice) + TEXT("_Preview"), LOAD_NoWarn);
    return LoadSkyTexture(FString(SunsetSkies[Choice - PanoramaCount - 1].Texture) + TEXT("_Preview"), LOAD_NoWarn);
}

uint32 EROutdoorBackdrop::RollLightingSeed()
{
#if !UE_BUILD_SHIPPING
    uint32 TestSeed = 0;
    if (FParse::Value(FCommandLine::Get(), TEXT("EROutdoorLightingSeed="), TestSeed) && TestSeed != 0)
        return TestSeed;
#endif
    return FGuid::NewGuid().A | 2u;
}

bool EROutdoorBackdrop::Apply(AActor& Host, uint32 Seed, const FLinearColor& GroundTint, UMaterialInterface* GroundSurface, uint32 LightingSeed)
{
    if(!Host.GetWorld()->IsGameWorld() || Host.GetNetMode()==NM_DedicatedServer || !FApp::CanEverRender())return false;

    static const FName SkyName(TEXT("EROutdoorSky")), GroundName(TEXT("EROutdoorHorizon"));
    auto* Sky=FindObjectFast<UStaticMeshComponent>(&Host,SkyName);
    if(!Sky)
    {
        auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        auto* Finish=LoadObject<UMaterialInterface>(nullptr,*(FString(BackdropPath)+TEXT("M_OutdoorSky.M_OutdoorSky")));
        if(Sphere && Finish)
            Sky=AddBackdropPart(Host,SkyName,Sphere,UMaterialInstanceDynamic::Create(Finish,&Host),FVector::ZeroVector,FVector(20000));
    }

    auto* Horizon=FindObjectFast<UStaticMeshComponent>(&Host,GroundName);
    if(!Horizon)
    {
        auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
        if(Plane)
            Horizon=AddBackdropPart(Host,GroundName,Plane,GroundSurface,FVector(0,0,-500),FVector(5000,5000,1));
    }
    if(Horizon)
    {
        if(GroundSurface)
        {
            Horizon->SetMaterial(0,GroundSurface);
        }
        else
        {
            auto* Finish=Cast<UMaterialInstanceDynamic>(Horizon->GetMaterial(0));
            if(!Finish)
            {
                auto* Parent=LoadObject<UMaterialInterface>(nullptr,*(FString(BackdropPath)+TEXT("M_OutdoorHorizon.M_OutdoorHorizon")));
                if(Parent)
                {
                    Finish=UMaterialInstanceDynamic::Create(Parent,&Host);
                    Horizon->SetMaterial(0,Finish);
                }
            }
            if(Finish)Finish->SetVectorParameterValue(TEXT("Tint"),GroundTint);
        }
    }

    auto* SkyFinish=Sky ? Cast<UMaterialInstanceDynamic>(Sky->GetMaterial(0)) : nullptr;
    if(!SkyFinish)return false;
    FRandomStream Cosmetic(int32(HashCombineFast(Seed,0x534b5942u)));
    const int32 SkyChoice = SelectedSky(*Host.GetWorld());
    const bool bNight = SkyChoice >= FirstNightChoice;
    const bool bSunset = SkyChoice == 0 ? (LightingSeed & 1u) != 0 : SkyChoice > PanoramaCount && !bNight;
    const int32 StandardIndex = SkyChoice > 0 && SkyChoice <= PanoramaCount ? SkyChoice - 1 : Cosmetic.RandRange(0, PanoramaCount - 1);
    FString TextureName = StandardTextureName(StandardIndex);
    float PanoramaYaw = PanoramaSunYaw[StandardIndex];
    UTexture2D* Panorama = nullptr;
    if (bNight) TextureName = NightTextureName(SkyChoice);
    else if (bSunset)
    {
        FRandomStream SunsetRandom(int32(LightingSeed >> 1));
        const int32 SunsetIndex = SkyChoice > PanoramaCount ?
            SkyChoice - PanoramaCount - 1 : SunsetRandom.RandRange(0, UE_ARRAY_COUNT(SunsetSkies) - 1);
        const FSunsetSky& Sunset = SunsetSkies[SunsetIndex];
        TextureName = Sunset.Texture;
        PanoramaYaw = Sunset.SunYaw;
        Panorama = LoadSkyTexture(TextureName, LOAD_NoWarn);
        if (!Panorama)
        {
            // Older maps bundles still contain the original sunset panoramas.
            constexpr int32 FallbackIndices[] = {0, 8, 10};
            const int32 Fallback = FallbackIndices[SunsetRandom.RandRange(0, UE_ARRAY_COUNT(FallbackIndices) - 1)];
            TextureName = StandardTextureName(Fallback);
            PanoramaYaw = PanoramaSunYaw[Fallback];
        }
    }
    if (!Panorama) Panorama = LoadSkyTexture(TextureName);
    if (bNight)
    {
        static const FName ExposureName(TEXT("EROutdoorNightExposure"));
        if (!FindObjectFast<UPostProcessComponent>(&Host, ExposureName))
        {
            auto* Exposure = NewObject<UPostProcessComponent>(&Host, ExposureName);
            Host.AddInstanceComponent(Exposure);
            Exposure->SetupAttachment(Host.GetRootComponent());
            Exposure->bUnbound = true;
            Exposure->Priority = 100.f;
            Exposure->Settings.bOverride_AutoExposureBias = true;
            Exposure->Settings.AutoExposureBias = -1.25f;
            Exposure->RegisterComponent();
        }
        for (TActorIterator<ADirectionalLight> It(Host.GetWorld()); It; ++It)
            It->GetLightComponent()->SetVisibility(false);
        // Keep a little light on the ground and walls even on presets without sky GI.
        static const FName FillName(TEXT("EROutdoorNightFill"));
        if (!FindObjectFast<UDirectionalLightComponent>(&Host, FillName))
        {
            auto* Fill = NewObject<UDirectionalLightComponent>(&Host, FillName);
            Host.AddInstanceComponent(Fill);
            Fill->SetupAttachment(Host.GetRootComponent());
            Fill->SetMobility(EComponentMobility::Movable);
            Fill->SetWorldRotation(FRotator(-55, 35, 0));
            Fill->SetLightColor(FLinearColor(.66f, .75f, 1.f));
            Fill->SetIntensity(.18f);
            Fill->SetCastShadows(false);
            Fill->SetAtmosphereSunLight(false);
            Fill->SetAffectGlobalIllumination(false);
            Fill->RegisterComponent();
        }
        for (TActorIterator<ASkyLight> It(Host.GetWorld()); It; ++It)
        {
            auto* Ambient = It->GetLightComponent();
            Ambient->SetIntensity(.29f);
            Ambient->SetLightColor(FLinearColor(.55f, .68f, 1.f));
            Ambient->SetLowerHemisphereColor(FLinearColor(.02f, .025f, .04f));
            if (!Ambient->IsRealTimeCaptureEnabled()) Ambient->SetCaptureIsDirty();
        }
    }
    Sky->SetVisibility(Panorama!=nullptr);
    if(!Panorama)return false;
    SkyFinish->SetTextureParameterValue(TEXT("Panorama"),Panorama);

    UDirectionalLightComponent* MainLight=nullptr;
    for(TActorIterator<ADirectionalLight> It(Host.GetWorld());It;++It)
    {
        auto* Candidate=Cast<UDirectionalLightComponent>(It->GetLightComponent());
        if(Candidate && Candidate->IsVisible() && (!MainLight || Candidate->Intensity>MainLight->Intensity))
            MainLight=Candidate;
    }
    float YawOffset=0;
    if(MainLight)
    {
        if (bSunset) ApplySunsetLighting(Host, *MainLight, SunsetElevation);
        const FVector TowardSun=-MainLight->GetForwardVector();
        const float LightYaw=FMath::RadiansToDegrees(FMath::Atan2(TowardSun.Y,TowardSun.X));
        YawOffset=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LightYaw,PanoramaYaw));
    }
    SkyFinish->SetScalarParameterValue(TEXT("PanoramaYawRadians"),YawOffset);

    UE_LOG(LogTemp,Display,TEXT("ER_OUTDOOR_BACKDROP panorama=%s seed=%u yaw_offset=%.1f lighting=%s lighting_seed=%u"),
        *TextureName,Seed,FMath::RadiansToDegrees(YawOffset), bNight ? TEXT("night") : bSunset ? TEXT("sunset") : TEXT("standard"), LightingSeed);
    return true;
}
