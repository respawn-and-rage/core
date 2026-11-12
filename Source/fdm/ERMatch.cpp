#include "ERMatch.h"
#include "ERServerSettingsProbe.h"
#include "ERMovement.h"
#include "ERWorldPoseProbe.h"
#include "ERHitFidelityProbe.h"
#include "ERLoadoutProbe.h"
#include "ERFieldClothingProbe.h"
#include "ERClaymore.h"
#include "ERLoadout.h"
#include "EROnline.h"
#include "ERLobbyProbe.h"
#include "ERCapacityProbe.h"
#include "ERMapCatalog.h"
#include "EROutdoorBackdrop.h"
#include "ERRecon.h"
#include "ERReconProbe.h"
#include "ERCharacter.h"
#include "ERCorpse.h"
#include "ERSkull.h"
#include "ERTeamGallery.h"
#include "ERSmokeCloud.h"
#include "ERPlayerController.h"
#include "ERWeapon.h"
#include "ERNetworkProbe.h"
#include "ERNetworkReadinessProbe.h"
#include "ERBotController.h"
#include "ERBotProbe.h"
#include "ERSessionProbe.h"
#include "ERSpawnRisk.h"
#include "ERBotMotionProbe.h"
#include "ERMovementProbe.h"
#include "ERTraversalProbe.h"
#include "ERAccuracyProbe.h"
#include "ERRecoilProbe.h"
#include "ERAimPunchProbe.h"
#include "ERAudioProbe.h"
#include "ERArsenalProbe.h"
#include "ERGrenadeProbe.h"
#include "ERReferenceWeaponProbe.h"
#include "ERCellLabProbe.h"
#include "ERProceduralMap.h"
#include "ERIndustrialMapProbe.h"
#include "ERProceduralProbe.h"
#include "ERMapTemplateProbe.h"
#include "ERDustTemplateProbe.h"
#include "ERFireTrainingProbe.h"
#include "ERRoomsProbe.h"
#include "ERVillageProbe.h"
#include "ERBlockStressProbe.h"
#include "ERBlockCollapseProbe.h"
#include "ERBlockNetworkProbe.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "HAL/PlatformTime.h"
#include "ERBotLoadoutProbe.h"

void AERPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AERPlayerState, bLobbyReady); DOREPLIFETIME(AERPlayerState, MapVote);
    DOREPLIFETIME(AERPlayerState, MatchModeVote); DOREPLIFETIME(AERPlayerState, Skulls);
    DOREPLIFETIME(AERPlayerState, SpareMagazines); DOREPLIFETIME(AERPlayerState, HECount);
    DOREPLIFETIME(AERPlayerState, SmokeCount); DOREPLIFETIME(AERPlayerState, StunCount);
    DOREPLIFETIME(AERPlayerState, PendingTeam); DOREPLIFETIME(AERPlayerState, Team); DOREPLIFETIME(AERPlayerState, Kills);
    DOREPLIFETIME(AERPlayerState, Deaths); DOREPLIFETIME(AERPlayerState, TeamKills);
    DOREPLIFETIME(AERPlayerState, ActiveClaymores);
    DOREPLIFETIME_CONDITION(AERPlayerState, KillStreak, COND_OwnerOnly);
    DOREPLIFETIME(AERPlayerState, LoadoutPreset); DOREPLIFETIME(AERPlayerState, LoadoutVariant);
    DOREPLIFETIME(AERPlayerState, PrimaryWeapon);
    DOREPLIFETIME(AERPlayerState, AlphaSkin); DOREPLIFETIME(AERPlayerState, OmegaSkin); DOREPLIFETIME(AERPlayerState, ActiveSkin);
}
void AERGameState::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority() && UGameplayStatics::GetCurrentLevelName(this)==TEXT("L_Depot"))
    {
        DepotSkySeed=EROutdoorBackdrop::RollLightingSeed();
        OnRep_DepotSkySeed();
    }
}

void AERGameState::OnRep_DepotSkySeed()
{
    if(DepotSkySeed!=0)EROutdoorBackdrop::Apply(*this,DepotSkySeed,FLinearColor(.20f,.19f,.16f),nullptr,DepotSkySeed);
}

void AERGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AERGameState, ServerRules);
    DOREPLIFETIME(AERGameState, DepotSkySeed);
    DOREPLIFETIME(AERGameState, LoadoutCatalog);
    DOREPLIFETIME(AERGameState, ServerMaxPlayers);
    DOREPLIFETIME(AERGameState, bOnline);
    DOREPLIFETIME(AERGameState, bLobby); DOREPLIFETIME(AERGameState, bLobbyStarting);
    DOREPLIFETIME(AERGameState, LobbyAlphaBots); DOREPLIFETIME(AERGameState, LobbyOmegaBots);
    DOREPLIFETIME(AERGameState, MatchMode);
    DOREPLIFETIME(AERGameState, AlphaScore); DOREPLIFETIME(AERGameState, OmegaScore);
    DOREPLIFETIME(AERGameState, MatchEndServerTime); DOREPLIFETIME(AERGameState, RestartServerTime);
    DOREPLIFETIME(AERGameState, Winner); DOREPLIFETIME(AERGameState, CountdownEndServerTime);
}
bool AERGameState::IsPreMatchCountdown() const
{
    return !bLobby && CountdownEndServerTime > 0 && GetMatchState() == MatchState::WaitingToStart;
}
void AERGameState::MulticastKillFeed_Implementation(const FERKillFeedEntry& Entry)
{
    if (GetNetMode() == NM_DedicatedServer) return;
    FERKillFeedEntry& Added = KillFeed.Add_GetRef(Entry);
    Added.ReceivedAt = GetWorld()->GetTimeSeconds();
    while (KillFeed.Num() > 5) KillFeed.RemoveAt(0);
}
void AERGameState::OnRep_ServerRules()
{
    for(TActorIterator<AERCharacter> It(GetWorld());It;++It)
        if(auto* M=Cast<UERMovement>(It->GetCharacterMovement()))M->ApplyServerSettings();
    for(TActorIterator<AERWeapon> It(GetWorld());It;++It)It->RefreshTuning();
    // Refresh the visible lobby summary if it was opened before replication.
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* PC=Cast<AERPlayerController>(It->Get());PC && PC->IsLocalController() && PC->bMenuOpen)PC->ShowMenu(true);
}
AERGameMode::AERGameMode()
{
    DefaultPawnClass = AERCharacter::StaticClass();
    PlayerControllerClass = AERPlayerController::StaticClass();
    PlayerStateClass = AERPlayerState::StaticClass();
    GameStateClass = AERGameState::StaticClass();
    static ConstructorHelpers::FClassFinder<AERHUD> MatchHUD(TEXT("/Game/EclipseRecoil/Art/HUDKillIcons/BP_ERMatchHUD"));
    HUDClass = MatchHUD.Succeeded() ? MatchHUD.Class.Get() : AERHUD::StaticClass();
    bDelayedStart = true;
    bUseSeamlessTravel = false;
}
void AERGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    BotWeaponRandom.Initialize(int32(FPlatformTime::Cycles()));
    const auto& Rules=ERServer::Rules(GetWorld());
    ScoreLimit=FMath::RoundToInt(Rules.Get("Match.ScoreLimit"));
    MatchDuration=Rules.Get("Match.Duration");
    bLobby = MapName.Contains(TEXT("L_MainMenu")) && UGameplayStatics::HasOption(Options, TEXT("ERLobby"));
    bUseSeamlessTravel = bLobby;
    if (!bLobby && UGameplayStatics::GetIntOption(Options, TEXT("ERMatchMode"), 0) == int32(EERMatchMode::SkullHunt))
        MatchMode = EERMatchMode::SkullHunt;
    MaxHumanPlayers = ERServer::ValidMaxPlayers(UGameplayStatics::GetIntOption(Options, TEXT("ERMaxPlayers"), ERServer::DefaultMaxPlayers));
    // Set before PreLogin, including initial load and seamless travel.
    if (GameSession) GameSession->MaxPlayers = MaxHumanPlayers;
    // -EROnlineServer: advertise this dedicated server through EOS (idempotent across travel).
    if (GetNetMode() == NM_DedicatedServer) GetGameInstance()->GetSubsystem<UEROnline>()->StartDedicated(MaxHumanPlayers, MapName);
    const uint32 LightingSeed = EROutdoorBackdrop::IsOutdoorMap(*GetWorld()) ? EROutdoorBackdrop::RollLightingSeed() : 0;
    for (TActorIterator<AERProceduralMap> It(GetWorld()); It; ++It)
        {
            It->LayoutConfig.OutdoorLightingSeed=LightingSeed;
            It->LayoutConfig.ExpectedTeamSize=FMath::RoundToInt(Rules.Get("Map.ExpectedTeamSize"));
            It->LayoutConfig.Seed = UGameplayStatics::GetIntOption(Options, TEXT("MapSeed"), Rules.Get("Map.FixedSeed")>0?int32(Rules.Get("Map.Seed")):It->LayoutConfig.Seed);
            FParse::Value(FCommandLine::Get(), TEXT("ERMapSeed="), It->LayoutConfig.Seed);
            It->LayoutConfig.CrossingOverride=0;
            It->LayoutConfig.bEnablePCGExMacroGraph=false;
            It->bSelectMapCandidates=false;
#if !UE_BUILD_SHIPPING
            It->bSelectMapCandidates=UGameplayStatics::GetIntOption(Options,TEXT("MapSelection"),0)!=0 || FParse::Param(FCommandLine::Get(),TEXT("ERMapSelection"));
            // The authority decides; clients follow the descriptor, never a local flag.
            It->LayoutConfig.bEnablePCGExMacroGraph=UGameplayStatics::GetIntOption(Options,TEXT("RoomsMacroGraph"),0)!=0 || FParse::Param(FCommandLine::Get(),TEXT("ERRoomsMacroGraph"));
            if(FParse::Param(FCommandLine::Get(),TEXT("ERIndustrialMapProbe")))
                It->LayoutConfig.CrossingOverride=UGameplayStatics::GetIntOption(Options,TEXT("MapCrossing"),0);
#endif
            It->LayoutConfig.Floors=UGameplayStatics::GetIntOption(Options,TEXT("MapFloors"),It->LayoutConfig.Floors);
            It->LayoutConfig.RoomCount=UGameplayStatics::GetIntOption(Options,TEXT("MapRooms"),It->LayoutConfig.RoomCount);
            It->LayoutConfig.AdditionalMaterials=UGameplayStatics::GetIntOption(Options,TEXT("MapMaterials"),It->LayoutConfig.AdditionalMaterials)!=0;
            It->LayoutConfig.WindowGlass=UGameplayStatics::GetIntOption(Options,TEXT("MapGlass"),It->LayoutConfig.WindowGlass)!=0;
        }
    bStationaryBots = UGameplayStatics::ParseOption(Options, TEXT("ERBotMode")).Equals(TEXT("Target"), ESearchCase::IgnoreCase);
    DesiredBots = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("ERBots"), 0), 0, bStationaryBots ? 2 * ERSkins::Count : 3);
    bMovementBots = UGameplayStatics::ParseOption(Options, TEXT("ERBotMode")).Equals(TEXT("Movement"), ESearchCase::IgnoreCase);
    bConfiguredBots = UGameplayStatics::HasOption(Options, TEXT("ERAlphaBots")) || UGameplayStatics::HasOption(Options, TEXT("EROmegaBots"));
    AlphaBotCount = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("ERAlphaBots"), 0), 0, 10);
    OmegaBotCount = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("EROmegaBots"), 0), 0, 10);
    if (bConfiguredBots) DesiredBots = AlphaBotCount + OmegaBotCount;
}
void AERGameMode::RemoveBot()
{
    if (TestBots.IsEmpty()) return;
    AERBotController* Bot = TestBots.Pop();
    if (IsValid(Bot))
    {
        UE_LOG(LogTemp,Display,TEXT("ER_BOT_REMOVED name=%s"),*Bot->GetPlayerState<AERPlayerState>()->GetPlayerName());
        GetWorld()->GetSubsystem<UERGameplayMessages>()->Forget(Bot->GetPlayerState<AERPlayerState>());
        if (APawn* Pawn = Bot->GetPawn()) { Bot->UnPossess(); Pawn->Destroy(); }
        if (Bot->TrainingStart) Bot->TrainingStart->Destroy();
        Bot->Destroy();
    }
    NumBots = TestBots.Num();
}
void AERGameMode::PrunePendingSlots()
{
    const double Now = GetWorld()->GetTimeSeconds();
    PendingHumanSlots.RemoveAll([Now](double ExpiresAt) { return Now >= ExpiresAt; });
}
void AERGameMode::FillBots()
{
    if (UGameplayStatics::GetCurrentLevelName(GetWorld()).Contains(TEXT("L_MainMenu")) || GetNumPlayers() == 0) return;
    PrunePendingSlots();
    const int32 Count = (bStationaryBots || bConfiguredBots) ? DesiredBots : FMath::Min(DesiredBots, FMath::Max(0, MaxHumanPlayers-GetNumPlayers()-PendingHumanSlots.Num()));
    while (TestBots.Num() > Count) RemoveBot();
    while (TestBots.Num() < Count)
    {
        auto* Bot = GetWorld()->SpawnActor<AERBotController>();
        auto* State = Bot ? Bot->GetPlayerState<AERPlayerState>() : nullptr;
        if (!State) { if (Bot) Bot->Destroy(); break; }
        Bot->bTrainingTarget = bStationaryBots;
        Bot->bMovementOnly = bMovementBots;
        Bot->MovementPhaseOffset=(TestBots.Num()%6)*.4f;
        State->SetIsABot(true);
        State->AlphaSkin = State->OmegaSkin = (TestBots.Num() + 1) % ERSkins::Count;
        State->SetPlayerName(FString::Printf(TEXT("%s %d"), bStationaryBots ? TEXT("Target") : TEXT("Bot"), TestBots.Num()+1));
        if (bConfiguredBots) State->Team = TestBots.Num() < AlphaBotCount ? EERTeam::Alpha : EERTeam::Omega;
        else if (bStationaryBots && DesiredBots >= 2)
        {
            int32 AlphaTargets = 0, OmegaTargets = 0;
            for (const auto& Target : TestBots) if (auto* PS = Target->GetPlayerState<AERPlayerState>())
            {
                AlphaTargets += PS->Team == EERTeam::Alpha;
                OmegaTargets += PS->Team == EERTeam::Omega;
            }
            State->Team = OmegaTargets <= AlphaTargets ? EERTeam::Omega : EERTeam::Alpha;
        }
        else AssignTeam(State);
        if (!bConfiguredBots && bStationaryBots && DesiredBots == 2 * ERSkins::Count)
        {
            const int32 Skin = TestBots.Num() / 2;
            State->AlphaSkin = State->OmegaSkin = Skin;
            State->SetPlayerName(FString::Printf(TEXT("%s %d"), State->Team == EERTeam::Alpha ? TEXT("Alpha") : TEXT("Omega"), Skin + 1));
            // Keep each variant's place on respawn. The two clear spawn strips
            // provide room for the full skin catalog without modifying the depot map.
            const FVector Position(State->Team == EERTeam::Alpha ? -2300.f : 2300.f, (Skin - (ERSkins::Count - 1) * .5f) * 180.f, 100.f);
            const FRotator Facing(0, State->Team == EERTeam::Alpha ? 0.f : 180.f, 0);
            Bot->TrainingStart = GetWorld()->SpawnActor<AERTeamPlayerStart>(Position, Facing);
            if (Bot->TrainingStart) Bot->TrainingStart->Team = State->Team;
        }
        TestBots.Add(Bot); NumBots = TestBots.Num();
        RestartPlayer(Bot);
        UE_LOG(LogTemp,Display,TEXT("ER_BOT_JOINED name=%s team=%s mode=%s skin=%d pawn=%d"),*State->GetPlayerName(), State->Team==EERTeam::Alpha ? TEXT("Alpha") : TEXT("Omega"),bStationaryBots ? TEXT("Target") : TEXT("Patrol"), State->ActiveSkin, Bot->GetPawn() != nullptr);
    }
}
void AERGameMode::BeginPlay()
{
    if(auto* State=GetGameState<AERGameState>())
    { State->MatchMode = MatchMode; State->ServerRules=ERServer::Rules(GetWorld());State->ServerRules.Normalize();State->ServerRules.Ready=true; }
    Super::BeginPlay();
    if(auto* GI=GetGameInstance<UERGameInstance>())
    {
        const FString Level=UGameplayStatics::GetCurrentLevelName(this,true);
        if(Level==TEXT("L_MainMenu"))GI->RoundMapHistory.Reset();
        else if(GetNetMode()==NM_ListenServer || GetNetMode()==NM_DedicatedServer)
            for(const auto& Entry:ERMaps::All())if(FPackageName::GetShortName(Entry.Package)==Level)
                ERMaps::RecordRoundMap(FName(Entry.Package),GI->RoundMapHistory,&ERServer::Rules(GetWorld()));
    }
    if (auto* State = GetGameState<AERGameState>())
    { State->bOnline = GetGameInstance()->GetSubsystem<UEROnline>()->IsOnline(); State->ServerMaxPlayers = MaxHumanPlayers; State->bLobby = bLobby; State->LobbyAlphaBots = AlphaBotCount; State->LobbyOmegaBots = OmegaBotCount; }
    if(auto* State=GetGameState<AERGameState>())
    {
        const auto* Catalog=DefaultPawnClass?Cast<AERCharacter>(DefaultPawnClass->GetDefaultObject()):nullptr;
        for(bool Omega:{false,true})for(const FName Preset:ERLoadouts::Presets())for(const FName Variant:ERLoadouts::Variants())
            State->LoadoutCatalog.Add(ERLoadouts::Resolve(Preset,Variant,Omega,Catalog,&State->ServerRules));
    }
    if (!GetWorld()->GetMapName().Contains(TEXT("L_MainMenu")))
        GetWorld()->SpawnActor<AERReconDirector>(ReconClass ? ReconClass.Get() : AERReconDirector::StaticClass());
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("ERServerRulesProbe")) || FParse::Param(FCommandLine::Get(),TEXT("ERServerRulesGallery")))GetWorld()->SpawnActor<AERServerSettingsProbe>();
    if(FParse::Param(FCommandLine::Get(),TEXT("ERNetworkReadinessProbe")))GetWorld()->SpawnActor<AERNetworkReadinessProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERCapacityProbe"))) GetWorld()->SpawnActor<AERCapacityProbe>();
    int32 LobbyProbeClient = 0;
    if (FParse::Param(FCommandLine::Get(), TEXT("ERWindowInputProbe")) || FParse::Param(FCommandLine::Get(), TEXT("EROnlineUnconfiguredProbe")) || FParse::Param(FCommandLine::Get(), TEXT("ERMenuGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERSettingsWrite")) || FParse::Param(FCommandLine::Get(), TEXT("ERSettingsRead")) || FParse::Param(FCommandLine::Get(), TEXT("ERLobbyProbeHost")) || FParse::Param(FCommandLine::Get(), TEXT("ERLobbyProbeTimeout")) || FParse::Value(FCommandLine::Get(), TEXT("ERLobbyProbeClient="), LobbyProbeClient)) GetWorld()->SpawnActor<AERLobbyProbe>();
    if (FParse::Param(FCommandLine::Get(),TEXT("ERLoadoutProbe"))) GetWorld()->SpawnActor<AERLoadoutProbe>();
    if (FParse::Param(FCommandLine::Get(),TEXT("ERFieldClothingProbe"))) GetWorld()->SpawnActor<AERFieldClothingProbe>();
    if (FParse::Param(FCommandLine::Get(),TEXT("ERBotLoadoutProbe"))) GetWorld()->SpawnActor<AERBotLoadoutProbe>();
    if (FParse::Param(FCommandLine::Get(),TEXT("ERReconProbe"))) GetWorld()->SpawnActor<AERReconProbe>();
    if (GetWorld()->GetMapName().Contains(TEXT("L_CentralBuilding")) &&
        (FParse::Param(FCommandLine::Get(),TEXT("ERIndustrialMapProbe")) || FParse::Param(FCommandLine::Get(),TEXT("ERWaterEffectsGallery"))))
        GetWorld()->SpawnActor<AERIndustrialMapProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERProtectionRespawnGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERProtectionGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERPostureGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERTeamGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERCorpseGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERSkinGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERFPSForearmsGallery"))) GetWorld()->SpawnActor<AERTeamGallery>();
