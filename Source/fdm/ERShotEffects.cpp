#include "ERShotEffects.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "Misc/App.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "NiagaraEmitter.h"
#include "NiagaraSpriteRendererProperties.h"
#endif

namespace
{
constexpr double MuzzleStreakLength = 55.;
constexpr double TracerInitialTravel = 155.;

void RemoveAfter(UWorld* World, UNiagaraComponent* Component, float Seconds)
{
    FTimerHandle Handle;
    const TWeakObjectPtr<UNiagaraComponent> Weak(Component);
    World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak]()
    { if (Weak.IsValid()) Weak->DestroyComponent(); }), Seconds, false);
}
}
void UERShotEffects::Prewarm(UWorld* World, UNiagaraSystem* System, const FVector& Position)
{
    if (!World || !System || !FApp::CanEverRender() || World->GetNetMode() == NM_DedicatedServer) return;
    if (auto* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Position,
        FRotator::ZeroRotator, FVector::OneVector, true, false, ENCPoolMethod::None, false))
    {
        Effect->SetVisibility(false);
        Effect->SetVariablePosition(TEXT("User.Position"), Position);
        Effect->SetVariableVec3(TEXT("User.Alignment"), FVector::ForwardVector);
        Effect->SetVariableVec2(TEXT("User.Size"), FVector2D(1,1));
        Effect->SetVariableLinearColor(TEXT("User.Color"), FLinearColor::Transparent);
        Effect->Activate(true);
        RemoveAfter(World, Effect, 2.f);
    }
}
void UERShotEffects::SpawnTracer(UWorld* World, UNiagaraSystem* System, const FVector& Start, const FVector& End, float Lifetime)
{
    if (!World || !System || !FApp::CanEverRender() || World->GetNetMode() == NM_DedicatedServer) return;
    const FVector Segment = End - Start;
    if (Segment.SizeSquared() < 1) return;
    // Purely cosmetic: the server has already applied damage. No ticking bullet actor.
    auto* Beam = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, (Start + End) * .5,
        FRotator::ZeroRotator, FVector::OneVector, true, false, ENCPoolMethod::None, false);
    if (!Beam) return;
    // Emitter-source sprites do not contribute particle bounds. Enclose the
    // complete segment so frustum culling cannot discard a visible tracer.
    const FVector Extent = Segment.GetAbs() * .5 + FVector(4);
    Beam->SetSystemFixedBounds(FBox(-Extent, Extent));
    Beam->SetVariablePosition(TEXT("User.Position"), (Start + End) * .5);
    Beam->SetVariableVec3(TEXT("User.Alignment"), Segment.GetSafeNormal());
    Beam->SetVariableVec2(TEXT("User.Size"), FVector2D(.12, Segment.Size()));
    Beam->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(4.f, 2.2f, .65f, .7f));
    Beam->Activate(true);
    RemoveAfter(World, Beam, Lifetime);
}
void UERShotEffects::SpawnImpact(UWorld* World, UNiagaraSystem* System, const FVector& Position, const FVector& Normal)
{
    if (!World || !System || !FApp::CanEverRender() || World->GetNetMode() == NM_DedicatedServer || Normal.IsNearlyZero()) return;
    // One tiny, soft sprite for 35ms; no directional burst or spark fan.
    if (auto* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Position + Normal * .15f,
        FRotator::ZeroRotator, FVector::OneVector, true, false, ENCPoolMethod::None, false))
    {
        Effect->SetSystemFixedBounds(FBox(FVector(-3), FVector(3)));
        Effect->SetVariableVec2(TEXT("User.Size"), FVector2D(1.6,1.6));
        Effect->SetVariableVec3(TEXT("User.Alignment"), FVector::UpVector);
        Effect->Activate(true);
        static TArray<TWeakObjectPtr<UNiagaraComponent>> Impacts;
        Impacts.RemoveAll([](const auto& Item) { return !Item.IsValid(); });
        while (Impacts.Num() >= 64)
        {
            if (Impacts[0].IsValid()) Impacts[0]->DestroyComponent();
            Impacts.RemoveAt(0);
        }
        Impacts.Add(Effect);
        RemoveAfter(World, Effect, .035f);
    }
}
void UERShotEffects::SpawnWaterSplash(UWorld* World, UNiagaraSystem* System, const FVector& Surface)
{
    if(!World || !System || !FApp::CanEverRender() || World->GetNetMode()==NM_DedicatedServer)return;
    TArray<TWeakObjectPtr<UNiagaraComponent>> Sprays;
    for(int32 I=0;I<5;++I)
    {
        const float Angle=float(I)*2.f*PI/5.f;
        const FVector Offset(FMath::Cos(Angle)*6,FMath::Sin(Angle)*6,14);
        if(auto* Effect=UNiagaraFunctionLibrary::SpawnSystemAtLocation(World,System,Surface+Offset,
            FRotator::ZeroRotator,FVector::OneVector,true,false,ENCPoolMethod::None,false))
        {
            Effect->SetSystemFixedBounds(FBox(FVector(-100),FVector(100)));
            Effect->SetVariablePosition(TEXT("User.Position"),Surface+Offset);
            Effect->SetVariableVec3(TEXT("User.Alignment"),(FVector(Offset.X,Offset.Y,18)).GetSafeNormal());
            Effect->SetVariableVec2(TEXT("User.Size"),FVector2D(12.f,48.f));
            Effect->SetVariableLinearColor(TEXT("User.Color"),FLinearColor(.32f,.75f,1.f,.45f));
            Effect->Activate(true);
            Sprays.Add(Effect);
        }
    }
    if(Sprays.IsEmpty())return;
    const double Started=World->GetTimeSeconds();
    auto Handle=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Handle,FTimerDelegate::CreateLambda(
        [WeakWorld=TWeakObjectPtr<UWorld>(World),Sprays,Surface,Started,Handle]()
        {
            UWorld* ActiveWorld=WeakWorld.Get();if(!ActiveWorld)return;
            const float T=FMath::Clamp(float((ActiveWorld->GetTimeSeconds()-Started)/.3),0.f,1.f);
            for(int32 I=0;I<Sprays.Num();++I)if(auto* Effect=Sprays[I].Get())
            {
                const float Angle=float(I)*2.f*PI/5.f;
                const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0);
                const FVector Position=Surface+Radial*(6+50*T)+FVector(0,0,14+42*T-22*T*T);
                Effect->SetWorldLocation(Position);
                Effect->SetVariablePosition(TEXT("User.Position"),Position);
                Effect->SetVariableVec2(TEXT("User.Size"),FVector2D(12-8*T,48-30*T));
                if(T>=1.f)Effect->DestroyComponent();
            }
            if(T>=1.f)ActiveWorld->GetTimerManager().ClearTimer(*Handle);
        }),.025f,true);
}
void UERShotEffects::SpawnWaterExplosion(UWorld* World, UNiagaraSystem* System, const FVector& Surface)
{
    if(!World || !System || !FApp::CanEverRender() || World->GetNetMode()==NM_DedicatedServer)return;
    // A radial crown and central plume at the waterline. Presentation only:
    // explosion authority and damage are unchanged.
    TArray<TWeakObjectPtr<UNiagaraComponent>> Sprays;
    for(int32 I=0;I<13;++I)
    {
        const bool Centre=I==12;
        const float Angle=float(I)*2.f*PI/12.f;
        const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FVector Origin=Surface+(Centre ? FVector(0,0,70):Radial*55+FVector(0,0,55));
        if(auto* Effect=UNiagaraFunctionLibrary::SpawnSystemAtLocation(World,System,Origin,
            FRotator::ZeroRotator,FVector::OneVector,true,false,ENCPoolMethod::None,false))
        {
            Effect->SetSystemFixedBounds(FBox(FVector(-350),FVector(350)));
            Effect->SetVariablePosition(TEXT("User.Position"),Origin);
            Effect->SetVariableVec3(TEXT("User.Alignment"),Centre ? FVector::UpVector:(Radial+FVector(0,0,1.6)).GetSafeNormal());
            Effect->SetVariableVec2(TEXT("User.Size"),Centre ? FVector2D(48,160):FVector2D(22,105));
            Effect->SetVariableLinearColor(TEXT("User.Color"),FLinearColor(.32f,.72f,1.f,.6f));
            Effect->Activate(true);
            Sprays.Add(Effect);
        }
    }
    if(Sprays.IsEmpty())return;
    const double Started=World->GetTimeSeconds();
    auto Handle=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Handle,FTimerDelegate::CreateLambda(
        [WeakWorld=TWeakObjectPtr<UWorld>(World),Sprays,Surface,Started,Handle]()
        {
            UWorld* ActiveWorld=WeakWorld.Get();if(!ActiveWorld)return;
            const float T=FMath::Clamp(float((ActiveWorld->GetTimeSeconds()-Started)/.65),0.f,1.f);
            for(int32 I=0;I<Sprays.Num();++I)if(auto* Effect=Sprays[I].Get())
            {
                const bool Centre=I==12;
                const float Angle=float(I)*2.f*PI/12.f;
                const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0);
                const FVector Position=Surface+(Centre ? FVector(0,0,70+90*T-65*T*T)
                    :Radial*(55+150*T)+FVector(0,0,55+95*T-45*T*T));
                Effect->SetWorldLocation(Position);
                Effect->SetVariablePosition(TEXT("User.Position"),Position);
                Effect->SetVariableVec2(TEXT("User.Size"),Centre ? FVector2D(48-35*T,160+60*T-170*T*T)
                    :FVector2D(22-13*T,105+100*T-160*T*T));
                if(T>=1.f)Effect->DestroyComponent();
            }
            if(T>=1.f)ActiveWorld->GetTimerManager().ClearTimer(*Handle);
        }),.025f,true);
}
bool UERShotEffects::ConfigureImpact(UNiagaraSystem* System, UMaterialInterface* Material)
{
    // Both effects use a single emitter-source sprite; only size/material differ.
    return ConfigureTracer(System, Material);
}

