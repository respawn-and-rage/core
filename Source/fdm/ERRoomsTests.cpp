#include "ERProceduralMap.h"
#include "ERMapCatalog.h"
#include "ERCharacter.h"
#include "Misc/AutomationTest.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "Algo/Reverse.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRoomsLayoutTest,"EclipseRecoil.Destruction.RoomsLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERRoomsLayoutTest::RunTest(const FString& Parameters)
{
    auto* T=NewObject<UERMapTemplate>();T->TemplateId=TEXT("test.rooms");T->Archetype=EERMapArchetype::Rooms;T->Symmetry=EERTemplateSymmetry::HalfTurn;
    for(int32 I=0;I<3;++I)
    {
        FERRoomsTheme Theme;Theme.Name=FName(*FString::Printf(TEXT("Theme%d"),I));
        Theme.Wall=Theme.Floor=Theme.Ceiling=Theme.Wood=NewObject<UMaterial>(T);T->Rooms.Themes.Add(Theme);
    }
    FERProceduralConfig C;C.TemplateAsset=T;FERBlockRoomConfig B;FERProceduralLayout L,Again;
    TSet<int32> Counts,Themes;TSet<float> Heights;int32 Polygons=0;TMap<int32,float> Areas;
    TArray<int32> Seeds;for(int32 I=0;I<512;++I)Seeds.Add(I);Seeds.Append({7919,-17,INT32_MIN,INT32_MAX});
    auto Key=[](FVector P){return FString::Printf(TEXT("%lld/%lld/%lld"),FMath::RoundToInt64(P.X*100),FMath::RoundToInt64(P.Y*100),FMath::RoundToInt64(P.Z*100));};
    for(int32 Seed:Seeds)
    {
        C.Seed=Seed;if(!ERProcedural::Build(C,B,L)){AddError(FString::Printf(TEXT("Rooms seed rejected: %d"),Seed));return false;}
        TestTrue(TEXT("6/8/10 rooms"),L.Rooms.Num()==6 || L.Rooms.Num()==8 || L.Rooms.Num()==10);Counts.Add(L.Rooms.Num());Themes.Add(L.RoomsTheme);
        float MeanArea=0;TArray<int32> Degree;Degree.Init(0,L.RoomNodes.Num());
        TSet<int32> Connected{0};
        for(const auto& E:L.RoomLinks)
        {
            ++Degree[E.A];++Degree[E.B];
            const float Slope=FMath::Abs(E.Start.Z-E.End.Z)/FVector::Dist2D(E.Start,E.End);
            TestTrue(TEXT("walkable slopes <= 1:2"),Slope<=.5f);
            TestTrue(TEXT("nonregular octagon retains a wide floor and standing height"),E.Width-2*E.Cut>=190 && E.Height>=560 && E.Height-2*E.Cut>=430);
        }
        for(int32 I=0;I<L.RoomNodes.Num();++I)for(const auto& E:L.RoomLinks)
        {if(Connected.Contains(E.A))Connected.Add(E.B);if(Connected.Contains(E.B))Connected.Add(E.A);}
        TestEqual(TEXT("every chamber and junction connected"),Connected.Num(),L.RoomNodes.Num());
        TestEqual(TEXT("two team bases"),L.RoomNodes.FilterByPredicate([](const auto& V){return V.Base;}).Num(),2);
        for(int32 I=0;I<L.RoomNodes.Num();++I)
        {
            const auto& V=L.RoomNodes[I];TestTrue(TEXT("arena height encloses upper room roofs"),V.Center.Z+V.Height+20<=L.ArenaSize.Z);TestTrue(TEXT("no room dead end"),Degree[I]>=2);Heights.Add(V.Center.Z);
            if(!V.Junction){MeanArea+=V.Size.X*V.Size.Y;if(V.Polygon.Num()>4)++Polygons;}
            const FVector Mirror(-V.Center.X,-V.Center.Y,V.Center.Z);
            TestTrue(TEXT("room half-turn pairing"),L.RoomNodes.ContainsByPredicate([&](const auto& D){return Mirror.Equals(D.Center) && D.Size.Equals(V.Size) && D.Upper==V.Upper;}));
        }
        Areas.FindOrAdd(L.Rooms.Num())+=MeanArea/L.Rooms.Num();
        TSet<FString> Vertices;for(const auto& F:L.RoomShell)for(const FVector P:{F.A,F.B,F.C,F.D})Vertices.Add(Key(P));
        for(const auto& F:L.RoomShell)
        {
            TestTrue(TEXT("no degenerate shell face"),FVector::CrossProduct(F.B-F.A,F.C-F.A).SizeSquared()>1);
            for(const FVector P:{F.A,F.B,F.C,F.D})if(!Vertices.Contains(Key({-P.X,-P.Y,P.Z}))){AddError(TEXT("shell half-turn mismatch"));return false;}
        }
        TestEqual(TEXT("paired upper rooms with real ramp routes"),L.StairRoutes.Num(),L.RoomNodes.FilterByPredicate([](const auto& V){return V.Upper;}).Num());
        TestEqual(TEXT("eight starts per team"),L.AlphaSpawns.Num(),8);
        for(int32 I=0;I<8;++I){auto P=L.AlphaSpawns[I].GetLocation();P.X=-P.X;P.Y=-P.Y;TestTrue(TEXT("paired starts"),P.Equals(L.OmegaSpawns[I].GetLocation()));}
        TestEqual(TEXT("flat panels replace every hollow crate"),L.Panels.Num(),L.RoomScreens.Num());
        TestTrue(TEXT("larger rooms retain sparse cover"),L.RoomScreens.Num()>=8);
        for(int32 I=0;I<L.RoomLinks.Num();++I)
        {
            const auto Screens=L.RoomScreens.FilterByPredicate([&](const auto& S){return S.Link==I;});
            for(int32 J=1;J<Screens.Num();++J)TestTrue(TEXT("corridor boards spaced at least five metres"),FVector::Dist2D(Screens[J-1].Frame.GetLocation(),Screens[J].Frame.GetLocation())>=499.9);
            if(FVector::Dist2D(L.RoomLinks[I].Start,L.RoomLinks[I].End)<1000)TestTrue(TEXT("short corridors stay unfurnished"),Screens.IsEmpty());
        }
        for(int32 I=0;I<L.RoomNodes.Num();++I)if(!L.RoomNodes[I].Junction)
        {
            const auto& V=L.RoomNodes[I];TestTrue(TEXT("rooms remain at least sixteen metres across"),V.Size.GetMin()>=1600);
            const auto Screens=L.RoomScreens.FilterByPredicate([&](const auto& S){return S.Room==I;});
            TestTrue(TEXT("no dense room pockets"),Screens.Num()<=2);
            if(Screens.Num()==2)TestTrue(TEXT("room screens at least five metres apart"),FVector::Dist2D(Screens[0].Frame.GetLocation(),Screens[1].Frame.GetLocation())>=500);
        }
        TestTrue(TEXT("upper corridor opens onto gallery"),L.RoomLinks.ContainsByPredicate([&](const auto& E){return E.Start.Z>L.RoomNodes[E.A].Center.Z+200 || E.End.Z>L.RoomNodes[E.B].Center.Z+200;}));
        const auto& Lower=L.RoomLinks[L.RoomLinks.Num()-2];const auto& Upper=L.RoomLinks.Last();
        TestTrue(TEXT("crossing has separate lower and upper volumes"),Upper.Start.Z-Lower.Start.Z>=600 && Upper.End.Z-Lower.End.Z>=600);
        for(const auto& S:L.RoomScreens)
        {
            TestTrue(TEXT("screens have standing-height cover and calibrated wood depth"),S.Size.Z>=240 && S.Size.Y==2);
            const FTransform Mirror=S.Frame*FTransform(FRotator(0,180,0));
            TestTrue(TEXT("screen geometry is half-turn paired"),L.RoomScreens.ContainsByPredicate([&](const auto& D){return D.Frame.Equals(Mirror,.01) && D.Size.Equals(S.Size,.01);}));
        }
        TestTrue(TEXT("cover is destructible wood"),!L.Panels.IsEmpty());
        for(const auto& P:L.Panels)TestTrue(TEXT("selected wood theme only"),P.Material==EERBlockMaterial::Wood && P.Surface==T->Rooms.Themes[L.RoomsTheme].Wood);
        if(Seed==42)
        {
            TestTrue(TEXT("repeat builds"),ERProcedural::Build(C,B,Again));
            TestEqual(TEXT("same theme"),L.RoomsTheme,Again.RoomsTheme);TestEqual(TEXT("same face count"),L.RoomShell.Num(),Again.RoomShell.Num());
            for(int32 I=0;I<L.RoomShell.Num();++I)TestTrue(TEXT("identical deterministic geometry"),L.RoomShell[I].A.Equals(Again.RoomShell[I].A));
            auto Old=T->Rooms.Themes;Algo::Reverse(T->Rooms.Themes);ERProcedural::Build(C,B,Again);T->Rooms.Themes=Old;
            for(int32 I=0;I<L.RoomShell.Num();++I)TestTrue(TEXT("art choices cannot move geometry"),L.RoomShell[I].A.Equals(Again.RoomShell[I].A));
        }
    }
    TestEqual(TEXT("all three room counts sampled"),Counts.Num(),3);TestEqual(TEXT("all existing themes sampled"),Themes.Num(),3);TestTrue(TEXT("multiple floor and junction elevations"),Heights.Num()>=3);
    TestTrue(TEXT("nonrectangular rooms generated"),Polygons>100);
    // Force each count to compare actual per-room area, independently of seed frequency.
    for(int32 N=2;N<=4;++N){T->Rooms.MinLaneRooms=T->Rooms.MaxLaneRooms=N;ERProcedural::Build(C,B,L);float Sum=0;for(const auto& V:L.RoomNodes)if(!V.Junction)Sum+=V.Size.X*V.Size.Y;Areas.Add(2+N*2,Sum/L.Rooms.Num());}
    TestTrue(TEXT("more rooms means smaller rooms"),Areas[6]>Areas[8] && Areas[8]>Areas[10]);
    const int32 Before=L.RoomShell.Num();T->Rooms.CornerCut=NAN;
    TestFalse(TEXT("invalid dimensions rejected"),ERProcedural::Build(C,B,L));TestEqual(TEXT("invalid recipe preserves previous layout"),L.RoomShell.Num(),Before);
    const FERMapCatalogEntry* RoomsEntry=nullptr;
    for(const auto& Entry:ERMaps::All())if(FName(Entry.Id)==TEXT("rooms")){RoomsEntry=&Entry;break;}
    if(TestNotNull(TEXT("Rooms remains in the offline map catalog"),RoomsEntry))
    {
        TestTrue(TEXT("Rooms remains available in offline menu labels"),ERMaps::Labels().Contains(FString(RoomsEntry->Label)));
        TestFalse(TEXT("Rooms is excluded from hosted rotation"),RoomsEntry->InRotation);
        TestFalse(TEXT("Rooms is absent from the actual rotation pool"),ERMaps::Rotation().Contains(FName(RoomsEntry->Package)));
    }
    AddInfo(FString::Printf(TEXT("ER_ROOMS_LAYOUT seeds=516 counts=6,8,10 themes=3 result=%s"),HasAnyErrors() ? TEXT("FAIL"):TEXT("PASS")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERRoomsPhysicsTest,"EclipseRecoil.Destruction.RoomsPhysics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERRoomsPhysicsTest::RunTest(const FString& Parameters)
{
    auto* Asset=LoadObject<UERMapTemplate>(nullptr,TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_Rooms.DA_Rooms"));
    if(!TestNotNull(TEXT("saved Rooms recipe"),Asset))return false;
    UWorld::InitializationValues V;V.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).CreateFXSystem(false).EnableTraceCollision(true);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&V);if(!World)return false;
    auto* Map=World->SpawnActor<AERProceduralMap>();Map->LayoutConfig.TemplateAsset=Asset;
    const auto Capsule=FCollisionShape::MakeCapsule(34,92.5);FCollisionQueryParams Q(SCENE_QUERY_STAT(ERRoomsPhysics),false);FHitResult Hit;
    TSet<int32> PhysicalCounts;
    for(int32 Seed:{13,42,73,511,7919,-17})
    {
        Map->LayoutConfig.Seed=Seed;if(!TestTrue(TEXT("physical generation"),Map->Regenerate()))continue;
        const auto& L=Map->GetLayout();PhysicalCounts.Add(L.Rooms.Num());TestTrue(TEXT("valid cell collision"),Map->ValidateCells());
        for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& P:*Starts)
        {
            TestFalse(TEXT("clear standing spawn"),World->OverlapBlockingTestByChannel(P.GetLocation(),FQuat::Identity,ECC_Pawn,Capsule,Q));
            TestTrue(TEXT("supported spawn"),World->LineTraceSingleByChannel(Hit,P.GetLocation(),P.GetLocation()-FVector(0,0,120),ECC_Visibility,Q));
        }
        for(const auto* Paths:{&L.RoomAccessRoutes,&L.StairRoutes,&L.CrossingRoutes,&L.Routes})for(const auto& Path:*Paths)for(int32 I=1;I<Path.Num();++I)
        {
            const FVector A=Path[I-1],D=Path[I];
            TestFalse(FString::Printf(TEXT("standing route clear seed=%d %s -> %s"),Seed,*A.ToString(),*D.ToString()),World->SweepSingleByChannel(Hit,A,D,FQuat::Identity,ECC_Pawn,Capsule,Q));
            const int32 Samples=FMath::CeilToInt((D-A).Size()/50);
            for(int32 J=0;J<=Samples;++J)
            {
                // Sample inside each segment. A ray exactly on a butt joint
                // can report the vertical end-cap normal at the shared edge;
                // the complete capsule sweep above still includes both seams.
                const FVector P=FMath::Lerp(A,D,(J+.5f)/(Samples+1));
                const bool Floor=World->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,125),ECC_Visibility,Q);
                TestTrue(FString::Printf(TEXT("continuous floor seed=%d at=%s hit=%s normal=%s"),Seed,*P.ToString(),Floor ? *Hit.ImpactPoint.ToString():TEXT("none"),*Hit.ImpactNormal.ToString()),Floor && Hit.ImpactNormal.Z>.7);
            }
        }
        for(const auto& E:L.RoomLinks)
        {
            const FVector P=FMath::Lerp(E.Start,E.End,.02f),Side=FVector::CrossProduct(FVector::UpVector,(E.End-E.Start).GetSafeNormal2D());
            const FVector Dir=(E.End-E.Start).GetSafeNormal2D();
            const FVector Corner=E.Start+Side*(E.Width*.5f-10)+FVector(0,0,E.Height-10);
            TestTrue(TEXT("portal clipped corner seals the room/corridor joint"),World->LineTraceSingleByChannel(Hit,Corner-Dir*80,Corner+Dir*80,ECC_GameTraceChannel1,Q));
            TestTrue(TEXT("corridor side sealed"),World->LineTraceSingleByChannel(Hit,P+FVector(0,0,200),P+Side*(E.Width*.5f+40)+FVector(0,0,200),ECC_GameTraceChannel1,Q));
            TestTrue(TEXT("corridor roof sealed"),World->LineTraceSingleByChannel(Hit,P+FVector(0,0,E.Height-30),P+FVector(0,0,E.Height+40),ECC_GameTraceChannel1,Q));
            const float SideLimit=E.Width*.5f-E.Cut-35;
            const int32 LinkIndex=int32(&E-L.RoomLinks.GetData());
            const int32 ScreenCount=L.RoomScreens.FilterByPredicate([&](const auto& S){return S.Link==LinkIndex;}).Num();
            if(ScreenCount>=3)for(float A:{-1.f,-.5f,0.f,.5f,1.f})for(float B:{-1.f,-.5f,0.f,.5f,1.f})
            {
                const FVector From=E.Start+Dir*2+Side*(A*SideLimit)+FVector(0,0,172.5f);
                const FVector To=E.End-Dir*2+Side*(B*SideLimit)+FVector(0,0,172.5f);
                TestTrue(TEXT("no uninterrupted straight or diagonal corridor sightline"),World->LineTraceSingleByChannel(Hit,From,To,ECC_GameTraceChannel1,Q));
                FERCellId Id;
                TestTrue(TEXT("corridor view is blocked by real destructible wood"),Map->ResolveCell(Hit,Id) && Map->GetCellWorld().GetPanels()[Id.Panel].Material==EERBlockMaterial::Wood);
            }
            TestTrue(TEXT("lower clipped corner has real collision"),World->LineTraceSingleByChannel(Hit,P+FVector(0,0,30),P+Side*(E.Width*.5f+20)+FVector(0,0,30),ECC_GameTraceChannel1,Q));
        }
        for(const auto& Node:L.RoomNodes)
            TestTrue(TEXT("room roof closes volume"),World->LineTraceSingleByChannel(Hit,Node.Center+FVector(0,0,Node.Height-30),Node.Center+FVector(0,0,Node.Height+30),ECC_Visibility,Q));
        // There is no global floor in the void between the sealed volumes.
        const float Gap=(FMath::Abs(L.RoomNodes[0].Center.Y)-L.RoomNodes[0].Size.Y*.5f+Asset->Rooms.CorridorWidth*.5f)*.5f;
        TestFalse(TEXT("no external walkable apron"),World->LineTraceSingleByChannel(Hit,{L.ArenaSize.X*.48,Gap,200},{L.ArenaSize.X*.48,Gap,-100},ECC_Visibility,Q));
    }
    TestEqual(TEXT("physics covers all three room counts"),PhysicalCounts.Num(),3);
    World->DestroyWorld(false);AddInfo(FString::Printf(TEXT("ER_ROOMS_PHYSICS seeds=6 counts=6,8,10 result=%s"),HasAnyErrors() ? TEXT("FAIL"):TEXT("PASS")));return true;
}
#endif
