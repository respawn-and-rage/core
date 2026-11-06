#include "ERGrenadeExplosion.h"
#include "ERProceduralMap.h"
#include "ERShotEffects.h"
#include "EngineUtils.h"
#include "ERPerformanceSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVectorUniform.h"

AERGrenadeExplosion::AERGrenadeExplosion()
{
    PrimaryActorTick.bCanEverTick = true;
    Debris = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Debris"));
    SetRootComponent(Debris);
    Debris->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Debris->SetCastShadow(false);
    Debris->bComponentUseFixedSkelBounds = true;
    Debris->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    Debris->SetCullDistance(4000);
    Flash = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flash"));
    Flash->SetupAttachment(Debris);
    Flash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Flash->SetCastShadow(false);
    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
    Light->SetupAttachment(Debris);
    Light->SetCastShadows(false);
    Light->SetLightColor(FLinearColor(1.f, .92f, .8f));
    Light->SetAttenuationRadius(450);
    Burst = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("Burst"));
    Burst->SetupAttachment(Debris);
    Burst->bAutoActivate = false;
    Burst->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Burst->SetCastShadow(false);
}

bool AERGrenadeExplosion::ConfigureCompactBurst(UParticleSystem* System)
{
#if WITH_EDITOR
    if (!System || !System->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Effects/Grenade/"))) return false;
    System->Modify();
    System->bUseFixedRelativeBoundingBox=true;
    System->FixedRelativeBoundingBox=FBox(FVector(-400,-400,-40),FVector(400,400,450));
    for (const auto& Emitter : System->Emitters)
        if (Emitter) for (const auto& LOD : Emitter->LODLevels)
            if (LOD && LOD->RequiredModule)
            {
                const FString Name=Emitter->EmitterName.ToString();
                // Remove the demo's decorative ring and billboard flare.
                LOD->bEnabled=Name==TEXT("Smoke");
                Emitter->DetailModeBitmask=15;
                LOD->RequiredModule->EmitterLoops = 1;
                LOD->RequiredModule->EmitterDelay=0;
                LOD->RequiredModule->EmitterDelayLow=0;
                const bool Smoke=Name==TEXT("Smoke"), Fire=Name==TEXT("Fire");
                if(LOD->SpawnModule)
                {
                    auto* Rate=NewObject<UDistributionFloatConstant>(LOD->SpawnModule);Rate->Constant=0;
                    LOD->SpawnModule->Rate.Distribution=Rate;
                    LOD->SpawnModule->BurstList.Reset();
                    FParticleBurst B;B.Time=0;B.Count=Smoke?3:1;B.CountLow=B.Count;
                    LOD->SpawnModule->BurstList.Add(B);
                }
                for(const auto& Module:LOD->Modules)
                {
                    if(auto* Color=Cast<UParticleModuleColorOverLife>(Module);Color && Fire)
                    {
                        auto* D=NewObject<UDistributionVectorUniform>(Color);
                        D->Min=D->Max=FVector(.22f);
                        Color->ColorOverLife.Distribution=D;
                    }
                    if(auto* Life=Cast<UParticleModuleLifetime>(Module))
                    {
                        auto* D=NewObject<UDistributionFloatUniform>(Life);
                        D->Min=Smoke?.55f:.08f;D->Max=Smoke?.95f:.12f;
                        Life->Lifetime.Distribution=D;
                    }
                    if(auto* Size=Cast<UParticleModuleSize>(Module);Size && (Smoke || Fire))
                    {
                        auto* D=NewObject<UDistributionVectorUniform>(Size);
                        D->Min=FVector(60.f);D->Max=FVector(110.f);
                        Size->StartSize.Distribution=D;
                    }
                    if(auto* Velocity=Cast<UParticleModuleVelocity>(Module);Velocity && (Smoke || Fire))
                    {
                        auto* D=NewObject<UDistributionVectorUniform>(Velocity);
                        D->Min=FVector(-65,-65,35);
                        D->Max=FVector(65,65,90);
                        Velocity->StartVelocity.Distribution=D;
                    }
                }
                UE_LOG(LogTemp,Display,TEXT("ER_BURST_EMITTER name=%s enabled=%d"),*Name,LOD->bEnabled);
            }
    System->PostEditChange();
    System->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

bool AERGrenadeExplosion::AssignDebrisMaterials(USkeletalMesh* Mesh, UMaterialInterface* Surface, UMaterialInterface* Side)
{
#if WITH_EDITOR
    if (!Mesh || !Surface || !Side || !Mesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Effects/Grenade/"))) return false;
    Mesh->Modify();
    TArray<FSkeletalMaterial> Materials = Mesh->GetMaterials();
    for (auto& Slot : Materials)
        Slot.MaterialInterface = Slot.MaterialSlotName.ToString().Contains(TEXT("Side")) ? Side : Surface;
    Mesh->SetMaterials(Materials);
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

void AERGrenadeExplosion::PrecacheEffect(TSubclassOf<AERGrenadeExplosion> EffectClass)
{
    if (!EffectClass || !FApp::CanEverRender()) return;
    static TSet<TWeakObjectPtr<UClass>> Prepared;
    if (Prepared.Contains(EffectClass.Get())) return;
    Prepared.Add(EffectClass.Get());
    // Blueprint component defaults contain the art before the first grenade is
    // fired, so the engine can prepare its rendering pipelines during loadout.
    auto* Defaults = EffectClass->GetDefaultObject<AERGrenadeExplosion>();
    Defaults->Debris->PrecachePSOs();
    Defaults->Flash->PrecachePSOs();
    Defaults->Burst->PrecachePSOs();
}

void AERGrenadeExplosion::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() == NM_DedicatedServer || !BurstTemplate) { Destroy(); return; }
    USoundBase* SelectedSound=ExplosionSound;
    UNiagaraSystem* SelectedWaterSystem=nullptr;
    FVector WaterSurface=FVector::ZeroVector;
    for(TActorIterator<AERProceduralMap> It(GetWorld());It;++It)
        if(It->WaterDepthAt(GetActorLocation())>5.f && It->WaterSurfaceAt(GetActorLocation(),WaterSurface))
        {if(It->WaterExplosionSound)SelectedSound=It->WaterExplosionSound;SelectedWaterSystem=It->WaterExplosionSystem;break;}
    if(SelectedSound)
    {
        auto* Audio=UGameplayStatics::SpawnSoundAtLocation(this,SelectedSound,GetActorLocation(),FRotator::ZeroRotator,
            .65f,1.f,0.f,ExplosionAttenuation,ExplosionConcurrency,true);
        UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_AUDIO sound=%s started=%d spatial=%d concurrency=%d"),
            *GetNameSafe(SelectedSound),Audio && Audio->IsPlaying(),ExplosionAttenuation!=nullptr,ExplosionConcurrency!=nullptr);
    }
    if(SelectedWaterSystem)
    {
        UERShotEffects::SpawnWaterExplosion(GetWorld(),SelectedWaterSystem,WaterSurface);
        Destroy(); // The underwater burst replaces the dry flash, debris and light.
        return;
    }
    SetActorScale3D(FVector(FMath::Clamp(BurstScale, .1f, 2.f)));
    Debris->SetVisibility(false);
    Flash->SetStaticMesh(FlashMesh);
    if(FlashMaterial) Flash->SetMaterial(0,FlashMaterial);
    Flash->SetVisibility(FlashMesh && FlashMaterial);
    Flash->SetWorldScale3D(FVector(.45f));
    Burst->SetTemplate(BurstTemplate);
    Burst->SetRelativeLocation(FVector(0,0,25));
    Burst->ActivateSystem(true);
    Burst->SetVisibility(true);
    Burst->SetHiddenInGame(false);
    Duration = 3.f;
    Light->SetLightColor(FLinearColor(1.f,.92f,.8f));
    Light->SetIntensity(1500);
    if(ERPerformance::Get().Defaults || ERPerformance::Get().Effects>1) SpawnGroundChips();
    SetLifeSpan(Duration + .05f);
    UE_LOG(LogTemp, Display, TEXT("ER_GRENADE_BURST system=%s scale=%.2f duration=%.2f cosmetic_collision=none"),
        *GetNameSafe(BurstTemplate), BurstScale, Duration);
}

void AERGrenadeExplosion::SpawnGroundChips()
{
    if(!GroundFragmentMesh) return;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERGrenadeGround),false,this);
    FHitResult Ground;
    if(!GetWorld()->LineTraceSingleByObjectType(Ground,GetActorLocation()+FVector(0,0,5),
        GetActorLocation()-FVector(0,0,60),FCollisionObjectQueryParams(ECC_WorldStatic),Query)
        || Ground.ImpactNormal.Z<.65f) return;
    const int32 Count=FMath::RandRange(3,6);
    for(int32 I=0;I<Count;++I)
    {
        const FVector Offset(FMath::FRandRange(-35.f,35.f),FMath::FRandRange(-35.f,35.f),0);
        FHitResult Surface;
        if(!GetWorld()->LineTraceSingleByObjectType(Surface,Ground.ImpactPoint+Offset+FVector(0,0,15),
            Ground.ImpactPoint+Offset-FVector(0,0,15),FCollisionObjectQueryParams(ECC_WorldStatic),Query)
            || Surface.ImpactNormal.Z<.65f) continue;
        auto* Chip=NewObject<UStaticMeshComponent>(this);AddInstanceComponent(Chip);
        Chip->SetStaticMesh(GroundFragmentMesh);Chip->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Chip->SetCastShadow(false);Chip->RegisterComponent();
        Chip->SetWorldLocation(Surface.ImpactPoint+Surface.ImpactNormal*4);
        if(auto* Component=Surface.GetComponent();Component && Component->GetMaterial(0)) Chip->SetMaterial(0,Component->GetMaterial(0));
        const FVector Scale(FMath::FRandRange(.08f,.18f),FMath::FRandRange(.06f,.14f),FMath::FRandRange(.025f,.05f));
        Chip->SetWorldScale3D(Scale);Chip->SetWorldRotation(FRotator(0,FMath::FRandRange(0.f,360.f),0));
        GroundChips.Add({Chip,FVector(Offset.X*4,Offset.Y*4,FMath::FRandRange(160.f,290.f)),
            FVector(FMath::FRandRange(-240.f,240.f),FMath::FRandRange(-240.f,240.f),FMath::FRandRange(-180.f,180.f)),Scale});
    }
    GroundFragmentCount=GroundChips.Num();
    UE_LOG(LogTemp,Display,TEXT("ER_GRENADE_GROUND_CHIPS count=%d"),GroundFragmentCount);
}

