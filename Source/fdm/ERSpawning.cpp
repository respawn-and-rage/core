#include "ERMatch.h"
#include "ERCharacter.h"
#include "ERBotController.h"
#include "ERProceduralMap.h"
#include "ERSpawnRisk.h"
#include "ERMapPlayability.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

namespace
{
const FName AdditionalStartTag(TEXT("ERAdditionalStart"));
}

void AERGameMode::PrepareTeamStarts()
{
    TActorIterator<AERProceduralMap> Map(GetWorld());
    if (Map)
    {
        if (PreparedSpawnMap == *Map && PreparedSpawnEpoch == Map->GetMapEpoch() && PreparedSpawnSeed == Map->LayoutConfig.Seed) return;
        FERProceduralLayout InitialLayout;
        const FERProceduralLayout* Layout = &Map->GetLayout();
        // HandleStartingNewPlayer may run before the map actor's BeginPlay.
        // Build at most once per seed/epoch, then reuse the real layout metadata.
        if (Map->GetMapEpoch() == 0)
        {
            Map->ResolveMapCandidate();
            if (!ERProcedural::Build(Map->LayoutConfig, Map->Config, InitialLayout, Map->CellSettings)) return;
            Layout = &InitialLayout;
        }
        if (Layout->AlphaSpawns.IsEmpty() || Layout->OmegaSpawns.IsEmpty()) return;
        for (const EERTeam Team : {EERTeam::Alpha, EERTeam::Omega})
        {
            TArray<AERTeamPlayerStart*> Starts;
            for (TActorIterator<AERTeamPlayerStart> Start(GetWorld()); Start; ++Start)
            {
                if (Start->Team != Team || Start->ActorHasTag(AdditionalStartTag)) continue;
                bool PrivateBotStart = false;
                for (const auto& Bot : TestBots) if (IsValid(Bot.Get()) && Bot->TrainingStart == *Start) PrivateBotStart = true;
                if (!PrivateBotStart) Starts.Add(*Start);
            }
            const auto& Frames = Team == EERTeam::Alpha ? Layout->AlphaSpawns : Layout->OmegaSpawns;
            while (Starts.Num() < Frames.Num())
            {
                auto* Start = GetWorld()->SpawnActor<AERTeamPlayerStart>();
                if (!Start) break;
                Start->Team = Team;
                Starts.Add(Start);
            }
            for (int32 I = 0; I < Starts.Num(); ++I)
            {
                Starts[I]->GetRootComponent()->SetMobility(EComponentMobility::Movable);
                Starts[I]->SetActorTransform(Frames[I % Frames.Num()] * Map->GetActorTransform());
            }
        }
        // Overflow positions belonged to the previous arena, even if its new
        // seed happened to preserve an old floor. Recompute them on demand.
        for (TActorIterator<AERTeamPlayerStart> Start(GetWorld()); Start; ++Start)
            if (Start->ActorHasTag(AdditionalStartTag))
            {
                bool PrivateBotStart = false;
                for (const auto& Bot : TestBots) if (IsValid(Bot.Get()) && Bot->TrainingStart == *Start) PrivateBotStart = true;
                if (!PrivateBotStart) Start->Destroy();
            }
        if (SpawnMemory) SpawnMemory->Reset();
        PreparedSpawnMap = *Map;
        PreparedSpawnEpoch = Map->GetMapEpoch();
        PreparedSpawnSeed = Map->LayoutConfig.Seed;
        return;
    }
}

