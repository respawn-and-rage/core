#include "ERShotHistory.h"
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
    History.Frames={A,B};FVector Point;
    TestFalse(TEXT("Insufficient history falls back to current"),History.Sample(.9,Pose));
    TestTrue(TEXT("Interpolate moving target"),History.Sample(1.05,Pose,&Point));
    TestTrue(TEXT("Position halfway"),Pose[0].GetLocation().Equals(FVector(50,0,0),.001));
    TestTrue(TEXT("Rotation halfway"),Pose[0].GetRotation().Equals(FRotator(0,45,0).Quaternion(),.001));
    TestTrue(TEXT("Diagnostic point follows the same interpolation"),Point.Equals(FVector(50,0,0),.001));
    TestTrue(TEXT("Newest pose available"),History.Sample(1.2,Pose));
    TestTrue(TEXT("Never extrapolate"),Pose[0].GetLocation().Equals(FVector(100,0,0),.001));
    History.Frames[1].Bodies.Reset();
    TestFalse(TEXT("Never interpolate incompatible body sets"),History.Sample(1.05,Pose));
    return true;
}
#endif
