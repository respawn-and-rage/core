#include "ERRecon.h"
#include "Misc/AutomationTest.h"
#include "ERSmokeCloud.h"
#include "ERBlockRoom.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERReconFogTest,"EclipseRecoil.Recon.FogAndSight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERReconFogTest::RunTest(const FString& Parameters)
{
    TArray<uint8> Fog; Fog.Init(0,ERRecon::FogBytes);
    for (int32 I : {0,7,8,63,64,4095})
    {
        TestFalse(TEXT("Unseen initially"),ERRecon::IsExplored(Fog,I));
        ERRecon::Explore(Fog,I); TestTrue(TEXT("Seen persists"),ERRecon::IsExplored(Fog,I));
    }
    ERRecon::Explore(Fog,-1); ERRecon::Explore(Fog,4096);
    TestFalse(TEXT("No adjacent reveal"),ERRecon::IsExplored(Fog,9));
    TestFalse(TEXT("Invalid cell rejected"),ERRecon::IsExplored(Fog,4096));
    TArray<uint8> Other; Other.Init(0,ERRecon::FogBytes);
    TestFalse(TEXT("Other team stays hidden"),ERRecon::IsExplored(Other,0));
    TestTrue(TEXT("Forward sight"),ERRecon::InSightCone(FVector::ZeroVector,FVector::ForwardVector,FVector(1000,0,0),3000,false));
    TestFalse(TEXT("Behind hidden"),ERRecon::InSightCone(FVector::ZeroVector,FVector::ForwardVector,FVector(-1000,0,0),3000,false));
    TestTrue(TEXT("Drone sees behind"),ERRecon::InSightCone(FVector::ZeroVector,FVector::ForwardVector,FVector(-1000,0,0),4200,true));
    TestFalse(TEXT("Drone range bounded"),ERRecon::InSightCone(FVector::ZeroVector,FVector::ForwardVector,FVector(4201,0,0),4200,true));
    TestTrue(TEXT("Drone ground footprint ignores altitude"),ERRecon::InSightCone(FVector(0,0,2500),FVector::ForwardVector,FVector(3600,0,170),4200,true));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERReconFlightTest,"EclipseRecoil.Recon.Flight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERReconFlightTest::RunTest(const FString& Parameters)
{
    FERDroneFlight F; F.Launch=FVector(-2000,0,70); F.Center=FVector(0,0,2200); F.Radius=FVector2D(1800); F.StartedAt=10; F.Team=EERTeam::Alpha;
    TestTrue(TEXT("Launch at base"),AERReconDrone::FlightPosition(F,10).Equals(F.Launch,.01));
    TestTrue(TEXT("Vertical ascent"),AERReconDrone::FlightPosition(F,12).Equals(FVector(-2000,0,1135),.01));
    TestTrue(TEXT("Clear roofs before orbit"),FMath::IsNearlyEqual(AERReconDrone::FlightPosition(F,14).Z,2200.,.01));
    const FVector Entry=AERReconDrone::FlightPosition(F,18);
    TestTrue(TEXT("Full lap returns"),Entry.Equals(AERReconDrone::FlightPosition(F,18+2*PI/.16),.01));
    TestTrue(TEXT("No teleport at orbit join"),Entry.Equals(AERReconDrone::FlightPosition(F,18-.0001),1));
    F.Team=EERTeam::Omega;
    TestTrue(TEXT("Opposite teams opposite entry"),AERReconDrone::FlightPosition(F,18).Equals(FVector(-1800,0,2200),.01));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERReconSmokeTest,"EclipseRecoil.Recon.SmokeOcclusion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERReconSmokeTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Values; Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Smoke test world"),World)) return false;
    auto* Smoke=World->SpawnActor<AERSmokeCloud>(); Smoke->RebuildContainment();
    Smoke->ShapePhase=FVector::ZeroVector; Smoke->StartedAtServerTime=-3;
    const FVector Left(-1000,0,170), Right(1000,0,170);
    TestTrue(TEXT("Dense occupied smoke blocks player ray"),Smoke->ReconOpticalDepth(Left,Right)>=1);
    TestTrue(TEXT("Dense smoke blocks overhead drone ray"),Smoke->ReconOpticalDepth(FVector(0,0,2500),FVector(0,0,170))>=1);
    TestEqual(TEXT("Ray misses cloud"),Smoke->ReconOpticalDepth(FVector(-1000,600,170),FVector(1000,600,170)),0.f);
    Smoke->StartedAtServerTime=0;
    TestEqual(TEXT("No instant opaque box before growth"),Smoke->ReconOpticalDepth(Left,Right),0.f);
    Smoke->StartedAtServerTime=-Smoke->Duration+.01;
    TestTrue(TEXT("Faded cloud no longer blocks"),Smoke->ReconOpticalDepth(Left,Right)<1);
    Smoke->StartedAtServerTime=-Smoke->Duration-.1;
    TestEqual(TEXT("Expired cloud is transparent"),Smoke->ReconOpticalDepth(Left,Right),0.f);
    Smoke->StartedAtServerTime=-3;
    Smoke->AddShot(FVector(0,0,40),FVector(0,0,160),400,12);
    TestTrue(TEXT("Blast pocket restores player sight"),Smoke->ReconOpticalDepth(Left,Right)<1);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERReconSmokeInvalidationTest,"EclipseRecoil.Recon.SmokeLocalInvalidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERReconSmokeInvalidationTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if(!TestNotNull(TEXT("Containment test world"),World))return false;
    const auto SpawnRoom=[&](FVector Position)
    {
        FActorSpawnParameters Params;Params.bDeferConstruction=true;
        auto* Room=World->SpawnActor<AERBlockRoom>(Position,FRotator::ZeroRotator,Params);
        Room->FinishSpawning(FTransform(Position));TestTrue(TEXT("Room generates"),Room->Regenerate());return Room;
    };
    auto* Near=SpawnRoom(FVector::ZeroVector);
    auto* Far=SpawnRoom(FVector(100000,0,0));
    auto* Smoke=World->SpawnActor<AERSmokeCloud>();
    const FERCellId Cell{0,0,0,0};
    Smoke->VolumeOrigin=Near->GetActorTransform().TransformPosition(Near->GetCellWorld().GetCellCenter(Cell));
    Smoke->RebuildContainment();
    TestTrue(TEXT("First poll establishes room/chunk baseline"),Smoke->PollGeometryChanges());
    TestFalse(TEXT("Unchanged world does not rebuild"),Smoke->PollGeometryChanges());
    const int32 ChunkCount=Near->GetCellWorld().GetChunks().Num();
    const uint32 OldLayout=Near->GetCellWorld().GetLayoutDigest();
    Near->Config.Height-=5;
    TestTrue(TEXT("Pristine offline room regenerates"),Near->Regenerate());
    TestEqual(TEXT("Layout change retains chunk count"),Near->GetCellWorld().GetChunks().Num(),ChunkCount);
    TestTrue(TEXT("Layout changed without a network epoch"),Near->GetCellWorld().GetLayoutDigest()!=OldLayout);
    TestTrue(TEXT("Pristine same-size layout invalidates local watch"),Smoke->PollGeometryChanges());
    TestFalse(TEXT("New layout baseline remains stable"),Smoke->PollGeometryChanges());
    TMap<FERCellId,float> Damage;Damage.Add(Cell,TNumericLimits<float>::Max());
    TestTrue(TEXT("Distant fixture cell is alive"),Far->GetCellWorld().IsAlive(Cell));
    TestEqual(TEXT("Distant cell removed"),Far->ApplyCellBatch(Damage).Destroyed,1);
    TestFalse(TEXT("Distant destruction does not invalidate containment"),Smoke->PollGeometryChanges());
    TestTrue(TEXT("Local fixture cell is alive"),Near->GetCellWorld().IsAlive(Cell));
    TestEqual(TEXT("Local cell removed"),Near->ApplyCellBatch(Damage).Destroyed,1);
    TestTrue(TEXT("Local destroyed cell invalidates containment"),Smoke->PollGeometryChanges());
    TestFalse(TEXT("Local revision is consumed once"),Smoke->PollGeometryChanges());
    Near->Regenerate();
    TestTrue(TEXT("Regeneration invalidates local watch"),Smoke->PollGeometryChanges());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMinimapFogGeometryTest,"EclipseRecoil.Recon.MinimapFogGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMinimapFogGeometryTest::RunTest(const FString& Parameters)
{
    FERMinimapFog Geometry;FRandomStream Random(733);
    for(int32 Pattern=0;Pattern<3;++Pattern)
    {
        TArray<uint8> Bits;Bits.Init(0,ERRecon::FogBytes);
        for(int32 I=0;I<4096;++I)if(Pattern==0 || (Pattern==1 && Random.FRand()<.65))ERRecon::Explore(Bits,I);
        Geometry.Update(Bits);
        for(double Scale:{.25,1.,4.})for(const FVector2D& Origin:{FVector2D::ZeroVector,FVector2D(.18,-.31)})
        {
            TArray<int32> Coverage;Coverage.Init(0,4096);int32 Quads=0;
            Geometry.Visit(Origin,Scale,.97,[&](int32 X,int32 Y,int32 Size)
            {
                ++Quads;
                for(int32 YY=Y;YY<Y+Size;++YY)for(int32 XX=X;XX<X+Size;++XX)++Coverage[YY*64+XX];
            });
            bool NoLeak=true,NoOverlap=true,Complete=true;
            for(int32 Y=0;Y<64;++Y)for(int32 X=0;X<64;++X)
            {
                const int32 I=Y*64+X;const bool Seen=ERRecon::IsExplored(Bits,I);
                const FVector2D Lo=(FVector2D(double(X)/64-.5,double(Y)/64-.5)-Origin)*(2*Scale);
                const FVector2D Hi=Lo+FVector2D(2*Scale/64);
                const FVector2D Nearest(FMath::Clamp(0.,Lo.X,Hi.X),FMath::Clamp(0.,Lo.Y,Hi.Y));
                NoLeak&=Coverage[I]==0 || Seen;NoOverlap&=Coverage[I]<=1;
                if(Seen && Nearest.SizeSquared()<=1.000001)Complete&=Coverage[I]==1;
            }
            TestTrue(TEXT("Merged quads never reveal unexplored cells"),NoLeak);
            TestTrue(TEXT("Merged quads never overlap"),NoOverlap);
            TestTrue(TEXT("All visible explored cells remain covered"),Complete);
            if(Pattern==0 && Scale==.25)TestEqual(TEXT("Fully explored interior reduces 4096 cells to one quad"),Quads,1);
            if(Pattern==2)TestEqual(TEXT("Clearing fog leaves no geometry from prior mask"),Quads,0);
        }
    }
    return true;
}
#endif