AERTeamPlayerStart* AERGameMode::FindAdditionalStart(AController* Player, TConstArrayView<AERTeamPlayerStart*> Anchors)
{
    const auto* Capsule = DefaultPawnClass->GetDefaultObject<ACharacter>()->GetCapsuleComponent();
    const float Radius = Capsule->GetScaledCapsuleRadius(), HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    const auto Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
    FCollisionQueryParams SpaceQuery(SCENE_QUERY_STAT(ERAdditionalSpawn), false);
    if (Player->GetPawn()) SpaceQuery.AddIgnoredActor(Player->GetPawn());
    FCollisionQueryParams GeometryQuery(SpaceQuery);
    for (TActorIterator<APawn> Pawn(GetWorld()); Pawn; ++Pawn) GeometryQuery.AddIgnoredActor(*Pawn);
    // Search is bounded spatially, not by a player count. Never escape through
    // walls or spawn in mid-air; if the base is full, normal spawn retries wait.
    for (int32 Ring = 1; Ring <= 8; ++Ring) for (auto* Anchor : Anchors)
    {
        if (Anchor->ActorHasTag(AdditionalStartTag)) continue; // no creeping away from the team base
        for (int32 Side = 0; Side < Ring * 8; ++Side)
        {
            const float Angle = Side * 2.f * PI / (Ring * 8);
            FVector Position = Anchor->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Ring * 150.f);
            FHitResult Floor;
            if (!GetWorld()->LineTraceSingleByObjectType(Floor, Position + FVector(0,0,150), Position - FVector(0,0,300), FCollisionObjectQueryParams(ECC_WorldStatic), GeometryQuery) || Floor.ImpactNormal.Z < .7f) continue;
            Position.Z = Floor.ImpactPoint.Z + HalfHeight + 2;
            if (FMath::Abs(Position.Z - Anchor->GetActorLocation().Z) > 40) continue;
            if (GetWorld()->OverlapBlockingTestByChannel(Position, FQuat::Identity, ECC_Pawn, Shape, SpaceQuery)) continue;
            FHitResult Route;
            if (GetWorld()->SweepSingleByChannel(Route, Anchor->GetActorLocation(), Position, FQuat::Identity, ECC_Pawn, Shape, GeometryQuery)) continue;
            bool Supported = true;
            for (const FVector Edge : {FVector(Radius*.8f,0,0), FVector(-Radius*.8f,0,0), FVector(0,Radius*.8f,0), FVector(0,-Radius*.8f,0)})
            {
                FHitResult Support;
                const FVector Feet = Position - FVector(0,0,HalfHeight);
                if (!GetWorld()->LineTraceSingleByObjectType(Support, Feet + Edge + FVector(0,0,10), Feet + Edge - FVector(0,0,12), FCollisionObjectQueryParams(ECC_WorldStatic), GeometryQuery) || Support.ImpactNormal.Z < .7f)
                { Supported = false; break; }
            }
            if (!Supported) continue;
            // Initial overflow/training starts obey the same rule as recipe
            // starts. After destruction, ordinary respawn risk scoring applies.
            bool Occluded=true;
            for(TActorIterator<AERProceduralMap> Map(GetWorld());Map;++Map)
                if(Map->LayoutConfig.TemplateAsset && Map->LayoutConfig.ExpectedTeamSize>0 && Map->GetCellWorld().GetRemovedCount()==0 && Map->GetCellWorld().GetDetachedCount()==0)
                {
                    FERProceduralLayout Starts;Starts.AlphaSpawns.Add(FTransform(Position+FVector(0,0,100-HalfHeight-2)));
                    const auto Team=Player->GetPlayerState<AERPlayerState>()->Team;
                    for(TActorIterator<AERTeamPlayerStart> Opponent(GetWorld());Opponent;++Opponent)
                        if(Opponent->Team!=EERTeam::None && Opponent->Team!=Team)Starts.OmegaSpawns.Add(FTransform(Opponent->GetActorLocation()+FVector(0,0,Opponent->ActorHasTag(AdditionalStartTag) ? 100-HalfHeight-2:0)));
                    Occluded&=Map->IsMapReady() && ERPlayability::InitialSpawnsOccludedInWorld(GetWorld(),FTransform::Identity,Starts);
                }
            if(!Occluded)continue;
            auto* Start = GetWorld()->SpawnActor<AERTeamPlayerStart>(Position, Anchor->GetActorRotation());
            if (!Start) return nullptr;
            Start->Team = Player->GetPlayerState<AERPlayerState>()->Team;
            Start->Tags.Add(AdditionalStartTag);
            UE_LOG(LogTemp, Display, TEXT("ER_SPAWN_ADDITIONAL team=%d location=%s"), int32(Start->Team), *Position.ToCompactString());
            return Start;
        }
    }
    return nullptr;
}

