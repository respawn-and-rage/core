#include "ERRecon.h"
#include "ERCharacter.h"
#include "ERBotController.h"
#include "ERCorpse.h"
#include "ERPlayerController.h"
#include "ERProceduralMap.h"
#include "ERWeapon.h"
#include "ERSmokeCloud.h"
#include "ERBlockDebris.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/DamageEvents.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Net/UnrealNetwork.h"
#include "Misc/App.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
int32 TeamIndex(EERTeam Team) { return Team == EERTeam::Omega ? 1 : 0; }
EERTeam TeamAt(int32 Index) { return Index ? EERTeam::Omega : EERTeam::Alpha; }
double ServerTime(const UWorld* World)
{
    const auto* GS = World->GetGameState();
    return GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}
}

FLinearColor ERRecon::TeamColor(EERTeam Team)
{
    return Team == EERTeam::Omega ? FLinearColor(1,.25f,.1f) : FLinearColor(.2f,.65f,1);
}
bool ERRecon::InSightCone(const FVector& Eye, const FVector& Forward, const FVector& Target, float Range, bool Panoramic, float FullAngle)
{
    const FVector Delta = Target - Eye;
    // Drone optics cover a ground footprint; altitude must not shrink it.
    if ((Panoramic ? Delta.SizeSquared2D() : Delta.SizeSquared()) > FMath::Square(Range)) return false;
    return Panoramic || Delta.SizeSquared() < 1 || FVector::DotProduct(Forward, Delta.GetSafeNormal()) >= FMath::Cos(FMath::DegreesToRadians(FullAngle*.5f));
}
bool ERRecon::IsExplored(const TArray<uint8>& Bits, int32 Cell)
{
    return Cell >= 0 && Cell < Grid*Grid && Bits.IsValidIndex(Cell/8) && (Bits[Cell/8] & (1 << (Cell%8)));
}
void ERRecon::Explore(TArray<uint8>& Bits, int32 Cell)
{
    if (Cell >= 0 && Cell < Grid*Grid && Bits.Num() == FogBytes) Bits[Cell/8] |= 1 << (Cell%8);
}
UERReconComponent::UERReconComponent() { SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick = false; }
void UERReconComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UERReconComponent, Snapshot, COND_OwnerOnly);
}

