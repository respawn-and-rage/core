#pragma once
#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
class AERCharacter;
class UERPostureMesh;
namespace ERShotTrace
{
// One synchronous cartridge reuses targets/history, but rechecks current cover
// and alive state for every pellet and penetration.
struct FContext
{
    struct FTarget
    {
        AERCharacter* Character=nullptr;
        UERPostureMesh* Posture=nullptr;
        TArray<FTransform> Pose;
        bool Historical=false;
    };
    UWorld* World=nullptr;
    double ShotTime=-1;
    FCollisionQueryParams WorldQuery;
    TArray<FTarget,TInlineAllocator<24>> Targets;
    FContext(UWorld* InWorld,const FCollisionQueryParams& Query,double Time);
};
double RewindSeconds(double PingMilliseconds,double ValidatedViewAge);
// RTT/2 plus only the age of the last server-accepted view, total capped at 200ms.
double BulletTime(const AERCharacter* Shooter);
// Current cover always wins. Negative time keeps exact, uncompensated melee semantics.
bool Pellet(UWorld* World,FHitResult& Hit,const FVector& Start,const FVector& End,const FCollisionQueryParams& Query,double ShotTime=-1,FContext* Context=nullptr);
}
