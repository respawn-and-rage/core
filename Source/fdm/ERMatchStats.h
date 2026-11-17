#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

class AController;
class AERCharacter;
class AERGameMode;
class AERGameState;
class AERPlayerState;
struct FERMessageDamage;
struct FHitResult;

// Owned by the authoritative game mode. The archive worker receives a completed
// match by value and never accesses the world or any player objects.
struct FERMatchStats
{
    void Begin(const AERGameMode& Mode);
    int32 RememberAttacker(const AERPlayerState* State);
    void Record(const AERCharacter& Victim, const AController* Attacker,
        const FERMessageDamage& Facts, const FHitResult* Hit, float HealthLost, float ArmorLost);
    void Finish(const AERGameState& Scores);
    static void ReadGraph(const FString& Day, const FString& Match, TFunction<void(FString)> Complete);
    static void WaitForWrites();

private:
    struct FParticipant
    {
        int32 PlayerId;
        FString Name;
        uint8 Team;
        bool bBot;
    };

    enum class ECause : uint8 { Other, Bullet, Melee, Projectile, Explosion, Fall, Drowning, OutOfWorld };

    struct FEvent
    {
        FDateTime Time;
        int32 Attacker;
        int32 Victim;
        FName Weapon;
        FName Bone;
        FVector3f Impact;
        float HealthLost;
        float ArmorLost;
        ECause Cause;
        bool bImpact;
        bool bDeath;
        bool bHeadshot;
    };

    int32 Participant(const AERPlayerState* State);
    void Write(const FString& Directory) const;

    FGuid MatchId;
    FDateTime Started;
    FDateTime Finished;
    FString Day;
    FString Map;
    TOptional<int32> Seed;
    FString ModeName;
    int32 AlphaScore = 0;
    int32 OmegaScore = 0;
    uint8 Winner = 0;
    TArray<FParticipant> Participants;
    TMap<FObjectKey, int32> LatestParticipant;
    TArray<FEvent> Events;
};