bool UERShotEffects::ConfigureTracer(UNiagaraSystem* System, UMaterialInterface* Material)
{
#if WITH_EDITOR
    if (!System || !Material || !System->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Effects/Shots/"))) return false;
    auto* Source = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/SingleLoopingParticle.SingleLoopingParticle"));
    if (!Source) return false;
    System->Modify();
    TSet<FGuid> Handles;
    for (const auto& Handle : System->GetEmitterHandles()) Handles.Add(Handle.GetId());
    System->RemoveEmitterHandlesById(Handles);
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Source, Source->GetExposedVersion().VersionGuid);
    auto Handle = System->GetEmitterHandles().Last();
    const auto Emitter = Handle.GetInstance();
    auto* Data = Handle.GetEmitterData();
    if (!Data || !Emitter.Emitter) return false;
    Data->bLocalSpace = false;
    const auto Renderers = Data->GetRenderers();
    for (auto* Renderer : Renderers) Emitter.Emitter->RemoveRenderer(Renderer, Emitter.Version);
    auto* Sprite = NewObject<UNiagaraSpriteRendererProperties>(Emitter.Emitter);
    Sprite->Material = Material;
    Sprite->SourceMode = ENiagaraRendererSourceDataMode::Emitter;
    Sprite->Alignment = ENiagaraSpriteAlignment::CustomAlignment;
    Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
    const auto AddParameter = [System](const FNiagaraTypeDefinition& Type, const TCHAR* Name)
    { System->GetExposedParameters().AddParameter(FNiagaraVariable(Type, FName(Name))); };
    AddParameter(FNiagaraTypeDefinition::GetPositionDef(), TEXT("User.Position"));
    AddParameter(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Alignment"));
    AddParameter(FNiagaraTypeDefinition::GetVec2Def(), TEXT("User.Size"));
    AddParameter(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.Color"));
    Sprite->PositionBinding.SetValue(NAME_None, Emitter, Sprite->SourceMode);
    Sprite->SpriteAlignmentBinding.SetValue(TEXT("User.Alignment"), Emitter, Sprite->SourceMode);
    Sprite->SpriteSizeBinding.SetValue(TEXT("User.Size"), Emitter, Sprite->SourceMode);
    Sprite->ColorBinding.SetValue(NAME_None, Emitter, Sprite->SourceMode);
    Emitter.Emitter->AddRenderer(Sprite, Emitter.Version);
    FPropertyChangedEvent Changed(FindFProperty<FProperty>(UNiagaraSpriteRendererProperties::StaticClass(), TEXT("SourceMode")));
    Sprite->PostEditChangeProperty(Changed);
    System->RequestCompile(true);
    System->WaitForCompilationComplete();
    System->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

AERSpentCasing::AERSpentCasing()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    InitialLifeSpan = 3.f;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetRelativeScale3D(FVector(.03f,.009f,.009f));
    Mesh->SetCollisionObjectType(ECC_PhysicsBody);
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCastShadow(false);
    Mesh->SetReceivesDecals(false);
    Mesh->SetLinearDamping(.25f);
    Mesh->SetAngularDamping(.6f);
    Mesh->BodyInstance.bUseCCD = true;
}
void AERSpentCasing::BeginPlay()
{
    Super::BeginPlay();
    Mesh->SetSimulatePhysics(true);
    Mesh->SetEnableGravity(true);
}
AERSpentCasing* AERSpentCasing::Eject(UWorld* World, TSubclassOf<AERSpentCasing> Class,
    const FVector& Position, const FVector& Right, const FVector& Forward, float VisualScale)
{
    if (!World || !Class || !FApp::CanEverRender() || World->GetNetMode() == NM_DedicatedServer) return nullptr;
    TArray<AERSpentCasing*> Casings;
    for (TActorIterator<AERSpentCasing> It(World); It; ++It) Casings.Add(*It);
    Casings.Sort([](const auto& A, const auto& B) { return A.GetGameTimeSinceCreation() > B.GetGameTimeSinceCreation(); });
    while (Casings.Num() >= 32) { Casings[0]->Destroy(); Casings.RemoveAt(0); }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;
    auto* Casing = World->SpawnActor<AERSpentCasing>(Class,
        FTransform(FRotator::MakeFromEuler(FVector(20,45,10)), Position, FVector(VisualScale)), Params);
    if (Casing)
    {
        Casing->Mesh->SetPhysicsLinearVelocity(Right * FMath::FRandRange(65.f,95.f) +
            FVector::UpVector * FMath::FRandRange(35.f,60.f) + Forward * FMath::FRandRange(-10.f,10.f));
        Casing->Mesh->SetPhysicsAngularVelocityInDegrees(FVector(340,210,120));
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("ERShotAudit")))
            UE_LOG(LogTemp, Display, TEXT("ER_CASING_EJECT net=%d replicated=%d sim=%d pawn=%d projectile=%d"),
                int32(World->GetNetMode()), Casing->GetIsReplicated(), Casing->Mesh->IsSimulatingPhysics(),
                int32(Casing->Mesh->GetCollisionResponseToChannel(ECC_Pawn)),
                int32(Casing->Mesh->GetCollisionResponseToChannel(ECC_GameTraceChannel1)));
#endif
    }
    return Casing;
}

