#pragma once
#include "CoreMinimal.h"
#include "ERBlockWorld.h"
#include "ERBlockCombat.generated.h"
namespace ERShotTrace {struct FContext;}

USTRUCT(BlueprintType)
struct FERBlockShotSettings
{
    GENERATED_BODY()
    // Legacy serialized tuning, retained for asset compatibility. Bullet penetration
    // now consumes current cover HP directly from weapon Damage.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Energy = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1", ClampMax="16")) int32 MaxPenetrations = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin=".01", ClampMax="1")) float MinImpactCosine = .15f;
    // Legacy structural override, no longer used by HP-budget bullet traversal.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOverrideStructuralDamage = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", EditCondition="bOverrideStructuralDamage")) float StructuralDamage = 36;
    float GetStructuralDamage(float WeaponDamage) const { return bOverrideStructuralDamage ? StructuralDamage : WeaponDamage; }
};
// Structural blast profile, separate from character blast damage. Gameplay
// values to tune, not a reconstruction of a real grenade.
USTRUCT(BlueprintType)
struct FERBlockExplosionProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float BaseDamage = 400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float MinimumDamage = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float InnerRadius = 25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float OuterRadius = 200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin=".1", ClampMax="8")) float FalloffExponent = 2;
    // Shielding cells crossed before exposure becomes zero.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="16")) int32 MaxShieldingLayers = 4;
    // 5cm cell worlds: intact material crossed before exposure becomes zero.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="400")) float MaxShieldingCm = 40;
    // Pre-profile linear envelope: Damage * (1 - d / Radius).
    static FERBlockExplosionProfile Linear(float Radius, float Damage)
    { FERBlockExplosionProfile P; P.BaseDamage=Damage; P.InnerRadius=0; P.OuterRadius=Radius; P.FalloffExponent=1; return P; }
    bool IsValid() const;
};
struct FERBlockBatchResult
{
    double TotalMs=0, QueryMs=0, ExposureMs=0, MutationMs=0;
    int32 CoverTraces=0;
    int32 Candidates = 0, Occluded = 0, Attenuated = 0, Damaged = 0, Destroyed = 0, GroupsRebuilt = 0;
};
struct FERBlockShotResult
{
    double TotalMs=0;
    FVector End = FVector::ZeroVector;
    FHitResult TerminalHit;
    float RemainingDamage = 0;
    float TerminalDamage = 0;
    int32 BlockHits = 0, Penetrations = 0, ExtraTraces = 0;
    bool bLimited = false;
};
namespace ERBlockCombat
{
    FERBlockShotResult ContinueShot(UWorld* World, const FHitResult& InitialHit, const FVector& Start,
        const FVector& Direction, float Range, float Damage, const FERBlockShotSettings& Settings,
        const FCollisionQueryParams& Query,AController* Instigator,AActor* Causer,ERShotTrace::FContext* Context=nullptr);
    // F(d): 1 inside InnerRadius, ((Outer-d)/(Outer-Inner))^Exponent, 0 from OuterRadius.
    double ExplosionFalloff(const FERBlockExplosionProfile& Profile, double Distance);
    // Profile damage at d times the material multiplier and exposure in [0,1].
    double ExplosionCellDamage(const FERBlockExplosionProfile& Profile, double Distance, float Multiplier, double Exposure);
    // Spatially coherent structural falloff: broad lobes plus small edge chips.
    // World positions keep the contour continuous across panels/rooms.
    double ExplosionFractureDistance(const FERBlockExplosionProfile& Profile, const FVector& Origin, const FVector& CellCenter);
}