AERReconBase::AERReconBase()
{
    bReplicates = true; bAlwaysRelevant = true; SetCanBeDamaged(false);
    Case = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosedCase")); SetRootComponent(Case);
    PrimaryActorTick.bCanEverTick = true;
    Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid")); Lid->SetupAttachment(Case);
    Lock = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lock")); Lock->SetupAttachment(Case);
    Lid->SetCollisionEnabled(ECollisionEnabled::NoCollision); Lock->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Emblem = CreateDefaultSubobject<UDecalComponent>(TEXT("TeamEmblem")); Emblem->SetupAttachment(Lid);
    Emblem->DecalSize=FVector(3,13,13); Emblem->SetRelativeRotation(FRotator(-90,0,0));
    // Only the grounded lower shell blocks movement; animated parts are cosmetic.
    Case->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Case->SetCollisionObjectType(ECC_WorldStatic);
    Case->SetCollisionResponseToAllChannels(ECR_Block);
}
void AERReconBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERReconBase, Team); DOREPLIFETIME(AERReconBase, LaunchAt);
}
void AERReconBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* Material=Team==EERTeam::Omega ? OmegaGraffiti.Get() : AlphaGraffiti.Get();
    if(Emblem->GetDecalMaterial()!=Material) Emblem->SetDecalMaterial(Material);
    // Original source rotation keys, sampled against replicated server time.
    const double Age=ServerTime(GetWorld())-LaunchAt;
    const float Key=FMath::Clamp(float(Age*25),0.f,100.f);
    auto Angle=[Key](const TArray<float>& Keys)
    {
        if(Keys.Num()!=101) return 0.f;
        const int32 I=FMath::Min(99,FMath::FloorToInt(Key));
        return FMath::RadiansToDegrees(FMath::Lerp(Keys[I],Keys[I+1],Key-I));
    };
    Lid->SetRelativeRotation(FRotator(0,0,-Angle(LidOpening)));
    Lock->SetRelativeRotation(FRotator(0,0,-Angle(LockOpening)));
}
AERReconDrone::AERReconDrone()
{
    bReplicates = true; bAlwaysRelevant = true; PrimaryActorTick.bCanEverTick = true;
    ShotTarget = CreateDefaultSubobject<USphereComponent>(TEXT("ShotTarget")); SetRootComponent(ShotTarget);
    ShotTarget->InitSphereRadius(43);
    ShotTarget->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ShotTarget->SetCollisionObjectType(ECC_WorldDynamic);
    ShotTarget->SetCollisionResponseToAllChannels(ECR_Ignore);
    ShotTarget->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body")); Body->SetupAttachment(ShotTarget);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->bReceivesDecals=true;
    UndersideSymbol = CreateDefaultSubobject<UDecalComponent>(TEXT("UndersideTeamDecal")); UndersideSymbol->SetupAttachment(Body);
    UndersideSymbol->SetRelativeLocation(FVector(0,0,-8));
    UndersideSymbol->SetRelativeRotation(FRotator(90,0,0));
    UndersideSymbol->DecalSize=FVector(8,15,15);

}
FVector AERReconDrone::FlightPosition(const FERDroneFlight& Route, double Time)
{
    const double Age = FMath::Max(0., Time-Route.StartedAt);
    const double Phase = Route.Team == EERTeam::Omega ? PI : 0;
    const double Angle = Phase + FMath::Max(0., Age-8.) * .16;
    const FVector Orbit = Route.Center + FVector(FMath::Cos(Angle)*Route.Radius.X, FMath::Sin(Angle)*Route.Radius.Y, 0);
    if (Age >= 8) return Orbit;
    // First climb vertically clear of the spawn, then join the orbit above all roofs.
    const FVector High(Route.Launch.X, Route.Launch.Y, Route.Center.Z);
    return Age < 4 ? FMath::Lerp(Route.Launch, High, Age/4) : FMath::Lerp(High, Orbit, (Age-4)/4);
}
void AERReconDrone::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Flight.Team == EERTeam::None || bShotDown) return;
    const double Now = ServerTime(GetWorld());
    auto* SymbolMaterial=Flight.Team==EERTeam::Omega ? OmegaSymbolMaterial.Get() : AlphaSymbolMaterial.Get();
    if (UndersideSymbol->GetDecalMaterial()!=SymbolMaterial) UndersideSymbol->SetDecalMaterial(SymbolMaterial);
    const FVector Position = FlightPosition(Flight, Now);
    const FVector Direction = FlightPosition(Flight, Now+.1)-Position;
    const float Yaw = Direction.SizeSquared2D()>.001 ? Direction.Rotation().Yaw : (Flight.Team==EERTeam::Omega ? 180.f : 0.f);
    SetActorLocationAndRotation(Position, FRotator(0,Yaw,0));
}
float AERReconDrone::TakeDamage(float Amount, const FDamageEvent& Event, AController* DamageInstigator, AActor* Causer)
{
    const auto* Weapon = Cast<AERWeapon>(Causer);
    if (!HasAuthority() || bShotDown || Amount <= 0 || !Event.IsOfType(FPointDamageEvent::ClassID) ||
        !Weapon || !Weapon->Data || Weapon->Data->Payload != EERPayload::Bullet) return 0;
    bShotDown = true;
    ShotTarget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (Director.IsValid())
    {
        Director->MulticastDroneBreak(GetActorLocation(),GetActorRotation(),static_cast<const FPointDamageEvent&>(Event).ShotDirection.GetSafeNormal(),FMath::Rand());
        Director->DroneDestroyed(this);
    }
    Destroy();
    return Amount;
}
void AERReconDrone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERReconDrone, Flight);
}

