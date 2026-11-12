#include "EROutdoorBackdrop.h"
#include "Components/DirectionalLightComponent.h"
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

constexpr int32 PanoramaCount=15;
// Approximate azimuth of the sun or brightest cloud opening in each source image.
constexpr float PanoramaSunYaw[PanoramaCount]={
    -21,-125,22,91,101,-107,88,177,169,154,-96,-29,-171,-52,85
};
struct FSunsetSky
{
    int32 Panorama;
    float SunElevation;
};
constexpr FSunsetSky SunsetSkies[] = {{0, 19.f}, {8, 17.f}, {10, 16.f}};

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
    UE_LOG(LogTemp, Display, TEXT("ER_OUTDOOR_SUNSET sun_elevation=%.1f sun_lux=%.1f"),
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
    const FString Name = World.GetMapName();
    return Name.EndsWith(TEXT("L_Village")) || Name.EndsWith(TEXT("L_Depot")) ||
        Name.EndsWith(TEXT("L_CentralBuilding")) || Name.EndsWith(TEXT("L_MiddleEasternAlleys")) ||
        Name.EndsWith(TEXT("L_Town"));
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
    const bool bSunset = (LightingSeed & 1u) != 0;
    FRandomStream SunsetRandom(int32(LightingSeed >> 1));
    const FSunsetSky* SunsetSky = bSunset ?
        &SunsetSkies[SunsetRandom.RandRange(0, UE_ARRAY_COUNT(SunsetSkies) - 1)] : nullptr;
    const int32 Index=SunsetSky ? SunsetSky->Panorama : Cosmetic.RandRange(0,PanoramaCount-1);
    const FString TextureName=FString::Printf(TEXT("T_Sky_CSS_%03d"),Index+1);
    auto* Panorama=LoadObject<UTexture2D>(nullptr,*(FString(BackdropPath)+TextureName+TEXT(".")+TextureName));
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
        if (SunsetSky) ApplySunsetLighting(Host, *MainLight, SunsetSky->SunElevation);
        const FVector TowardSun=-MainLight->GetForwardVector();
        const float LightYaw=FMath::RadiansToDegrees(FMath::Atan2(TowardSun.Y,TowardSun.X));
        YawOffset=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LightYaw,PanoramaSunYaw[Index]));
    }
    SkyFinish->SetScalarParameterValue(TEXT("PanoramaYawRadians"),YawOffset);

    UE_LOG(LogTemp,Display,TEXT("ER_OUTDOOR_BACKDROP panorama=%s seed=%u yaw_offset=%.1f lighting=%s lighting_seed=%u"),
        *TextureName,Seed,FMath::RadiansToDegrees(YawOffset), bSunset ? TEXT("sunset") : TEXT("standard"), LightingSeed);
    return true;
}