#endif
    if (GameSession) GameSession->MaxPlayers = MaxHumanPlayers;
    GetWorldTimerManager().SetTimer(MatchTimer, this, &AERGameMode::CheckMatch, 0.2f, true);
#if !UE_BUILD_SHIPPING
    if (GetWorld()->GetMapName().Contains(TEXT("L_Killhouse")) && FParse::Param(FCommandLine::Get(),TEXT("ERMapTemplateProbe"))) GetWorld()->SpawnActor<AERMapTemplateProbe>();
    if ((GetWorld()->GetMapName().Contains(TEXT("L_DustMirror")) || GetWorld()->GetMapName().Contains(TEXT("L_MiddleEasternAlleys"))) && FParse::Param(FCommandLine::Get(),TEXT("ERDustTemplateProbe"))) GetWorld()->SpawnActor<AERDustTemplateProbe>();
    if (GetWorld()->GetMapName().Contains(TEXT("L_FireTraining")) && FParse::Param(FCommandLine::Get(),TEXT("ERFireTrainingProbe"))) GetWorld()->SpawnActor<AERFireTrainingProbe>();
    if (GetWorld()->GetMapName().Contains(TEXT("L_Rooms")) && FParse::Param(FCommandLine::Get(),TEXT("ERRoomsProbe"))) GetWorld()->SpawnActor<AERRoomsProbe>();
    if (GetWorld()->GetMapName().Contains(TEXT("L_Village")) && FParse::Param(FCommandLine::Get(),TEXT("ERVillageProbe"))) GetWorld()->SpawnActor<AERVillageProbe>();
    if (GetWorld()->GetMapName().Contains(TEXT("L_Town")) && FParse::Param(FCommandLine::Get(),TEXT("ERTownProbe"))) GetWorld()->SpawnActor<AERMapTemplateProbe>();
    if (GetNetMode() == NM_Standalone && GetWorld()->GetMapName().Contains(TEXT("L_DestructionLab")) && FParse::Param(FCommandLine::Get(), TEXT("ERCellLabProbe"))) GetWorld()->SpawnActor<AERCellLabProbe>();
    if (GetNetMode() == NM_Standalone && GetWorld()->GetMapName().Contains(TEXT("L_ProceduralLab")) && FParse::Param(FCommandLine::Get(), TEXT("ERProceduralProbe"))) GetWorld()->SpawnActor<AERProceduralProbe>();
    if (GetNetMode() == NM_Standalone && GetWorld()->GetMapName().Contains(TEXT("L_ProceduralLab")) && FParse::Param(FCommandLine::Get(), TEXT("ERBlockStressProbe"))) GetWorld()->SpawnActor<AERBlockStressProbe>();
    if (GetNetMode()==NM_Standalone && GetWorld()->GetMapName().Contains(TEXT("L_DestructionLab")) && FParse::Param(FCommandLine::Get(),TEXT("ERBlockCollapseProbe"))) GetWorld()->SpawnActor<AERBlockCollapseProbe>();
    if(FParse::Param(FCommandLine::Get(),TEXT("ERWorldPoseProbe")))GetWorld()->SpawnActor<AERWorldPoseProbe>();
    if(FParse::Param(FCommandLine::Get(),TEXT("ERHitFidelityProbe")) || FParse::Param(FCommandLine::Get(),TEXT("ERHitLagProbe")))GetWorld()->SpawnActor<AERHitFidelityProbe>();
    if (FParse::Param(FCommandLine::Get(),TEXT("ERBlockNetworkProbe")) && GetWorld()->GetMapName().Contains(TEXT("L_ProceduralLab")))GetWorld()->SpawnActor<AERBlockNetworkProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERNetworkProbe"))) GetWorld()->SpawnActor<AERNetworkProbe>();
    if(FParse::Param(FCommandLine::Get(),TEXT("ERTraversalProbe")))GetWorld()->SpawnActor<AERTraversalProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERMovementProbe"))) GetWorld()->SpawnActor<AERMovementProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERSessionProbe"))) GetWorld()->SpawnActor<AERSessionProbe>();
    if (FParse::Param(FCommandLine::Get(),TEXT("ERBotMotionProbe"))) GetWorld()->SpawnActor<AERBotMotionProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERBotProbe")) || FParse::Param(FCommandLine::Get(), TEXT("ERTargetSkinsProbe"))) GetWorld()->SpawnActor<AERBotProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERAccuracyProbe"))) GetWorld()->SpawnActor<AERAccuracyProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERAudioProbe"))) GetWorld()->SpawnActor<AERAudioProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERAimPunchProbe"))) GetWorld()->SpawnActor<AERAimPunchProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERRecoilProbe"))) GetWorld()->SpawnActor<AERRecoilProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERReferenceWeaponProbe"))) GetWorld()->SpawnActor<AERReferenceWeaponProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERGrenadeProbe")) || FParse::Param(FCommandLine::Get(), TEXT("ERGrenadeCombatProbe"))) GetWorld()->SpawnActor<AERGrenadeProbe>();
    if (FParse::Param(FCommandLine::Get(), TEXT("ERArsenalProbe")) || FParse::Param(FCommandLine::Get(), TEXT("ERFPSPoseGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERArsenalGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERHitscanGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERGrenadeGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERSmokeGallery")) || FParse::Param(FCommandLine::Get(), TEXT("ERMuzzleGallery")))
        GetWorld()->SpawnActor<AERArsenalProbe>();
