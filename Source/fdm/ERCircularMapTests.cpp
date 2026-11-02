#include "ERProceduralMap.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ERIndustrialMapTestUtils.h"
#include "Components/DynamicMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERCircularCentralTest,"EclipseRecoil.Maps.CircularCentralBuilding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERCircularCentralTest::RunTest(const FString& Parameters)
{
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("test.circular");T->Archetype=EERMapArchetype::CentralBuilding;
    FERProceduralConfig C;C.TemplateAsset=T;FERBlockRoomConfig B;TSet<bool> Modes;
    for(int32 TeamSize:{1,2,4,8,16})for(int32 Seed=0;Seed<516;++Seed)
    {
        C.ExpectedTeamSize=TeamSize;C.Seed=Seed;FERProceduralLayout L;
        if(!ERProcedural::Build(C,B,L)){AddError(FString::Printf(TEXT("circular layout failed size=%d seed=%d"),TeamSize,Seed));return false;}
        Modes.Add(L.CentralHill);
        if(!L.CircularArena || L.LayoutSymmetry!=EERTemplateSymmetry::Mirror || L.AlphaSpawns.Num()!=10 || L.OmegaSpawns.Num()!=10)return false;
        if(FMath::Abs(ERProcedural::PlayableAreaM2(L)-TeamSize*1000)>TeamSize*100+.01)return false;
        for(int32 I=0;I<10;++I)
        {
            const auto A=L.AlphaSpawns[I].GetLocation(),O=L.OmegaSpawns[I].GetLocation();
            if(!O.Equals({A.X,-A.Y,A.Z},.001) || FVector2D(A).Size()>L.ArenaSize.X*.5-100)return false;
        }
        for(const auto& Route:L.CrossingRoutes)for(const auto P:Route)
            if(FMath::Abs(P.Z-ERProcedural::CentralGroundHeight(L,FVector2D(P))-100)>.001)return false;
    }
    TestEqual(TEXT("both hill configurations sampled"),Modes.Num(),2);
    UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=T;Map->LayoutConfig.ExpectedTeamSize=2;Map->bCeilings=false;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ERCircularTerrain),false);FHitResult Hit;const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);
    for(int32 Seed:{42,73,7919,1})
    {
        Map->LayoutConfig.Seed=Seed;
        if(!TestTrue(TEXT("physical circular map generated"),Map->Regenerate()))continue;
        const auto& L=Map->GetLayout();TestTrue(TEXT("valid cell world"),Map->ValidateCells());
        Map->RequestSupportCheck();ERIndustrialTest::Settle(*Map);TestEqual(TEXT("intact building remains supported"),Map->GetCellWorld().GetDetachedCount(),int64(0));
        for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& S:*Starts)
        {
            TestFalse(TEXT("spawn capsule clear"),World->OverlapBlockingTestByChannel(S.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));
            TestTrue(TEXT("spawn supported"),World->LineTraceSingleByChannel(Hit,S.GetLocation(),S.GetLocation()-FVector(0,0,120),ECC_WorldStatic,Q));
        }
        for(const auto& Site:L.CoverSites)
        {
            const bool Blocked=World->SweepSingleByChannel(Hit,Site.Entry,Site.Hide,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,Site.HalfHeight),Q);
            if(Blocked)AddError(FString::Printf(TEXT("cover access from=%s to=%s hit=%s"),*Site.Entry.ToString(),*Site.Hide.ToString(),*GetNameSafe(Hit.GetComponent())));
            TestFalse(TEXT("cover pocket accessible"),Blocked);
            TestTrue(TEXT("cover hides crouching or standing eye"),World->LineTraceSingleByChannel(Hit,Site.SightFrom,Site.SightTo,ECC_GameTraceChannel1,Q));
        }
        for(const auto& Path:L.CrossingRoutes)for(int32 I=1;I<Path.Num();++I)
            if(World->SweepSingleByChannel(Hit,Path[I-1],Path[I],FQuat::Identity,ECC_Pawn,Capsule,Q)){AddError(FString::Printf(TEXT("circular crossing obstructed at %s"),*Path[I].ToString()));break;}
        for(int32 I=0;I<32;++I)
        {
            const FVector2D P(L.ArenaSize.X*.4*FMath::Cos(I*.7),L.ArenaSize.Y*.4*FMath::Sin(I*.7));
            const float Height=ERProcedural::CentralGroundHeight(L,P);
            TestTrue(TEXT("terrain triangles reflect exactly"),FMath::Abs(Height-ERProcedural::CentralGroundHeight(L,{P.X,-P.Y}))<.001);
            if(World->LineTraceSingleByChannel(Hit,FVector(P,Height+10),FVector(P,Height-10),ECC_WorldStatic,Q))
                TestTrue(TEXT("terrain sampler matches actual collision triangles"),FMath::Abs(Hit.ImpactPoint.Z-Height)<.1);
            else AddError(TEXT("missing terrain collision"));
        }
    }
    World->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERTemplateMirrorTest,"EclipseRecoil.Maps.TemplateMirror",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERTemplateMirrorTest::RunTest(const FString& Parameters)
{
    for(const FString& Name:{TEXT("CentralBuilding"),TEXT("MiddleEasternAlleys"),TEXT("FireTraining"),TEXT("Rooms"),TEXT("Village")})
    {
        const FString Path=FString::Printf(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_%s.DA_%s"),*Name,*Name);
        auto* Asset=LoadObject<UERMapTemplate>(nullptr,*Path);if(!TestNotNull(*Name,Asset))continue;
        auto* T=DuplicateObject<UERMapTemplate>(Asset,GetTransientPackage());T->Symmetry=EERTemplateSymmetry::Mirror;
        FERProceduralConfig C;C.TemplateAsset=T;C.ExpectedTeamSize=2;
        UWorld::InitializationValues Values;Values.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);if(!World)return false;
        auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig=C;Map->bCeilings=false;
        if(TestTrue(*(Name+TEXT(" physical mirror")),Map->Regenerate()))
        {
            TestTrue(TEXT("reflected cell grids are valid"),Map->ValidateCells());
            const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);FCollisionQueryParams Q(SCENE_QUERY_STAT(ERTemplateMirror),false);FHitResult Hit;
            for(const auto* Starts:{&Map->GetLayout().AlphaSpawns,&Map->GetLayout().OmegaSpawns})for(const auto& Start:*Starts)
            {
                TestFalse(*(Name+TEXT(" reflected spawn clear")),World->OverlapBlockingTestByChannel(Start.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));
                TestTrue(*(Name+TEXT(" reflected spawn supported")),World->LineTraceSingleByChannel(Hit,Start.GetLocation(),Start.GetLocation()-FVector(0,0,125),ECC_WorldStatic,Q));
            }
        }
        World->DestroyWorld(false);
        for(int32 Seed:{42,73,7919,1})
        {
            C.Seed=Seed;FERProceduralLayout L;
            if(!TestTrue(*FString::Printf(TEXT("%s mirror seed=%d"),*Name,Seed),ERProcedural::Build(C,FERBlockRoomConfig(),L)))continue;
            TestEqual(TEXT("reflection mode retained"),L.LayoutSymmetry,EERTemplateSymmetry::Mirror);
            for(int32 I=0;I<L.AlphaSpawns.Num();++I)
            {const auto A=L.AlphaSpawns[I].GetLocation();TestTrue(TEXT("teams reflect across Y=0"),L.OmegaSpawns[I].GetLocation().Equals({A.X,-A.Y,A.Z},.001));}
        }
    }
    return true;
}
#endif
