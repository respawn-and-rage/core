#include "ERGameplayMessages.h"
#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERPlayerController.h"
#include "ERWeapon.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"

namespace
{
FString Name(const AERPlayerState* Player)
{
    const FString Value = Player ? Player->GetPlayerName() : TEXT("the environment");
    return Value.Len() > 20 ? Value.Left(19) + TEXT("…") : Value;
}
AERPlayerController* Owner(AERPlayerState* Player)
{
    return Player ? Cast<AERPlayerController>(Player->GetOwner()) : nullptr;
}
}

void UERGameplayMessages::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    MessageRandom.Initialize(int32(FPlatformTime::Cycles()));
    FString Source;
    TSharedPtr<FJsonObject> Root;
    const FString Path = FPaths::ProjectConfigDir() / TEXT("GameplayMessages/respawn-and-rage-gameplay-messages.en-US.json");
    if (!FFileHelper::LoadFileToString(Source, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source), Root) || !Root)
    {
        UE_LOG(LogTemp, Error, TEXT("ER_MESSAGES_CATALOG_FAILED %s"), *Path);
        return; // Required kill/death labels still work without the catalog.
    }
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Root->TryGetArrayField(TEXT("events"), Items)) return;
    for (const auto& Item : *Items)
    {
        const auto& Json = Item->AsObject();
        if (!Json) continue;
        FERMessageEvent Event;
        const auto& Display = Json->GetObjectField(TEXT("display"));
        Event.Title = Display->GetStringField(TEXT("title"));
        Event.Priority = Display->GetIntegerField(TEXT("priority"));
        // Give players twice the reading time without altering the supplied catalog.
        Event.Duration = 2.f * FMath::Clamp(float(Display->GetNumberField(TEXT("duration_seconds"))), 1.f, 5.f);
        Event.Cooldown = FMath::Max(0.f, float(Display->GetNumberField(TEXT("cooldown_seconds"))));
        const auto& Messages = Json->GetObjectField(TEXT("messages"));
        for (const auto& Variant : Messages->GetArrayField(TEXT("actor"))) Event.Actor.Add(Variant->AsObject()->GetStringField(TEXT("text")));
        for (const auto& Variant : Messages->GetArrayField(TEXT("target"))) Event.Target.Add(Variant->AsObject()->GetStringField(TEXT("text")));
        Events.Add(Json->GetStringField(TEXT("id")), MoveTemp(Event));
    }
    UE_LOG(LogTemp, Display, TEXT("ER_MESSAGES_CATALOG events=%d"), Events.Num());
}

FString UERGameplayMessages::Select(AERPlayerState* Player, const FString& Id, bool Target, const FContext& Context)
{
    const auto* Event = Events.Find(Id);
    if (!Event) return FString();
    const auto& Variants = Target ? Event->Target : Event->Actor;
    const FString Key = Id + (Target ? TEXT(".target") : TEXT(".actor"));
    auto& State = History.FindOrAdd(Player);
    auto& Bag = State.Bags.FindOrAdd(Key);
    if (Bag.IsEmpty())
    {
        for (int32 I = 0; I < Variants.Num(); ++I) Bag.Add(I);
        for (int32 I = Bag.Num() - 1; I > 0; --I) Bag.Swap(I, MessageRandom.RandRange(0, I));
        if (Bag.Num() > 1 && State.LastVariants.Contains(Key) && Bag.Last() == State.LastVariants[Key]) Bag.Swap(0, Bag.Num()-1);
    }
    for (int32 I = Bag.Num()-1; I >= 0; --I)
    {
        FString Text = Variants[Bag[I]];
        for (const auto& Pair : Context) Text.ReplaceInline(*(TEXT("{") + Pair.Key + TEXT("}")), *Pair.Value, ESearchCase::CaseSensitive);
        if (Text.Contains(TEXT("{")) || Text.Contains(TEXT("}"))) continue;
        State.LastVariants.Add(Key, Bag[I]); Bag.RemoveAt(I);
        return Text;
    }
    return FString();
}

