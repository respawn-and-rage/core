#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ERGrenadeProbe.generated.h"

// Explicit LAN/render regression fixture. Never created in normal matches.
UCLASS(NotBlueprintable)
class AERGrenadeProbe : public AActor
{
    GENERATED_BODY()
public:
    AERGrenadeProbe();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    UPROPERTY(Replicated) int32 Phase=0;
    TArray<TWeakObjectPtr<class AERProjectile>> Grenades;
    TArray<FQuat> InitialRotations;
    TWeakObjectPtr<class AERSmokeCloud> Cloud;
    TWeakObjectPtr<class AStaticMeshActor> Seal;
    TWeakObjectPtr<class ACameraActor> RenderCamera;
    double Deadline=0;
    int32 SeenPhase=-1;
    bool bFixture=false,bFailed=false,bRemoteSpin=false;
    FQuat RemoteRotation=FQuat::Identity;
    TWeakObjectPtr<class AERProjectile> RemoteGrenade;
    struct FFlightSample
    {
        bool bHasSample=false;
        bool bMovedBetweenUpdates=false;
        FTransform RootTransform=FTransform::Identity;
        FVector MeshPosition=FVector::ZeroVector;
    };
    FFlightSample RemoteFlight[2];
    void Check(bool Value,const TCHAR* Label);
    void Fixture();
    void Advance(int32 Next,float Delay);
    void Capture(const TCHAR* Name);
    void TickCombat(float DeltaSeconds);
    TWeakObjectPtr<class AERProjectile> CookProjectile;
    TWeakObjectPtr<class AERWeapon> CookWeapon;
    TArray<TWeakObjectPtr<class AERCharacter>> DamageTargets;
    TArray<float> ExpectedHealth;
    double OriginalFuseEnd=0;
    int32 AmmoBeforeCook=0;
    float DropStartZ=0;
    TWeakObjectPtr<class AERProjectile> ShotGrenade;
    TWeakObjectPtr<class AERCharacter> ShotGrenadeHolder;
    TWeakObjectPtr<class AStaticMeshActor> ShotGrenadeCover;
    TArray<TWeakObjectPtr<class AERProjectile>> ChainGrenades;
    bool bRemoteHeldLogged=false,bRemoteReleaseLogged=false;
};
