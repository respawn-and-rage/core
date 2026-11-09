#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ERGameplayMessages.generated.h"

// Only facts supplied by authoritative damage. Defaults never imply a special hit.
struct FERMessageDamage
{
    bool Headshot = false, Grenade = false, Explosion = false, Melee = false, Fall = false;
    bool LastBullet = false, HasSourceLife = false;
    float DistanceMeters = -1;
    int32 WeaponKey = -1; // ERLoadouts::WeaponAssetName index for the kill feed icon.
    TWeakObjectPtr<class APawn> SourceLife;
};

struct FERGameplayToast
{
    FString Event, Title, Body;
    double Expires = 0;
    int32 Priority = 0;
};

struct FERMessageEvent
{
    FString Title;
    int32 Priority = 0;
    float Duration = 6, Cooldown = 0;
    TArray<FString> Actor, Target;
};

struct FERMessageHistory
{
    int32 Streak = 0, Chain = 0, DeathsWithoutKill = 0;
    uint8 Team = MAX_uint8;
    double ToastUntil = 0;
    int32 ToastPriority = 0;
    double LastKill = -100;
    TWeakObjectPtr<class AERPlayerState> Revenge;
    TMap<TWeakObjectPtr<class AERPlayerState>, int32> PairKills;
    TMap<FString, double> Cooldowns;
    TMap<FString, TArray<int32>> Bags;
    TMap<FString, int32> LastVariants;
};

struct FERMessageLeadState
{
    int32 LastSoleLeader = 0;
    bool Taken = false, Lost = false;
    void Update(int32 OldDifference, int32 NewDifference)
    {
        const int32 Before = FMath::Sign(OldDifference), After = FMath::Sign(NewDifference);
        Taken = After != 0 && After != Before;
        Lost = After != 0 && LastSoleLeader != 0 && LastSoleLeader != After;
        if (After != 0) LastSoleLeader = After;
    }
};

UCLASS()
class FDM_API UERGameplayMessages : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    void Damage(class AERCharacter* Victim, class AController* Killer, const FERMessageDamage& Facts, bool Dead);
    void MatchStarted();
    void MatchFinished();
    void CheckTime();
    void ScoresChanged(int32 OldAlpha, int32 OldOmega);
    void Forget(class AERPlayerState* Player);
    int32 CatalogSize() const { return Events.Num(); }
private:
    friend class FERGameplayMessagesTest;
    TMap<FString, FERMessageEvent> Events;
    TMap<TWeakObjectPtr<class AERPlayerState>, FERMessageHistory> History;
    TSet<int32> TimeWarnings;
    bool bFirstBlood = false;
    FRandomStream MessageRandom;
    FERMessageLeadState Lead;
    using FContext = TMap<FString, FString>;
    void Notify(class AERPlayerState* Player, const TArray<FString>& Candidates, bool Target, const FContext& Context,
        const FString& Confirmation = FString(), bool Required = false);
    FString Select(class AERPlayerState* Player, const FString& Event, bool Target, const FContext& Context);
    void Broadcast(const FString& Event, const FContext& Context = FContext());
};