void UERGameplayMessages::Notify(AERPlayerState* Player, const TArray<FString>& Candidates, bool Target,
    const FContext& Context, const FString& Confirmation, bool Required)
{
    auto* PC = Owner(Player);
    if (!PC) return;
    // Pick the best recognition first. A cooldown never falls back to generic banter.
    FString Best;
    bool BestTarget = Target;
    for (const FString& Candidate : Candidates)
    {
        const bool Audience = Target || Candidate.StartsWith(TEXT("target:"));
        const FString Id = Candidate.StartsWith(TEXT("target:")) ? Candidate.Mid(7) : Candidate;
        if (const auto* Event = Events.Find(Id); Event && !(Audience ? Event->Target : Event->Actor).IsEmpty())
            if (Best.IsEmpty() || Event->Priority > Events[Best].Priority || (Event->Priority == Events[Best].Priority && Id < Best))
            { Best = Id; BestTarget = Audience; }
    }
    FString Title, Body;
    int32 Priority = 0;
    float Duration = 6;
    if (!Best.IsEmpty())
    {
        const auto& Event = Events[Best];
        const FString Key = Best + (BestTarget ? TEXT(".target") : TEXT(".actor"));
        auto& State = History.FindOrAdd(Player);
        const double Now = GetWorld()->GetTimeSeconds();
        if (Required || ((!State.Cooldowns.Contains(Key) || Now >= State.Cooldowns[Key]) &&
            (State.ToastUntil <= Now || Event.Priority >= State.ToastPriority)))
        {
            Title = Event.Title; Body = Select(Player, Best, BestTarget, Context);
            Priority = Event.Priority; Duration = Event.Duration;
            State.Cooldowns.Add(Key, Now + Event.Cooldown);
            if (!Required) { State.ToastUntil = Now + Duration; State.ToastPriority = Priority; }
        }
    }
    if (!Title.IsEmpty() || !Confirmation.IsEmpty())
        PC->ClientGameplayMessage(Best, Confirmation.IsEmpty() ? Title : Confirmation, Body, Priority, Duration, !Confirmation.IsEmpty());
}