AERReconDirector::AERReconDirector()
{
    bReplicates = true; bAlwaysRelevant = true; PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .05f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    BirdCamera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("BirdCamera")); BirdCamera->SetupAttachment(RootComponent);
    BirdCamera->bCaptureEveryFrame = false; BirdCamera->bCaptureOnMovement = false;
    BirdCamera->ProjectionType = ECameraProjectionMode::Orthographic;
    BirdCamera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    BirdCamera->ShowFlags.SetAtmosphere(false); BirdCamera->ShowFlags.SetFog(false);
    BirdCamera->ShowFlags.SetMotionBlur(false); BirdCamera->ShowFlags.SetTemporalAA(false);
    BirdCamera->ShowFlags.SetDynamicShadows(false);
    BirdCamera->bUseRayTracingIfEnabled = false;
}
void AERReconDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERReconDirector, Frame);
}
void AERReconDirector::ClearFleet()
{
    ClearDebris();
    for (auto& Team : Teams)
    {
        if (Team.Drone.IsValid()) Team.Drone->Destroy();
        if (Team.Base.IsValid()) Team.Base->Destroy();
        Team = FTeamRecon(); Team.Explored.Init(0, ERRecon::FogBytes);
    }
}
void AERReconDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority()) ClearFleet();
    ClearDebris();
    for(UDecalComponent* Mark:Graffiti) if(IsValid(Mark)) Mark->DestroyComponent();
    Graffiti.Reset();
    BirdCamera->TextureTarget = nullptr; BirdView = nullptr;
    Super::EndPlay(Reason);
}
void AERReconDirector::ClearDebris()
{
    for (AERBlockDebris* Piece:LocalDebris) if (IsValid(Piece)) Piece->Destroy();
    LocalDebris.Reset();
}
void AERReconDirector::MulticastDroneBreak_Implementation(FVector_NetQuantize Position, FRotator Rotation, FVector_NetQuantizeNormal Direction, int32 Seed)
{
    ++BreakEvents; LastBreakPosition=Position;
    if (GetNetMode()==NM_DedicatedServer || !FApp::CanEverRender()) return;
    LocalDebris.RemoveAll([](const auto& Piece){return !IsValid(Piece.Get());});
    FRandomStream Random(Seed);
    for (int32 I=0;I<DebrisMeshes.Num() && I<5;++I)
    {
        if (!DebrisMeshes[I]) continue;
        while (LocalDebris.Num()>=24) { LocalDebris[0]->Destroy(); LocalDebris.RemoveAt(0); }
        const FVector Offset=Rotation.RotateVector(FVector(I==0?0:(I%2?18:-18),I==0?0:(I<3?18:-18),0));
        const FTransform Transform(Rotation,FVector(Position)+Offset);
        auto* Piece=GetWorld()->SpawnActorDeferred<AERBlockDebris>(AERBlockDebris::StaticClass(),Transform,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Piece) continue;
        Piece->Initialize(DebrisMeshes[I],nullptr,Transform,6.f,4.f);
        Piece->FinishSpawning(Piece->GetActorTransform());
        Piece->Mesh->SetMassOverrideInKg(NAME_None,I==0?1.f:.2f,true);
        Piece->Mesh->SetPhysicsLinearVelocity(FVector(Direction)*180+Random.VRand()*140+FVector(0,0,100));
        Piece->Mesh->SetPhysicsAngularVelocityInDegrees(Random.VRand()*Random.FRandRange(180,540));
        LocalDebris.Add(Piece);
    }
    UE_LOG(LogTemp,Display,TEXT("ER_RECON_DEBRIS event=%u pieces=%d origin=%s"),BreakEvents,LocalDebris.Num(),*FVector(Position).ToString());
}
void AERReconDirector::ResetRecon()
{
    if (!HasAuthority()) return;
    ConfigureMap();
}
void AERReconDirector::ConfigureMap()
{
    ClearFleet();
    FBox Bounds(ForceInit);
    AERProceduralMap* Procedural = nullptr;
    SourceMapEpoch = 0;
    if (TActorIterator<AERProceduralMap> It(GetWorld()); It)
    {
        Procedural = *It; SourceMapEpoch = It->GetMapEpoch();
        const FVector Size = It->GetLayout().ArenaSize;
        Bounds = FBox(FVector(-Size.X*.5,-Size.Y*.5,0), FVector(Size.X*.5,Size.Y*.5,Size.Z)).TransformBy(It->GetActorTransform());
    }
    if (!Bounds.IsValid)
    {
        // Authored levels: include static gameplay geometry, never sky spheres,
        // lights, pawns or transient effects in the minimap extent.
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            if (*It == this || It->IsA<APawn>() || It->IsA<AERWeapon>() || It->IsA<AERCorpse>()) continue;
            const FBox Box = It->GetComponentsBoundingBox(false);
            if (It->GetActorEnableCollision() && Box.IsValid && Box.GetSize().GetMax() < 30000 && Box.GetSize().GetMax() > 50) Bounds += Box;
        }
    }
    if (!Bounds.IsValid) Bounds = FBox(FVector(-3000,-2200,0), FVector(3000,2200,1400));
    Frame.Center = FVector(Bounds.GetCenter().X,Bounds.GetCenter().Y,Bounds.Min.Z);
    // Square bird view; the corners of the playable rectangle fit inside the disc.
    Frame.Span = FMath::Max(2000., Bounds.GetSize().Size2D()*1.06);
    Frame.Top = Bounds.Max.Z;
    Frame.Indoor = 0; Frame.IndoorOrbit = FVector2D::ZeroVector;
    if (Procedural && Procedural->GetLayout().IndoorCeiling > 0)
    {
        // The warehouse roof is solid: fly, survey and launch beneath it.
        const FVector Size = Procedural->GetLayout().ArenaSize;
        Frame.Indoor = Bounds.Min.Z + Procedural->GetLayout().IndoorCeiling;
        Frame.IndoorOrbit = FVector2D(Size.X*.22, Size.Y*.3);
    }
    Frame.GeometryEpoch = SourceMapEpoch;
    ++Frame.Epoch; ForceNetUpdate();
    const auto Query = SightQuery();
    const bool ClosedRooms=Procedural && Procedural->LayoutConfig.TemplateAsset && Procedural->LayoutConfig.TemplateAsset->Archetype==EERMapArchetype::Rooms;
    SurfaceHeights.SetNum(ERRecon::Grid*ERRecon::Grid);
    for (int32 Cell=0; Cell<SurfaceHeights.Num(); ++Cell)
    {
        const FVector P=CellPosition(Cell);
        if(ClosedRooms)
        {
            SurfaceHeights[Cell]=Bounds.Min.Z+ERProcedural::RoomsFloorHeight(Procedural->GetLayout(),FVector2D(Procedural->GetActorTransform().InverseTransformPosition(P)))+8;
            continue;
        }
        FHitResult Surface;
        SurfaceHeights[Cell]=GetWorld()->LineTraceSingleByChannel(Surface,FVector(P.X,P.Y,Frame.Indoor>0 ? Frame.Indoor-10 : Frame.Top+100),FVector(P.X,P.Y,Frame.Center.Z-100),ECC_Visibility,Query)
            ? Surface.ImpactPoint.Z+8 : P.Z;
    }
    TArray<AERBotPatrolPoint*> Patrol;
    for (TActorIterator<AERBotPatrolPoint> It(GetWorld());It;++It) Patrol.Add(*It);
    Patrol.Sort([](const AERBotPatrolPoint& A,const AERBotPatrolPoint& B){return A.RouteIndex<B.RouteIndex;});
    for (int32 Index=0; !ClosedRooms && Index<2; ++Index)
    {
        FVector Spawn = Frame.Center + FVector(Index ? 2000 : -2000,0,100);
        FRotator Facing(0, Index ? 180 : 0, 0);
        if (Procedural)
        {
            const auto& Starts = Index ? Procedural->GetLayout().OmegaSpawns : Procedural->GetLayout().AlphaSpawns;
            if (!Starts.IsEmpty())
            {
                Spawn = Procedural->GetActorTransform().TransformPosition(Starts[0].GetLocation());
                Facing = (Procedural->GetActorQuat()*Starts[0].GetRotation()).Rotator();
            }
        }
        else for (TActorIterator<AERTeamPlayerStart> It(GetWorld()); It; ++It) if (It->Team == TeamAt(Index))
        { Spawn = It->GetActorLocation(); Facing = It->GetActorRotation(); break; }
        FVector Placement = Spawn;
        bool Placed = false;
        // Grounded clear footprint beside, never on, a player start. The case is
        // deliberately outside the standing capsule at each candidate.
        for (int32 Ring=0; Ring<4 && !Placed; ++Ring) for (int32 Step=0; Step<12 && !Placed; ++Step)
        {
            const float Angle = Facing.Yaw + 90 + Step*30;
            const FVector Offset = FRotator(0,Angle,0).Vector()*(180+Ring*80);
            FHitResult Ground;
            if (!GetWorld()->LineTraceSingleByChannel(Ground,Spawn+Offset+FVector(0,0,120),Spawn+Offset-FVector(0,0,350),ECC_Visibility,Query) || Ground.ImpactNormal.Z < .8) continue;
            Placement = Ground.ImpactPoint+FVector(0,0,2);
            if (FMath::Abs(Placement.Z-(Spawn.Z-92.5)) > 100) continue;
            const float LaunchTop=ClosedRooms ? Spawn.Z+FMath::Max(560.f,Procedural->LayoutConfig.TemplateAsset->Rooms.CorridorHeight)-120:Frame.Indoor>0 ? Frame.Indoor-20:Frame.Top+1200;
            if (GetWorld()->LineTraceTestByChannel(Placement+FVector(0,0,70),FVector(Placement.X,Placement.Y,LaunchTop),ECC_Visibility,Query)) continue;
            Placed = !GetWorld()->OverlapBlockingTestByChannel(Placement+FVector(0,0,25),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(FVector(45,40,24)),Query);
            for (TActorIterator<AERTeamPlayerStart> It(GetWorld()); It && Placed; ++It)
                if (FVector::DistSquared2D(Placement,It->GetActorLocation()) < FMath::Square(120.f)) Placed = false;
            // Reserve the walking corridor as well as spawn capsules. A clear
            // point beside a start can otherwise block an authored bot loop.
            for (int32 I=0; Placed && Patrol.Num()>1 && I<Patrol.Num(); ++I)
            {
                const FVector Closest=FMath::ClosestPointOnSegment(Placement,Patrol[I]->GetActorLocation(),Patrol[(I+1)%Patrol.Num()]->GetActorLocation());
                if (Placement.Z+50.f>Closest.Z-92.5f && Placement.Z<Closest.Z+92.5f &&
                    FVector::DistSquared2D(Placement,Closest)<FMath::Square(120.f)) Placed=false;
            }
        }
        if (!Placed)
        {
            UE_LOG(LogTemp, Warning, TEXT("ER_RECON_BASE_NO_CLEAR_GROUND team=%d"),Index);
            continue;
        }
        FActorSpawnParameters Params; Params.Owner = this; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const TSubclassOf<AERReconBase> Class = BaseClass ? BaseClass.Get() : AERReconBase::StaticClass();
        auto* Base = GetWorld()->SpawnActor<AERReconBase>(Class, Placement, Facing, Params);
        if (Base) { Base->Team = TeamAt(Index); Teams[Index].Base = Base; LaunchDrone(Index); }
    }
    UE_LOG(LogTemp,Display,TEXT("ER_RECON_RESET epoch=%u source=%u span=%.0f bases=%d/%d"),Frame.Epoch,SourceMapEpoch,Frame.Span,Teams[0].Base.IsValid(),Teams[1].Base.IsValid());
}
void AERReconDirector::LaunchDrone(int32 Index)
{
    for(TActorIterator<AERProceduralMap> It(GetWorld());It;++It)
        if(It->LayoutConfig.TemplateAsset && It->LayoutConfig.TemplateAsset->Archetype==EERMapArchetype::Rooms)return;
    auto& Team = Teams[Index];
    if (Team.Drone.IsValid() || !Team.Base.IsValid() || Team.Launched >= int32(ERServer::Get(GetWorld(),"Recon.Stock"))) return;
    const FVector Launch = Team.Base->GetActorLocation()+FVector(0,0,80);
    FTransform Transform(FRotator::ZeroRotator,Launch);
    const TSubclassOf<AERReconDrone> Class = DroneClass ? DroneClass.Get() : AERReconDrone::StaticClass();
    auto* Drone = GetWorld()->SpawnActorDeferred<AERReconDrone>(Class,Transform,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Drone) return;
    Drone->Director = this;
    Drone->Flight.Team = TeamAt(Index); Drone->Flight.Launch = Launch;
    Drone->Flight.Center = FVector(Frame.Center.X,Frame.Center.Y,Frame.Indoor>0 ? Frame.Indoor+50 : Frame.Top+900);
    Drone->Flight.Radius = Frame.Indoor>0 ? Frame.IndoorOrbit : FVector2D(Frame.Span*.26);
    Team.Base->LaunchAt = ServerTime(GetWorld()); Team.Base->ForceNetUpdate();
    Drone->Flight.StartedAt = Team.Base->LaunchAt+1.2;
    Drone->FinishSpawning(Transform);
    Team.Drone = Drone; ++Team.Launched; Team.NextLaunch = 0;
    UE_LOG(LogTemp,Display,TEXT("ER_RECON_LAUNCH team=%d number=%d time=%.2f"),Index,Team.Launched,Drone->Flight.StartedAt);
}
void AERReconDirector::DroneDestroyed(AERReconDrone* Drone)
{
    if (!HasAuthority() || !Drone) return;
    auto& Team = Teams[TeamIndex(Drone->Flight.Team)];
    if (Team.Drone.Get() != Drone) return;
    Team.Drone.Reset();
    Team.NextLaunch = Team.Launched < int32(ERServer::Get(GetWorld(),"Recon.Stock")) ? ServerTime(GetWorld())+ERServer::Get(GetWorld(),"Recon.Replacement") : 0;
    // Immediately remove drone-only contacts; explored terrain remains.
    UpdateVisibility();
    UE_LOG(LogTemp,Display,TEXT("ER_RECON_DOWN team=%d remaining=%d next=%.2f"),TeamIndex(Drone->Flight.Team),int32(ERServer::Get(GetWorld(),"Recon.Stock"))-Team.Launched,Team.NextLaunch);
}
AERReconDrone* AERReconDirector::ActiveDrone(EERTeam Team) const { return Teams[TeamIndex(Team)].Drone.Get(); }
int32 AERReconDirector::Remaining(EERTeam Team) const { const auto& T=Teams[TeamIndex(Team)]; if(!T.Base.IsValid())return 0; return int32(ERServer::Get(GetWorld(),"Recon.Stock"))-T.Launched+(T.Drone.IsValid()?1:0); }
double AERReconDirector::NextLaunch(EERTeam Team) const { return Teams[TeamIndex(Team)].NextLaunch; }
FVector AERReconDirector::CellPosition(int32 Cell) const
{
    // Capture camera yaw0: world +Y is right, world +X is up.
    return Frame.Center + FVector((.5-(Cell/ERRecon::Grid+.5)/ERRecon::Grid)*Frame.Span,
        ((Cell%ERRecon::Grid+.5)/ERRecon::Grid-.5)*Frame.Span,75);
}
int32 AERReconDirector::WorldCell(const FVector& World) const
{
    const FVector Delta = (World-Frame.Center)/Frame.Span;
    const int32 X = FMath::FloorToInt((Delta.Y+.5)*ERRecon::Grid);
    const int32 Y = FMath::FloorToInt((.5-Delta.X)*ERRecon::Grid);
    return X>=0 && X<ERRecon::Grid && Y>=0 && Y<ERRecon::Grid ? Y*ERRecon::Grid+X : INDEX_NONE;
}
FCollisionQueryParams AERReconDirector::SightQuery() const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERReconSight),true);
    Query.AddIgnoredActor(this);
    for (TActorIterator<APawn> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
    for (TActorIterator<AERCorpse> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
    for (TActorIterator<AERWeapon> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
    return Query;
}
bool AERReconDirector::CanSee(const FVector& Eye, const FVector& Forward, const FVector& Target, bool Drone) const
{
    TArray<AERSmokeCloud*> Clouds;
    for (TActorIterator<AERSmokeCloud> It(GetWorld()); It; ++It) Clouds.Add(*It);
    return ERRecon::InSightCone(Eye,Forward,Target,SightRange(Drone),Drone,ERServer::Get(GetWorld(),"Recon.PlayerAngle")) && SightClear(Eye,Target,SightQuery(),Clouds);
}
bool AERReconDirector::SightClear(const FVector& Eye, const FVector& Target, const FCollisionQueryParams& Query, const TArray<AERSmokeCloud*>& Clouds) const
{
    if (GetWorld()->LineTraceTestByChannel(Eye,Target,ECC_Visibility,Query)) return false;
    float Depth=0;
    for (auto* Cloud:Clouds) { Depth+=Cloud->ReconOpticalDepth(Eye,Target); if (Depth>=1.f) return false; }
    return true;
}
void AERReconDirector::UpdateVisibility()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERRecon_Visibility);
    struct FObserver { FVector Eye, Forward; bool Drone=false; };
    struct FPlayerSample { AERCharacter* Character; FVector Head; FERReconContact Contact; };
    TArray<FPlayerSample,TInlineAllocator<24>> Players;
    TArray<FObserver> Observers[2];
    const float PlayerAngle=ERServer::Get(GetWorld(),"Recon.PlayerAngle");
    const float PlayerRange=SightRange(false),DroneRange=SightRange(true);
    for (TActorIterator<AERCharacter> It(GetWorld()); It; ++It)
    {
        const auto* PS = It->GetPlayerState<AERPlayerState>();
        if (!PS || PS->Team == EERTeam::None || It->Health->bDead || !It->bNetworkReady) continue;
        FVector Eye; FRotator Aim; It->GetActorEyesViewPoint(Eye,Aim);
        Eye=It->Camera->GetComponentLocation();
        Observers[TeamIndex(PS->Team)].Add({Eye,Aim.Vector(),false});
        FERReconContact Contact;Contact.Position=It->GetActorLocation();Contact.Yaw=Aim.Yaw;Contact.Team=PS->Team;
        Players.Add({*It,Eye,Contact});
    }
    const auto Query = SightQuery();
    TArray<AERSmokeCloud*> Clouds;
    for (TActorIterator<AERSmokeCloud> It(GetWorld()); It; ++It) Clouds.Add(*It);
    for (int32 Index=0; Index<2; ++Index)
    {
        if (Teams[Index].Drone.IsValid()) Observers[Index].Add({Teams[Index].Drone->GetActorLocation(),FVector::ForwardVector,true});
        int32 TraceBudget=2048;
        for (int32 Visited=0; Visited<ERRecon::Grid*ERRecon::Grid && TraceBudget>0; ++Visited)
        {
            const int32 Cell=Teams[Index].FogCursor;
            Teams[Index].FogCursor=(Cell+1)%(ERRecon::Grid*ERRecon::Grid);
            if (ERRecon::IsExplored(Teams[Index].Explored,Cell)) continue;
            const FVector Ground = CellPosition(Cell);
            for (const auto& Source : Observers[Index])
            {
                if (TraceBudget<=0) break;
                // The overhead camera sees roofs as well as ground. Players
                // can reveal an upper storey from their own elevation; no ray
                // bypasses a roof/wall to spot a person beneath it.
                const FVector Target(Ground.X,Ground.Y,Source.Drone ? SurfaceHeights[Cell] : Source.Eye.Z-25);
                if (!ERRecon::InSightCone(Source.Eye,Source.Forward,Target,Source.Drone ? DroneRange:PlayerRange,Source.Drone,PlayerAngle)) continue;
                --TraceBudget;
                if (SightClear(Source.Eye,Target,Query,Clouds)) { ERRecon::Explore(Teams[Index].Explored,Cell); break; }
            }
        }
    }
    // Sight depends on the team, never on the receiving controller. Keep one
    // observation per team/target; personalize only the self marker afterwards.
    TBitArray<> VisiblePlayers[2];
    for(auto& Visible:VisiblePlayers)Visible.Init(false,Players.Num());
    for(int32 Index=0;Index<2;++Index)
        for(int32 PlayerIndex=0;PlayerIndex<Players.Num();++PlayerIndex)
        {
            const auto& Sample=Players[PlayerIndex];
            bool Visible=false;
            for(const auto& Source:Observers[Index])
            {
                for(const FVector& Target:{Sample.Head,FVector(Sample.Contact.Position)})
                    if(ERRecon::InSightCone(Source.Eye,Source.Forward,Target,Source.Drone ? DroneRange:PlayerRange,Source.Drone,PlayerAngle) &&
                        SightClear(Source.Eye,Target,Query,Clouds)){Visible=true;break;}
                if(Visible)break;
            }
            if(Visible)
            {
                VisiblePlayers[Index][PlayerIndex]=true;
                ERRecon::Explore(Teams[Index].Explored,WorldCell(Sample.Contact.Position));
            }
        }
    const double Now = ServerTime(GetWorld());
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* PC=Cast<AERPlayerController>(It->Get());
        const auto* PS=PC ? PC->GetPlayerState<AERPlayerState>() : nullptr;
        if (!PC || !PS || !PC->Recon) continue;
        FERReconSnapshot Snapshot; Snapshot.Epoch=Frame.Epoch; Snapshot.Team=PS->Team; Snapshot.SampleTime=Now;
        if (PS->Team != EERTeam::None)
        {
            const int32 Index=TeamIndex(PS->Team);
            Snapshot.Explored=Teams[Index].Explored;
            Snapshot.Remaining=Remaining(PS->Team); Snapshot.Airborne=Teams[Index].Drone.IsValid(); Snapshot.NextLaunch=Teams[Index].NextLaunch;
            for(int32 PlayerIndex=0;PlayerIndex<Players.Num();++PlayerIndex)
            {
                const auto& Sample=Players[PlayerIndex];
                const bool Self=Sample.Character==PC->GetPawn();
                if(!Self && !VisiblePlayers[Index][PlayerIndex])continue;
                FERReconContact Contact=Sample.Contact;Contact.Self=Self;
                Snapshot.Contacts.Add(Contact);
                // A visible person always occupies a revealed map cell.
                ERRecon::Explore(Teams[Index].Explored,WorldCell(Contact.Position));
                ERRecon::Explore(Snapshot.Explored,WorldCell(Contact.Position));
            }
            if (auto* Drone=Teams[Index].Drone.Get())
            {
                FERReconContact Contact; Contact.Position=Drone->GetActorLocation(); Contact.Team=PS->Team; Contact.Drone=true;
                Snapshot.Contacts.Add(Contact);
            }
        }
        PC->Recon->Snapshot=MoveTemp(Snapshot); PC->ForceNetUpdate();
    }
}
void AERReconDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (DebrisEpoch!=Frame.Epoch) { ClearDebris(); DebrisEpoch=Frame.Epoch; }
    if (HasAuthority())
    {
        uint32 Epoch=0;
        if (TActorIterator<AERProceduralMap> It(GetWorld()); It) Epoch=It->GetMapEpoch();
        if (SourceMapEpoch != Epoch) ConfigureMap();
        const double Now=ServerTime(GetWorld());
        if (Now >= NextUpdate)
        {
            NextUpdate=Now+.25;
            for (int32 I=0; I<2; ++I) if (Teams[I].NextLaunch>0 && Now>=Teams[I].NextLaunch) LaunchDrone(I);
            UpdateVisibility();
        }
    }
    if (GetNetMode()==NM_DedicatedServer || !FApp::CanEverRender() || !Frame.Epoch) return;
    for (TActorIterator<AERBlockRoom> It(GetWorld()); It; ++It)
        if (!It->IsMapReady() || It->GetMapEpoch()!=Frame.GeometryEpoch) return;
    if (GraffitiEpoch != Frame.Epoch) DecorateBases();
    TickGraffitiGallery();
    if (CapturedEpoch != Frame.Epoch)
    {
        if (!bCapturePending) { CaptureAfter=GetWorld()->GetTimeSeconds()+1; bCapturePending=true; }
        if (GetWorld()->GetTimeSeconds()>=CaptureAfter) CaptureBirdView();
    }
}
void AERReconDirector::CaptureBirdView()
{
    if (!BirdView)
    {
        BirdView=NewObject<UTextureRenderTarget2D>(this);
        BirdView->RenderTargetFormat=RTF_RGBA8; BirdView->ClearColor=FLinearColor(.04,.05,.06,1);
        BirdView->Filter=TF_Trilinear;
        BirdView->bAutoGenerateMips=true;
        BirdView->MipsSamplerFilter=TF_Bilinear;
        BirdView->InitAutoFormat(1024,1024); BirdView->UpdateResourceImmediate(true);
    }
    BirdCamera->TextureTarget=BirdView;
    BirdCamera->OrthoWidth=Frame.Span;
    BirdCamera->SetWorldLocation(FVector(Frame.Center.X,Frame.Center.Y,Frame.Top+3000));
    BirdCamera->SetWorldRotation(FRotator(-90,0,0));
    BirdCamera->HiddenActors.Reset(); BirdCamera->HiddenComponents.Reset();
    // Roofed arenas show their floor plan: hide only the roof art groups.
    for (TActorIterator<AERProceduralMap> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for (auto* Part : Parts) if (Part->ComponentHasTag(TEXT("ERTemplateRoof"))) BirdCamera->HideComponent(Part);
    }
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        if (It->IsA<APawn>() || It->IsA<AERCorpse>() || It->IsA<AERWeapon>() || It->IsA<AERReconDrone>() || It->IsA<AERProjectile>() || It->IsA<AERBlockDebris>() || It->IsA<AERSmokeCloud>()) BirdCamera->HiddenActors.Add(*It);
    BirdCamera->CaptureScene(); CapturedEpoch=Frame.Epoch; bCapturePending=false;
    UE_LOG(LogTemp,Display,TEXT("ER_RECON_CAPTURE epoch=%u size=1024"),CapturedEpoch);
}