void AERGrenadeExplosion::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Elapsed += DeltaSeconds;
    Flash->SetWorldScale3D(FVector(.45f+.35f*FMath::Clamp(Elapsed/.065f,0.f,1.f)));
    if(Elapsed>=.065f) Flash->SetVisibility(false);
    for(auto& Chip:GroundChips)
    {
        if(!Chip.bSettled)
        {
            Chip.Velocity.Z-=980.f*DeltaSeconds;
            const FVector Start=Chip.Mesh->GetComponentLocation(),End=Start+Chip.Velocity*DeltaSeconds;
            FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(ERGroundChip),false,this);
            if(GetWorld()->LineTraceSingleByObjectType(Hit,Start,End,FCollisionObjectQueryParams(ECC_WorldStatic),Query))
            { Chip.Mesh->SetWorldLocation(Hit.ImpactPoint+Hit.ImpactNormal*3);Chip.bSettled=true; }
            else { Chip.Mesh->SetWorldLocation(End);Chip.Mesh->AddWorldRotation(FRotator(Chip.Spin.X,Chip.Spin.Y,Chip.Spin.Z)*DeltaSeconds); }
        }
        Chip.Mesh->SetWorldScale3D(Chip.Scale*FMath::Clamp((Duration-Elapsed)/.65f,0.f,1.f));
    }
    if (Elapsed>=.2f && Elapsed-DeltaSeconds<.2f)
        UE_LOG(LogTemp,Display,TEXT("ER_BURST_RENDER active=%d particles=%d visible=%d location=%s bounds=%s"),Burst->IsActive(),Burst->GetNumActiveParticles(),Burst->IsVisible(),*Burst->GetComponentLocation().ToString(),*Burst->Bounds.GetBox().ToString());
    if (Elapsed < .07f)
    {
        const float Pulse = FMath::Clamp(1.f - Elapsed / .07f, 0.f, 1.f);
        Light->SetIntensity(1500 * Pulse * Pulse);
    }
    else Light->SetVisibility(false);
}
