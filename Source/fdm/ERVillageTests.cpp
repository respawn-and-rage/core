#include "ERProceduralMap.h"
#include "ERMapTheme.h"
#include "ERMapCatalog.h"
#include "ERDestructibleMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERVillageLayoutTest,"EclipseRecoil.Destruction.VillageLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERVillageLayoutTest::RunTest(const FString& Parameters)
{
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("test.village");T->Archetype=EERMapArchetype::Village;T->Symmetry=EERTemplateSymmetry::HalfTurn;
    FERProceduralConfig C;C.TemplateAsset=T;FERBlockRoomConfig B;FERProceduralLayout L,Again;
    TSet<int32> BlockCounts,BaseVariants,Storeys;FString Manifest=TEXT("[");
    TSet<FString> Shapes;TArray<int32> Seeds;for(int32 I=0;I<512;++I)Seeds.Add(I);Seeds.Append({7919,-17,INT32_MIN,INT32_MAX});
    auto Key=[](const FERCellPanelDesc& P,bool Turn)
    {
        FBox Box=FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform);FVector V=Box.GetCenter(),S=Box.GetSize();if(Turn){V.X=-V.X;V.Y=-V.Y;}
        return FString::Printf(TEXT("%lld/%lld/%lld:%lld/%lld/%lld:%d"),FMath::RoundToInt64(V.X*100),FMath::RoundToInt64(V.Y*100),FMath::RoundToInt64(V.Z*100),FMath::RoundToInt64(S.X*100),FMath::RoundToInt64(S.Y*100),FMath::RoundToInt64(S.Z*100),uint8(P.Material));
    };
    for(int32 Seed:Seeds)
    {
        C.Seed=Seed;if(!TestTrue(TEXT("Village accepts seed"),ERProcedural::Build(C,B,L)))return false;
        const int32 N=L.VillageBlocks.Num();int32 Floors=0,Centered=0;
        for(float Y=-L.ArenaSize.Y*.5f;Y<L.ArenaSize.Y*.5f;Y+=100)
        {
            const float H=ERProcedural::VillageGroundHeight(L,{0,Y});
            TestTrue(TEXT("terrain has no transverse slope"),FMath::IsNearlyEqual(H,ERProcedural::VillageGroundHeight(L,{L.ArenaSize.X*.4f,Y}),.001f));
            TestTrue(TEXT("both teams face the same height profile"),FMath::IsNearlyEqual(H,ERProcedural::VillageGroundHeight(L,{0,-Y}),.001f));
            TestTrue(TEXT("terrain grade stays walkable"),FMath::Abs(H-ERProcedural::VillageGroundHeight(L,{0,Y+10}))<=5.01f);
        }
        for(const auto& Block:L.VillageBlocks)
        {
            Floors+=Block.Storeys;Storeys.Add(Block.Storeys);
            if(Block.SelfSymmetric){++Centered;TestTrue(TEXT("odd centre block stays at origin"),FVector2D(Block.Frame.GetLocation()).IsNearlyZero());}
        }
        BlockCounts.Add(N);BaseVariants.Add(L.VillageBaseVariant);
        TestTrue(TEXT("variable block count within recipe"),N>=T->Village.MinCentralBlocks && N<=T->Village.MaxCentralBlocks);
        TestEqual(TEXT("one self-symmetric centre only for odd counts"),Centered,N%2);
        TestTrue(TEXT("upper floors respect construction budget"),Floors<=T->Village.StoreyBudget);
        TestTrue(TEXT("construction remains within shared panel budget"),L.Panels.Num()<=4096);
        TestEqual(TEXT("two huts per floor plus base and fallback shacks"),L.SecondaryStructures.Num(),Floors*2+2+(L.VillageBaseVariant==2 ? 2:0));
        TestEqual(TEXT("three access flights per pair plus base stairs"),L.StairRoutes.Num(),((N+1)/2)*6+6);
        TestEqual(TEXT("ten starts per team"),L.AlphaSpawns.Num(),10);
        TestEqual(TEXT("one deck per floor plus four spawn structures"),L.ElevatedDecks.Num(),Floors+4);
        TestEqual(TEXT("two designated ground passages per block"),L.VillagePassages.Num(),N*2);
        TestEqual(TEXT("three sealed bays per block"),L.VillageClosedGround.Num(),N*3);
        TestEqual(TEXT("two real upper ladders per extra floor"),L.VillageUpperLadders.Num(),(Floors-N)*2);
        TestEqual(TEXT("sparse variants have two side walls per flank"),L.VillageSideWalls.Num(),N<=2 ? 4:2);
        TestEqual(TEXT("each side wall has a bypass"),L.VillageSideBypasses.Num(),L.VillageSideWalls.Num());
        for(const auto& Wall:L.VillageSideWalls)
        {
            TestTrue(TEXT("side cover blocks standing sight height"),Wall.GetSize().Z>=240);
            TestTrue(TEXT("side cover widens in sparse variants"),FMath::IsNearlyEqual(Wall.GetSize().X,double(N==1 ? 700:N==2 ? 580:260),.01));
        }
        Shapes.Add(L.ArenaSize.ToString()+FString::FromInt(N)+L.ElevatedDecks[0].GetCenter().ToString());
        if(Seed>=0 && Seed<512)
        {
            if(Seed)Manifest+=TEXT(",");
            Manifest+=FString::Printf(TEXT("{\"seed\":%d,\"width\":%.0f,\"length\":%.0f,\"base\":%d,\"blocks\":["),Seed,L.ArenaSize.X,L.ArenaSize.Y,L.VillageBaseVariant);
            for(int32 I=0;I<N;++I){const auto& P=L.VillageBlocks[I];const auto V=P.Frame.GetLocation();if(I)Manifest+=TEXT(",");Manifest+=FString::Printf(TEXT("{\"x\":%.0f,\"y\":%.0f,\"w\":%.0f,\"d\":%.0f,\"floors\":%d}"),V.X,V.Y,P.Size.X,P.Size.Y,P.Storeys);}
            Manifest+=TEXT("],\"side_walls\":[");
            for(int32 I=0;I<L.VillageSideWalls.Num();++I){const auto& Wall=L.VillageSideWalls[I];const auto P=Wall.GetCenter(),S=Wall.GetSize();if(I)Manifest+=TEXT(",");Manifest+=FString::Printf(TEXT("{\"x\":%.0f,\"y\":%.0f,\"w\":%.0f,\"d\":%.0f,\"h\":%.0f}"),P.X,P.Y,S.X,S.Y,S.Z);}
            Manifest+=TEXT("]}");
        }
        TSet<FString> Panels;for(const auto& P:L.Panels)Panels.Add(Key(P,false));
        bool Solid=false;
        for(const auto& P:L.Panels)
        {
            if(!Panels.Contains(Key(P,true))){AddError(TEXT("unpaired Village construction"));return false;}
            TestTrue(TEXT("only timber and sheet metal"),P.Material==EERBlockMaterial::Wood || P.Material==EERBlockMaterial::Metal);
            if(P.Material==EERBlockMaterial::Wood){Solid|=P.Size.Y>=8;TestTrue(TEXT("wood cells retain calibrated depth"),FMath::IsNearlyEqual(P.Cell.Y,2));}
            else TestTrue(TEXT("metal retains calibrated sheet thickness"),FMath::IsNearlyEqual(P.Size.Y,.2));
            TestFalse(TEXT("no permanently anchored upper timber"),P.bAnchored);
            const auto Box=FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform);
            TestTrue(TEXT("gameplay construction stays inside red rectangle"),Box.Min.X>=-L.ArenaSize.X*.5-.1 && Box.Max.X<=L.ArenaSize.X*.5+.1 && Box.Min.Y>=-L.ArenaSize.Y*.5-.1 && Box.Max.Y<=L.ArenaSize.Y*.5+.1);
        }
        TestTrue(TEXT("massive beams exist"),Solid);
        for(int32 I=0;I<10;++I)
        {
            const auto A=L.AlphaSpawns[I].GetLocation(),O=L.OmegaSpawns[I].GetLocation();
            TestTrue(TEXT("spawn transforms are half-turn pairs"),FVector(-A.X,-A.Y,A.Z).Equals(O));
            TestTrue(TEXT("starts inside opposed outer strips"),A.Y<-L.ArenaSize.Y*.5+450 && O.Y>L.ArenaSize.Y*.5-450);
        }
        C.Seed=Seed;ERProcedural::Build(C,B,Again);
        TestTrue(TEXT("deterministic layout"),L.ArenaSize==Again.ArenaSize && L.Panels.Num()==Again.Panels.Num());
        for(int32 I=0;I<L.Panels.Num();++I)if(!L.Panels[I].Transform.Equals(Again.Panels[I].Transform)){AddError(TEXT("nondeterministic Village"));return false;}
    }
    Manifest+=TEXT("]");IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Testing")),true);
    TestTrue(TEXT("actual generated variety manifest exported"),FFileHelper::SaveStringToFile(Manifest,*(FPaths::ProjectSavedDir()/TEXT("Testing/village-variety.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestEqual(TEXT("all block counts exercised"),BlockCounts.Num(),5);TestEqual(TEXT("all base variants exercised"),BaseVariants.Num(),3);TestEqual(TEXT("all storey counts exercised"),Storeys.Num(),3);
    const auto Previous=L.Panels.Num();T->Village.DeckHeight=NAN;
    TestFalse(TEXT("invalid recipe rejected atomically"),ERProcedural::Build(C,B,L));TestEqual(TEXT("previous output retained"),L.Panels.Num(),Previous);
    T->Village.DeckHeight=240;T->Village.BlockWidth={2001,2002};
    TestFalse(TEXT("empty sampling grid rejected atomically"),ERProcedural::Build(C,B,L));TestEqual(TEXT("invalid grid retains previous output"),L.Panels.Num(),Previous);
    TestTrue(TEXT("seed changes whole plan dimensions"),Shapes.Num()>10);
    TestTrue(TEXT("Village in rotation"),ERMaps::Rotation().Contains(FName(TEXT("/Game/EclipseRecoil/Maps/L_Village"))));
    AddInfo(FString::Printf(TEXT("ER_VILLAGE_LAYOUT seeds=516 shapes=%d result=%s"),Shapes.Num(),HasAnyErrors() ? TEXT("FAIL"):TEXT("PASS")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERVillagePhysicsTest,"EclipseRecoil.Destruction.VillagePhysics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERVillagePhysicsTest::RunTest(const FString& Parameters)
{
    auto* Saved=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_Village"));
    auto* Asset=Saved ? DuplicateObject<UERMapTemplate>(Saved,GetTransientPackage()):nullptr;
    if(!TestNotNull(TEXT("saved Village recipe"),Asset))return false;
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=Asset;Map->bCeilings=false;
    const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);FCollisionQueryParams Q(SCENE_QUERY_STAT(ERVillagePhysics),false);FHitResult Hit;
    for(int32 Seed:{0,1,2,9,4,5,6,40,8,13,42,73,511,7919,-17})
    {
        Map->LayoutConfig.Seed=Seed;Asset->Village.DeckHeight=240+20*(uint32(Seed)%3);if(!TestTrue(TEXT("Village physical generation"),Map->Regenerate()))continue;
        AddInfo(FString::Printf(TEXT("ER_VILLAGE_PHYSICAL_SEED %d"),Seed));
        const auto& L=Map->GetLayout();
        for(float Y=-L.ArenaSize.Y*.5f+80;Y<L.ArenaSize.Y*.5f-80;Y+=100)
        {
            const float H=ERProcedural::VillageGroundHeight(L,{0,Y});
            // Use a flank, clear of central timber platforms.
            const float X=L.ArenaSize.X*.5f-80;
            if(World->LineTraceSingleByChannel(Hit,{X,Y,H+25},{X,Y,H-25},ECC_Visibility,Q) && Hit.GetComponent() && Hit.GetComponent()->GetName()==TEXT("AlleyTerrain"))
                TestTrue(TEXT("visible terrain and queried ground agree"),FMath::IsNearlyEqual(float(Hit.ImpactPoint.Z),H,.05f));
        }
        TestTrue(TEXT("valid cell world"),Map->ValidateCells());TestTrue(TEXT("Village uses a shared palette"),L.ThemeIndex!=INDEX_NONE && !L.ThemeName.IsNone());
        int32 Floors=0;for(const auto& Block:L.VillageBlocks)Floors+=Block.Storeys;
        TestEqual(TEXT("all storey decks plus four spawn structures"),L.ElevatedDecks.Num(),Floors+4);
        TestEqual(TEXT("two rear spawn galleries"),L.Bases.Num(),2);
        TestEqual(TEXT("two corner lookouts"),L.DistrictTowers.Num(),2);
        TestEqual(TEXT("two side shacks"),L.TownBuildings.Num(),2);
        TestEqual(TEXT("eight gray cover pockets"),L.CoverSites.Num(),Floors*4);
        int32 Skins=0;
        for(const auto& P:Map->GetCellWorld().GetPanels())if(!P.Skin.IsEmpty())
        {
            ++Skins;TestTrue(TEXT("imported timber retains surfaces and bounded triangles"),P.Surface && P.InteriorSurface && P.Skin.Num()<=30000);
            if(Asset->ThemeLibrary && Asset->ThemeLibrary->SharedSurfaces)
                TestTrue(TEXT("both imported skin faces use the shared catalog"),Asset->ThemeLibrary->SharedSurfaces->Contains(P.Surface) && Asset->ThemeLibrary->SharedSurfaces->Contains(P.InteriorSurface));
        }
        TestEqual(TEXT("two lookout skins and two side shack skins"),Skins,4);
        for(const auto& Cover:L.CoverSites)
        {
            TestFalse(TEXT("crouched hide position clear"),World->OverlapBlockingTestByChannel(Cover.Hide+FVector(0,0,2),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,60),Q));
            TestTrue(TEXT("gray cover interrupts crouched sightline"),World->LineTraceSingleByChannel(Hit,Cover.SightFrom,Cover.SightTo,ECC_Visibility,Q));
            TestFalse(TEXT("cover pocket has an open access"),World->SweepSingleByChannel(Hit,Cover.Entry+FVector(0,0,2),Cover.Hide+FVector(0,0,2),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,60),Q));
        }
        bool Door=false;const FVector Storage(L.TownBuildings[0].GetCenter().X,L.TownBuildings[0].GetCenter().Y,L.TownBuildings[0].Min.Z);const float StorageHalf=L.TownBuildings[0].GetExtent().Y;
        for(float DX=-190;DX<=190;DX+=20)for(float Side:{-1.f,1.f})
            Door|=!World->SweepSingleByChannel(Hit,Storage+FVector(DX,Side*(StorageHalf+60),100),Storage+FVector(DX,Side*(StorageHalf-85),100),FQuat::Identity,ECC_Pawn,Capsule,Q);
        TestTrue(TEXT("side shack has real standing through-door"),Door);
        for(const auto& D:L.ElevatedDecks)
            TestTrue(TEXT("every platform has a physical floor"),World->LineTraceSingleByChannel(Hit,{D.GetCenter().X,D.GetCenter().Y,D.Max.Z+20},{D.GetCenter().X,D.GetCenter().Y,D.Max.Z-30},ECC_Visibility,Q));
        TestTrue(TEXT("spawn galleries are actually raised"),L.AlphaSpawns[0].GetLocation().Z>=260);
        TestTrue(TEXT("central covers block direct spawn-to-spawn sightline"),World->LineTraceSingleByChannel(Hit,L.AlphaSpawns[2].GetLocation()+FVector(0,0,64),L.OmegaSpawns[2].GetLocation()+FVector(0,0,64),ECC_Visibility,Q));
        TestEqual(TEXT("side shack and extra storey ladders"),L.Ladders.Num(),2+L.VillageUpperLadders.Num());
        for(const auto& Ladder:L.Ladders)
        {
            for(float H=25;H<Ladder.DeckHeight;H+=25)TestTrue(TEXT("timber ladder rungs are physical"),World->LineTraceSingleByChannel(Hit,Ladder.Frame.TransformPosition({15,0,H}),Ladder.Frame.TransformPosition({-15,0,H}),ECC_Visibility,Q));
            TestFalse(TEXT("standing roof landing clear"),World->OverlapBlockingTestByChannel(Ladder.Frame.TransformPosition({-100,0,Ladder.DeckHeight+95}),FQuat::Identity,ECC_Pawn,Capsule,Q));
        }
        for(const auto& Ladder:L.VillageUpperLadders)
        {
            const FVector From=Ladder.Frame.TransformPosition({55,0,100}),To=Ladder.Frame.TransformPosition({55,0,395}),Landing=Ladder.Frame.TransformPosition({-100,0,395});
            const bool WellBlocked=World->SweepSingleByChannel(Hit,From,To,FQuat::Identity,ECC_Pawn,Capsule,Q);
            TestFalse(TEXT("real upper ladder well clears a standing climber"),WellBlocked);
            if(WellBlocked)AddInfo(FString::Printf(TEXT("ER_VILLAGE_WELL_HIT seed=%d ladder=%s component=%s point=%s normal=%s"),Seed,*Ladder.Frame.GetLocation().ToString(),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString()));
            const bool ExitBlocked=World->SweepSingleByChannel(Hit,To,Landing,FQuat::Identity,ECC_Pawn,Capsule,Q);
            TestFalse(FString::Printf(TEXT("seed %d upper ladder %s exit clear"),Seed,*Ladder.Frame.GetLocation().ToString()),ExitBlocked);
            if(ExitBlocked)AddInfo(FString::Printf(TEXT("ER_VILLAGE_EXIT_HIT seed=%d component=%s point=%s normal=%s"),Seed,*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString()));
            TestTrue(TEXT("upper ladder landing supported"),World->SweepSingleByChannel(Hit,Landing,Landing-FVector(0,0,120),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20),Q));
        }
        for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& S:*Starts)
        {TestFalse(TEXT("standing spawn clear"),World->OverlapBlockingTestByChannel(S.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));TestTrue(TEXT("spawn supported"),World->SweepSingleByChannel(Hit,S.GetLocation(),S.GetLocation()-FVector(0,0,120),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20),Q));}
        for(const auto& Route:L.CrossingRoutes)for(int32 I=1;I<Route.Num();++I)
            TestFalse(TEXT("full flank route clear"),World->SweepSingleByChannel(Hit,Route[I-1],Route[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& Flight:L.StairRoutes)TestFalse(TEXT("standing stair approach remains clear"),World->OverlapBlockingTestByChannel(Flight[0],FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& Route:L.VillageSideBypasses)for(int32 I=1;I<Route.Num();++I)
            TestFalse(TEXT("standing bypass around widened side wall is clear"),World->SweepSingleByChannel(Hit,Route[I-1],Route[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& Wall:L.VillageSideWalls)
        {
            const auto P=Wall.GetCenter();const float InnerX=P.X<0 ? Wall.Max.X:Wall.Min.X;
            TestTrue(TEXT("lateral cover blocks standing eye-height sightline"),World->LineTraceSingleByChannel(Hit,{P.X,P.Y-100,Wall.Min.Z+164},{P.X,P.Y+100,Wall.Min.Z+164},ECC_Visibility,Q));
            TestTrue(TEXT("lateral cover extends across its full raised span"),World->LineTraceSingleByChannel(Hit,{InnerX+(P.X<0 ? -40:40),P.Y-100,Wall.Min.Z+220},{InnerX+(P.X<0 ? -40:40),P.Y+100,Wall.Min.Z+220},ECC_Visibility,Q));
            TestFalse(TEXT("standing hide position behind side wall clear"),World->OverlapBlockingTestByChannel({P.X,P.Y-70,Wall.Min.Z+100},FQuat::Identity,ECC_Pawn,Capsule,Q));
        }
        for(const auto& House:L.SecondaryStructures)
        {
            const auto P=House.Frame.GetLocation();const float Approach=House.Size.Y*.5f+40;
            TestFalse(FString::Printf(TEXT("seed %d hut %s has real standing doors"),Seed,*P.ToString()),World->SweepSingleByChannel(Hit,House.Frame.TransformPosition({0,-Approach,100}),House.Frame.TransformPosition({0,Approach,100}),FQuat::Identity,ECC_Pawn,Capsule,Q));
        }
        const float HX=L.ArenaSize.X*.5,HY=L.ArenaSize.Y*.5;
        TestFalse(TEXT("hidden upper boundary does not block bullets"),World->LineTraceSingleByChannel(Hit,{HX-100,0,650},{HX+100,0,650},ECC_Visibility,Q));
        for(float Z:{100.f,400.f,650.f,float(L.ArenaSize.Z-100)})for(const FVector Axis:{FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0)})
        {
            const FVector Edge(Axis.X*HX,Axis.Y*HY,Z);
            TestTrue(TEXT("perimeter blocks standing capsule at ground and roof escape heights"),World->SweepSingleByChannel(Hit,Edge-Axis*100,Edge+Axis*100,FQuat::Identity,ECC_Pawn,Capsule,Q));
        }
        for(const auto& P:L.FixedPieces)if(P.VisualOnly && P.Mesh && !P.Mesh->GetName().Contains(TEXT("SkyDome")))
        {
            const auto Size=P.Transform.GetScale3D()*100,Center=P.Transform.GetLocation();
            TestTrue(TEXT("foliage footprint entirely outside playable area"),FMath::Abs(Center.X)-Size.X*.5>HX || FMath::Abs(Center.Y)-Size.Y*.5>HY);
        }
        TestTrue(TEXT("palisade and panorama meshes bound"),Asset->Village.PalisadePoleMesh && Asset->Village.SkyDomeMesh);
        TestTrue(TEXT("decorative houses removed"),Asset->Village.ExteriorHouses.IsEmpty());
        TestTrue(TEXT("exterior trees removed"),Asset->Village.ExteriorTrees.IsEmpty());
        for(const auto& Passage:L.VillagePassages)for(int32 I=1;I<Passage.Num();++I)
            TestFalse(TEXT("only designated dogleg corridor remains traversable"),World->SweepSingleByChannel(Hit,Passage[I-1],Passage[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& D:L.VillageBlocks)
            for(float DX=-D.Size.X*.5f+100;DX<=D.Size.X*.5f-100;DX+=100)
                TestTrue(TEXT("no straight shortcut across closed central block"),World->SweepSingleByChannel(Hit,D.Frame.TransformPosition({DX,-D.Size.Y*.5f-80,100}),D.Frame.TransformPosition({DX,D.Size.Y*.5f+80,100}),FQuat::Identity,ECC_Pawn,Capsule,Q));
        for(const auto& Polygon:L.VillageClosedGround)for(int32 I=0;I<Polygon.Num();++I)
        {
            const FVector2D A=Polygon[I],Edge=Polygon[(I+1)%Polygon.Num()]-A,N=FVector2D(-Edge.Y,Edge.X).GetSafeNormal(),P=A+Edge*.5;
            TestTrue(TEXT("sealed bay wall is actual visible collision"),World->LineTraceSingleByChannel(Hit,{P.X+N.X*30,P.Y+N.Y*30,ERProcedural::VillageGroundHeight(L,P)+100},{P.X-N.X*30,P.Y-N.Y*30,ERProcedural::VillageGroundHeight(L,P)+100},ECC_Visibility,Q));
        }
        for(int32 I=1;I<L.LowerRoute.Num();++I)TestFalse(TEXT("middle ground lane still traversable"),World->SweepSingleByChannel(Hit,L.LowerRoute[I-1],L.LowerRoute[I],FQuat::Identity,ECC_Pawn,Capsule,Q));
        if(Seed==0 || Seed==42 || Seed==73)
        {
            // Ground-only diagnostic; upper decks hide this plan in aerial shots.
            const double Scale=FMath::Min(800./L.ArenaSize.X,820./L.ArenaSize.Y);
            auto Screen=[&](FVector2D P){return FVector2D(450+P.X*Scale,490+P.Y*Scale);};
            FString SVG=TEXT("<svg xmlns='http://www.w3.org/2000/svg' width='1000' height='1000' viewBox='0 0 900 980'><defs><pattern id='walk' width='12' height='12' patternUnits='userSpaceOnUse'><rect width='12' height='12' fill='#c0d9ad'/><path d='M-3 3L3-3M0 12L12 0M9 15L15 9' stroke='#6d9b58' stroke-width='2'/></pattern></defs><rect width='900' height='980' fill='#f2f3ed'/><text x='450' y='30' text-anchor='middle' font-family='sans-serif' font-size='22'>Village — passaggi al piano terra</text>");
            auto PolygonSVG=[&](const TArray<FVector2D>& Points,const TCHAR* Fill)
            {
                FString List;for(const auto& P:Points){const auto Pixel=Screen(P);List+=FString::Printf(TEXT("%.2f,%.2f "),Pixel.X,Pixel.Y);}
                SVG+=FString::Printf(TEXT("<polygon points='%s' fill='%s' stroke='#4c4c3f' stroke-width='2'/>"),*List,Fill);
            };
            PolygonSVG({{-HX,-HY},{HX,-HY},{HX,HY},{-HX,HY}},TEXT("#e1e5d7"));
            for(const auto& Block:L.VillageBlocks)
            {
                const FVector C=Block.Frame.GetLocation();const FBox D(C-FVector(Block.Size.X,Block.Size.Y,0)*.5f,C+FVector(Block.Size.X,Block.Size.Y,0)*.5f);PolygonSVG({{D.Min.X,D.Min.Y},{D.Max.X,D.Min.Y},{D.Max.X,D.Max.Y},{D.Min.X,D.Max.Y}},TEXT("url(#walk)"));
            }
            for(const auto& P:L.VillageClosedGround)PolygonSVG(P,TEXT("#795d47"));
            for(const auto& Wall:L.VillageSideWalls)PolygonSVG({{Wall.Min.X,Wall.Min.Y},{Wall.Max.X,Wall.Min.Y},{Wall.Max.X,Wall.Max.Y},{Wall.Min.X,Wall.Max.Y}},TEXT("#83919a"));
            for(const auto& Route:L.VillagePassages)
            {
                FString List;for(const auto& P:Route){const auto Pixel=Screen({P.X,P.Y});List+=FString::Printf(TEXT("%.2f,%.2f "),Pixel.X,Pixel.Y);}
                SVG+=FString::Printf(TEXT("<polyline points='%s' fill='none' stroke='#245e67' stroke-width='3' stroke-dasharray='7 4'/>"),*List);
            }
            SVG+=TEXT("<text x='450' y='935' text-anchor='middle' font-family='sans-serif' font-size='17'>Verde: corridoi · Marrone: campate chiuse · Grigio: muretti laterali</text><text x='450' y='960' text-anchor='middle' font-family='sans-serif' font-size='14'>Sezione sotto le piattaforme — le stanze superiori non sono mostrate</text></svg>");
            IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("Testing")),true);
            TestTrue(TEXT("ground plan exported"),FFileHelper::SaveStringToFile(SVG,*(FPaths::ProjectSavedDir()/(Seed==73 ? TEXT("Testing/Village-GroundPlan.svg"):FString::Printf(TEXT("Testing/Village-GroundPlan-seed%d.svg"),Seed)))));
        }
    }
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    AddInfo(FString::Printf(TEXT("ER_VILLAGE_PHYSICS seeds=15 result=%s"),HasAnyErrors() ? TEXT("FAIL"):TEXT("PASS")));return true;
}
#endif