void UERGameplayMessages::Damage(AERCharacter* Victim, AController* Killer, const FERMessageDamage& Facts, bool Dead)
{
    if (!Victim || !Victim->HasAuthority()) return;
    const auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    if (!Mode || !Mode->IsMatchInProgress()) return;
    auto* V = Victim->GetPlayerState<AERPlayerState>();
    auto* K = Killer ? Killer->GetPlayerState<AERPlayerState>() : nullptr;
    if (!V) return;
    for (auto* Player : {V, K}) if (Player)
    {
        if (const auto* State = History.Find(Player); State && State->Team != uint8(Player->Team)) Forget(Player);
        History.FindOrAdd(Player).Team = uint8(Player->Team);
    }
    const bool Other = K && K != V;
    const bool Friendly = Other && V->Team != EERTeam::None && V->Team == K->Team;
    const FContext Context{{TEXT("actor"), Name(K)}, {TEXT("target"), Name(V)},
        {TEXT("distance_m"), FString::FromInt(FMath::RoundToInt(FMath::Max(0.f, Facts.DistanceMeters)))}};
    if (!Dead)
    {
        if (Friendly) { Notify(K, {TEXT("friendly_hit")}, false, Context); Notify(V, {TEXT("friendly_hit")}, true, Context); }
        return;
    }
    // Capture victim state before resetting it, and before weapon cancellation.
    const int32 VictimStreak = History.FindOrAdd(V).Streak;
    const bool Reloading = Victim->Weapon && Victim->Weapon->bReloading;
    const bool Cooking = Victim->Weapon && Victim->Weapon->bCookingGrenade;
    auto& VictimState = History.FindOrAdd(V);
    VictimState.Streak = VictimState.Chain = 0;
    VictimState.LastKill = -100;
    ++VictimState.DeathsWithoutKill;
    VictimState.Revenge = Other && !Friendly ? K : nullptr;
    if (Other)
    {
        Victim->DeathFocus = Cast<AERCharacter>(Killer->GetPawn());
        Victim->DeathFocusPosition = Killer->GetPawn() ? Killer->GetPawn()->GetActorLocation() : Victim->GetActorLocation();
        Victim->bHasDeathFocus = Killer->GetPawn() != nullptr;
        Victim->ForceNetUpdate();
        Notify(K, {Friendly ? TEXT("teamkill") : TEXT("kill")}, false, Context,
            (Friendly ? TEXT("TEAMKILL · ") : TEXT("KILLED · ")) + Name(V), true);
        Notify(V, {Friendly ? TEXT("teamkill") : TEXT("kill")}, true, Context,
            (Friendly ? TEXT("TEAMKILLED BY · ") : TEXT("KILLED BY · ")) + Name(K), true);
    }
    else
    {
        Notify(V, {}, false, Context, K == V ? TEXT("SELF-INFLICTED DEATH") : TEXT("YOU DIED"), true);
    }
    TArray<FString> ActorEvents, VictimEvents;
    if (Reloading) VictimEvents.Add(TEXT("death_while_reloading"));
    if (Cooking) VictimEvents.Add(TEXT("death_with_grenade_ready"));
    if (Facts.Fall) VictimEvents.Add(TEXT("fall_death"));
    if (Facts.Drowning) VictimEvents.Add(TEXT("drowning_death"));
    if (K == V && Facts.Grenade) VictimEvents.Add(TEXT("self_grenade_death"));
    if (K == V && Facts.Explosion && !Facts.Grenade) VictimEvents.Add(TEXT("self_explosion_death"));
    if (Friendly)
    {
        if (K->TeamKills + 1 >= 2) ActorEvents.Add(TEXT("teamkill_repeat")); // Scoring follows this callback.
        if (Facts.Grenade) ActorEvents.Add(TEXT("teamkill_grenade"));
        if (Facts.Melee) ActorEvents.Add(TEXT("teamkill_melee"));
    }
    else if (Other)
    {
        auto& State = History.FindOrAdd(K);
        if (!bFirstBlood) { bFirstBlood = true; ActorEvents.Add(TEXT("first_blood")); }
        if (Facts.Headshot) ActorEvents.Add(TEXT("headshot"));
        if (Facts.DistanceMeters >= 40) ActorEvents.Add(TEXT("longshot"));
        if (Facts.Melee) ActorEvents.Add(TEXT("melee_kill"));
        if (Facts.Grenade) ActorEvents.Add(TEXT("grenade_kill"));
        if (Facts.LastBullet) ActorEvents.Add(TEXT("last_bullet_kill"));
        const auto* Pawn = Cast<AERCharacter>(Killer->GetPawn());
        const bool ActiveLife = Pawn && !Pawn->Health->bDead && (!Facts.HasSourceLife || Facts.SourceLife == Pawn);
        if (ActiveLife && Pawn->Health->Health <= 10) ActorEvents.Add(TEXT("low_health_kill"));
        FContext KillContext = Context;
        if (Pawn) KillContext.Add(TEXT("health"), FString::FromInt(FMath::CeilToInt(Pawn->Health->Health)));
        KillContext.Add(TEXT("streak_count"), FString::FromInt(VictimStreak));
        if (VictimStreak >= 5) ActorEvents.Add(TEXT("shutdown"));
        if (ActiveLife)
        {
            if (State.Revenge == V) { State.Revenge.Reset(); ActorEvents.Add(TEXT("revenge")); }
            if (State.DeathsWithoutKill >= 3) ActorEvents.Add(TEXT("comeback_kill"));
            State.DeathsWithoutKill = 0;
            ++State.Streak;
            if (State.Streak <= 25 && State.Streak % 5 == 0) ActorEvents.Add(FString::Printf(TEXT("killstreak_%d"), State.Streak));
            const double Now = GetWorld()->GetTimeSeconds();
            // Multi-victim explosions need aggregation; defer their rapid-chain recognition.
            State.Chain = Facts.Explosion ? 0 : Now - State.LastKill <= 4 ? State.Chain + 1 : 1;
            State.LastKill = Facts.Explosion ? -100 : Now;
            const TCHAR* Chains[] = {TEXT(""), TEXT(""), TEXT("double_kill"), TEXT("triple_kill"), TEXT("quad_kill"), TEXT("penta_kill")};
            if (State.Chain >= 2 && !Facts.Explosion) ActorEvents.Add(State.Chain < 6 ? Chains[State.Chain] : TEXT("multikill"));
            KillContext.Add(TEXT("kill_count"), FString::FromInt(State.Chain));
        }
        // Pair state belongs to match identities, independently of life counters.
        const int32 Reverse = History.FindOrAdd(V).PairKills.FindRef(K);
        History.FindOrAdd(V).PairKills.Add(K, 0);
        const int32 Count = ++History.FindOrAdd(K).PairKills.FindOrAdd(V);
        if (Reverse >= 4) ActorEvents.Add(TEXT("domination_broken"));
        if (Count == 4) ActorEvents.Add(TEXT("domination"));
        Notify(K, ActorEvents, false, KillContext);
        // Evaluate victim variants independently; an actor-only recognition cannot silence their death context.
        for (const auto& Id : ActorEvents) if (const auto* Event = Events.Find(Id); Event && !Event->Target.IsEmpty()) VictimEvents.Add(TEXT("target:") + Id);
        Notify(V, VictimEvents, false, KillContext);
        return;
    }
    Notify(K, ActorEvents, false, Context);
    if (Friendly)
    {
        for (const auto& Id : ActorEvents) VictimEvents.Add(TEXT("target:") + Id);
        Notify(V, VictimEvents, false, Context);
    }
    else Notify(V, VictimEvents, false, Context);
}

