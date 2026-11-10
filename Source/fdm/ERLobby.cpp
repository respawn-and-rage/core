#include "ERClaymore.h"
#include "ERMatch.h"
#include "ERPlayerController.h"
#include "ERMapCatalog.h"
#include "ERLobbyRules.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

const TCHAR* ERMatchModes::Label(EERMatchMode Mode)
{
    return Mode == EERMatchMode::SkullHunt ? TEXT("Skull Hunt") : TEXT("Team Deathmatch");
}

void AERPlayerState::CopyProperties(APlayerState* Target)
{
    Super::CopyProperties(Target);
    if (auto* PS = Cast<AERPlayerState>(Target))
    {
        PS->LoadoutPreset=LoadoutPreset; PS->LoadoutVariant=LoadoutVariant;
        PS->Team = Team; PS->PendingTeam = PendingTeam; PS->PrimaryWeapon = PrimaryWeapon;
        PS->AlphaSkin = AlphaSkin; PS->OmegaSkin = OmegaSkin;
        PS->SpareMagazines = SpareMagazines; PS->HECount = HECount;
        PS->SmokeCount = SmokeCount; PS->StunCount = StunCount;
    }
}
void AERGameMode::CheckLobby()
{
    if (!bLobby || bLobbyTravelPending) return;
    auto* State = GetGameState<AERGameState>();
    if (!State) return;
    int32 Humans = 0, Ready = 0;
    TArray<int32> Votes;
    int32 DeathmatchVotes = 0, SkullHuntVotes = 0;
    for (auto Item : State->PlayerArray)
    {
        auto* PS = Cast<AERPlayerState>(Item);
        if (!PS || !PS->CanVoteInLobby()) continue;
        ++Humans; Ready += PS->bLobbyReady; Votes.Add(PS->MapVote);
        DeathmatchVotes += PS->MatchModeVote == int32(EERMatchMode::TeamDeathmatch);
        SkullHuntVotes += PS->MatchModeVote == int32(EERMatchMode::SkullHunt);
    }
    if (!ERLobby::HasQuorum(Humans, Ready,ERServer::Get(GetWorld(),"Match.ReadyPercent"))) return;
    const auto Leaders = ERMaps::LobbyLeaders(Votes,&ERServer::Rules(GetWorld()));
    if (Leaders.IsEmpty()) return;
    const int32 Map = Leaders[FMath::RandHelper(Leaders.Num())];
    const EERMatchMode WinningMode = SkullHuntVotes > DeathmatchVotes ? EERMatchMode::SkullHunt : EERMatchMode::TeamDeathmatch;
    State->MatchMode = WinningMode;
    bLobbyTravelPending = true;
    State->bLobbyStarting = true;
    State->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("ER_LOBBY_START humans=%d ready=%d map=%d mode=%s"), Humans, Ready, Map, ERMatchModes::Label(WinningMode));
    // Give all clients a frame to see the transition status before seamless travel.
    FTimerHandle TravelTimer;
    GetWorldTimerManager().SetTimer(TravelTimer, FTimerDelegate::CreateWeakLambda(this, [this, Map, WinningMode]()
    {
        const auto& Entry = ERMaps::At(Map);
        FString URL = FString(Entry.Package) + FString::Printf(TEXT("?listen?ERAlphaBots=%d?EROmegaBots=%d?ERBotMode=Patrol?ERMaxPlayers=%d?MaxPlayers=%d"), AlphaBotCount, OmegaBotCount, MaxHumanPlayers, MaxHumanPlayers);
        URL += FString::Printf(TEXT("?ERMatchMode=%d"), int32(WinningMode));
        if (GetGameState<AERGameState>()->bOnline) URL += TEXT("?EROnline=1");
        if (Entry.Procedural) URL += FString::Printf(TEXT("?MapSeed=%d"), ERServer::Get(GetWorld(),"Map.FixedSeed")>0?int32(ERServer::Get(GetWorld(),"Map.Seed")):FMath::Rand());
        if (!GetWorld()->ServerTravel(URL, true))
        {
            bLobbyTravelPending = false;
            GetGameState<AERGameState>()->bLobbyStarting = false;
            GetGameState<AERGameState>()->MatchMode = EERMatchMode::TeamDeathmatch;
            for (auto Item : GameState->PlayerArray) if (auto* PS = Cast<AERPlayerState>(Item)) PS->bLobbyReady = false;
            UE_LOG(LogTemp, Error, TEXT("ER_LOBBY_TRAVEL_FAILED"));
        }
    }), 1.f, false);
}
bool AERPlayerController::IsInLobby() const
{
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    return IsMenuLevel() && State && State->bLobby;
}
void AERPlayerController::ServerLobbyTeam_Implementation(int32 Team)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    if (!Mode || !Mode->IsLobby() || !State || State->bLobbyStarting || Team < 0 || Team > 1) return;
    if (auto* PS = GetPlayerState<AERPlayerState>())
    { AERClaymore::RemoveFor(GetWorld(),PS); PS->Team = Team == 0 ? EERTeam::Alpha : EERTeam::Omega; PS->bLobbyReady = false; PS->ForceNetUpdate(); }
}
void AERPlayerController::ServerLobbyVote_Implementation(int32 Map)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    if (!Mode || !Mode->IsLobby() || !State || State->bLobbyStarting || Map < -1 || Map >= ERMaps::All().Num()) return;
    if (Map >= 0 && !ERMaps::Enabled(Map,&ERServer::Rules(GetWorld()))) return;
    if (auto* PS = GetPlayerState<AERPlayerState>()) { PS->MapVote = Map; PS->ForceNetUpdate(); }
}
void AERPlayerController::ServerLobbyMatchModeVote_Implementation(int32 Mode)
{
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    auto* PS = GetPlayerState<AERPlayerState>();
    if (!State || !State->bLobby || State->bLobbyStarting || !PS || !PS->CanVoteInLobby()) return;
    if (Mode != int32(EERMatchMode::TeamDeathmatch) && Mode != int32(EERMatchMode::SkullHunt)) return;
    PS->MatchModeVote = Mode;
    PS->ForceNetUpdate();
}

void AERPlayerController::ServerLobbyReady_Implementation(bool Ready)
{
    auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    if (!Mode || !Mode->IsLobby() || !State || State->bLobbyStarting) return;
    if (auto* PS = GetPlayerState<AERPlayerState>()) { PS->bLobbyReady = Ready; PS->ForceNetUpdate(); }
    Mode->CheckLobby();
}
void AERPlayerController::ServerSetSupplies_Implementation(int32 Magazines, int32 HE, int32 Smoke, int32 Stun)
{
    if(!GetWorld()->URL.HasOption(TEXT("ERLegacyArsenal")))return;
    const auto* State = GetWorld()->GetGameState<AERGameState>();
    if (State && State->bLobbyStarting) return;
    if (Magazines < 0 || Magazines > 20 || HE < 0 || HE > 20 || Smoke < 0 || Smoke > 20 || Stun < 0 || Stun > 20) return;
    if (auto* PS = GetPlayerState<AERPlayerState>())
    {
        PS->SpareMagazines = Magazines; PS->HECount = HE; PS->SmokeCount = Smoke; PS->StunCount = Stun;
        PS->bLobbyReady = false; PS->ForceNetUpdate();
    }
}
void AERPlayerController::PostSeamlessTravel()
{
    Super::PostSeamlessTravel();
    MenuPage = 0;
    if (IsLocalController()) ShowMenu(IsMenuLevel());
}