AERAttachedTracer::AERAttachedTracer()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    InitialLifeSpan = .5f; // Safety cleanup if the source disappears before ticking.
    Beam = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Beam"));
    SetRootComponent(Beam);
    Beam->SetAutoActivate(false);
    Beam->SetForceSolo(true);
    Beam->SetTickBehavior(ENiagaraTickBehavior::UsePrereqs);
}
AERAttachedTracer* AERAttachedTracer::Spawn(AERCharacter* Character, UNiagaraSystem* System,
    const FVector& End, bool bInFirstPerson, float Duration, const FVector* PredictedMuzzle)
{
    if (!Character || !System || !FApp::CanEverRender() || Character->GetNetMode() == NM_DedicatedServer || !Character->Weapon) return nullptr;
    const FVector Tip = PredictedMuzzle ? *PredictedMuzzle : Character->GetRenderedMuzzle(bInFirstPerson);
    const FVector ToEnd = End - Tip;
    const double Distance = ToEnd.Size();
    if (Distance < 1.) return nullptr;
    const double Speed = Character->GetVelocity().Size();
    const double RunSpeed = FMath::Max(1., double(Character->GetCharacterMovement()->MaxWalkSpeed));
    const double Gap = bInFirstPerson ? (PredictedMuzzle ? MuzzleStreakLength : 0.) :
        (Speed < 1. ? .75 : FMath::Lerp(50., 100., FMath::Clamp(Speed / RunSpeed, 0., 1.)));
    // Keep a visible segment ending at the impact, even at point-blank range.
    const double MaxGap = Distance - FMath::Min(5., Distance * .25);
    const FVector Start = Tip + (ToEnd / Distance) * FMath::Min(Gap, MaxGap);
    return SpawnSegment(Character, System, Start, End, bInFirstPerson, Duration, bInFirstPerson);
}