void UERGameplayMessages::Broadcast(const FString& Event, const FContext& Context)
{
    if (auto* GS = GetWorld()->GetGameState<AERGameState>())
        for (APlayerState* Item : GS->PlayerArray) if (auto* Player = Cast<AERPlayerState>(Item)) Notify(Player, {Event}, false, Context);
}
void UERGameplayMessages::MatchStarted()
{
    History.Empty(); TimeWarnings.Empty(); bFirstBlood = false; Lead = {};
    if (GetWorld()->GetNetMode() != NM_Client) Broadcast(TEXT("match_start"));
}
void UERGameplayMessages::MatchFinished()
{
    auto* GS = GetWorld()->GetGameState<AERGameState>();
    if (!GS) return;
    for (APlayerState* Item : GS->PlayerArray) if (auto* Player = Cast<AERPlayerState>(Item))
        Notify(Player, {GS->Winner == EERTeam::None ? TEXT("draw") : GS->Winner == Player->Team ? TEXT("team_victory") : TEXT("team_defeat")}, false,
            {{TEXT("team"), Player->Team == EERTeam::Alpha ? TEXT("Alpha") : TEXT("Omega")}});
}
void UERGameplayMessages::CheckTime()
{
    const auto* GS = GetWorld()->GetGameState<AERGameState>();
    if (!GS || !GS->IsMatchInProgress()) return;
    const double Remaining = GS->MatchEndServerTime - GS->GetServerWorldTimeSeconds();
    // Short test matches do not retroactively cross thresholds at startup.
    const auto* Mode = GetWorld()->GetAuthGameMode<AERGameMode>();
    for (int32 Seconds : {60, 30}) if (Mode && Mode->MatchDuration > Seconds && Remaining <= Seconds && !TimeWarnings.Contains(Seconds))
    { TimeWarnings.Add(Seconds); Broadcast(TEXT("time_warning"), {{TEXT("seconds"), FString::FromInt(Seconds)}}); }
}
void UERGameplayMessages::ScoresChanged(int32 OldAlpha, int32 OldOmega)
{
    const auto* GS = GetWorld()->GetGameState<AERGameState>();
    if (!GS || !GS->IsMatchInProgress()) return;
    const int32 After = FMath::Sign(GS->AlphaScore - GS->OmegaScore);
    Lead.Update(OldAlpha - OldOmega, GS->AlphaScore - GS->OmegaScore);
    if (!Lead.Taken && !Lead.Lost) return;
    for (APlayerState* Item : GS->PlayerArray) if (auto* Player = Cast<AERPlayerState>(Item))
    {
        const bool Leading = (After > 0) == (Player->Team == EERTeam::Alpha);
        if ((Leading && Lead.Taken) || (!Leading && Lead.Lost)) Notify(Player, {Leading ? TEXT("lead_taken") : TEXT("lead_lost")}, false, {});
    }
}
void UERGameplayMessages::Forget(AERPlayerState* Player)
{
    History.Remove(Player);
    for (auto& Pair : History)
    {
        Pair.Value.PairKills.Remove(Player);
        if (Pair.Value.Revenge == Player) Pair.Value.Revenge.Reset();
    }
}
