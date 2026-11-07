#include "ERShotHistory.h"
#include "ERShotTrace.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERShotHistoryTest,"EclipseRecoil.Weapons.ShotHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FERShotHistoryTest::RunTest(const FString&)
{
    FERShotHistory History;TArray<FTransform> Pose;
    TestFalse(TEXT("No invented history"),History.Sample(1,Pose));
    FERShotPose A;A.Time=1;A.ReferencePoint=FVector(0,0,0);
    A.Bodies.Add(FTransform(FQuat::Identity,FVector(0,0,0)));
    FERShotPose B;B.Time=1.1;B.ReferencePoint=FVector(100,0,0);
    B.Bodies.Add(FTransform(FRotator(0,90,0),FVector(100,0,0)));
    History.Append(A.Time)=A;History.Append(B.Time)=B;FVector Point;
    TestFalse(TEXT("Insufficient history falls back to current"),History.Sample(.9,Pose));
    TestTrue(TEXT("Interpolate moving target"),History.Sample(1.05,Pose,&Point));
    TestTrue(TEXT("Position halfway"),Pose[0].GetLocation().Equals(FVector(50,0,0),.001));
    TestTrue(TEXT("Rotation halfway"),Pose[0].GetRotation().Equals(FRotator(0,45,0).Quaternion(),.001));
    TestTrue(TEXT("Diagnostic point follows the same interpolation"),Point.Equals(FVector(50,0,0),.001));
    TestTrue(TEXT("Newest pose available"),History.Sample(1.2,Pose));
    TestTrue(TEXT("Never extrapolate"),Pose[0].GetLocation().Equals(FVector(100,0,0),.001));
    History.Append(B.Time).Bodies.Reset();
    TestFalse(TEXT("Never interpolate incompatible body sets"),History.Sample(1.05,Pose));
    History.Reset();
    for(int32 I=0;I<400;++I)
    {
        auto& Frame=History.Append(2.+I*.001);
        Frame.Bodies.Add(FTransform(FVector(I,0,0)));
    }
    TestEqual(TEXT("Ring remains bounded at high simulation rates"),History.Num(),FERShotHistory::Capacity);
    TestTrue(TEXT("Interpolation survives multiple physical ring wraps"),History.Sample(2.35,Pose));
    TestTrue(TEXT("Ring preserves chronological order"),Pose[0].GetLocation().Equals(FVector(350,0,0),.001));
    TestFalse(TEXT("Overwritten history is unavailable"),History.Sample(2.1,Pose));
    auto& Replacement=History.Append(2.+399*.001);Replacement.Bodies.Add(FTransform(FVector(999,0,0)));
    TestEqual(TEXT("Same tick replaces rather than appends"),History.Num(),FERShotHistory::Capacity);
    TestTrue(TEXT("Replacement is sampled"),History.Sample(2.4,Pose));
    TestTrue(TEXT("Replacement is latest"),Pose[0].GetLocation().Equals(FVector(999,0,0),.001));
    History.Append(3.,FVector(1000,0,0)).Bodies.Add(FTransform(FVector(1000,0,0)));
    TestEqual(TEXT("Large teleport clears interpolation"),History.Num(),1);
    TestFalse(TEXT("No history before teleport"),History.Sample(2.9,Pose));
    History.Append(.1,FVector(1000,0,0));
    TestEqual(TEXT("World clock reset clears history"),History.Num(),1);
    History.Reset();
    TestFalse(TEXT("Death/reset discards all available frames"),History.Sample(10,Pose));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERShotContextTest,"EclipseRecoil.Weapons.ShotContext",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERShotContextTest::RunTest(const FString&)
{
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("shot context world"),World))return false;
    auto* Wall=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(5,100,100));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERShotContextTest),false);
    ERShotTrace::FContext Context(World,Query,-1);FHitResult Hit;
    TestTrue(TEXT("Cartridge sees current solid cover"),ERShotTrace::Pellet(World,Hit,FVector(-100,0,0),FVector(100,0,0),Query,-1,&Context));
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Later pellet observes cover opened during same cartridge"),ERShotTrace::Pellet(World,Hit,FVector(-100,0,0),FVector(100,0,0),Query,-1,&Context));
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
