#include "ERBlockCombat.h"
#include "ERShotTrace.h"
#include "ERWeapon.h"
#include "ERCharacter.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ERBlockRoom.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

bool FERBlockExplosionProfile::IsValid() const
{
    for (float Value : {BaseDamage, MinimumDamage, InnerRadius, OuterRadius, FalloffExponent})
        if (!FMath::IsFinite(Value) || Value < 0) return false;
    return OuterRadius > 0 && OuterRadius <= 5000 && InnerRadius < OuterRadius && MinimumDamage <= BaseDamage &&
        FalloffExponent > 0 && MaxShieldingLayers >= 0;
}
double ERBlockCombat::ExplosionFalloff(const FERBlockExplosionProfile& Profile, double Distance)
{
    if (!Profile.IsValid() || !FMath::IsFinite(Distance) || Distance >= Profile.OuterRadius) return 0;
    if (Distance <= Profile.InnerRadius) return 1;
    return FMath::Pow((Profile.OuterRadius - Distance) / (Profile.OuterRadius - Profile.InnerRadius), double(Profile.FalloffExponent));
}
double ERBlockCombat::ExplosionCellDamage(const FERBlockExplosionProfile& Profile, double Distance, float Multiplier, double Exposure)
{
    if (!Profile.IsValid() || !FMath::IsFinite(Distance) || Distance >= Profile.OuterRadius ||
        !FMath::IsFinite(Multiplier) || Multiplier <= 0 || !FMath::IsFinite(Exposure) || Exposure <= 0) return 0;
    const double Falloff = ExplosionFalloff(Profile, Distance);
    // Each cell receives the local value; the base is not a total to divide.
    const double Local = Profile.MinimumDamage + (double(Profile.BaseDamage) - Profile.MinimumDamage) * Falloff;
    return Local * Multiplier * FMath::Min(1., Exposure);
}
double ERBlockCombat::ExplosionFractureDistance(const FERBlockExplosionProfile& Profile, const FVector& Origin, const FVector& CellCenter)
{
    const FVector Offset = CellCenter - Origin;
    const double Distance = Offset.Size();
    // Preserve full damage in the core and the original hard outer limit.
    if (Distance <= Profile.InnerRadius || Distance >= Profile.OuterRadius) return Distance;
    // No per-cell random rolls: neighbouring voxels should form torn edges,
    // rather than a spray of isolated surviving cells. Origin varies the shape
    // between detonation locations without consuming the gameplay RNG.
    const FVector Phase = Origin * .0037 + FVector(17.13, 43.71, 9.23);
    const double Lobes = FMath::PerlinNoise3D(Offset / Distance * 2.8 + Phase);
    const double Chips = FMath::PerlinNoise3D(Offset / 22. + Phase * 1.73);
    // Retain the original usable breach; irregular fractures extend its edge
    // by at most 28%, always clipped to the existing outer blast envelope.
    const double Stretch = FMath::Clamp(1.08 + .4 * Lobes + .16 * Chips, 1., 1.28);
    return Profile.InnerRadius + (Distance - Profile.InnerRadius) / Stretch;
}
FERBlockShotResult ERBlockCombat::ContinueShot(UWorld* World, const FHitResult& InitialHit, const FVector& Start,
    const FVector& Direction, float Range, float Damage, const FERBlockShotSettings& Settings,
    const FCollisionQueryParams& Query, AController* Instigator, AActor* Causer)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERBlock_ContinueShot);
    const double StartTime=FPlatformTime::Seconds();
    FERBlockShotResult Result;
    Result.End = InitialHit.ImpactPoint;
    Result.TerminalHit = InitialHit;
    if (!World || World->GetNetMode() == NM_Client || Start.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero() ||
        !FMath::IsFinite(Range) || Range <= 0 || !FMath::IsFinite(Damage) || Damage <= 0) return Result;
    const FVector Ray = Direction.GetSafeNormal();
    const int32 Limit = FMath::Clamp(Settings.MaxPenetrations, 1, 16);
    Result.RemainingDamage = Damage;
    FHitResult Hit = InitialHit;
    FVector Cursor = Start;
    double RemainingRange = Range;
    TArray<AERBlockRoom*> Rooms;
    int32 CellSteps = 0;
    while (Hit.bBlockingHit)
    {
        Result.End = Hit.ImpactPoint; Result.TerminalHit = Hit;
        auto* Room = Cast<AERBlockRoom>(Hit.GetActor());
        if (Room && Room->CanMutateBlocks())
        {
            // One shared HP budget across every cell, panel and room on this ray.
            if (Result.Penetrations >= Limit || ++CellSteps > 4 * Limit) { Result.bLimited = true; break; }
            Rooms.AddUnique(Room);
            // Firearms use the same four supplied decals on fixed and cell surfaces.
            // Keep the material fallback for direct structural probes/other causers.
            auto* ImpactWeapon = Cast<AERWeapon>(Causer);
            const auto Step = Room->ShootCells(Hit, Cursor, Ray, RemainingRange, Result.RemainingDamage,
                Limit - Result.Penetrations, !ImpactWeapon);
            if (ImpactWeapon)
                ImpactWeapon->MulticastImpact(Hit.ImpactPoint, Hit.ImpactNormal,
                    FMath::FRandRange(0.f, 360.f), uint8(FMath::RandRange(0, 3)));
            Result.BlockHits += Step.Cells; Result.bLimited |= Step.bLimited;
            if (Step.bStop) { Result.End = Step.Exit; break; }
            Result.Penetrations += Step.Runs;
            RemainingRange -= FVector::DotProduct(Step.Exit - Cursor, Ray); Cursor = Step.Exit;
            if (RemainingRange <= .1) break;
        }
        else
        {
            Result.TerminalDamage = UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Result.RemainingDamage, Ray, Hit, Instigator, Causer, UDamageType::StaticClass());
            break;
        }
        Result.End = Cursor + Ray * RemainingRange;
        Result.TerminalHit = FHitResult();
        ++Result.ExtraTraces;
        const auto* Gun=Cast<AERWeapon>(Causer);
        const bool Anatomical=Gun && Gun->Data && Gun->Data->bUseBodyHitZones;
        if (!(Anatomical ? ERShotTrace::Pellet(World,Hit,Cursor,Result.End,Query,
            Cast<AERCharacter>(Gun->GetOwner()) ? ERShotTrace::BulletTime(CastChecked<AERCharacter>(Gun->GetOwner())):-1) :
            World->LineTraceSingleByChannel(Hit, Cursor, Result.End, ECC_GameTraceChannel1, Query))) break;
    }
    for (auto* Room : Rooms)
        Room->LastCombat = FString::Printf(TEXT("shot hits=%d penetrations=%d remaining_damage=%.1f limited=%d target=%s damage=%.1f"), Result.BlockHits, Result.Penetrations, Result.RemainingDamage, Result.bLimited, Result.TerminalHit.GetActor() && !Result.TerminalHit.GetActor()->IsA<AERBlockRoom>() ? *Result.TerminalHit.GetActor()->GetName() : TEXT("none"), Result.TerminalDamage);
    Result.TotalMs=(FPlatformTime::Seconds()-StartTime)*1000;
    return Result;
}