void AERAttachedTracer::SpawnMuzzleStreak(AERCharacter* Character, UNiagaraSystem* System, uint32 ShotKey)
{
    if (!System || !FApp::CanEverRender()) return;
    SpawnSegment(Character, System, FVector::ZeroVector, FVector::ZeroVector, true, .028f, false, ShotKey);
}

AERAttachedTracer* AERAttachedTracer::SpawnSegment(AERCharacter* Character, UNiagaraSystem* System,
    const FVector& Start, const FVector& End, bool bInFirstPerson, float Duration, bool bInTravel,
    TOptional<uint32> PredictedShot)
{
    auto* Tracer = Character->GetWorld()->SpawnActorDeferred<AERAttachedTracer>(StaticClass(), FTransform::Identity,
        Character, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Tracer) return nullptr;
    Tracer->Source = Character;
    Tracer->ShotData = Character->Weapon->Data;
    Tracer->LastStart = Start;
    Tracer->LastEnd = End;
    Tracer->bFirstPerson = bInFirstPerson;
    Tracer->bTravel = bInTravel;
    Tracer->PredictedShotKey = PredictedShot;
    Tracer->DurationSeconds = Duration;
    Tracer->Beam->SetAsset(System);
    UGameplayStatics::FinishSpawningActor(Tracer, FTransform::Identity);
    return Tracer;
}
void AERAttachedTracer::BeginPlay()
{
    Super::BeginPlay();
    Beam->AddTickPrerequisiteActor(this);
    Beam->PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    if (PredictedShotKey.IsSet()) return;
    // Start with a visible segment on the very first frame.
    const FVector Segment = LastEnd - LastStart;
    SetVisibleSegment(LastStart, bTravel ? LastStart + Segment.GetClampedToMaxSize(TracerInitialTravel) : LastEnd);
    Beam->Activate(true);
}
void AERAttachedTracer::BeginMuzzleStreak(AERCharacter& Character)
{
    // Camera and movement have finished updating for this shot's render frame.
    const FVector BarrelAxis = Character.Weapon->Data->Mesh ? FVector::ForwardVector : FVector::RightVector;
    FVector Ahead = Character.GetVisualMuzzle(true) + Character.FirstPersonRifle->GetComponentQuat().RotateVector(BarrelAxis);
    if (const auto* PC = Cast<APlayerController>(Character.GetController()); PC && PC->PlayerCameraManager)
        Ahead = PC->PlayerCameraManager->GetCameraCacheView().TransformWorldToFirstPerson(Ahead, false);
    LastStart = Character.GetRenderedMuzzle(true);
    LastEnd = LastStart + (Ahead - LastStart).GetSafeNormal() * MuzzleStreakLength;
    SetVisibleSegment(LastStart, LastEnd);
    Beam->Activate(true);
    Character.RecordPredictedTracer(PredictedShotKey.GetValue(), LastStart);
}
void AERAttachedTracer::SetVisibleSegment(const FVector& Start, const FVector& End)
{
    const FVector Segment = End - Start;
    Beam->SetWorldLocation((Start + End) * .5);
    const FVector Extent = Segment.GetAbs() * .5 + FVector(4);
    Beam->SetSystemFixedBounds(FBox(-Extent, Extent));
    Beam->SetVariableVec3(TEXT("User.Alignment"), Segment.GetSafeNormal());
    Beam->SetVariableVec2(TEXT("User.Size"), FVector2D(bFirstPerson ? .55 : .12, Segment.Size()));
}
void AERAttachedTracer::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Source.IsValid() || Source->Health->bDead || !Source->Weapon || Source->Weapon->Data != ShotData.Get())
    { Destroy(); return; }
    const double Now = GetWorld()->GetTimeSeconds();
    if (VisibleSince < 0)
    {
        VisibleSince = Now;
        if (PredictedShotKey.IsSet()) BeginMuzzleStreak(*Source.Get());
    }
    const double Age = Now - VisibleSince;
    if (bTravel)
    {
        constexpr double Speed = 26000.; // cm/s, visual travel only
        constexpr double StreakLength = 500.;
        const FVector Segment = LastEnd - LastStart;
        const double Distance = Segment.Size();
        const double Travel = TracerInitialTravel + Age * Speed;
        if (Age >= FMath::Max(double(DurationSeconds), Distance / Speed) && Travel - StreakLength >= Distance)
        { Destroy(); return; }
        const FVector Direction = Segment / Distance;
        SetVisibleSegment(LastStart + Direction * FMath::Clamp(Travel - StreakLength, 0., FMath::Max(0., Distance - 25.)),
            LastStart + Direction * FMath::Min(Travel, Distance));
    }
    else if (Age >= DurationSeconds) { Destroy(); return; }
    ++UpdatedFrames;
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("ERTracerAudit")))
    {
        const auto* PC = Cast<APlayerController>(Source->GetController());
        const float CameraAge = PC && PC->PlayerCameraManager ? float(Now - PC->PlayerCameraManager->GetCameraCacheTime()) : 0.f;
        UE_LOG(LogTemp, Display, TEXT("ER_TRACER_FRAME first_person=%d frames=%u speed=%.1f camera_age=%.4f lead=%.2f start=%s end=%s"),
            bFirstPerson, UpdatedFrames, Source->GetVelocity().Size2D(), CameraAge,
            FVector::Dist(LastStart, Source->GetRenderedMuzzle(bFirstPerson)), *LastStart.ToString(), *LastEnd.ToString());
    }
#endif
}
