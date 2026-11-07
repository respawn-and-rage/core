#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERWeapon.h"
#include "ERWorldWeaponPose.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERStablePostureTest,"EclipseRecoil.Movement.StablePosture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERStablePostureTest::RunTest(const FString&)
{
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("posture world"),World))return false;
    auto* Pawn=World->SpawnActor<AERCharacter>(FVector(0,0,92.5),FRotator::ZeroRotator);
    auto* Movement=CastChecked<UERMovement>(Pawn->GetCharacterMovement());
    Movement->SetMovementMode(MOVE_Walking);
    const FVector Standing=Pawn->GetActorLocation();
    for(int32 Frame=0;Frame<30;++Frame)Movement->UpdateCharacterStateBeforeMovement(1.f/60);
    TestEqual(TEXT("stable standing height"),Pawn->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),92.5f);
    TestTrue(TEXT("stable posture does not drift"),Pawn->GetActorLocation().Equals(Standing,.0001));
    Pawn->GetCapsuleComponent()->SetCapsuleHalfHeight(34);
    Pawn->SetActorLocation(FVector(0,0,34));
    Movement->Locomotion.HalfHeight=34;Movement->Locomotion.ProneBlend=.3f;Movement->bProneWanted=true;
    const FVector Low=Pawn->GetActorLocation();
    Movement->UpdateCharacterStateBeforeMovement(.1f);
    TestTrue(TEXT("prone blend advances even when capsule reached its target"),Movement->Locomotion.ProneBlend>.3f);
    TestEqual(TEXT("blend update retains the settled capsule"),Pawn->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),34.f);
    for(int32 Frame=0;Frame<90;++Frame)Movement->UpdateCharacterStateBeforeMovement(1.f/60);
    TestEqual(TEXT("prone blend finishes"),Movement->Locomotion.ProneBlend,1.f);
    TestTrue(TEXT("settled prone feet do not move"),Pawn->GetActorLocation().Equals(Low,.0001));
    Movement->bProneWanted=false;
    for(int32 Frame=0;Frame<120;++Frame)Movement->UpdateCharacterStateBeforeMovement(1.f/60);
    TestEqual(TEXT("standing transition remains available"),Pawn->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),92.5f);
    TestEqual(TEXT("standing transition clears prone blend"),Movement->Locomotion.ProneBlend,0.f);
    TestTrue(TEXT("standing transition preserves the feet"),FMath::IsNearlyEqual(Pawn->GetActorLocation().Z,92.5,.001));
    World->DestroyWorld(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERBodyClearanceTest,"EclipseRecoil.Movement.BodyClearance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERBodyClearanceTest::RunTest(const FString&)
{
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("Collision world"),World))return false;
    auto Box=[&](FVector Position,FVector Size)
    {
        auto* Actor=World->SpawnActor<AActor>();
        auto* Component=NewObject<UStaticMeshComponent>(Actor);
        Actor->SetRootComponent(Component);Actor->AddInstanceComponent(Component);
        Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Component->SetCollisionProfileName(TEXT("BlockAll"));Component->RegisterComponent();
        Component->SetWorldLocation(Position);Component->SetWorldScale3D(Size/100.0);
        return Component;
    };
    Box(FVector(0,0,-25),FVector(2000,2000,50));
    auto* Pawn=World->SpawnActor<AERCharacter>(FVector(0,0,36.15),FRotator::ZeroRotator);
    auto* Move=Cast<UERMovement>(Pawn->GetCharacterMovement());
    Pawn->GetCapsuleComponent()->SetCapsuleHalfHeight(34);
    Move->Locomotion.HalfHeight=34;Move->Locomotion.ProneBlend=1;
    TestTrue(TEXT("Lying footprint clears flat floor"),Move->HasProneClearance(Pawn->GetActorLocation(),FQuat::Identity,1));
    auto* Wall=Box(FVector(170,0,100),FVector(10,400,200));
    FHitResult Hit;
    Move->MoveUpdatedComponent(FVector(100,0,0),FQuat::Identity,true,&Hit);
    TestTrue(TEXT("Head stops before wall outside central capsule"),Hit.bBlockingHit && Pawn->GetActorLocation().X<31);
    Pawn->SetActorLocation(FVector(0,0,36.15));
    Wall->SetWorldLocation(FVector(-70,0,100));
    TestFalse(TEXT("Entry rejected when future feet intersect wall"),Move->HasProneClearance(Pawn->GetActorLocation(),FQuat::Identity,1));
    Wall->SetWorldLocation(FVector(0,80,100));Wall->SetWorldScale3D(FVector(4,.1,2));
    TestTrue(TEXT("Parallel body fits beside wall"),Move->HasProneClearance(Pawn->GetActorLocation(),FQuat::Identity,1));
    TestTrue(TEXT("Turning cannot swing head through wall"),FMath::Abs(Move->ConstrainProneRotation(FRotator(0,90,0)).Yaw)<80);
    Wall->SetWorldLocation(FVector(80,0,100));Wall->SetWorldScale3D(FVector(.1,4,2));
    for(const TCHAR* Key : {TEXT("Pistol"),TEXT("SMG"),TEXT("Shotgun"),TEXT("Assault"),TEXT("Carbine"),TEXT("Sniper"),TEXT("Launcher")})
    {
        const FString Path=FString::Printf(TEXT("/Game/EclipseRecoil/Weapons/Sketchfab/%s/DA_%s"),Key,Key);
        const auto* Data=LoadObject<UERWeaponData>(nullptr,*Path);
        if(!TestNotNull(*Path,Data) || !Data->Mesh)continue;
        const auto Bounds=Data->Mesh->GetBounds();
        FTransform Desired(FQuat::Identity,FVector(80,0,100)-Bounds.Origin*Data->WorldWeaponScale,FVector(Data->WorldWeaponScale));
        const auto Clear=ERWorldWeaponPose::ClearObstacles(*Data,*Pawn,Desired);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ERWeaponClearanceTest),false,Pawn);
        TestFalse(*FString::Printf(TEXT("%s full bounds outside thin wall"),Key),World->OverlapBlockingTestByChannel(Clear.TransformPosition(Bounds.Origin),Clear.GetRotation(),ECC_Pawn,
            FCollisionShape::MakeBox(Bounds.BoxExtent*Data->WorldWeaponScale+FVector(2)),Query));
    }
    World->DestroyWorld(false);
    return true;
}
#endif
