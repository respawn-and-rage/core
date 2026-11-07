#include "ERClaymore.h"
#include "ERCharacter.h"
#include "ERPlayerController.h"
#include "ERMatch.h"
#include "ERWeapon.h"
#include "ERRecon.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "ERGrenadeExplosion.h"
#include "ERBlockRoom.h"
#include "ERWeaponBallistics.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AERClaymore::AERClaymore()
{
    bReplicates=true;SetReplicateMovement(true);PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickInterval=.1f;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Device"));Body->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body->SetStaticMesh(Cube.Object);Body->SetWorldScale3D(FVector(.08f,.30f,.18f));
    Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Body->SetCollisionObjectType(ECC_WorldDynamic);
    Body->SetCollisionResponseToAllChannels(ECR_Ignore);Body->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Body->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);Body->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
    Front=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DangerDirection"));Front->SetupAttachment(Body);
    Front->SetStaticMesh(Cube.Object);Front->SetRelativeLocation(FVector(110,0,0));
    Front->SetRelativeScale3D(FVector(1.2f,.15f,.15f));Front->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterial> BeamMaterial(TEXT("/Game/EclipseRecoil/Weapons/Sketchfab/Claymore/M_ClaymoreLaser.M_ClaymoreLaser"));
    LaserLeft=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaserLeft"));
    LaserRight=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaserRight"));
    for(auto* Laser : {LaserLeft.Get(),LaserRight.Get()})
    {
        Laser->SetupAttachment(RootComponent);Laser->SetStaticMesh(Cylinder.Object);
        Laser->SetMaterial(0,BeamMaterial.Object);Laser->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Laser->SetGenerateOverlapEvents(false);Laser->SetCastShadow(false);Laser->SetVisibility(false);
    }
}
bool AERClaymore::Placement(AERCharacter* C,FHitResult& Hit,FRotator& Rotation,FString* Failure)
{
    auto Reject=[Failure](const TCHAR* Reason){if(Failure)*Failure=Reason;return false;};
    if(!C || !C->Health || C->Health->bDead)return Reject(TEXT("Cannot place a Claymore now."));
    if(C->Health->IsProtected())return Reject(TEXT("Leave spawn protection before placing a Claymore."));
    const auto* Defaults=GetDefault<AERClaymore>();
    const float Reach=FMath::Max(0.f,Defaults->PlacementRange);
    const FRotator Aim=C->GetEffectiveAimRotation();
    const FVector Start=C->Camera->GetComponentLocation();
    const FVector Feet=C->GetActorLocation()-FVector(0,0,C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector Facing=FRotator(0,Aim.Yaw,0).Vector();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERClaymorePlacement),true,C);
    const FCollisionObjectQueryParams Static(ECC_WorldStatic);
    // Placement is always just ahead of the feet, independent of camera pitch.
    const FVector Ahead=Feet+Facing*FMath::Min(65.f,Reach);
    if(!C->GetWorld()->LineTraceSingleByObjectType(Hit,Ahead+FVector(0,0,40),Ahead-FVector(0,0,60),Static,Q))
        return Reject(TEXT("No reachable ground ahead. Move to a clear floor."));
    if(Hit.ImpactNormal.Z<.7f || FMath::Abs(Hit.ImpactPoint.Z-Feet.Z)>100 || FVector::Dist2D(C->GetActorLocation(),Hit.ImpactPoint)>Reach)
        return Reject(TEXT("Choose a nearby, stable ground surface."));
    FHitResult Cover;
    if(C->GetWorld()->LineTraceSingleByObjectType(Cover,Start,Hit.ImpactPoint+Hit.ImpactNormal*12,Static,Q))
        return Reject(TEXT("An obstacle blocks the placement position."));
    for(TActorIterator<AERTeamPlayerStart> It(C->GetWorld());It;++It)
        if(FVector::Dist2D(It->GetActorLocation(),Hit.ImpactPoint)<300)return Reject(TEXT("Move away from the protected spawn area."));
    for(TActorIterator<AERCharacter> It(C->GetWorld());It;++It)
        if((*It)->Health->IsProtected() && FVector::DistSquared((*It)->GetActorLocation(),Hit.ImpactPoint)<FMath::Square(300.f))
            return Reject(TEXT("Move away from players with spawn protection."));
    const FVector Forward=FVector::VectorPlaneProject(Facing,Hit.ImpactNormal).GetSafeNormal();
    Rotation=FRotationMatrix::MakeFromXZ(Forward,Hit.ImpactNormal).Rotator();
    FHitResult Clearance;
    const FVector Position=Hit.ImpactPoint+Hit.ImpactNormal*12;
    if(C->GetWorld()->SweepSingleByChannel(Clearance,Position,Position+Hit.ImpactNormal,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(8),Q))
        return Reject(TEXT("Not enough space for the Claymore here."));
    if(Failure)Failure->Reset();
    return true;
}
bool AERClaymore::Place(AERCharacter* C)
{
    if(!C || !C->HasAuthority())return false;
    FHitResult Hit;FRotator R;FString Failure;
    if(!Placement(C,Hit,R,&Failure))
    {
        if(auto* PC=Cast<AERPlayerController>(C->GetController()))PC->ClientGameplayMessage(TEXT("claymore_placement"),TEXT("Cannot place Claymore"),Failure,1,3,false);
        return false;
    }
    auto* PS=C->GetPlayerState<AERPlayerState>();if(!PS)return false;
    const FTransform Transform(R,Hit.ImpactPoint+Hit.ImpactNormal*10);
    auto* Mine=C->GetWorld()->SpawnActorDeferred<AERClaymore>(StaticClass(),Transform,PS,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Mine)return false;
    Mine->DeviceMesh=C->Weapon && C->Weapon->Data?C->Weapon->Data->PlacedMesh.Get():nullptr;Mine->OnRep_DeviceMesh();
    if(C->Weapon && C->Weapon->ProjectileClass)Mine->BlastEffect=C->Weapon->ProjectileClass->GetDefaultObject<AERProjectile>()->GrenadeExplosionClass;
    Mine->SessionOwner=PS;Mine->PlacedTeam=uint8(PS->Team);Mine->SupportActor=Hit.GetActor();Mine->SupportPoint=Hit.ImpactPoint;
    UGameplayStatics::FinishSpawningActor(Mine,Transform);return true;
}
void AERClaymore::BeginPlay()
{
    Super::BeginPlay();
    Body->SetReceivesDecals(false);Front->SetReceivesDecals(false);
    LaserLeft->SetReceivesDecals(false);LaserRight->SetReceivesDecals(false);
    if(HasAuthority())
    {
        DamageRadius=ERServer::Get(GetWorld(),"Claymore.DamageRadius")*100;
        DetectionRange=DamageRadius*ERServer::Get(GetWorld(),"Claymore.TriggerPercent")*.01f;
        ConeHalfAngle=ERServer::Get(GetWorld(),"Claymore.DamageAngle")*.5f;
        DetectionHalfAngle=FMath::Min(ConeHalfAngle,ERServer::Get(GetWorld(),"Claymore.DetectionAngle")*.5f);
        PeakDamage=ERServer::Get(GetWorld(),"Claymore.Damage");ArmorRatio=ERServer::Get(GetWorld(),"Claymore.ArmorRatio");
        ArmDelay=ERServer::Get(GetWorld(),"Claymore.ArmDelay");Lifetime=ERServer::Get(GetWorld(),"Claymore.Lifetime");
        StructuralRadius=ERServer::Get(GetWorld(),"Claymore.StructuralRadius")*100;StructuralDamage=ERServer::Get(GetWorld(),"Claymore.StructuralDamage");
        ArmAt=GetWorld()->GetTimeSeconds()+ArmDelay;
    }
    UpdateLasers();
}
bool AERClaymore::HasSupport() const
{
    if(!SupportActor.IsValid() || SupportActor->IsActorBeingDestroyed())return false;
    FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERClaymoreSupport),true,this);
    return GetWorld()->LineTraceSingleByObjectType(H,SupportPoint+GetActorUpVector()*5,SupportPoint-GetActorUpVector()*8,
        FCollisionObjectQueryParams(ECC_WorldStatic),Q) && H.GetActor()==SupportActor.Get() && FVector::DistSquared(H.ImpactPoint,SupportPoint)<9 &&
        FVector::DotProduct(H.ImpactNormal,GetActorUpVector())>.9f;
}
// A ground-plane sector: capsule height must not create a blind spot near the mine.
bool AERClaymore::TriggerTarget(AERCharacter* T) const
{
    if(!T || T->Health->bDead || !T->bNetworkReady)return false;
    const FVector Delta=T->GetActorLocation()-GetActorLocation();
    const FVector Flat=FVector::VectorPlaneProject(Delta,GetActorUpVector());
    if(Delta.SizeSquared()>=FMath::Square(DamageRadius) || Flat.SizeSquared()>FMath::Square(DetectionRange) || Flat.IsNearlyZero() ||
        FMath::Abs(FVector::DotProduct(Delta,GetActorUpVector()))>200 ||
        FVector::DotProduct(Flat.GetSafeNormal(),GetActorForwardVector())+KINDA_SMALL_NUMBER<FMath::Cos(FMath::DegreesToRadians(DetectionHalfAngle)))return false;
    FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERClaymoreTriggerLOS),true,this);Q.AddIgnoredActor(T);
    return !GetWorld()->LineTraceSingleByObjectType(H,GetActorLocation()+GetActorUpVector()*8,T->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
}
void AERClaymore::UpdateLasers()
{
    if(GetNetMode()==NM_DedicatedServer)return;
    if(!LaserMaterial && LaserLeft->GetMaterial(0))
    {
        LaserMaterial=UMaterialInstanceDynamic::Create(LaserLeft->GetMaterial(0),this);
        LaserLeft->SetMaterial(0,LaserMaterial);LaserRight->SetMaterial(0,LaserMaterial);
    }
    if(LaserMaterial)LaserMaterial->SetVectorParameterValue(TEXT("Color"),ERRecon::TeamColor(EERTeam(PlacedTeam)));
    const FVector Start=GetActorLocation()+GetActorUpVector()*8;
    int32 Side=-1;
    for(auto* Laser : {LaserLeft.Get(),LaserRight.Get()})
    {
        const FVector Direction=GetActorForwardVector().RotateAngleAxis(Side*DetectionHalfAngle,GetActorUpVector());Side=1;
        FVector End=Start+Direction*DetectionRange;
        FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERClaymoreLaser),true,this);
        if(GetWorld()->LineTraceSingleByObjectType(H,Start,End,FCollisionObjectQueryParams(ECC_WorldStatic),Q))End=H.ImpactPoint;
        Laser->SetWorldLocation((Start+End)*.5f);
        Laser->SetWorldRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
        Laser->SetWorldScale3D(FVector(.006f,.006f,FVector::Distance(Start,End)/100.f));
        Laser->SetVisibility(State==EERClaymoreState::Armed);
    }
}
bool AERClaymore::VisibleTarget(AERCharacter* T) const
{
    if(!T || T->Health->bDead || !T->bNetworkReady)return false;
    const FVector Start=GetActorLocation()+GetActorUpVector()*8;
    // The blast has its own forward-only cone and never damages targets behind the device.
    const FVector Delta=FVector::VectorPlaneProject(T->GetActorLocation()-Start,GetActorUpVector());
    if(Delta.IsNearlyZero() || FVector::DotProduct(Delta.GetSafeNormal(),GetActorForwardVector())<FMath::Cos(FMath::DegreesToRadians(ConeHalfAngle)))return false;
    FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERClaymoreLOS),true,this);Q.AddIgnoredActor(T);
    return !GetWorld()->LineTraceSingleByObjectType(H,Start,T->GetActorLocation(),FCollisionObjectQueryParams(ECC_WorldStatic),Q);
}
void AERClaymore::Tick(float Delta)
{
    Super::Tick(Delta);UpdateLasers();if(!HasAuthority() || State==EERClaymoreState::Terminal)return;
    if(!IsValid(SessionOwner) || SessionOwner->IsInactive() || uint8(SessionOwner->Team)!=PlacedTeam || !HasSupport()) { Terminate();return; }
    const double Now=GetWorld()->GetTimeSeconds();
    if(State==EERClaymoreState::Arming && Now>=ArmAt)
    { State=EERClaymoreState::Armed;ExpiresAt=Now+Lifetime;if(SessionOwner) { ++SessionOwner->ActiveClaymores;bCounted=true;SessionOwner->ForceNetUpdate(); }ForceNetUpdate(); }
    if(ExpiresAt>0 && Now>=ExpiresAt) { Terminate();return; }
    if(State!=EERClaymoreState::Armed)return;
    TArray<FOverlapResult> Candidates;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERClaymoreCandidates),false,this);
    GetWorld()->OverlapMultiByObjectType(Candidates,GetActorLocation(),FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),FCollisionShape::MakeSphere(FMath::Sqrt(FMath::Square(DetectionRange)+FMath::Square(200.f))),Q);
    for(const auto& Candidate:Candidates)
    {
        auto* T=Cast<AERCharacter>(Candidate.GetActor());const auto* PS=T?T->GetPlayerState<AERPlayerState>():nullptr;
        if(PS && uint8(PS->Team)!=PlacedTeam && !T->Health->IsProtected() && TriggerTarget(T)) { Detonate();return; }
    }
}
void AERClaymore::Terminate()
{
    if(State==EERClaymoreState::Terminal)return;
    State=EERClaymoreState::Terminal;
    if(bCounted && IsValid(SessionOwner)) { SessionOwner->ActiveClaymores=FMath::Max(0,SessionOwner->ActiveClaymores-1);SessionOwner->ForceNetUpdate();bCounted=false; }
    SetActorTickEnabled(false);Destroy();
}
void AERClaymore::Detonate()
{
    if(!HasAuthority() || State!=EERClaymoreState::Armed)return;
    if(GetWorld()->GetTimeSeconds()>=ExpiresAt) { Terminate();return; }
    State=EERClaymoreState::Triggered;ForceNetUpdate();
    auto* Controller=SessionOwner?Cast<AController>(SessionOwner->GetOwner()):nullptr;
    TArray<FOverlapResult> Candidates;FCollisionQueryParams Q(SCENE_QUERY_STAT(ERClaymoreDamage),false,this);
    GetWorld()->OverlapMultiByObjectType(Candidates,GetActorLocation(),FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),FCollisionShape::MakeSphere(DamageRadius),Q);
    TSet<AERCharacter*> Damaged;
    for(const auto& Candidate:Candidates)
    {
        if(State!=EERClaymoreState::Triggered)break;
        auto* T=Cast<AERCharacter>(Candidate.GetActor());if(!T || Damaged.Contains(T) || !VisibleTarget(T))continue;
        Damaged.Add(T);
        const FVector Direction=(T->GetActorLocation()-GetActorLocation()).GetSafeNormal();
        const FHitResult H(T,T->GetCapsuleComponent(),T->GetActorLocation(),-Direction);
        float Damage=ERWeaponBallistics::GrenadeDamage(PeakDamage,FVector::Distance(GetActorLocation(),T->GetActorLocation()),0,DamageRadius);
        UGameplayStatics::ApplyPointDamage(T,Damage,Direction,H,Controller,this,UDamageType::StaticClass());
    }
    MulticastDetonation(BlastEffect);
    // Dedicated structural profile; front-biased origin, no HE profile inheritance.
    if(State==EERClaymoreState::Triggered)AERBlockRoom::ApplyWorldExplosion(GetWorld(),GetActorLocation()+GetActorForwardVector()*StructuralRadius,FERBlockExplosionProfile::Linear(StructuralRadius,StructuralDamage),this);
    Terminate();
}
float AERClaymore::TakeDamage(float Amount,const FDamageEvent&,AController*,AActor*)
{ if(HasAuthority() && Amount>0)Terminate();return Amount; }
void AERClaymore::RemoveFor(UWorld* World,AERPlayerState* Owner)
{ for(TActorIterator<AERClaymore> It(World);It;++It)if(!Owner || It->SessionOwner==Owner)It->Terminate(); }
void AERClaymore::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERClaymore,DeviceMesh);DOREPLIFETIME(AERClaymore,SessionOwner);DOREPLIFETIME(AERClaymore,PlacedTeam);
    DOREPLIFETIME(AERClaymore,DetectionRange);DOREPLIFETIME(AERClaymore,DetectionHalfAngle);DOREPLIFETIME(AERClaymore,State);DOREPLIFETIME(AERClaymore,ArmAt);DOREPLIFETIME(AERClaymore,ExpiresAt);
}