#endif
}
bool AERGameMode::HasOpenPlayerSlot()
{
    PrunePendingSlots();
    return int64(GetNumPlayers()) + PendingHumanSlots.Num() < MaxHumanPlayers;
}
void AERGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    PrunePendingSlots();
    const bool bFull = !HasOpenPlayerSlot();
    // PreLogin and spawning the human can happen in different frames. Keep the
    // freed slot reserved so CheckMatch cannot refill it with another bot.
    if (!bFull && !bStationaryBots && !bConfiguredBots)
        while (!TestBots.IsEmpty() && GetNumPlayers()+TestBots.Num()+PendingHumanSlots.Num() >= MaxHumanPlayers) RemoveBot();
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    const auto* OnlineService = GetGameInstance()->GetSubsystem<UEROnline>();
    // An EOS dedicated server never falls back to unauthenticated direct IP.
    if (UEROnline::RequiresOnline() && !OnlineService->IsOnline()) ErrorMessage = TEXT("Server is connecting to online services. Try again shortly.");
    else if (OnlineService->IsOnline() && (!UniqueId.IsValid() || UniqueId.GetType() != FName(TEXT("EOS")))) ErrorMessage = TEXT("Epic sign-in is required for this Internet server.");
    if (bLobbyTravelPending) ErrorMessage = TEXT("Match is starting. Try again shortly.");
    if (bFull) ErrorMessage = FString::Printf(TEXT("Server full: maximum %d players."), MaxHumanPlayers);
    if (ErrorMessage.IsEmpty()) PendingHumanSlots.Add(GetWorld()->GetTimeSeconds()+30);
}
void AERGameMode::PostLogin(APlayerController* Player)
{
    Super::PostLogin(Player);
    GetGameState<AERGameState>()->bOnline = GetGameInstance()->GetSubsystem<UEROnline>()->IsOnline();
    GetGameInstance()->GetSubsystem<UEROnline>()->RegisterPlayer(Player);
}
void AERGameMode::Logout(AController* Player)
{
    if(auto* PS=Player->GetPlayerState<AERPlayerState>())AERClaymore::RemoveFor(GetWorld(),PS);
    if (auto* Character = Cast<AERCharacter>(Player->GetPawn())) if (Character->Weapon) Character->Weapon->Cancel();
    GetWorld()->GetSubsystem<UERGameplayMessages>()->Forget(Player->GetPlayerState<AERPlayerState>());
    GetGameInstance()->GetSubsystem<UEROnline>()->UnregisterPlayer(Player);
    Super::Logout(Player);
    UE_LOG(LogTemp,Display,TEXT("ER_PLAYER_LEFT players=%d"),GetNumPlayers());
}
FString AERGameMode::SanitizeName(const FString& Name)
{
    FString Result;
    for (TCHAR C : Name.Left(24)) if (FChar::IsAlnum(C) || C == TCHAR(' ') || C == TCHAR('_') || C == TCHAR('-')) Result.AppendChar(C);
    Result.TrimStartAndEndInline();
    return Result.IsEmpty() ? TEXT("Recruit") : Result;
}
FString AERGameMode::InitNewPlayer(APlayerController* Player, const FUniqueNetIdRepl& Id, const FString& Options, const FString& Portal)
{
    FString Error = Super::InitNewPlayer(Player, Id, Options, Portal);
    if (Error.IsEmpty())
    {
        auto* State = Player->GetPlayerState<AERPlayerState>();
        if (State)
        {
            State->SpareMagazines = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Magazines"), -1), -1, 20);
            State->HECount = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("HE"), -1), -1, 20);
            State->SmokeCount = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Smoke"), -1), -1, 20);
            State->StunCount = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Stun"), -1), -1, 20);
            State->PrimaryWeapon = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Primary"), GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal")) ? 0 : 4), 0, 9);
            State->AlphaSkin = ERSkins::ValidIndex(UGameplayStatics::GetIntOption(Options, TEXT("AlphaSkin"), 0));
            State->OmegaSkin = ERSkins::ValidIndex(UGameplayStatics::GetIntOption(Options, TEXT("OmegaSkin"), 0));
            const auto Kit=ERLoadouts::Resolve(FName(*UGameplayStatics::ParseOption(Options,TEXT("Preset"))), FName(*UGameplayStatics::ParseOption(Options,TEXT("Variant"))));
            State->LoadoutPreset=Kit.Preset;State->LoadoutVariant=Kit.Variant;
            const int32 RequestedTeam = UGameplayStatics::GetIntOption(Options, TEXT("Team"), -1);
            if (RequestedTeam == 0 || RequestedTeam == 1) State->Team = RequestedTeam == 0 ? EERTeam::Alpha : EERTeam::Omega;
            State->SetPlayerName(SanitizeName(UGameplayStatics::ParseOption(Options, TEXT("Name")))); AssignTeam(State);
        }
    }
    return Error;
}
void AERGameMode::AssignTeam(AERPlayerState* Player)
{
    if (!Player || Player->Team != EERTeam::None) return;
    int32 Alpha = 0, Omega = 0;
    for (APlayerState* Item : GameState->PlayerArray)
    {
        auto* State = Cast<AERPlayerState>(Item);
        if (!State || State == Player || State->IsInactive()) continue;
        Alpha += State->Team == EERTeam::Alpha; Omega += State->Team == EERTeam::Omega;
    }
    if (Alpha == Omega) { Player->Team = NextTieTeam; NextTieTeam = NextTieTeam == EERTeam::Alpha ? EERTeam::Omega : EERTeam::Alpha; }
    else Player->Team = Alpha < Omega ? EERTeam::Alpha : EERTeam::Omega;
}
void AERGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
    if (!Player->IsLocalController() && !PendingHumanSlots.IsEmpty()) PendingHumanSlots.RemoveAt(0);
    AssignTeam(Player->GetPlayerState<AERPlayerState>());
    if (UGameplayStatics::GetCurrentLevelName(GetWorld()).Contains(TEXT("L_MainMenu"))) return;
    const auto* PS = Player->GetPlayerState<AERPlayerState>();
    UE_LOG(LogTemp,Display,TEXT("ER_PLAYER_JOINED players=%d team=%s"),GetNumPlayers(),PS && PS->Team==EERTeam::Alpha ? TEXT("Alpha") : TEXT("Omega"));
    if (!Player->GetPawn() && GetMatchState() != MatchState::WaitingPostMatch) RestartPlayer(Player);
    FillBots();
    if (ReadyToStartMatch()) StartMatch();
}
bool AERGameMode::ReadyToStartMatch_Implementation()
{
    if (GetMatchState() != MatchState::WaitingToStart || GetWorld()->GetMapName().Contains(TEXT("L_MainMenu"))) return false;
    auto* State = GetGameState<AERGameState>();
    bool bWorldReady = GetNumPlayers() > 0;
    for(TActorIterator<AERProceduralMap> Map(GetWorld());Map;++Map)
        if(Map->LayoutConfig.TemplateAsset && !Map->IsMapReady())bWorldReady=false;
    if (!State) return bWorldReady;
    if (!bWorldReady) { State->CountdownEndServerTime = 0; return false; }
    // Players spawn into the loaded world but stay frozen until the countdown ends.
    // The match clock starts afterwards, in HandleMatchHasStarted.
    const double Now = State->GetServerWorldTimeSeconds();
    if (State->CountdownEndServerTime <= 0)
    {
        // Strictly positive: 0 means "not started", and the server clock can read 0 on the first frame.
        State->CountdownEndServerTime = FMath::Max(Now + PreMatchCountdownSeconds(), UE_KINDA_SMALL_NUMBER);
        State->ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("ER_MATCH_COUNTDOWN seconds=%.1f"), State->CountdownEndServerTime - Now);
    }
    return Now >= State->CountdownEndServerTime;
}
float AERGameMode::PreMatchCountdownSeconds() const
{
    float Seconds = ERServer::Get(GetWorld(),"Match.Countdown");
#if !UE_BUILD_SHIPPING
    // Automated probes and galleries time themselves from match start; an explicit
    // -ERMatchCountdown=<s> still overrides this for tests of the countdown itself.
    if (FParse::Value(FCommandLine::Get(), TEXT("ERMatchCountdown="), Seconds)) return FMath::Clamp(Seconds, 0.f, 60.f);
    TArray<FString> Tokens, Switches;
    FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);
    for (const FString& Switch : Switches)
        if (Switch.StartsWith(TEXT("ER")) && (Switch.Contains(TEXT("Probe")) || Switch.Contains(TEXT("Gallery")))) return 0;