AERTeamPlayerStart* AERGameMode::ChooseScoredStart(EERTeam Team, const TArray<AERTeamPlayerStart*>& Clear)
{
    if (!SpawnMemory) SpawnMemory = MakeShared<FERSpawnMemory>();
    const auto Tuning = ERSpawnRisk::Tuning(GetWorld());
    // Rotate past the cap so overflow starts stay reachable in a crowded base.
    const int32 Count = FMath::Min(Clear.Num(), Tuning.MaxCandidates), First = FMath::RandHelper(Clear.Num());
    TArray<FERSpawnCandidate> Candidates;
    for (int32 I = 0; I < Count; ++I) Candidates.AddDefaulted_GetRef().Location = Clear[(First + I) % Clear.Num()]->GetActorLocation();
    TArray<FERSpawnThreat> Threats; TArray<FERSpawnHazard> Hazards;
    ERSpawnRisk::GatherThreats(GetWorld(), Team, Threats, Hazards);
    const int32 Side = Team == EERTeam::Omega ? 1 : 0;
    const double Now = GetWorld()->GetTimeSeconds();
    const int32 Traces = ERSpawnRisk::Score(GetWorld(), Candidates, Threats, Hazards, SpawnMemory->Deaths[Side], SpawnMemory->Starts[Side], Now, Tuning);
    const int32 Choice = ERSpawnRisk::Pick(Candidates, Tuning, [](int32 Options) { return FMath::RandHelper(Options); });
    auto* Start = Clear[(First + Choice) % Clear.Num()];
    SpawnMemory->Add(SpawnMemory->Starts[Side], Start->GetActorLocation(), Now);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("ERSpawnAudit")))
    {
        int32 Exposed=0,Incomplete=0;
        for(const auto& C:Candidates){Exposed+=C.Seen>0;Incomplete+=C.Unknown>0;}
        UE_LOG(LogTemp,Display,TEXT("ER_SPAWN_CHOICE team=%d candidates=%d exposed=%d enemies=%d traces=%d chosen_seen=%d risk=%.0f incomplete=%d chosen_unknown=%d budget_hit=%d"),
            Side,Candidates.Num(),Exposed,Threats.Num(),Traces,Candidates[Choice].Seen,Candidates[Choice].Risk,Incomplete,Candidates[Choice].Unknown,
            Incomplete>0 && Traces>=Tuning.MaxTraces);
    }
#endif
    return Start;
}

AActor* AERGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    auto* State = Player ? Player->GetPlayerState<AERPlayerState>() : nullptr;
    if (!State) return nullptr;
    for(TActorIterator<AERProceduralMap> Map(GetWorld());Map;++Map)
        if(Map->LayoutConfig.TemplateAsset && !Map->IsMapReady())return nullptr;
    PrepareTeamStarts();
    const auto* Capsule = DefaultPawnClass->GetDefaultObject<ACharacter>()->GetCapsuleComponent();
    const auto Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
    auto* Bot = Cast<AERBotController>(Player);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERSpawn), false);
    if (Player->GetPawn()) Query.AddIgnoredActor(Player->GetPawn());
    if (Bot && Bot->TrainingStart)
        return GetWorld()->OverlapBlockingTestByChannel(Bot->TrainingStart->GetActorLocation(), FQuat::Identity, ECC_Pawn, Shape, Query) ? nullptr : Bot->TrainingStart.Get();

    TArray<AERTeamPlayerStart*> Anchors, Clear;
    for (TActorIterator<AERTeamPlayerStart> Start(GetWorld()); Start; ++Start)
    {
        if (Start->Team != State->Team) continue;
        bool PrivateBotStart = false;
        for (const auto& Other : TestBots) if (IsValid(Other.Get()) && Other->TrainingStart == *Start) PrivateBotStart = true;
        if (PrivateBotStart) continue;
        Anchors.Add(*Start);
        if (!GetWorld()->OverlapBlockingTestByChannel(Start->GetActorLocation(), FQuat::Identity, ECC_Pawn, Shape, Query)) Clear.Add(*Start);
    }
    if (Bot && bConfiguredBots)
    {
        Bot->TrainingStart = FindAdditionalStart(Player, Anchors);
        if (Bot->TrainingStart) return Bot->TrainingStart;
    }
    // Every placement filter already ran. Scoring only orders starts that are legal anyway.
    // The fixed Depot keeps random starts: it is the reference for baseline regressions.
    if (!Clear.IsEmpty()) return PreparedSpawnMap.IsValid() && ERSpawnRisk::Enabled(GetWorld()) ? ChooseScoredStart(State->Team, Clear) : Clear[FMath::RandHelper(Clear.Num())];
    return FindAdditionalStart(Player, Anchors);
}
