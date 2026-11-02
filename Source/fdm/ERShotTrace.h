#pragma once
#include "CoreMinimal.h"
class AERCharacter;
namespace ERShotTrace
{
// Server estimate only: RTT/2, capped to 200ms.
double BulletTime(const AERCharacter* Shooter);
// Current cover always wins. Negative time keeps exact, uncompensated melee semantics.
bool Pellet(UWorld* World, FHitResult& Hit, const FVector& Start, const FVector& End, const FCollisionQueryParams& Query, double ShotTime = -1);
}