#endif
    return Seconds;
}
void AERGameMode::RestartPlayer(AController* Player)
{
    if (bLobby) return;
    if (!Player || Player->IsPendingKillPending() || Player->GetPawn()) return;
    if (auto* PS=Player->GetPlayerState<AERPlayerState>(); PS && PS->PendingTeam!=EERTeam::None)
    {
        AERClaymore::RemoveFor(GetWorld(),PS);
        PS->Team=PS->PendingTeam;PS->PendingTeam=EERTeam::None;PS->KillStreak.Reset();PS->ForceNetUpdate();
    }
    // The engine's generic fallback may use an occupied cached start or world origin.
    // A team match must wait for an actually clear start instead.
    if (AActor* Start = ChoosePlayerStart(Player))
    {
        if (auto* PS = Player->GetPlayerState<AERPlayerState>())
        {
            // The full stationary catalog retains its explicit skin index on respawn.
            const bool bCatalogTarget = PS->IsABot() && bStationaryBots && DesiredBots == 2 * ERSkins::Count;
            if(!GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal")) && !bCatalogTarget)
            {
                if(PS->IsABot()) { PS->LoadoutPreset=ERLoadouts::Presets()[BotWeaponRandom.RandRange(0,4)];PS->LoadoutVariant=ERLoadouts::Variants()[BotWeaponRandom.RandRange(0,2)]; }
                const auto Kit=ERLoadouts::Resolve(PS->LoadoutPreset,PS->LoadoutVariant,PS->Team==EERTeam::Omega,Cast<AERCharacter>(GetDefaultPawnClassForController(Player)->GetDefaultObject()),&ERServer::Rules(GetWorld()));
                PS->LoadoutPreset=Kit.Preset;PS->LoadoutVariant=Kit.Variant;
                PS->AlphaSkin=ERLoadouts::Resolve(Kit.Preset,Kit.Variant,false,Cast<AERCharacter>(GetDefaultPawnClassForController(Player)->GetDefaultObject()),&ERServer::Rules(GetWorld())).Skin;
                PS->OmegaSkin=ERLoadouts::Resolve(Kit.Preset,Kit.Variant,true,Cast<AERCharacter>(GetDefaultPawnClassForController(Player)->GetDefaultObject()),&ERServer::Rules(GetWorld())).Skin;
            }
            PS->ActiveSkin = ERSkins::ValidIndex(PS->Team == EERTeam::Alpha ? PS->AlphaSkin : PS->OmegaSkin); PS->ForceNetUpdate(); }
        TGuardValue<bool> SpawnGuard(bSpawningPresetPawn,!GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal")) && !GetWorld()->URL.HasOption(TEXT("ERFullArsenalTest")));
        RestartPlayerAtPlayerStart(Player, Start);
    }
    if (auto* Character = Cast<AERCharacter>(Player->GetPawn()))
    {
        Character->InitializeLoadout(Player->GetPlayerState<AERPlayerState>()->PrimaryWeapon);
        if (auto* Bot = Cast<AERBotController>(Player))
        { if(Character->bPresetInventory)Bot->SpawnWeaponSlot=0;else SelectBotSpawnWeapon(Bot,Character); }
    }
}
void AERGameMode::SelectBotSpawnWeapon(AERBotController* Bot, AERCharacter* Character)
{
    // ERLegacyArsenal is a pinned regression catalog. Ordinary sessions use the
    // current ten-slot inventory and select afresh on every spawn/respawn.
    if (!Character->UsesReferenceArsenal() || !GetWorld()->URL.HasOption(TEXT("ERFullArsenalTest"))) return;
    TArray<int32> Eligible;
    for (int32 Slot = 0; Slot < Character->Inventory.Num(); ++Slot)
    {
        const auto* Gun = Character->Inventory[Slot].Get();
        if (Gun && Gun->Data && Gun->Data->Payload == EERPayload::Bullet && Gun->Data->MagazineSize > 0) Eligible.Add(Slot);
    }
    if (!ensureMsgf(!Eligible.IsEmpty(), TEXT("Bot loadout has no bullet firearm"))) return;
    BotWeaponBag.RemoveAll([&](int32 Slot) { return !Eligible.Contains(Slot); });
    if (BotWeaponBag.IsEmpty())
    {
        BotWeaponBag = Eligible;
        for (int32 I = BotWeaponBag.Num()-1; I > 0; --I) BotWeaponBag.Swap(I, BotWeaponRandom.RandRange(0,I));
    }
    Bot->SpawnWeaponSlot = BotWeaponBag.Pop();
    Character->ServerSelectWeapon(Bot->SpawnWeaponSlot);
    if (auto* PS = Bot->GetPlayerState<AERPlayerState>()) { PS->PrimaryWeapon = Bot->SpawnWeaponSlot; PS->ForceNetUpdate(); }
    UE_LOG(LogTemp,Display,TEXT("ER_BOT_SPAWN_WEAPON bot=%s slot=%d weapon=%s"),*Bot->GetName(),Bot->SpawnWeaponSlot,*Character->Weapon->Data->DisplayName);
}
UClass* AERGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
    const auto* Bot = Cast<AERBotController>(InController);
    if (Bot && TrainingTargetPawnClass)
    {
        UE_LOG(LogTemp, Display, TEXT("ER_BOT_PAWN_CLASS mode=%s class=%s"), bStationaryBots ? TEXT("Target") : TEXT("Patrol"), *TrainingTargetPawnClass->GetPathName());
        return TrainingTargetPawnClass.Get();
    }
    return Super::GetDefaultPawnClassForController_Implementation(InController);
}
void AERGameMode::HandleMatchHasStarted()
{
    Super::HandleMatchHasStarted();
    auto* State = GetGameState<AERGameState>();
    State->AlphaScore = State->OmegaScore = 0;
    State->Winner = EERTeam::None;
    State->CountdownEndServerTime = 0;
    State->MatchEndServerTime = State->GetServerWorldTimeSeconds() + MatchDuration;
    for (APlayerState* Item : State->PlayerArray)
        if (auto* PS = Cast<AERPlayerState>(Item))
        {
            PS->Kills = PS->Deaths = PS->TeamKills = PS->Skulls = 0;
            PS->KillStreak.Reset();
        }
    GetWorld()->GetSubsystem<UERGameplayMessages>()->MatchStarted();
    UE_LOG(LogTemp, Display, TEXT("ER_MATCH_STARTED players=%d duration=%.1f"), GetNumPlayers(), MatchDuration);
}
void AERGameMode::CheckMatch()
{
    if (bLobby) { CheckLobby(); return; }
    if (UGameplayStatics::GetCurrentLevelName(GetWorld()).Contains(TEXT("L_MainMenu"))) return;
    auto* State = GetGameState<AERGameState>();
    if (!State) return;
    FillBots();
    GetWorld()->GetSubsystem<UERGameplayMessages>()->CheckTime();
    if (GetMatchState() == MatchState::WaitingToStart && ReadyToStartMatch()) StartMatch();
    if (IsMatchInProgress() && (State->AlphaScore >= ScoreLimit || State->OmegaScore >= ScoreLimit || State->GetServerWorldTimeSeconds() >= State->MatchEndServerTime)) FinishRound();
    if (GetMatchState() == MatchState::WaitingPostMatch && State->GetServerWorldTimeSeconds() >= State->RestartServerTime) ResetRound();
    // Retry only initial spawns. A death never automatically requests a respawn.
    if (GetMatchState() == MatchState::WaitingToStart || IsMatchInProgress())
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
            if (auto* PC = Cast<AERPlayerController>(It->Get()); PC && !PC->GetPawn() && !PC->bAwaitingRespawn) RestartPlayer(PC);
    if (GetMatchState() == MatchState::WaitingToStart || IsMatchInProgress())
        for (AERBotController* Bot : TestBots) if (IsValid(Bot) && !Bot->GetPawn() && !Bot->bAwaitingRespawn) RestartPlayer(Bot);
}
void AERGameMode::FinishRound()
{
    AERClaymore::RemoveFor(GetWorld());
    auto* State = GetGameState<AERGameState>();
    State->Winner = State->AlphaScore == State->OmegaScore ? EERTeam::None : (State->AlphaScore > State->OmegaScore ? EERTeam::Alpha : EERTeam::Omega);
    State->RestartServerTime = State->GetServerWorldTimeSeconds() + ERServer::Get(GetWorld(),"Match.Intermission");
    EndMatch();
    GetWorld()->GetSubsystem<UERGameplayMessages>()->MatchFinished();
    for (TActorIterator<AERWeapon> It(GetWorld()); It; ++It) It->Cancel();
    for (TActorIterator<AERProjectile> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<AERSkull> It(GetWorld()); It; ++It) It->Destroy();
}
void AERGameMode::ResetRound(bool AllowMapTravel)
{
    if (bRoundTravelPending) return;
    auto* GI=GetGameInstance<UERGameInstance>();
    if (AllowMapTravel && GI && (GetNetMode() == NM_ListenServer || GetNetMode() == NM_DedicatedServer))
    {
        const FString CurrentLevel = UGameplayStatics::GetCurrentLevelName(this, true);
        FName CurrentPackage;
        for (const auto& Entry : ERMaps::All())
            if (FPackageName::GetShortName(Entry.Package) == CurrentLevel)
                CurrentPackage = FName(Entry.Package);
        const auto Choices = ERMaps::NextRoundMaps(CurrentPackage,GI->RoundMapHistory,&ERServer::Rules(GetWorld()));
        if (!Choices.IsEmpty())
        {
            FURL NextURL = GetWorld()->URL;
            NextURL.Map = Choices[FMath::RandHelper(Choices.Num())].ToString();
            NextURL.RemoveOption(TEXT("MapSeed="));
            NextURL.AddOption(*FString::Printf(TEXT("MapSeed=%d"), (ERServer::Get(GetWorld(),"Map.FixedSeed")>0?int32(ERServer::Get(GetWorld(),"Map.Seed")):FMath::Rand())));
            // Keep connected players and their team/loadout through the map change.
            bUseSeamlessTravel = true;
            UE_LOG(LogTemp, Display, TEXT("ER_ROUND_TRAVEL from=%s to=%s"), *CurrentLevel, *NextURL.Map);
            if (GetWorld()->ServerTravel(NextURL.ToString(), true))
            {
                bRoundTravelPending = true;
                return;
            }
            UE_LOG(LogTemp, Error, TEXT("ER_ROUND_TRAVEL_FAILED; resetting current arena"));
        }
    }
    AERClaymore::RemoveFor(GetWorld());
    for (TActorIterator<AERCorpse> It(GetWorld()); It; ++It) It->Destroy();
    for (TActorIterator<AERSmokeCloud> It(GetWorld()); It; ++It) It->Destroy();
    SetMatchState(MatchState::WaitingToStart);
    for (TActorIterator<AController> It(GetWorld()); It; ++It)
    {
        AController* Controller = *It;
        auto* PC = Cast<AERPlayerController>(Controller);
        auto* Bot = Cast<AERBotController>(Controller);
        if (!PC && !Bot) continue;
        if (APawn* Old = Controller->GetPawn()) { Controller->UnPossess(); Old->Destroy(); }
        if (PC) { PC->bAwaitingRespawn = false; PC->RespawnAllowedAt = 0; }
        if (Bot) Bot->ResetLife();
        if (auto* PS = Controller->GetPlayerState<AERPlayerState>())
        {
            PS->Kills = PS->Deaths = PS->TeamKills = PS->Skulls = 0;
            PS->KillStreak.Reset();
        }
    }
    // Production archetypes start the next round with a fresh deterministic
    // seed. Publish the new epoch before spawning anyone into its collision.
    for(TActorIterator<AERProceduralMap> It(GetWorld());It;++It)
        if(It->LayoutConfig.TemplateAsset && (It->LayoutConfig.TemplateAsset->Archetype==EERMapArchetype::CentralBuilding || It->LayoutConfig.TemplateAsset->Archetype==EERMapArchetype::MiddleEasternAlleys || It->LayoutConfig.TemplateAsset->Archetype==EERMapArchetype::FireTraining || It->LayoutConfig.TemplateAsset->Archetype==EERMapArchetype::Rooms))
        {
            // Stationary targets keep a start across deaths, but that start
            // belongs to the old arena. Choose a fresh clear start this round.
            for(const auto& Bot:TestBots)if(IsValid(Bot.Get()) && Bot->TrainingStart)
            {Bot->TrainingStart->Destroy();Bot->TrainingStart=nullptr;}
            const int32 Previous=It->LayoutConfig.Seed;
            It->LayoutConfig.Seed=ERServer::Get(GetWorld(),"Map.FixedSeed")>0?int32(ERServer::Get(GetWorld(),"Map.Seed")):int32(HashCombine(uint32(Previous),It->GetMapEpoch()+7919u));
            if(!It->Regenerate()){It->LayoutConfig.Seed=Previous;It->Regenerate();}
        }
    for(TActorIterator<AController> It(GetWorld());It;++It)
        if(Cast<AERPlayerController>(*It) || Cast<AERBotController>(*It))RestartPlayer(*It);
    auto* State = GetGameState<AERGameState>();
    State->AlphaScore = State->OmegaScore = 0; State->MatchEndServerTime = 0; State->RestartServerTime = 0; State->Winner = EERTeam::None;
    State->CountdownEndServerTime = 0;
    if (ReadyToStartMatch()) StartMatch();
    for (TActorIterator<AERReconDirector> It(GetWorld()); It; ++It) It->ResetRecon();
}
void AERGameMode::RecordDeath(AERCharacter* Victim, AController* Killer, const FERMessageDamage* Facts)
{
    auto* Scores = GetGameState<AERGameState>();
    const int32 OldAlpha = Scores->AlphaScore, OldOmega = Scores->OmegaScore;
    auto* VictimPS = Victim->GetPlayerState<AERPlayerState>();
    auto* KillerPS = Killer ? Killer->GetPlayerState<AERPlayerState>() : nullptr;
    if (VictimPS)
    {
        // Names and teams travel by value: either player may leave before the row expires.
        FERKillFeedEntry Entry;
        Entry.Victim = VictimPS->GetPlayerName(); Entry.VictimTeam = VictimPS->Team;
        if (KillerPS && KillerPS != VictimPS) { Entry.Killer = KillerPS->GetPlayerName(); Entry.KillerTeam = KillerPS->Team; }
        if (Facts)
        {
            Entry.WeaponKey = int8(FMath::Clamp(Facts->WeaponKey, -1, 10));
            Entry.bHeadshot = Facts->Headshot; Entry.bFall = Facts->Fall;
        }
        Scores->MulticastKillFeed(Entry);
    }
    if (VictimPS)
    {
        ++VictimPS->Deaths;
        VictimPS->KillStreak.Reset();
    }
    if (VictimPS && VictimPS->Team != EERTeam::None)
    {
        if (!SpawnMemory) SpawnMemory = MakeShared<FERSpawnMemory>();
        auto& Deaths = SpawnMemory->Deaths[VictimPS->Team == EERTeam::Omega ? 1 : 0];
        SpawnMemory->Add(Deaths, Victim->GetActorLocation(), GetWorld()->GetTimeSeconds());
    }
    bool bDropSkull = false;
    if (VictimPS && KillerPS && KillerPS != VictimPS)
    {
        const bool bTeamKill = VictimPS->Team == KillerPS->Team;
        if (bTeamKill) ++KillerPS->TeamKills;
        else
        {
            ++KillerPS->Kills;
            if (KillerPS->Team != EERTeam::None && VictimPS->Team != EERTeam::None && !KillerPS->IsABot())
                KillerPS->KillStreak.Add(Facts && Facts->Headshot ? 1 : 0);
        }
        if (MatchMode == EERMatchMode::SkullHunt)
            bDropSkull = !bTeamKill && KillerPS->Team != EERTeam::None && VictimPS->Team != EERTeam::None;
        else
        {
            const int32 Points = FMath::RoundToInt32(ERServer::Get(GetWorld(), bTeamKill ? FName("Match.TeamKillPoints") : FName("Match.KillPoints")));
            if (KillerPS->Team == EERTeam::Alpha) Scores->AlphaScore += Points;
            else if (KillerPS->Team == EERTeam::Omega) Scores->OmegaScore += Points;
        }
    }
    if (auto* PC = Cast<AERPlayerController>(Victim->Controller))
    {
        PC->bAwaitingRespawn = true;
        PC->RespawnAllowedAt = GetWorld()->GetTimeSeconds() + ERServer::Get(GetWorld(),"Health.Respawn");
    }
    if (auto* Bot = Cast<AERBotController>(Victim->Controller))
    {
        Bot->bAwaitingRespawn = true;
        Bot->RespawnAllowedAt = GetWorld()->GetTimeSeconds()+ERServer::Get(GetWorld(),"Health.Respawn");
    }
    GetWorld()->GetSubsystem<UERGameplayMessages>()->ScoresChanged(OldAlpha, OldOmega);
    if (bDropSkull && IsMatchInProgress()) DropSkull(*Victim, VictimPS->Team);
    CheckMatch();
}
void AERGameMode::RequestRespawn(AController* Controller)
{
    auto* PC = Cast<AERPlayerController>(Controller);
    auto* Bot = Cast<AERBotController>(Controller);
    if (!IsMatchInProgress() || (!PC && !Bot)) return;
    const bool Waiting = PC ? PC->bAwaitingRespawn : Bot->bAwaitingRespawn;
    const double AllowedAt = PC ? PC->RespawnAllowedAt : Bot->RespawnAllowedAt;
    if (!Waiting || GetWorld()->GetTimeSeconds() < AllowedAt) return;
    auto* Old = Cast<AERCharacter>(Controller->GetPawn());
    if (Old && !Old->Health->bDead) return;
    if (!ChoosePlayerStart(Controller)) return;
    if (Old) { Controller->UnPossess(); Old->Destroy(); }
    RestartPlayer(Controller);
    if (Controller->GetPawn())
    {
        if (PC) { PC->bAwaitingRespawn = false; PC->RespawnAllowedAt = 0; }
        if (Bot) Bot->ResetLife();
    }
}

void AERGameState::OnRep_ReplicatedWorldTimeSecondsDouble()
{
    Super::OnRep_ReplicatedWorldTimeSecondsDouble();
    AimClockReceiptTime=GetWorld()->GetTimeSeconds();
}
double AERGameState::GetAimPunchServerTimeSeconds() const
{
    if(HasAuthority() || AimClockReceiptTime<0)return GetServerWorldTimeSeconds();
    return ReplicatedWorldTimeSecondsDouble+FMath::Max(0.,GetWorld()->GetTimeSeconds()-AimClockReceiptTime);
}

void AERGameState::OnRep_LoadoutCatalog()
{
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(auto* PC=Cast<AERPlayerController>(It->Get());PC && PC->IsLocalController() && PC->bMenuOpen && PC->MenuPage==6)
        { PC->LoadoutPortraits.Empty();PC->ShowMenu(true); }
}