void AERClaymore::OnRep_DeviceMesh()
{
    if(DeviceMesh) { Body->SetStaticMesh(DeviceMesh);Body->SetRelativeScale3D(FVector(1));Body->SetRelativeLocation(FVector(0,0,-10)); }
    Front->SetVisibility(!DeviceMesh);
}

void AERClaymore::EndPlay(const EEndPlayReason::Type Reason)
{
    if(HasAuthority() && bCounted && IsValid(SessionOwner)) { SessionOwner->ActiveClaymores=FMath::Max(0,SessionOwner->ActiveClaymores-1);SessionOwner->ForceNetUpdate();bCounted=false; }
    Super::EndPlay(Reason);
}

void AERClaymore::MulticastDetonation_Implementation(TSubclassOf<AERGrenadeExplosion> Effect)
{
    if(GetNetMode()==NM_DedicatedServer || !Effect)return;
    TArray<AERGrenadeExplosion*> Bursts;
    for(TActorIterator<AERGrenadeExplosion> It(GetWorld());It;++It)Bursts.Add(*It);
    Bursts.Sort([](const AERGrenadeExplosion& A,const AERGrenadeExplosion& B){return A.GetGameTimeSinceCreation()>B.GetGameTimeSinceCreation();});
    while(Bursts.Num()>=8) { Bursts[0]->Destroy();Bursts.RemoveAt(0); } // Cosmetic bursts only; never remove a mine.
    if(auto* Burst=GetWorld()->SpawnActor<AERGrenadeExplosion>(Effect,GetActorLocation(),GetActorRotation()))Burst->SetActorScale3D(FVector(.25f));
}
